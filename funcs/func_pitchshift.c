/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
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
 * \brief Pitch Shift Audio Effect
 *
 * \author David Vossel <dvossel@digium.com>
 *
 * \ingroup functions
 */

/************************* SMB FUNCTION LICENSE *********************************
*
* SYNOPSIS: Routine for doing pitch shifting while maintaining
* duration using the Short Time Fourier Transform.
*
* DESCRIPTION: The routine takes a pitchShift factor value which is between 0.5
* (one octave down) and 2. (one octave up). A value of exactly 1 does not change
* the pitch. num_samps_to_process tells the routine how many samples in indata[0...
* num_samps_to_process-1] should be pitch shifted and moved to outdata[0 ...
* num_samps_to_process-1]. The two buffers can be identical (ie. it can process the
* data in-place). fft_frame_size defines the FFT frame size used for the
* processing. Typical values are 1024, 2048 and 4096. It may be any value <=
* MAX_FRAME_LENGTH but it MUST be a power of 2. osamp is the STFT
* oversampling factor which also determines the overlap between adjacent STFT
* frames. It should at least be 4 for moderate scaling ratios. A value of 32 is
* recommended for best quality. sampleRate takes the sample rate for the signal
* in unit Hz, ie. 44100 for 44.1 kHz audio. The data passed to the routine in
* indata[] should be in the range [-1.0, 1.0), which is also the output range
* for the data, make sure you scale the data accordingly (for 16bit signed integers
* you would have to divide (and multiply) by 32768).
*
* COPYRIGHT 1999-2009 Stephan M. Bernsee <smb [AT] dspdimension [DOT] com>
*
*                        The Wide Open License (WOL)
*
* Permission to use, copy, modify, distribute and sell this software and its
* documentation for any purpose is hereby granted without fee, provided that
* the above copyright notice and this license appear in all source copies.
* THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT EXPRESS OR IMPLIED WARRANTY OF
* ANY KIND. See http://www.dspguru.com/wol.htm for more information.
*
*****************************************************************************/

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/audiohook.h"
#include <math.h>

/*** DOCUMENTATION
	<function name="PITCH_SHIFT" language="en_US">
		<synopsis>
			Pitch shift both tx and rx audio streams on a channel.
		</synopsis>
		<syntax>
			<parameter name="channel direction" required="true">
				<para>Direction can be either <literal>rx</literal>, <literal>tx</literal>, or
				<literal>both</literal>.  The direction can either be set to a valid floating
				point number between 0.1 and 4.0 or one of the enum values listed below. A value
				of 1.0 has no effect.  Greater than 1 raises the pitch. Lower than 1 lowers
				the pitch.</para>

				<para>The pitch amount can also be set by the following values</para>
				<enumlist>
					<enum name = "highest" />
					<enum name = "higher" />
					<enum name = "high" />
					<enum name = "low" />
					<enum name = "lower" />
					<enum name = "lowest" />
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Examples:</para>
			<para>exten => 1,1,Set(PITCH_SHIFT(tx)=highest); raises pitch an octave </para>
			<para>exten => 1,1,Set(PITCH_SHIFT(rx)=higher) ; raises pitch more </para>
			<para>exten => 1,1,Set(PITCH_SHIFT(both)=high)   ; raises pitch </para>
			<para>exten => 1,1,Set(PITCH_SHIFT(rx)=low)    ; lowers pitch </para>
			<para>exten => 1,1,Set(PITCH_SHIFT(tx)=lower)  ; lowers pitch more </para>
			<para>exten => 1,1,Set(PITCH_SHIFT(both)=lowest) ; lowers pitch an octave </para>

			<para>exten => 1,1,Set(PITCH_SHIFT(rx)=0.8)    ; lowers pitch </para>
			<para>exten => 1,1,Set(PITCH_SHIFT(tx)=1.5)    ; raises pitch </para>
		</description>
	</function>
 ***/

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define MAX_FRAME_LENGTH 256

#define HIGHEST 2
#define HIGHER 1.5
#define HIGH 1.25
#define LOW .85
#define LOWER .7
#define LOWEST .5

struct fft_data {
	float in_fifo[MAX_FRAME_LENGTH];
	float out_fifo[MAX_FRAME_LENGTH];
	float fft_worksp[2*MAX_FRAME_LENGTH];
	float last_phase[MAX_FRAME_LENGTH/2+1];
	float sum_phase[MAX_FRAME_LENGTH/2+1];
	float output_accum[2*MAX_FRAME_LENGTH];
	float ana_freq[MAX_FRAME_LENGTH];
	float ana_magn[MAX_FRAME_LENGTH];
	float syn_freq[MAX_FRAME_LENGTH];
	float sys_magn[MAX_FRAME_LENGTH];
	long gRover;
	float shift_amount;
};

struct pitchshift_data {
	struct ast_audiohook audiohook;

	struct fft_data rx;
	struct fft_data tx;
};

static void smb_fft(float *fft_buffer, long fft_frame_size, long sign);
static void smb_pitch_shift(float pitchShift, long num_samps_to_process, long fft_frame_size, long osamp, float sample_rate, int16_t *indata, int16_t *outdata, struct fft_data *fft_data);
static int pitch_shift(struct ast_frame *f, float amount, struct fft_data *fft_data);

static void destroy_callback(void *data)
{
	struct pitchshift_data *shift = data;

	ast_audiohook_destroy(&shift->audiohook);
	ast_free(shift);
};

static const struct ast_datastore_info pitchshift_datastore = {
	.type = "pitchshift",
	.destroy = destroy_callback
};

static int pitchshift_cb(struct ast_audiohook *audiohook, struct ast_channel *chan, struct ast_frame *f, enum ast_audiohook_direction direction)
{
	struct ast_datastore *datastore = NULL;
	struct pitchshift_data *shift = NULL;


	if (!f) {
		return 0;
	}
	if (audiohook->status == AST_AUDIOHOOK_STATUS_DONE) {
		return -1;
	}

	if (!(datastore = ast_channel_datastore_find(chan, &pitchshift_datastore, NULL))) {
		return -1;
	}

	shift = datastore->data;

	if (direction == AST_AUDIOHOOK_DIRECTION_WRITE) {
		pitch_shift(f, shift->tx.shift_amount, &shift->tx);
	} else {
		pitch_shift(f, shift->rx.shift_amount, &shift->rx);
	}

	return 0;
}

static int pitchshift_helper(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_datastore *datastore = NULL;
	struct pitchshift_data *shift = NULL;
	int new = 0;
	float amount = 0;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	ast_channel_lock(chan);
	if (!(datastore = ast_channel_datastore_find(chan, &pitchshift_datastore, NULL))) {
		ast_channel_unlock(chan);

		if (!(datastore = ast_datastore_alloc(&pitchshift_datastore, NULL))) {
			return 0;
		}
		if (!(shift = ast_calloc(1, sizeof(*shift)))) {
			ast_datastore_free(datastore);
			return 0;
		}

		ast_audiohook_init(&shift->audiohook, AST_AUDIOHOOK_TYPE_MANIPULATE, "pitch_shift", AST_AUDIOHOOK_MANIPULATE_ALL_RATES);
		shift->audiohook.manipulate_callback = pitchshift_cb;
		datastore->data = shift;
		new = 1;
	} else {
		ast_channel_unlock(chan);
		shift = datastore->data;
	}


	if (!strcasecmp(value, "highest")) {
		amount = HIGHEST;
	} else if (!strcasecmp(value, "higher")) {
		amount = HIGHER;
	} else if (!strcasecmp(value, "high")) {
		amount = HIGH;
	} else if (!strcasecmp(value, "lowest")) {
		amount = LOWEST;
	} else if (!strcasecmp(value, "lower")) {
		amount = LOWER;
	} else if (!strcasecmp(value, "low")) {
		amount = LOW;
	} else {
		if (!sscanf(value, "%30f", &amount) || (amount <= 0) || (amount > 4)) {
			goto cleanup_error;
		}
	}

	if (!strcasecmp(data, "rx")) {
		shift->rx.shift_amount = amount;
	} else if (!strcasecmp(data, "tx")) {
		shift->tx.shift_amount = amount;
	} else if (!strcasecmp(data, "both")) {
		shift->rx.shift_amount = amount;
		shift->tx.shift_amount = amount;
	} else {
		goto cleanup_error;
	}

	if (new) {
		ast_channel_lock(chan);
		ast_channel_datastore_add(chan, datastore);
		ast_channel_unlock(chan);
		ast_audiohook_attach(chan, &shift->audiohook);
	}

	return 0;

cleanup_error:

	ast_log(LOG_ERROR, "Invalid argument provided to the %s function\n", cmd);
	if (new) {
		ast_datastore_free(datastore);
	}
	return -1;
}

static void smb_fft(float *fft_buffer, long fft_frame_size, long sign)
{
	float wr, wi, arg, *p1, *p2, temp;
	float tr, ti, ur, ui, *p1r, *p1i, *p2r, *p2i;
	long i, bitm, j, le, le2, k;

	for (i = 2; i < 2 * fft_frame_size - 2; i += 2) {
		for (bitm = 2, j = 0; bitm < 2 * fft_frame_size; bitm <<= 1) {
			if (i & bitm) {
				j++;
			}
			j <<= 1;
		}
		if (i < j) {
			p1 = fft_buffer + i; p2 = fft_buffer + j;
			temp = *p1; *(p1++) = *p2;
			*(p2++) = temp; temp = *p1;
			*p1 = *p2; *p2 = temp;
		}
	}
	for (k = 0, le = 2; k < (long) (log(fft_frame_size) / log(2.) + .5); k++) {
		le <<= 1;
		le2 = le>>1;
		ur = 1.0;
		ui = 0.0;
		arg = M_PI / (le2>>1);
		wr = cos(arg);
		wi = sign * sin(arg);
		for (j = 0; j < le2; j += 2) {
			p1r = fft_buffer+j; p1i = p1r + 1;
			p2r = p1r + le2; p2i = p2r + 1;
			for (i = j; i < 2 * fft_frame_size; i += le) {
				tr = *p2r * ur - *p2i * ui;
				ti = *p2r * ui + *p2i * ur;
				*p2r = *p1r - tr; *p2i = *p1i - ti;
				*p1r += tr; *p1i += ti;
				p1r += le; p1i += le;
				p2r += le; p2i += le;
			}
			tr = ur * wr - ui * wi;
			ui = ur * wi + ui * wr;
			ur = tr;
		}
	}
}

static void smb_pitch_shift(float pitchShift, long num_samps_to_process, long fft_frame_size, long osamp, float sample_rate, int16_t *indata, int16_t *outdata, struct fft_data *fft_data)
{
	float *in_fifo = fft_data->in_fifo;
	float *out_fifo = fft_data->out_fifo;
	float *fft_worksp = fft_data->fft_worksp;
	float *last_phase = fft_data->last_phase;
	float *sum_phase = fft_data->sum_phase;
	float *output_accum = fft_data->output_accum;
	float *ana_freq = fft_data->ana_freq;
	float *ana_magn = fft_data->ana_magn;
	float *syn_freq = fft_data->syn_freq;
	float *sys_magn = fft_data->sys_magn;

	double magn, phase, tmp, window, real, imag;
	double freq_per_bin, expct;
	long i,k, qpd, index, in_fifo_latency, step_size, fft_frame_size2;

	/* set up some handy variables */
	fft_frame_size2 = fft_frame_size / 2;
	step_size = fft_frame_size / osamp;
	freq_per_bin = sample_rate / (double) fft_frame_size;
	expct = 2. * M_PI * (double) step_size / (double) fft_frame_size;
	in_fifo_latency = fft_frame_size-step_size;

	if (fft_data->gRover == 0) {
		fft_data->gRover = in_fifo_latency;
	}

	/* main processing loop */
	for (i = 0; i < num_samps_to_process; i++){

		/* As long as we have not yet collected enough data just read in */
		in_fifo[fft_data->gRover] = indata[i];
		outdata[i] = out_fifo[fft_data->gRover - in_fifo_latency];
		fft_data->gRover++;

		/* now we have enough data for processing */
		if (fft_data->gRover >= fft_frame_size) {
			fft_data->gRover = in_fifo_latency;

			/* do windowing and re,im interleave */
			for (k = 0; k < fft_frame_size;k++) {
				window = -.5 * cos(2. * M_PI * (double) k / (double) fft_frame_size) + .5;
				fft_worksp[2*k] = in_fifo[k] * window;
				fft_worksp[2*k+1] = 0.;
			}

			/* ***************** ANALYSIS ******************* */
			/* do transform */
			smb_fft(fft_worksp, fft_frame_size, -1);

			/* this is the analysis step */
			for (k = 0; k <= fft_frame_size2; k++) {

				/* de-interlace FFT buffer */
				real = fft_worksp[2*k];
				imag = fft_worksp[2*k+1];

				/* compute magnitude and phase */
				magn = 2. * sqrt(real * real + imag * imag);
				phase = atan2(imag, real);

				/* compute phase difference */
				tmp = phase - last_phase[k];
				last_phase[k] = phase;

				/* subtract expected phase difference */
				tmp -= (double) k * expct;

				/* map delta phase into +/- Pi interval */
				qpd = tmp / M_PI;
				if (qpd >= 0) {
					qpd += qpd & 1;
				} else {
					qpd -= qpd & 1;
				}
				tmp -= M_PI * (double) qpd;

				/* get deviation from bin frequency from the +/- Pi interval */
				tmp = osamp * tmp / (2. * M_PI);

				/* compute the k-th partials' true frequency */
				tmp = (double) k * freq_per_bin + tmp * freq_per_bin;

				/* store magnitude and true frequency in analysis arrays */
				ana_magn[k] = magn;
				ana_freq[k] = tmp;

			}

			/* ***************** PROCESSING ******************* */
			/* this does the actual pitch shifting */
			memset(sys_magn, 0, fft_frame_size * sizeof(float));
			memset(syn_freq, 0, fft_frame_size * sizeof(float));
			for (k = 0; k <= fft_frame_size2; k++) {
				index = k * pitchShift;
				if (index <= fft_frame_size2) {
					sys_magn[index] += ana_magn[k];
					syn_freq[index] = ana_freq[k] * pitchShift;
				}
			}

			/* ***************** SYNTHESIS ******************* */
			/* this is the synthesis step */
			for (k = 0; k <= fft_frame_size2; k++) {

				/* get magnitude and true frequency from synthesis arrays */
				magn = sys_magn[k];
				tmp = syn_freq[k];

				/* subtract bin mid frequency */
				tmp -= (double) k * freq_per_bin;

				/* get bin deviation from freq deviation */
				tmp /= freq_per_bin;

				/* take osamp into account */
				tmp = 2. * M_PI * tmp / osamp;

				/* add the overlap phase advance back in */
				tmp += (double) k * expct;

				/* accumulate delta phase to get bin phase */
				sum_phase[k] += tmp;
				phase = sum_phase[k];

				/* get real and imag part and re-interleave */
				fft_worksp[2*k] = magn * cos(phase);
				fft_worksp[2*k+1] = magn * sin(phase);
			}

			/* zero negative frequencies */
			for (k = fft_frame_size + 2; k < 2 * fft_frame_size; k++) {
				fft_worksp[k] = 0.;
			}

			/* do inverse transform */
			smb_fft(fft_worksp, fft_frame_size, 1);

			/* do windowing and add to output accumulator */
			for (k = 0; k < fft_frame_size; k++) {
				window = -.5 * cos(2. * M_PI * (double) k / (double) fft_frame_size) + .5;
				output_accum[k] += 2. * window * fft_worksp[2*k] / (fft_frame_size2 * osamp);
			}
			for (k = 0; k < step_size; k++) {
				out_fifo[k] = output_accum[k];
			}

			/* shift accumulator */
			memmove(output_accum, output_accum+step_size, fft_frame_size * sizeof(float));

			/* move input FIFO */
			for (k = 0; k < in_fifo_latency; k++) {
				in_fifo[k] = in_fifo[k+step_size];
			}
		}
	}
}

static int pitch_shift(struct ast_frame *f, float amount, struct fft_data *fft)
{
	int16_t *fun = (int16_t *) f->data.ptr;
	int samples;

	/* an amount of 1 has no effect */
	if (!amount || amount == 1 || !fun || (f->samples % 32)) {
		return 0;
	}
	for (samples = 0; samples < f->samples; samples += 32) {
		smb_pitch_shift(amount, 32, MAX_FRAME_LENGTH, 32, ast_format_get_sample_rate(f->subclass.format), fun+samples, fun+samples, fft);
	}

	return 0;
}

static struct ast_custom_function pitch_shift_function = {
	.name = "PITCH_SHIFT",
	.write = pitchshift_helper,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&pitch_shift_function);
}

static int load_module(void)
{
	int res = ast_custom_function_register(&pitch_shift_function);
	return res ? AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "Audio Effects Dialplan Functions");

