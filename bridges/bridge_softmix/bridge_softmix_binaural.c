/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2016, Frank Haase, Dennis Guse
 *
 * Frank Haase <fra.haase@gmail.com>
 * Dennis Guse <dennis.guse@alumni.tu-berlin.de>
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
 * \brief Multi-party software based binaural mixing
 *
 * \author Frank Haase <fra.haase@googlemail.com>
 * \author Dennis Guse <dennis.guse@alumni.tu-berlin.de>
 *
 * \ingroup bridges
 */

#include "include/bridge_softmix_internal.h"

#ifdef BINAURAL_RENDERING
  #include "include/hrirs_configuration.h"
#endif

/*! The number of prealloced channels when a bridge will be created. */
#define CONVOLVE_CHANNEL_PREALLOC 3
/*! Max size of the convolve buffer. */
#define CONVOLVE_MAX_BUFFER 4096
/*! The default sample size in an binaural environment with a two-channel
 * codec at 48kHz is 960 samples.
 */
#define CONVOLUTION_SAMPLE_SIZE 960

#ifdef BINAURAL_RENDERING
  #if SOFTMIX_BINAURAL_SAMPLE_RATE != HRIRS_SAMPLE_RATE
	  #error HRIRs are required to be SOFTMIX_BINAURAL_SAMPLE_RATE Hz. Please adjust hrirs.h accordingly.
	#endif
  #if CONVOLUTION_SAMPLE_SIZE < HRIRS_IMPULSE_LEN
	  #error HRIRS_IMPULSE_LEN cannot be longer than CONVOLUTION_SAMPLE_SIZE. Please adjust hrirs.h accordingly.
	#endif
#endif

void reset_channel_pair(struct convolve_channel_pair *channel_pair,
		unsigned int default_sample_size)
{
	memset(channel_pair->chan_left.overlap_add, 0, sizeof(float) * default_sample_size);
	memset(channel_pair->chan_right.overlap_add, 0, sizeof(float) * default_sample_size);
}

void random_binaural_pos_change(struct softmix_bridge_data *softmix_data)
{
	/*
	 * We perform a shuffle of all channels, even the ones that aren't used at the
	 * moment of shuffling now. This has the efect that new members will be placed
	 * randomly too.
	 */
	unsigned int i;
	unsigned int j;
	struct convolve_channel_pair *tmp;

	if (softmix_data->convolve.chan_size < 2) {
		return;
	}

	srand(time(NULL));
	for (i = softmix_data->convolve.chan_size - 1; i > 0; i--) {
		j = rand() % (i + 1);
		tmp = softmix_data->convolve.cchan_pair[i];
		reset_channel_pair(tmp, softmix_data->default_sample_size);
		softmix_data->convolve.cchan_pair[i] = softmix_data->convolve.cchan_pair[j];
		softmix_data->convolve.cchan_pair[j] = tmp;
	}
}

int do_convolve(struct convolve_channel *chan, int16_t *in_samples,
		unsigned int in_sample_size, unsigned int hrtf_length)
{
#ifdef BINAURAL_RENDERING
	unsigned int i;

	if (in_sample_size != CONVOLUTION_SAMPLE_SIZE) {
		return -1;
	}

	/* FFT setting real part */
	for (i = 0; i < CONVOLUTION_SAMPLE_SIZE; i++) {
		chan->fftw_in[i] = in_samples[i] * (FLT_MAX / SHRT_MAX);
	}

	for (i = CONVOLUTION_SAMPLE_SIZE; i < hrtf_length; i++) {
		chan->fftw_in[i] = 0;
	}
	fftw_execute(chan->fftw_plan);

	/* Imaginary mulitplication (frequency space). */
	/* First FFTW result has never an imaginary part. */
	chan->fftw_in[0] = chan->fftw_out[0] * chan->hrtf[0];
	for (i = 1; i < (hrtf_length / 2); i++) {
		/* Real part */
		chan->fftw_in[i] = (chan->fftw_out[i] * chan->hrtf[i]) -
				(chan->fftw_out[hrtf_length - i] * chan->hrtf[hrtf_length - i]);
		/* Imaginary part */
		chan->fftw_in[hrtf_length - i] = (chan->fftw_out[i] * chan->hrtf[hrtf_length - i]) +
				(chan->fftw_out[hrtf_length - i] * chan->hrtf[i]);
	}

	/* The last (if even) FFTW result has never an imaginary part. */
	if (hrtf_length % 2 == 0) {
		chan->fftw_in[hrtf_length / 2] = chan->fftw_out[hrtf_length / 2] *
				chan->hrtf[hrtf_length / 2];
	}

	/* iFFT */
	fftw_execute(chan->fftw_plan_inverse);
	/* Remove signal increase due to iFFT. */
	for (i = 0; i < hrtf_length; i++) {
		chan->fftw_out[i] = chan->fftw_out[i] / (hrtf_length / 2);
	}

	/* Save the block for overlapp add in the next itteration. */
	for (i = 0; i < in_sample_size; i++) {
		chan->overlap_add[i] += chan->fftw_out[i];
	}

	/* Copy real part to the output, ignore the complex part. */
	for (i = 0; i < in_sample_size; i++) {
		chan->out_data[i] = chan->overlap_add[i] * (SHRT_MAX / FLT_MAX);
		chan->overlap_add[i] = chan->fftw_out[i + in_sample_size];
	}
#endif
	return 0;
}

struct convolve_channel_pair *do_convolve_pair(struct convolve_data *data,
		unsigned int pos_id, int16_t *in_samples, unsigned int in_sample_size,
		const char *channel_name)
{
	struct convolve_channel_pair *chan_pair;

	/* If a position has no active member we will not convolve. */
	if (data->pos_ids[pos_id] != 1) {
		ast_log(LOG_ERROR, "Channel %s: Channel pair has no active member! (pos id = %d)\n",
				channel_name, pos_id);
		return NULL;
	}

	chan_pair = data->cchan_pair[pos_id];
	if (do_convolve(&chan_pair->chan_left, in_samples, in_sample_size, data->hrtf_length)) {
		ast_log(LOG_ERROR, "Channel %s: Binaural processing failed.", channel_name);
		return NULL;
	}

	if (do_convolve(&chan_pair->chan_right, in_samples, in_sample_size, data->hrtf_length)) {
		ast_log(LOG_ERROR, "Channel %s: Binaural processing failed.", channel_name);
		return NULL;
	}

	return chan_pair;
}

float *get_hrir(unsigned int chan_pos, unsigned int chan_side)
{
#ifdef BINAURAL_RENDERING
	if (chan_side == HRIRS_CHANNEL_LEFT) {
		return hrirs_left[ast_binaural_positions[chan_pos]];
	} else if (chan_side == HRIRS_CHANNEL_RIGHT) {
		return hrirs_right[ast_binaural_positions[chan_pos]];
	}
#else
	ast_log(LOG_ERROR, "Requesting data for the binaural conference feature without "
			"it beeing active.\n");
#endif

	return NULL;
}

int init_convolve_channel(struct convolve_channel *channel, unsigned int hrtf_len,
		unsigned int chan_pos, unsigned int chan_side, unsigned int default_sample_size)
{
#ifdef BINAURAL_RENDERING
	unsigned int j;
	float *hrir;

	/* Prepare FFTW. */
	channel->fftw_in = (double *) fftw_malloc(sizeof(double) * (hrtf_len + 1));
	if (channel->fftw_in == NULL) {
		return -1;
	}

	channel->fftw_out = (double *) fftw_malloc(sizeof(double) * (hrtf_len + 1));
	if (channel->fftw_out == NULL) {
		fftw_free(channel->fftw_in);
		return -1;
	}

	memset(channel->fftw_in, 0, sizeof(double) * (hrtf_len + 1));
	memset(channel->fftw_out, 0, sizeof(double) * (hrtf_len + 1));

	channel->fftw_plan = fftw_plan_r2r_1d(hrtf_len, channel->fftw_in, channel->fftw_out,
			FFTW_R2HC, FFTW_PATIENT);
	channel->fftw_plan_inverse = fftw_plan_r2r_1d(hrtf_len, channel->fftw_in, channel->fftw_out,
			FFTW_HC2R, FFTW_PATIENT);
	channel->out_data = ast_calloc(CONVOLVE_MAX_BUFFER, sizeof(int16_t));
	if (channel->out_data == NULL) {
		fftw_free(channel->fftw_in);
		fftw_free(channel->fftw_out);
		return -1;
	}

	/* Reuse positions if all positions are already used. */
	chan_pos = chan_pos % HRIRS_IMPULSE_SIZE;

	/* Get HRTF for the channels spatial position. */
	hrir = get_hrir(chan_pos, chan_side);
	if (hrir == NULL) {
		fftw_free(channel->fftw_in);
		fftw_free(channel->fftw_out);
		ast_free(channel->out_data);
		return -1;
	}

	for (j = 0; j < HRIRS_IMPULSE_LEN; j++) {
		channel->fftw_in[j] = hrir[j];
	}

	for (j = HRIRS_IMPULSE_LEN; j < hrtf_len; j++) {
		channel->fftw_in[j] = 0;
	}

	fftw_execute(channel->fftw_plan);
	channel->hrtf = (double *) fftw_malloc(sizeof(double) * hrtf_len);
	if (channel->hrtf == NULL) {
		fftw_free(channel->fftw_in);
		fftw_free(channel->fftw_out);
		ast_free(channel->out_data);
		return -1;
	}

	for (j = 0; j < hrtf_len; j++) {
		channel->hrtf[j] = channel->fftw_out[j];
	}
	channel->overlap_add = ast_calloc(default_sample_size, sizeof(float));

	return 0;
#endif
	return -1;
}

int init_convolve_channel_pair(struct convolve_channel_pair *cchan_pair,
		unsigned int hrtf_len, unsigned int chan_pos, unsigned int default_sample_size)
{
#ifdef BINAURAL_RENDERING
	unsigned int hrirs_pos = chan_pos * 2;
	int success = 0;

	ast_debug(3, "Binaural pos for the new channel pair will be L: %d R: %d (pos id = %d)\n",
			hrirs_pos, hrirs_pos + 1, chan_pos);
	success = init_convolve_channel(&cchan_pair->chan_left, hrtf_len, chan_pos, HRIRS_CHANNEL_LEFT,
			default_sample_size);
	if (success == -1) {
		return success;
	}

	success = init_convolve_channel(&cchan_pair->chan_right, hrtf_len, chan_pos,
			HRIRS_CHANNEL_RIGHT, default_sample_size);
	if (success == -1) {
		free_convolve_channel(&cchan_pair->chan_left);
	}

	return success;
#else
	ast_log(LOG_ERROR, "Requesting data for the binaural conference feature "
			"without it beeing active.\n");

	return -1;
#endif
}

int init_convolve_data(struct convolve_data *data, unsigned int default_sample_size)
{
	unsigned int i;
	unsigned int j;
	int success;
	success = 0;

	data->pos_ids = ast_calloc(sizeof(int), sizeof(int) * CONVOLVE_CHANNEL_PREALLOC);
	if (data->pos_ids == NULL) {
		return -1;
	}
	data->chan_size = CONVOLVE_CHANNEL_PREALLOC;
	data->number_channels = 0;
	data->cchan_pair = ast_malloc(sizeof(struct convolve_channel_pair *) *
			CONVOLVE_CHANNEL_PREALLOC);
	if (data->cchan_pair == NULL) {
		ast_free(data->pos_ids);
		return -1;
	}

	for (i = 0; i < CONVOLVE_CHANNEL_PREALLOC; i++) {
		data->cchan_pair[i] = ast_malloc(sizeof(struct convolve_channel_pair));
		if (data->cchan_pair[i] == NULL) {
			ast_free(data->pos_ids);
			for (j = 0; j < i; j++) {
				ast_free(data->cchan_pair[j]);
			}
			ast_free(data->cchan_pair);
			return -1;
		}
	}

	data->hrtf_length = (default_sample_size * 2) - 1;
	for (i = 0; i < CONVOLVE_CHANNEL_PREALLOC; i++) {
		success = init_convolve_channel_pair(data->cchan_pair[i], data->hrtf_length, i,
				default_sample_size);
		if (success == -1) {
			ast_free(data->pos_ids);
			for (j = 0; j < i; j++) {
				free_convolve_channel_pair(data->cchan_pair[j]);
			}
			for (j = 0; j < CONVOLVE_CHANNEL_PREALLOC; j++) {
				ast_free(data->cchan_pair[j]);
			}
			return -1;
		}
	}

	return success;
}

void free_convolve_channel(struct convolve_channel *cchan)
{
#ifdef BINAURAL_RENDERING
	fftw_free(cchan->fftw_out);
	fftw_free(cchan->fftw_in);
	fftw_free(cchan->hrtf);
	ast_free(cchan->overlap_add);
	ast_free(cchan->out_data);
	fftw_destroy_plan(cchan->fftw_plan);
	fftw_destroy_plan(cchan->fftw_plan_inverse);
#endif
}

void free_convolve_channel_pair(struct convolve_channel_pair *cchan_pair)
{
	free_convolve_channel(&cchan_pair->chan_left);
	free_convolve_channel(&cchan_pair->chan_right);
}

void free_convolve_data(struct convolve_data *data)
{
	unsigned int i;
	ast_free(data->pos_ids);
	for (i = 0; i < data->chan_size; i++) {
		free_convolve_channel_pair(data->cchan_pair[i]);
		ast_free(data->cchan_pair[i]);
	}
	ast_free(data->cchan_pair);
}

int set_binaural_data_join(struct convolve_data *data, unsigned int default_sample_size)
{
	struct convolve_channel_pair **cchan_pair_tmp;
	unsigned int i;
	int *pos_ids_tmp;

	/* Raise the number of input channels. */
	data->number_channels++;
	/* We realloc another channel pair if we are out of prealloced ones. */
	/* We have prealloced one at the beginning of a conference and if a member leaves. */
	if (data->chan_size < data->number_channels)  {
		data->chan_size += 1;

		pos_ids_tmp = ast_realloc(data->pos_ids, data->chan_size * sizeof(int));
		if (pos_ids_tmp) {
			data->pos_ids = pos_ids_tmp;
		} else {
			goto binaural_join_fails;
		}

		data->pos_ids[data->chan_size - 1] = 0;
		cchan_pair_tmp = ast_realloc(data->cchan_pair,
				data->chan_size * sizeof(struct convolve_channel_pair *));
		if (cchan_pair_tmp) {
			data->cchan_pair = cchan_pair_tmp;
		} else {
			goto binaural_join_fails;
		}

		data->cchan_pair[data->chan_size - 1] = ast_malloc(sizeof(struct convolve_channel_pair));
		if (data->cchan_pair[data->chan_size - 1] == NULL) {
			goto binaural_join_fails;
		}

		i = init_convolve_channel_pair(data->cchan_pair[data->chan_size - 1], data->hrtf_length,
				data->chan_size - 1, default_sample_size);
		if (i == -1) {
			goto binaural_join_fails;
		}
	}

	for (i = 0; i < data->chan_size; i++) {
		if (data->pos_ids[i] == 0) {
			data->pos_ids[i] = 1;
			break;
		}
	}

	return i;

binaural_join_fails:
	data->number_channels--;
	data->chan_size -= 1;

	return -1;
}

void set_binaural_data_leave(struct convolve_data *data, unsigned int pos,
		unsigned int default_sample_size)
{
	if (pos >= data->chan_size || data->pos_ids[pos] == 0) {
		return;
	}

	reset_channel_pair(data->cchan_pair[pos], default_sample_size);
	data->number_channels--;
	data->pos_ids[pos] = 0;
}

void softmix_process_write_binaural_audio(struct softmix_channel *sc,
		unsigned int default_sample_size)
{
	unsigned int i;

	if (sc->write_frame.samples % default_sample_size != 0) {
		return;
	}

	/* If binaural is suspended, the source audio (mono) will be removed. */
	if (sc->binaural_suspended) {
		for (i = 0; i < default_sample_size; i++) {
			ast_slinear_saturated_subtract(&sc->final_buf[i * 2], &sc->our_buf[i]);
			ast_slinear_saturated_subtract(&sc->final_buf[(i * 2) + 1], &sc->our_buf[i]);
		}
		return;
	}

  /* If binaural is NOT suspended, the source audio (binaural) will be removed. */
	for (i = 0; i < default_sample_size; i++) {
		ast_slinear_saturated_subtract(&sc->final_buf[i * 2],
				&sc->our_chan_pair->chan_left.out_data[i]);
		ast_slinear_saturated_subtract(&sc->final_buf[(i * 2) + 1],
				&sc->our_chan_pair->chan_right.out_data[i]);
	}
}

void check_binaural_position_change(struct ast_bridge *bridge,
		struct softmix_bridge_data *softmix_data)
{
	unsigned int pos_change;
	struct ast_bridge_channel *bridge_channel;

	/*
	 * We only check binaural things if binaural is activated by the config
	 * and at least one binaural channel joined.
	 */
	if (!(bridge->softmix.binaural_active && softmix_data->convolve.binaural_active)) {
		return;
	}
	/*
	 * Before we pull any audio, we must check if any channel requests a
	 * change of binaural positions.
	 */
	pos_change = 0;
	AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
		if (!bridge_channel->binaural_pos_change) {
			continue;
		}
		ast_bridge_channel_lock_bridge(bridge_channel);
		bridge_channel->binaural_pos_change = 0;
		ast_bridge_unlock(bridge_channel->bridge);
		pos_change = 1;
	}

	if (pos_change) {
		random_binaural_pos_change(softmix_data);
	}
}

void add_binaural_mixing(struct ast_bridge *bridge, struct softmix_bridge_data *softmix_data,
		unsigned int softmix_samples, struct softmix_mixing_array *mixing_array,
		struct softmix_channel *sc, const char *channel_name)
{
	struct convolve_channel_pair *pair;

	pair = NULL;
	/* We only check binaural things if at least one binaural channel joined. */
	if (!(bridge->softmix.binaural_active && softmix_data->convolve.binaural_active
			&& (softmix_samples % CONVOLUTION_SAMPLE_SIZE) == 0)) {
		return;
	}

	if (!sc->is_announcement) {
		pair = do_convolve_pair(&softmix_data->convolve, sc->binaural_pos,
				mixing_array->buffers[mixing_array->used_entries], softmix_samples, channel_name);
	}
	sc->our_chan_pair = pair;
	mixing_array->chan_pairs[mixing_array->used_entries] = pair;
}

void binaural_mixing(struct ast_bridge *bridge, struct softmix_bridge_data *softmix_data,
		struct softmix_mixing_array *mixing_array, int16_t *bin_buf, int16_t *ann_buf)
{
	unsigned int idx;
	unsigned int x;

	if (!(bridge->softmix.binaural_active && softmix_data->convolve.binaural_active)) {
		return;
	}
	/* mix it like crazy (binaural channels) */
	memset(bin_buf, 0, MAX_DATALEN);
	memset(ann_buf, 0, MAX_DATALEN);

	for (idx = 0; idx < mixing_array->used_entries; idx++) {
		if (mixing_array->chan_pairs[idx] == NULL) {
			for (x = 0; x < softmix_data->default_sample_size; x++) {
				ast_slinear_saturated_add(bin_buf + (x * 2), mixing_array->buffers[idx] + x);
				ast_slinear_saturated_add(bin_buf + (x * 2) + 1, mixing_array->buffers[idx] + x);
				ann_buf[x * 2] = mixing_array->buffers[idx][x];
				ann_buf[(x * 2) + 1] = mixing_array->buffers[idx][x];
			}
		} else {
			for (x = 0; x < softmix_data->default_sample_size; x++) {
				ast_slinear_saturated_add(bin_buf + (x * 2),
						mixing_array->chan_pairs[idx]->chan_left.out_data + x);
				ast_slinear_saturated_add(bin_buf + (x * 2) + 1,
						mixing_array->chan_pairs[idx]->chan_right.out_data + x);
			}
		}
	}
}

void create_binaural_frame(struct ast_bridge_channel *bridge_channel,
		struct softmix_channel *sc, int16_t *bin_buf, int16_t *ann_buf,
		unsigned int softmix_datalen, unsigned int softmix_samples, int16_t *buf)
{
	unsigned int i;

	sc->write_frame.datalen = softmix_datalen * 2;
	sc->write_frame.samples = softmix_samples * 2;
	if (!bridge_channel->binaural_suspended) {
		sc->binaural_suspended = 0;
		if (sc->is_announcement) {
			memcpy(sc->final_buf, ann_buf, softmix_datalen * 2);
		} else {
			memcpy(sc->final_buf, bin_buf, softmix_datalen * 2);
		}
		return;
	}

	/*
	 * Mark that binaural output is suspended, since we use two channel audio
	 * we copy the same signals into both channels.
	 */
	sc->binaural_suspended = 1;
	for (i = 0; i < softmix_samples; i++) {
		sc->final_buf[i * 2] = buf[i];
		sc->final_buf[(i * 2) + 1] = buf[i];
	}
}
