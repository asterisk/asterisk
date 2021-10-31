/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * Goertzel routines are borrowed from Steve Underwood's tremendous work on the
 * DTMF detector.
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
 * \brief Convenience Signal Processing routines
 *
 * \author Mark Spencer <markster@digium.com>
 * \author Steve Underwood <steveu@coppice.org>
 */

/*! \li \ref dsp.c uses the configuration file \ref dsp.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page dsp.conf dsp.conf
 * \verbinclude dsp.conf.sample
 */

/* Some routines from tone_detect.c by Steven Underwood as published under the zapata library */
/*
	tone_detect.c - General telephony tone detection, and specific
					detection of DTMF.

	Copyright (C) 2001  Steve Underwood <steveu@coppice.org>

	Despite my general liking of the GPL, I place this code in the
	public domain for the benefit of all mankind - even the slimy
	ones who might try to proprietize my work and use it to my
	detriment.
*/

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <math.h>

#include "asterisk/module.h"
#include "asterisk/frame.h"
#include "asterisk/format_cache.h"
#include "asterisk/channel.h"
#include "asterisk/dsp.h"
#include "asterisk/ulaw.h"
#include "asterisk/alaw.h"
#include "asterisk/utils.h"
#include "asterisk/options.h"
#include "asterisk/config.h"
#include "asterisk/test.h"

/*! Number of goertzels for progress detect */
enum gsamp_size {
	GSAMP_SIZE_NA = 183,			/*!< North America - 350, 440, 480, 620, 950, 1400, 1800 Hz */
	GSAMP_SIZE_CR = 188,			/*!< Costa Rica, Brazil - Only care about 425 Hz */
	GSAMP_SIZE_UK = 160			/*!< UK disconnect goertzel feed - should trigger 400hz */
};

enum prog_mode {
	PROG_MODE_NA = 0,
	PROG_MODE_CR,
	PROG_MODE_UK
};

enum freq_index {
	/*! For US modes { */
	HZ_350 = 0,
	HZ_440,
	HZ_480,
	HZ_620,
	HZ_950,
	HZ_1400,
	HZ_1800, /*!< } */

	/*! For CR/BR modes */
	HZ_425 = 0,

	/*! For UK mode */
	HZ_350UK = 0,
	HZ_400UK,
	HZ_440UK
};

static struct progalias {
	char *name;
	enum prog_mode mode;
} aliases[] = {
	{ "us", PROG_MODE_NA },
	{ "ca", PROG_MODE_NA },
	{ "cr", PROG_MODE_CR },
	{ "br", PROG_MODE_CR },
	{ "uk", PROG_MODE_UK },
};

#define FREQ_ARRAY_SIZE 7

static struct progress {
	enum gsamp_size size;
	int freqs[FREQ_ARRAY_SIZE];
} modes[] = {
	{ GSAMP_SIZE_NA, { 350, 440, 480, 620, 950, 1400, 1800 } },	/*!< North America */
	{ GSAMP_SIZE_CR, { 425 } },					/*!< Costa Rica, Brazil */
	{ GSAMP_SIZE_UK, { 350, 400, 440 } },				/*!< UK */
};

/*!
 * \brief Default minimum average magnitude threshold to determine talking/noise by the DSP.
 *
 * \details
 * The magnitude calculated for this threshold is determined by
 * averaging the absolute value of all samples within a frame.
 *
 * This value is the threshold for which a frame's average magnitude
 * is determined to either be silence (below the threshold) or
 * noise/talking (at or above the threshold).  Please note that while
 * the default threshold is an even exponent of 2, there is no
 * requirement that it be so.  The threshold will work for any value
 * between 1 and 2^15.
 */
#define DEFAULT_THRESHOLD	512

enum busy_detect {
	BUSY_PERCENT = 10,	/*!< The percentage difference between the two last silence periods */
	BUSY_PAT_PERCENT = 7,	/*!< The percentage difference between measured and actual pattern */
	BUSY_THRESHOLD = 100,	/*!< Max number of ms difference between max and min times in busy */
	BUSY_MIN = 75,		/*!< Busy must be at least 80 ms in half-cadence */
	BUSY_MAX = 3100		/*!< Busy can't be longer than 3100 ms in half-cadence */
};

/*! Remember last 15 units */
#define DSP_HISTORY		15

#define TONE_THRESH		10.0	/*!< How much louder the tone should be than channel energy */
#define TONE_MIN_THRESH		1e8	/*!< How much tone there should be at least to attempt */

/*! All THRESH_XXX values are in GSAMP_SIZE chunks (us = 22ms) */
enum gsamp_thresh {
	THRESH_RING = 8,		/*!< Need at least 150ms ring to accept */
	THRESH_TALK = 2,		/*!< Talk detection does not work continuously */
	THRESH_BUSY = 4,		/*!< Need at least 80ms to accept */
	THRESH_CONGESTION = 4,		/*!< Need at least 80ms to accept */
	THRESH_HANGUP = 60,		/*!< Need at least 1300ms to accept hangup */
	THRESH_RING2ANSWER = 300	/*!< Timeout from start of ring to answer (about 6600 ms) */
};

#define	MAX_DTMF_DIGITS		128

/* Basic DTMF (AT&T) specs:
 *
 * Minimum tone on = 40ms
 * Minimum tone off = 50ms
 * Maximum digit rate = 10 per second
 * Normal twist <= 8dB accepted
 * Reverse twist <= 4dB accepted
 * S/N >= 15dB will detect OK
 * Attenuation <= 26dB will detect OK
 * Frequency tolerance +- 1.5% will detect, +-3.5% will reject
 */

#define DTMF_THRESHOLD		8.0e7
#define TONE_THRESHOLD		7.8e7

#define DEF_DTMF_NORMAL_TWIST		6.31	 /* 8.0dB */
#define DEF_RELAX_DTMF_NORMAL_TWIST	6.31	 /* 8.0dB */

#ifdef	RADIO_RELAX
#define DEF_DTMF_REVERSE_TWIST		2.51	 /* 4.01dB */
#define DEF_RELAX_DTMF_REVERSE_TWIST	6.61	 /* 8.2dB */
#else
#define DEF_DTMF_REVERSE_TWIST		2.51	 /* 4.01dB */
#define DEF_RELAX_DTMF_REVERSE_TWIST	3.98	 /* 6.0dB */
#endif

#define DTMF_RELATIVE_PEAK_ROW	6.3     /* 8dB */
#define DTMF_RELATIVE_PEAK_COL	6.3     /* 8dB */
#define DTMF_TO_TOTAL_ENERGY	42.0

#define BELL_MF_THRESHOLD	1.6e9
#define BELL_MF_TWIST		4.0     /* 6dB */
#define BELL_MF_RELATIVE_PEAK	12.6    /* 11dB */

#if defined(BUSYDETECT_TONEONLY) && defined(BUSYDETECT_COMPARE_TONE_AND_SILENCE)
#error You cant use BUSYDETECT_TONEONLY together with BUSYDETECT_COMPARE_TONE_AND_SILENCE
#endif

/* The CNG signal consists of the transmission of 1100 Hz for 1/2 second,
 * followed by a 3 second silent (2100 Hz OFF) period.
 */
#define FAX_TONE_CNG_FREQ	1100
#define FAX_TONE_CNG_DURATION	500	/* ms */
#define FAX_TONE_CNG_DB		16

/* This signal may be sent by the Terminating FAX machine anywhere between
 * 1.8 to 2.5 seconds AFTER answering the call.  The CED signal consists
 * of a 2100 Hz tone that is from 2.6 to 4 seconds in duration.
*/
#define FAX_TONE_CED_FREQ	2100
#define FAX_TONE_CED_DURATION	2600	/* ms */
#define FAX_TONE_CED_DB		16

#define DEFAULT_SAMPLE_RATE		8000

/* MF goertzel size */
#define MF_GSIZE		120

/* DTMF goertzel size */
#define DTMF_GSIZE		102

/* How many successive hits needed to consider begin of a digit
 * IE. Override with dtmf_hits_to_begin=4 in dsp.conf
 */
#define DEF_DTMF_HITS_TO_BEGIN	2

/* How many successive misses needed to consider end of a digit
 * IE. Override with dtmf_misses_to_end=4 in dsp.conf
 */
#define DEF_DTMF_MISSES_TO_END	3

/*!
 * \brief The default silence threshold we will use if an alternate
 * configured value is not present or is invalid.
 */
static const int DEFAULT_SILENCE_THRESHOLD = 256;

#define CONFIG_FILE_NAME "dsp.conf"

typedef struct {
	/*! The previous previous sample calculation (No binary point just plain int) */
	int v2;
	/*! The previous sample calculation (No binary point just plain int) */
	int v3;
	/*! v2 and v3 power of two exponent to keep value in int range */
	int chunky;
	/*! 15 bit fixed point goertzel coefficient = 2 * cos(2 * pi * freq / sample_rate) */
	int fac;
} goertzel_state_t;

typedef struct {
	int value;
	int power;
} goertzel_result_t;

typedef struct
{
	int freq;
	int block_size;
	int squelch;		/* Remove (squelch) tone */
	goertzel_state_t tone;
	float energy;		/* Accumulated energy of the current block */
	int samples_pending;	/* Samples remain to complete the current block */
	int mute_samples;	/* How many additional samples needs to be muted to suppress already detected tone */

	int hits_required;	/* How many successive blocks with tone we are looking for */
	float threshold;	/* Energy of the tone relative to energy from all other signals to consider a hit */

	int hit_count;		/* How many successive blocks we consider tone present */
	int last_hit;		/* Indicates if the last processed block was a hit */

} tone_detect_state_t;

typedef struct
{
	goertzel_state_t row_out[4];
	goertzel_state_t col_out[4];
	int hits;			/* How many successive hits we have seen already */
	int misses;			/* How many successive misses we have seen already */
	int lasthit;
	int current_hit;
	float energy;
	int current_sample;
	int mute_samples;
} dtmf_detect_state_t;

typedef struct
{
	goertzel_state_t tone_out[6];
	int current_hit;
	int hits[5];
	int current_sample;
	int mute_samples;
} mf_detect_state_t;

typedef struct
{
	char digits[MAX_DTMF_DIGITS + 1];
	int digitlen[MAX_DTMF_DIGITS + 1];
	int current_digits;
	int detected_digits;
	int lost_digits;

	union {
		dtmf_detect_state_t dtmf;
		mf_detect_state_t mf;
	} td;
} digit_detect_state_t;

static const float dtmf_row[] = {
	697.0,  770.0,  852.0,  941.0
};
static const float dtmf_col[] = {
	1209.0, 1336.0, 1477.0, 1633.0
};
static const float mf_tones[] = {
	700.0, 900.0, 1100.0, 1300.0, 1500.0, 1700.0
};
static const char dtmf_positions[] = "123A" "456B" "789C" "*0#D";
static const char bell_mf_positions[] = "1247C-358A--69*---0B----#";
static int thresholds[THRESHOLD_MAX];
static float dtmf_normal_twist;		/* AT&T = 8dB */
static float dtmf_reverse_twist;	/* AT&T = 4dB */
static float relax_dtmf_normal_twist;	/* AT&T = 8dB */
static float relax_dtmf_reverse_twist;	/* AT&T = 6dB */
static int dtmf_hits_to_begin;		/* How many successive hits needed to consider begin of a digit */
static int dtmf_misses_to_end;		/* How many successive misses needed to consider end of a digit */

static inline void goertzel_sample(goertzel_state_t *s, short sample)
{
	int v1;

	/*
	 * Shift previous values so
	 * v1 is previous previous value
	 * v2 is previous value
	 * until the new v3 is calculated.
	 */
	v1 = s->v2;
	s->v2 = s->v3;

	/* Discard the binary fraction introduced by s->fac */
	s->v3 = (s->fac * s->v2) >> 15;
	/* Scale sample to match previous values */
	s->v3 = s->v3 - v1 + (sample >> s->chunky);

	if (abs(s->v3) > (1 << 15)) {
		/* The result is now too large so increase the chunky power. */
		s->chunky++;
		s->v3 = s->v3 >> 1;
		s->v2 = s->v2 >> 1;
	}
}

static inline float goertzel_result(goertzel_state_t *s)
{
	goertzel_result_t r;

	r.value = (s->v3 * s->v3) + (s->v2 * s->v2);
	r.value -= ((s->v2 * s->v3) >> 15) * s->fac;
	/*
	 * We have to double the exponent because we multiplied the
	 * previous sample calculation values together.
	 */
	r.power = s->chunky * 2;
	return (float)r.value * (float)(1 << r.power);
}

static inline void goertzel_init(goertzel_state_t *s, float freq, unsigned int sample_rate)
{
	s->v2 = s->v3 = s->chunky = 0;
	s->fac = (int)(32768.0 * 2.0 * cos(2.0 * M_PI * freq / sample_rate));
}

static inline void goertzel_reset(goertzel_state_t *s)
{
	s->v2 = s->v3 = s->chunky = 0;
}

typedef struct {
	int start;
	int end;
} fragment_t;

/* Note on tone suppression (squelching). Individual detectors (DTMF/MF/generic tone)
 * report fragments of the frame in which detected tone resides and which needs
 * to be "muted" in order to suppress the tone. To mark fragment for muting,
 * detectors call mute_fragment passing fragment_t there. Multiple fragments
 * can be marked and ast_dsp_process later will mute all of them.
 *
 * Note: When tone starts in the middle of a Goertzel block, it won't be properly
 * detected in that block, only in the next. If we only mute the next block
 * where tone is actually detected, the user will still hear beginning
 * of the tone in preceeding block. This is why we usually want to mute some amount
 * of samples preceeding and following the block where tone was detected.
*/

struct ast_dsp {
	struct ast_frame f;
	int threshold;
	/*! Accumulated total silence in ms since last talking/noise. */
	int totalsilence;
	/*! Accumulated total talking/noise in ms since last silence. */
	int totalnoise;
	int features;
	int ringtimeout;
	int busymaybe;
	int busycount;
	struct ast_dsp_busy_pattern busy_cadence;
	int historicnoise[DSP_HISTORY];
	int historicsilence[DSP_HISTORY];
	goertzel_state_t freqs[FREQ_ARRAY_SIZE];
	int freqcount;
	int gsamps;
	enum gsamp_size gsamp_size;
	enum prog_mode progmode;
	int tstate;
	int tcount;
	int digitmode;
	int faxmode;
	int freqmode;
	int dtmf_began;
	int display_inband_dtmf_warning;
	float genergy;
	int mute_fragments;
	unsigned int sample_rate;
	fragment_t mute_data[5];
	digit_detect_state_t digit_state;
	tone_detect_state_t cng_tone_state;
	tone_detect_state_t ced_tone_state;
};

static void mute_fragment(struct ast_dsp *dsp, fragment_t *fragment)
{
	if (dsp->mute_fragments >= ARRAY_LEN(dsp->mute_data)) {
		ast_log(LOG_ERROR, "Too many fragments to mute. Ignoring\n");
		return;
	}

	dsp->mute_data[dsp->mute_fragments++] = *fragment;
}

static void ast_tone_detect_init(tone_detect_state_t *s, int freq, int duration, int amp, unsigned int sample_rate)
{
	int duration_samples;
	float x;
	int periods_in_block;

	s->freq = freq;

	/* Desired tone duration in samples */
	duration_samples = duration * sample_rate / 1000;
	/* We want to allow 10% deviation of tone duration */
	duration_samples = duration_samples * 9 / 10;

	/* If we want to remove tone, it is important to have block size not
	   to exceed frame size. Otherwise by the moment tone is detected it is too late
	   to squelch it from previous frames. Block size is 20ms at the given sample rate.*/
	s->block_size = (20 * sample_rate) / 1000;

	periods_in_block = s->block_size * freq / sample_rate;

	/* Make sure we will have at least 5 periods at target frequency for analysis.
	   This may make block larger than expected packet and will make squelching impossible
	   but at least we will be detecting the tone */
	if (periods_in_block < 5) {
		periods_in_block = 5;
	}

	/* Now calculate final block size. It will contain integer number of periods */
	s->block_size = periods_in_block * sample_rate / freq;

	/* tone_detect is generally only used to detect fax tones and we
	   do not need squelching the fax tones */
	s->squelch = 0;

	/* Account for the first and the last block to be incomplete
	   and thus no tone will be detected in them */
	s->hits_required = (duration_samples - (s->block_size - 1)) / s->block_size;

	goertzel_init(&s->tone, freq, sample_rate);

	s->samples_pending = s->block_size;
	s->hit_count = 0;
	s->last_hit = 0;
	s->energy = 0.0;

	/* We want tone energy to be amp decibels above the rest of the signal (the noise).
	   According to Parseval's theorem the energy computed in time domain equals to energy
	   computed in frequency domain. So subtracting energy in the frequency domain (Goertzel result)
	   from the energy in the time domain we will get energy of the remaining signal (without the tone
	   we are detecting). We will be checking that
		10*log(Ew / (Et - Ew)) > amp
	   Calculate threshold so that we will be actually checking
		Ew > Et * threshold
	*/

	x = pow(10.0, amp / 10.0);
	s->threshold = x / (x + 1);

	ast_debug(1, "Setup tone %d Hz, %d ms, block_size=%d, hits_required=%d\n", freq, duration, s->block_size, s->hits_required);
}

static void ast_fax_detect_init(struct ast_dsp *s)
{
	ast_tone_detect_init(&s->cng_tone_state, FAX_TONE_CNG_FREQ, FAX_TONE_CNG_DURATION, FAX_TONE_CNG_DB, s->sample_rate);
	ast_tone_detect_init(&s->ced_tone_state, FAX_TONE_CED_FREQ, FAX_TONE_CED_DURATION, FAX_TONE_CED_DB, s->sample_rate);
	if (s->faxmode & DSP_FAXMODE_DETECT_SQUELCH) {
		s->cng_tone_state.squelch = 1;
		s->ced_tone_state.squelch = 1;
	}

}

static void ast_freq_detect_init(struct ast_dsp *s, int freq, int dur, int db, int squelch)
{
	/* we can conveniently just use one of the two fax tone states */
	ast_tone_detect_init(&s->cng_tone_state, freq, dur, db, s->sample_rate);
	if (s->freqmode & squelch) {
		s->cng_tone_state.squelch = 1;
	}
}

static void ast_dtmf_detect_init(dtmf_detect_state_t *s, unsigned int sample_rate)
{
	int i;

	for (i = 0; i < 4; i++) {
		goertzel_init(&s->row_out[i], dtmf_row[i], sample_rate);
		goertzel_init(&s->col_out[i], dtmf_col[i], sample_rate);
	}
	s->lasthit = 0;
	s->current_hit = 0;
	s->energy = 0.0;
	s->current_sample = 0;
	s->hits = 0;
	s->misses = 0;
}

static void ast_mf_detect_init(mf_detect_state_t *s, unsigned int sample_rate)
{
	int i;

	for (i = 0; i < 6; i++) {
		goertzel_init(&s->tone_out[i], mf_tones[i], sample_rate);
	}
	s->hits[0] = s->hits[1] = s->hits[2] = s->hits[3] = s->hits[4] = 0;
	s->current_sample = 0;
	s->current_hit = 0;
}

static void ast_digit_detect_init(digit_detect_state_t *s, int mf, unsigned int sample_rate)
{
	s->current_digits = 0;
	s->detected_digits = 0;
	s->lost_digits = 0;
	s->digits[0] = '\0';

	if (mf) {
		ast_mf_detect_init(&s->td.mf, sample_rate);
	} else {
		ast_dtmf_detect_init(&s->td.dtmf, sample_rate);
	}
}

static int tone_detect(struct ast_dsp *dsp, tone_detect_state_t *s, int16_t *amp, int samples)
{
	float tone_energy;
	int i;
	int hit = 0;
	int limit;
	int res = 0;
	int16_t *ptr;
	short samp;
	int start, end;
	fragment_t mute = {0, 0};

	if (s->squelch && s->mute_samples > 0) {
		mute.end = (s->mute_samples < samples) ? s->mute_samples : samples;
		s->mute_samples -= mute.end;
	}

	for (start = 0; start < samples; start = end) {
		/* Process in blocks. */
		limit = samples - start;
		if (limit > s->samples_pending) {
			limit = s->samples_pending;
		}
		end = start + limit;

		for (i = limit, ptr = amp ; i > 0; i--, ptr++) {
			samp = *ptr;
			/* signed 32 bit int should be enough to square any possible signed 16 bit value */
			s->energy += (int32_t) samp * (int32_t) samp;

			goertzel_sample(&s->tone, samp);
		}

		s->samples_pending -= limit;

		if (s->samples_pending) {
			/* Finished incomplete (last) block */
			break;
		}

		tone_energy = goertzel_result(&s->tone);

		/* Scale to make comparable */
		tone_energy *= 2.0;
		s->energy *= s->block_size;

		ast_debug(10, "%d Hz tone %2d Ew=%.4E, Et=%.4E, s/n=%10.2f\n", s->freq, s->hit_count, tone_energy, s->energy, tone_energy / (s->energy - tone_energy));
		hit = 0;
		if (TONE_THRESHOLD <= tone_energy
			&& tone_energy > s->energy * s->threshold) {
			ast_debug(10, "%d Hz tone Hit! %2d Ew=%.4E, Et=%.4E, s/n=%10.2f\n", s->freq, s->hit_count, tone_energy, s->energy, tone_energy / (s->energy - tone_energy));
			hit = 1;
		}

		if (s->hit_count) {
			s->hit_count++;
		}

		if (hit == s->last_hit) {
			if (!hit) {
				/* Two successive misses. Tone ended */
				s->hit_count = 0;
			} else if (!s->hit_count) {
				s->hit_count++;
			}

		}

		if (s->hit_count == s->hits_required) {
			ast_debug(1, "%d Hz tone detected\n", s->freq);
			res = 1;
		}

		s->last_hit = hit;

		/* If we had a hit in this block, include it into mute fragment */
		if (s->squelch && hit) {
			if (mute.end < start - s->block_size) {
				/* There is a gap between fragments */
				mute_fragment(dsp, &mute);
				mute.start = (start > s->block_size) ? (start - s->block_size) : 0;
			}
			mute.end = end + s->block_size;
		}

		/* Reinitialise the detector for the next block */
		/* Reset for the next block */
		goertzel_reset(&s->tone);

		/* Advance to the next block */
		s->energy = 0.0;
		s->samples_pending = s->block_size;

		amp += limit;
	}

	if (s->squelch && mute.end) {
		if (mute.end > samples) {
			s->mute_samples = mute.end - samples;
			mute.end = samples;
		}
		mute_fragment(dsp, &mute);
	}

	return res;
}

static void store_digit(digit_detect_state_t *s, char digit)
{
	s->detected_digits++;
	if (s->current_digits < MAX_DTMF_DIGITS) {
		s->digitlen[s->current_digits] = 0;
		s->digits[s->current_digits++] = digit;
		s->digits[s->current_digits] = '\0';
	} else {
		ast_log(LOG_WARNING, "Digit lost due to full buffer\n");
		s->lost_digits++;
	}
}

static int dtmf_detect(struct ast_dsp *dsp, digit_detect_state_t *s, int16_t amp[], int samples, int squelch, int relax)
{
	float row_energy[4];
	float col_energy[4];
	int i;
	int j;
	int sample;
	short samp;
	int best_row;
	int best_col;
	int hit;
	int limit;
	fragment_t mute = {0, 0};

	if (squelch && s->td.dtmf.mute_samples > 0) {
		mute.end = (s->td.dtmf.mute_samples < samples) ? s->td.dtmf.mute_samples : samples;
		s->td.dtmf.mute_samples -= mute.end;
	}

	hit = 0;
	for (sample = 0; sample < samples; sample = limit) {
		/* DTMF_GSIZE is optimised to meet the DTMF specs. */
		if ((samples - sample) >= (DTMF_GSIZE - s->td.dtmf.current_sample)) {
			limit = sample + (DTMF_GSIZE - s->td.dtmf.current_sample);
		} else {
			limit = samples;
		}
		/* The following unrolled loop takes only 35% (rough estimate) of the
		   time of a rolled loop on the machine on which it was developed */
		for (j = sample; j < limit; j++) {
			samp = amp[j];
			s->td.dtmf.energy += (int32_t) samp * (int32_t) samp;
			/* With GCC 2.95, the following unrolled code seems to take about 35%
			   (rough estimate) as long as a neat little 0-3 loop */
			goertzel_sample(s->td.dtmf.row_out, samp);
			goertzel_sample(s->td.dtmf.col_out, samp);
			goertzel_sample(s->td.dtmf.row_out + 1, samp);
			goertzel_sample(s->td.dtmf.col_out + 1, samp);
			goertzel_sample(s->td.dtmf.row_out + 2, samp);
			goertzel_sample(s->td.dtmf.col_out + 2, samp);
			goertzel_sample(s->td.dtmf.row_out + 3, samp);
			goertzel_sample(s->td.dtmf.col_out + 3, samp);
		}
		s->td.dtmf.current_sample += (limit - sample);
		if (s->td.dtmf.current_sample < DTMF_GSIZE) {
			continue;
		}
		/* We are at the end of a DTMF detection block */
		/* Find the peak row and the peak column */
		row_energy[0] = goertzel_result(&s->td.dtmf.row_out[0]);
		col_energy[0] = goertzel_result(&s->td.dtmf.col_out[0]);

		for (best_row = best_col = 0, i = 1; i < 4; i++) {
			row_energy[i] = goertzel_result(&s->td.dtmf.row_out[i]);
			if (row_energy[i] > row_energy[best_row]) {
				best_row = i;
			}
			col_energy[i] = goertzel_result(&s->td.dtmf.col_out[i]);
			if (col_energy[i] > col_energy[best_col]) {
				best_col = i;
			}
		}
		ast_debug(10, "DTMF best '%c' Erow=%.4E Ecol=%.4E Erc=%.4E Et=%.4E\n",
			dtmf_positions[(best_row << 2) + best_col],
			row_energy[best_row], col_energy[best_col],
			row_energy[best_row] + col_energy[best_col], s->td.dtmf.energy);
		hit = 0;
		/* Basic signal level test and the twist test */
		if (row_energy[best_row] >= DTMF_THRESHOLD &&
		    col_energy[best_col] >= DTMF_THRESHOLD &&
		    col_energy[best_col] < row_energy[best_row] * (relax ? relax_dtmf_reverse_twist : dtmf_reverse_twist) &&
		    row_energy[best_row] < col_energy[best_col] * (relax ? relax_dtmf_normal_twist : dtmf_normal_twist)) {
			/* Relative peak test */
			for (i = 0; i < 4; i++) {
				if ((i != best_col &&
				    col_energy[i] * DTMF_RELATIVE_PEAK_COL > col_energy[best_col]) ||
				    (i != best_row
				     && row_energy[i] * DTMF_RELATIVE_PEAK_ROW > row_energy[best_row])) {
					break;
				}
			}
			/* ... and fraction of total energy test */
			if (i >= 4 &&
			    (row_energy[best_row] + col_energy[best_col]) > DTMF_TO_TOTAL_ENERGY * s->td.dtmf.energy) {
				/* Got a hit */
				hit = dtmf_positions[(best_row << 2) + best_col];
				ast_debug(10, "DTMF hit '%c'\n", hit);
			}
		}

/*
 * Adapted from ETSI ES 201 235-3 V1.3.1 (2006-03)
 * (40ms reference is tunable with hits_to_begin and misses_to_end)
 * each hit/miss is 12.75ms with DTMF_GSIZE at 102
 *
 * Character recognition: When not DRC *(1) and then
 *      Shall exist VSC > 40 ms (hits_to_begin)
 *      May exist 20 ms <= VSC <= 40 ms
 *      Shall not exist VSC < 20 ms
 *
 * Character recognition: When DRC and then
 *      Shall cease Not VSC > 40 ms (misses_to_end)
 *      May cease 20 ms >= Not VSC >= 40 ms
 *      Shall not cease Not VSC < 20 ms
 *
 * *(1) or optionally a different digit recognition condition
 *
 * Legend: VSC The continuous existence of a valid signal condition.
 *      Not VSC The continuous non-existence of valid signal condition.
 *      DRC The existence of digit recognition condition.
 *      Not DRC The non-existence of digit recognition condition.
 */

/*
 * Example: hits_to_begin=2 misses_to_end=3
 * -------A last_hit=A hits=0&1
 * ------AA hits=2 current_hit=A misses=0       BEGIN A
 * -----AA- misses=1 last_hit=' ' hits=0
 * ----AA-- misses=2
 * ---AA--- misses=3 current_hit=' '            END A
 * --AA---B last_hit=B hits=0&1
 * -AA---BC last_hit=C hits=0&1
 * AA---BCC hits=2 current_hit=C misses=0       BEGIN C
 * A---BCC- misses=1 last_hit=' ' hits=0
 * ---BCC-C misses=0 last_hit=C hits=0&1
 * --BCC-CC misses=0
 *
 * Example: hits_to_begin=3 misses_to_end=2
 * -------A last_hit=A hits=0&1
 * ------AA hits=2
 * -----AAA hits=3 current_hit=A misses=0       BEGIN A
 * ----AAAB misses=1 last_hit=B hits=0&1
 * ---AAABB misses=2 current_hit=' ' hits=2     END A
 * --AAABBB hits=3 current_hit=B misses=0       BEGIN B
 * -AAABBBB misses=0
 *
 * Example: hits_to_begin=2 misses_to_end=2
 * -------A last_hit=A hits=0&1
 * ------AA hits=2 current_hit=A misses=0       BEGIN A
 * -----AAB misses=1 hits=0&1
 * ----AABB misses=2 current_hit=' ' hits=2 current_hit=B misses=0 BEGIN B
 * ---AABBB misses=0
 */

		if (s->td.dtmf.current_hit) {
			/* We are in the middle of a digit already */
			if (hit != s->td.dtmf.current_hit) {
				s->td.dtmf.misses++;
				if (s->td.dtmf.misses == dtmf_misses_to_end) {
					/* There were enough misses to consider digit ended */
					s->td.dtmf.current_hit = 0;
				}
			} else {
				s->td.dtmf.misses = 0;
				/* Current hit was same as last, so increment digit duration (of last digit) */
				s->digitlen[s->current_digits - 1] += DTMF_GSIZE;
			}
		}

		/* Look for a start of a new digit no matter if we are already in the middle of some
		   digit or not. This is because hits_to_begin may be smaller than misses_to_end
		   and we may find begin of new digit before we consider last one ended. */

		if (hit != s->td.dtmf.lasthit) {
			s->td.dtmf.lasthit = hit;
			s->td.dtmf.hits = 0;
		}
		if (hit && hit != s->td.dtmf.current_hit) {
			s->td.dtmf.hits++;
			if (s->td.dtmf.hits == dtmf_hits_to_begin) {
				store_digit(s, hit);
				s->digitlen[s->current_digits - 1] = dtmf_hits_to_begin * DTMF_GSIZE;
				s->td.dtmf.current_hit = hit;
				s->td.dtmf.misses = 0;
			}
		}

		/* If we had a hit in this block, include it into mute fragment */
		if (squelch && hit) {
			if (mute.end < sample - DTMF_GSIZE) {
				/* There is a gap between fragments */
				mute_fragment(dsp, &mute);
				mute.start = (sample > DTMF_GSIZE) ? (sample - DTMF_GSIZE) : 0;
			}
			mute.end = limit + DTMF_GSIZE;
		}

		/* Reinitialise the detector for the next block */
		for (i = 0; i < 4; i++) {
			goertzel_reset(&s->td.dtmf.row_out[i]);
			goertzel_reset(&s->td.dtmf.col_out[i]);
		}
		s->td.dtmf.energy = 0.0;
		s->td.dtmf.current_sample = 0;
	}

	if (squelch && mute.end) {
		if (mute.end > samples) {
			s->td.dtmf.mute_samples = mute.end - samples;
			mute.end = samples;
		}
		mute_fragment(dsp, &mute);
	}

	return (s->td.dtmf.current_hit);	/* return the debounced hit */
}

static int mf_detect(struct ast_dsp *dsp, digit_detect_state_t *s, int16_t amp[],
		int samples, int squelch, int relax)
{
	float energy[6];
	int best;
	int second_best;
	int i;
	int j;
	int sample;
	short samp;
	int hit;
	int limit;
	fragment_t mute = {0, 0};

	if (squelch && s->td.mf.mute_samples > 0) {
		mute.end = (s->td.mf.mute_samples < samples) ? s->td.mf.mute_samples : samples;
		s->td.mf.mute_samples -= mute.end;
	}

	hit = 0;
	for (sample = 0; sample < samples; sample = limit) {
		/* 80 is optimised to meet the MF specs. */
		/* XXX So then why is MF_GSIZE defined as 120? */
		if ((samples - sample) >= (MF_GSIZE - s->td.mf.current_sample)) {
			limit = sample + (MF_GSIZE - s->td.mf.current_sample);
		} else {
			limit = samples;
		}
		/* The following unrolled loop takes only 35% (rough estimate) of the
		   time of a rolled loop on the machine on which it was developed */
		for (j = sample; j < limit; j++) {
			/* With GCC 2.95, the following unrolled code seems to take about 35%
			   (rough estimate) as long as a neat little 0-3 loop */
			samp = amp[j];
			goertzel_sample(s->td.mf.tone_out, samp);
			goertzel_sample(s->td.mf.tone_out + 1, samp);
			goertzel_sample(s->td.mf.tone_out + 2, samp);
			goertzel_sample(s->td.mf.tone_out + 3, samp);
			goertzel_sample(s->td.mf.tone_out + 4, samp);
			goertzel_sample(s->td.mf.tone_out + 5, samp);
		}
		s->td.mf.current_sample += (limit - sample);
		if (s->td.mf.current_sample < MF_GSIZE) {
			continue;
		}
		/* We're at the end of an MF detection block.  */
		/* Find the two highest energies. The spec says to look for
		   two tones and two tones only. Taking this literally -ie
		   only two tones pass the minimum threshold - doesn't work
		   well. The sinc function mess, due to rectangular windowing
		   ensure that! Find the two highest energies and ensure they
		   are considerably stronger than any of the others. */
		energy[0] = goertzel_result(&s->td.mf.tone_out[0]);
		energy[1] = goertzel_result(&s->td.mf.tone_out[1]);
		if (energy[0] > energy[1]) {
			best = 0;
			second_best = 1;
		} else {
			best = 1;
			second_best = 0;
		}
		/*endif*/
		for (i = 2; i < 6; i++) {
			energy[i] = goertzel_result(&s->td.mf.tone_out[i]);
			if (energy[i] >= energy[best]) {
				second_best = best;
				best = i;
			} else if (energy[i] >= energy[second_best]) {
				second_best = i;
			}
		}
		/* Basic signal level and twist tests */
		hit = 0;
		if (energy[best] >= BELL_MF_THRESHOLD && energy[second_best] >= BELL_MF_THRESHOLD
		    && energy[best] < energy[second_best]*BELL_MF_TWIST
		    && energy[best] * BELL_MF_TWIST > energy[second_best]) {
			/* Relative peak test */
			hit = -1;
			for (i = 0; i < 6; i++) {
				if (i != best && i != second_best) {
					if (energy[i]*BELL_MF_RELATIVE_PEAK >= energy[second_best]) {
						/* The best two are not clearly the best */
						hit = 0;
						break;
					}
				}
			}
		}
		if (hit) {
			/* Get the values into ascending order */
			if (second_best < best) {
				i = best;
				best = second_best;
				second_best = i;
			}
			best = best * 5 + second_best - 1;
			hit = bell_mf_positions[best];
			/* Look for two successive similar results */
			/* The logic in the next test is:
			   For KP we need 4 successive identical clean detects, with
			   two blocks of something different preceeding it. For anything
			   else we need two successive identical clean detects, with
			   two blocks of something different preceeding it. */
			if (hit == s->td.mf.hits[4] && hit == s->td.mf.hits[3] &&
			   ((hit != '*' && hit != s->td.mf.hits[2] && hit != s->td.mf.hits[1])||
			    (hit == '*' && hit == s->td.mf.hits[2] && hit != s->td.mf.hits[1] &&
			    hit != s->td.mf.hits[0]))) {
				store_digit(s, hit);
			}
		}


		if (hit != s->td.mf.hits[4] && hit != s->td.mf.hits[3]) {
			/* Two successive block without a hit terminate current digit */
			s->td.mf.current_hit = 0;
		}

		s->td.mf.hits[0] = s->td.mf.hits[1];
		s->td.mf.hits[1] = s->td.mf.hits[2];
		s->td.mf.hits[2] = s->td.mf.hits[3];
		s->td.mf.hits[3] = s->td.mf.hits[4];
		s->td.mf.hits[4] = hit;

		/* If we had a hit in this block, include it into mute fragment */
		if (squelch && hit) {
			if (mute.end < sample - MF_GSIZE) {
				/* There is a gap between fragments */
				mute_fragment(dsp, &mute);
				mute.start = (sample > MF_GSIZE) ? (sample - MF_GSIZE) : 0;
			}
			mute.end = limit + MF_GSIZE;
		}

		/* Reinitialise the detector for the next block */
		for (i = 0; i < 6; i++) {
			goertzel_reset(&s->td.mf.tone_out[i]);
		}
		s->td.mf.current_sample = 0;
	}

	if (squelch && mute.end) {
		if (mute.end > samples) {
			s->td.mf.mute_samples = mute.end - samples;
			mute.end = samples;
		}
		mute_fragment(dsp, &mute);
	}

	return (s->td.mf.current_hit); /* return the debounced hit */
}

static inline int pair_there(float p1, float p2, float i1, float i2, float e)
{
	/* See if p1 and p2 are there, relative to i1 and i2 and total energy */
	/* Make sure absolute levels are high enough */
	if ((p1 < TONE_MIN_THRESH) || (p2 < TONE_MIN_THRESH)) {
		return 0;
	}
	/* Amplify ignored stuff */
	i2 *= TONE_THRESH;
	i1 *= TONE_THRESH;
	e *= TONE_THRESH;
	/* Check first tone */
	if ((p1 < i1) || (p1 < i2) || (p1 < e)) {
		return 0;
	}
	/* And second */
	if ((p2 < i1) || (p2 < i2) || (p2 < e)) {
		return 0;
	}
	/* Guess it's there... */
	return 1;
}

static int __ast_dsp_call_progress(struct ast_dsp *dsp, short *s, int len)
{
	short samp;
	int x;
	int y;
	int pass;
	int newstate = DSP_TONE_STATE_SILENCE;
	int res = 0;
	int freqcount = dsp->freqcount > FREQ_ARRAY_SIZE ? FREQ_ARRAY_SIZE : dsp->freqcount;

	while (len) {
		/* Take the lesser of the number of samples we need and what we have */
		pass = len;
		if (pass > dsp->gsamp_size - dsp->gsamps) {
			pass = dsp->gsamp_size - dsp->gsamps;
		}
		for (x = 0; x < pass; x++) {
			samp = s[x];
			dsp->genergy += (int32_t) samp * (int32_t) samp;
			for (y = 0; y < freqcount; y++) {
				goertzel_sample(&dsp->freqs[y], samp);
			}
		}
		s += pass;
		dsp->gsamps += pass;
		len -= pass;
		if (dsp->gsamps == dsp->gsamp_size) {
			float hz[FREQ_ARRAY_SIZE];
			for (y = 0; y < FREQ_ARRAY_SIZE; y++) {
				hz[y] = goertzel_result(&dsp->freqs[y]);
			}
			switch (dsp->progmode) {
			case PROG_MODE_NA:
				if (pair_there(hz[HZ_480], hz[HZ_620], hz[HZ_350], hz[HZ_440], dsp->genergy)) {
					newstate = DSP_TONE_STATE_BUSY;
				} else if (pair_there(hz[HZ_440], hz[HZ_480], hz[HZ_350], hz[HZ_620], dsp->genergy)) {
					newstate = DSP_TONE_STATE_RINGING;
				} else if (pair_there(hz[HZ_350], hz[HZ_440], hz[HZ_480], hz[HZ_620], dsp->genergy)) {
					newstate = DSP_TONE_STATE_DIALTONE;
				} else if (hz[HZ_950] > TONE_MIN_THRESH * TONE_THRESH) {
					newstate = DSP_TONE_STATE_SPECIAL1;
				} else if (hz[HZ_1400] > TONE_MIN_THRESH * TONE_THRESH) {
					/* End of SPECIAL1 or middle of SPECIAL2 */
					if (dsp->tstate == DSP_TONE_STATE_SPECIAL1 || dsp->tstate == DSP_TONE_STATE_SPECIAL2) {
						newstate = DSP_TONE_STATE_SPECIAL2;
					}
				} else if (hz[HZ_1800] > TONE_MIN_THRESH * TONE_THRESH) {
					/* End of SPECIAL2 or middle of SPECIAL3 */
					if (dsp->tstate == DSP_TONE_STATE_SPECIAL2 || dsp->tstate == DSP_TONE_STATE_SPECIAL3) {
						newstate = DSP_TONE_STATE_SPECIAL3;
					}
				} else if (dsp->genergy > TONE_MIN_THRESH * TONE_THRESH) {
					newstate = DSP_TONE_STATE_TALKING;
				} else {
					newstate = DSP_TONE_STATE_SILENCE;
				}
				break;
			case PROG_MODE_CR:
				if (hz[HZ_425] > TONE_MIN_THRESH * TONE_THRESH) {
					newstate = DSP_TONE_STATE_RINGING;
				} else if (dsp->genergy > TONE_MIN_THRESH * TONE_THRESH) {
					newstate = DSP_TONE_STATE_TALKING;
				} else {
					newstate = DSP_TONE_STATE_SILENCE;
				}
				break;
			case PROG_MODE_UK:
				if (hz[HZ_400UK] > TONE_MIN_THRESH * TONE_THRESH) {
					newstate = DSP_TONE_STATE_HUNGUP;
				} else if (pair_there(hz[HZ_350UK], hz[HZ_440UK], hz[HZ_400UK], hz[HZ_400UK], dsp->genergy)) {
					newstate = DSP_TONE_STATE_DIALTONE;
				}
				break;
			default:
				ast_log(LOG_WARNING, "Can't process in unknown prog mode '%u'\n", dsp->progmode);
			}
			if (newstate == dsp->tstate) {
				dsp->tcount++;
				if (dsp->ringtimeout) {
					dsp->ringtimeout++;
				}
				switch (dsp->tstate) {
				case DSP_TONE_STATE_RINGING:
					if ((dsp->features & DSP_PROGRESS_RINGING) &&
					    (dsp->tcount == THRESH_RING)) {
						res = AST_CONTROL_RINGING;
						dsp->ringtimeout = 1;
					}
					break;
				case DSP_TONE_STATE_BUSY:
					if ((dsp->features & DSP_PROGRESS_BUSY) &&
					    (dsp->tcount == THRESH_BUSY)) {
						res = AST_CONTROL_BUSY;
						dsp->features &= ~DSP_FEATURE_CALL_PROGRESS;
					}
					break;
				case DSP_TONE_STATE_TALKING:
					if ((dsp->features & DSP_PROGRESS_TALK) &&
					    (dsp->tcount == THRESH_TALK)) {
						res = AST_CONTROL_ANSWER;
						dsp->features &= ~DSP_FEATURE_CALL_PROGRESS;
					}
					break;
				case DSP_TONE_STATE_SPECIAL3:
					if ((dsp->features & DSP_PROGRESS_CONGESTION) &&
					    (dsp->tcount == THRESH_CONGESTION)) {
						res = AST_CONTROL_CONGESTION;
						dsp->features &= ~DSP_FEATURE_CALL_PROGRESS;
					}
					break;
				case DSP_TONE_STATE_HUNGUP:
					if ((dsp->features & DSP_FEATURE_CALL_PROGRESS) &&
					    (dsp->tcount == THRESH_HANGUP)) {
						res = AST_CONTROL_HANGUP;
						dsp->features &= ~DSP_FEATURE_CALL_PROGRESS;
					}
					break;
				}
				if (dsp->ringtimeout == THRESH_RING2ANSWER) {
					ast_debug(1, "Consider call as answered because of timeout after last ring\n");
					res = AST_CONTROL_ANSWER;
					dsp->features &= ~DSP_FEATURE_CALL_PROGRESS;
				}
			} else {
				ast_debug(5, "Stop state %d with duration %d\n", dsp->tstate, dsp->tcount);
				ast_debug(5, "Start state %d\n", newstate);
				dsp->tstate = newstate;
				dsp->tcount = 1;
			}

			/* Reset goertzel */
			for (x = 0; x < 7; x++) {
				dsp->freqs[x].v2 = dsp->freqs[x].v3 = 0.0;
			}
			dsp->gsamps = 0;
			dsp->genergy = 0.0;
		}
	}

	return res;
}

int ast_dsp_call_progress(struct ast_dsp *dsp, struct ast_frame *inf)
{
	if (inf->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Can't check call progress of non-voice frames\n");
		return 0;
	}
	if (!ast_format_cache_is_slinear(inf->subclass.format)) {
		ast_log(LOG_WARNING, "Can only check call progress in signed-linear frames\n");
		return 0;
	}
	return __ast_dsp_call_progress(dsp, inf->data.ptr, inf->datalen / 2);
}

static int __ast_dsp_silence_noise(struct ast_dsp *dsp, short *s, int len, int *totalsilence, int *totalnoise, int *frames_energy)
{
	int accum;
	int x;
	int res = 0;

	if (!len) {
		return 0;
	}
	accum = 0;
	for (x = 0; x < len; x++) {
		accum += abs(s[x]);
	}
	accum /= len;
	if (accum < dsp->threshold) {
		/* Silent */
		dsp->totalsilence += len / (dsp->sample_rate / 1000);
		if (dsp->totalnoise) {
			/* Move and save history */
			memmove(dsp->historicnoise + DSP_HISTORY - dsp->busycount, dsp->historicnoise + DSP_HISTORY - dsp->busycount + 1, dsp->busycount * sizeof(dsp->historicnoise[0]));
			dsp->historicnoise[DSP_HISTORY - 1] = dsp->totalnoise;
/* we don't want to check for busydetect that frequently */
#if 0
			dsp->busymaybe = 1;
#endif
		}
		dsp->totalnoise = 0;
		res = 1;
	} else {
		/* Not silent */
		dsp->totalnoise += len / (dsp->sample_rate / 1000);
		if (dsp->totalsilence) {
			int silence1 = dsp->historicsilence[DSP_HISTORY - 1];
			int silence2 = dsp->historicsilence[DSP_HISTORY - 2];
			/* Move and save history */
			memmove(dsp->historicsilence + DSP_HISTORY - dsp->busycount, dsp->historicsilence + DSP_HISTORY - dsp->busycount + 1, dsp->busycount * sizeof(dsp->historicsilence[0]));
			dsp->historicsilence[DSP_HISTORY - 1] = dsp->totalsilence;
			/* check if the previous sample differs only by BUSY_PERCENT from the one before it */
			if (silence1 < silence2) {
				if (silence1 + silence1 * BUSY_PERCENT / 100 >= silence2) {
					dsp->busymaybe = 1;
				} else {
					dsp->busymaybe = 0;
				}
			} else {
				if (silence1 - silence1 * BUSY_PERCENT / 100 <= silence2) {
					dsp->busymaybe = 1;
				} else {
					dsp->busymaybe = 0;
				}
			}
		}
		dsp->totalsilence = 0;
	}
	if (totalsilence) {
		*totalsilence = dsp->totalsilence;
	}
	if (totalnoise) {
		*totalnoise = dsp->totalnoise;
	}
	if (frames_energy) {
		*frames_energy = accum;
	}
	return res;
}

int ast_dsp_busydetect(struct ast_dsp *dsp)
{
	int res = 0, x;
#ifndef BUSYDETECT_TONEONLY
	int avgsilence = 0, hitsilence = 0;
#endif
	int avgtone = 0, hittone = 0;

	/* if we have a 4 length pattern, the way busymaybe is set doesn't help us. */
	if (dsp->busy_cadence.length != 4) {
		if (!dsp->busymaybe) {
			return res;
		}
	}

	for (x = DSP_HISTORY - dsp->busycount; x < DSP_HISTORY; x++) {
#ifndef BUSYDETECT_TONEONLY
		avgsilence += dsp->historicsilence[x];
#endif
		avgtone += dsp->historicnoise[x];
	}
#ifndef BUSYDETECT_TONEONLY
	avgsilence /= dsp->busycount;
#endif
	avgtone /= dsp->busycount;
	for (x = DSP_HISTORY - dsp->busycount; x < DSP_HISTORY; x++) {
#ifndef BUSYDETECT_TONEONLY
		if (avgsilence > dsp->historicsilence[x]) {
			if (avgsilence - (avgsilence * BUSY_PERCENT / 100) <= dsp->historicsilence[x]) {
				hitsilence++;
			}
		} else {
			if (avgsilence + (avgsilence * BUSY_PERCENT / 100) >= dsp->historicsilence[x]) {
				hitsilence++;
			}
		}
#endif
		if (avgtone > dsp->historicnoise[x]) {
			if (avgtone - (avgtone * BUSY_PERCENT / 100) <= dsp->historicnoise[x]) {
				hittone++;
			}
		} else {
			if (avgtone + (avgtone * BUSY_PERCENT / 100) >= dsp->historicnoise[x]) {
				hittone++;
			}
		}
	}
#ifndef BUSYDETECT_TONEONLY
	if ((hittone >= dsp->busycount - 1) && (hitsilence >= dsp->busycount - 1) &&
	    (avgtone >= BUSY_MIN && avgtone <= BUSY_MAX) &&
	    (avgsilence >= BUSY_MIN && avgsilence <= BUSY_MAX))
#else
	if ((hittone >= dsp->busycount - 1) && (avgtone >= BUSY_MIN && avgtone <= BUSY_MAX))
#endif
	{
#ifdef BUSYDETECT_COMPARE_TONE_AND_SILENCE
		if (avgtone > avgsilence) {
			if (avgtone - avgtone*BUSY_PERCENT/100 <= avgsilence) {
				res = 1;
			}
		} else {
			if (avgtone + avgtone*BUSY_PERCENT/100 >= avgsilence) {
				res = 1;
			}
		}
#else
		res = 1;
#endif
	}

	/* If we have a 4-length pattern, we can go ahead and just check it in a different way. */
	if (dsp->busy_cadence.length == 4) {
		int x;
		int errors = 0;
		int errors_max = ((4 * dsp->busycount) / 100.0) * BUSY_PAT_PERCENT;

		for (x = DSP_HISTORY - (dsp->busycount); x < DSP_HISTORY; x += 2) {
			int temp_error;
			temp_error = abs(dsp->historicnoise[x] - dsp->busy_cadence.pattern[0]);
			if ((temp_error * 100) / dsp->busy_cadence.pattern[0] > BUSY_PERCENT) {
				errors++;
			}

			temp_error = abs(dsp->historicnoise[x + 1] - dsp->busy_cadence.pattern[2]);
			if ((temp_error * 100) / dsp->busy_cadence.pattern[2] > BUSY_PERCENT) {
				errors++;
			}

			temp_error = abs(dsp->historicsilence[x] - dsp->busy_cadence.pattern[1]);
			if ((temp_error * 100) / dsp->busy_cadence.pattern[1] > BUSY_PERCENT) {
				errors++;
			}

			temp_error = abs(dsp->historicsilence[x + 1] - dsp->busy_cadence.pattern[3]);
			if ((temp_error * 100) / dsp->busy_cadence.pattern[3] > BUSY_PERCENT) {
				errors++;
			}
		}

		ast_debug(5, "errors = %d  max = %d\n", errors, errors_max);

		if (errors <= errors_max) {
			return 1;
		}
	}

	/* If we know the expected busy tone length, check we are in the range */
	if (res && (dsp->busy_cadence.pattern[0] > 0)) {
		if (abs(avgtone - dsp->busy_cadence.pattern[0]) > MAX(dsp->busy_cadence.pattern[0]*BUSY_PAT_PERCENT/100, 20)) {
#ifdef BUSYDETECT_DEBUG
			ast_debug(5, "busy detector: avgtone of %d not close enough to desired %d\n",
				avgtone, dsp->busy_cadence.pattern[0]);
#endif
			res = 0;
		}
	}
#ifndef BUSYDETECT_TONEONLY
	/* If we know the expected busy tone silent-period length, check we are in the range */
	if (res && (dsp->busy_cadence.pattern[1] > 0)) {
		if (abs(avgsilence - dsp->busy_cadence.pattern[1]) > MAX(dsp->busy_cadence.pattern[1]*BUSY_PAT_PERCENT/100, 20)) {
#ifdef BUSYDETECT_DEBUG
		ast_debug(5, "busy detector: avgsilence of %d not close enough to desired %d\n",
			avgsilence, dsp->busy_cadence.pattern[1]);
#endif
			res = 0;
		}
	}
#endif
#if !defined(BUSYDETECT_TONEONLY) && defined(BUSYDETECT_DEBUG)
	if (res) {
		ast_debug(5, "ast_dsp_busydetect detected busy, avgtone: %d, avgsilence %d\n", avgtone, avgsilence);
	} else {
		ast_debug(5, "busy detector: FAILED with avgtone: %d, avgsilence %d\n", avgtone, avgsilence);
	}
#endif
	return res;
}

static int ast_dsp_silence_noise_with_energy(struct ast_dsp *dsp, struct ast_frame *f, int *total, int *frames_energy, int noise)
{
	short *s;
	int len;
	int x;
	unsigned char *odata;

	if (!f) {
		return 0;
	}

	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Can't calculate silence on a non-voice frame\n");
		return 0;
	}

	if (ast_format_cache_is_slinear(f->subclass.format)) {
		s = f->data.ptr;
		len = f->datalen/2;
	} else {
		odata = f->data.ptr;
		len = f->datalen;
		if (ast_format_cmp(f->subclass.format, ast_format_ulaw) == AST_FORMAT_CMP_EQUAL) {
			s = ast_alloca(len * 2);
			for (x = 0; x < len; x++) {
				s[x] = AST_MULAW(odata[x]);
			}
		} else if (ast_format_cmp(f->subclass.format, ast_format_alaw) == AST_FORMAT_CMP_EQUAL) {
			s = ast_alloca(len * 2);
			for (x = 0; x < len; x++) {
				s[x] = AST_ALAW(odata[x]);
			}
		} else {
			ast_log(LOG_WARNING, "Can only calculate silence on signed-linear, alaw or ulaw frames :(\n");
			return 0;
		}
	}

	if (noise) {
		return __ast_dsp_silence_noise(dsp, s, len, NULL, total, frames_energy);
	} else {
		return __ast_dsp_silence_noise(dsp, s, len, total, NULL, frames_energy);
	}
}

int ast_dsp_silence_with_energy(struct ast_dsp *dsp, struct ast_frame *f, int *totalsilence, int *frames_energy)
{
	return ast_dsp_silence_noise_with_energy(dsp, f, totalsilence, frames_energy, 0);
}

int ast_dsp_silence(struct ast_dsp *dsp, struct ast_frame *f, int *totalsilence)
{
	return ast_dsp_silence_noise_with_energy(dsp, f, totalsilence, NULL, 0);
}

int ast_dsp_noise(struct ast_dsp *dsp, struct ast_frame *f, int *totalnoise)
{
	return ast_dsp_silence_noise_with_energy(dsp, f, totalnoise, NULL, 1);
}


struct ast_frame *ast_dsp_process(struct ast_channel *chan, struct ast_dsp *dsp, struct ast_frame *af)
{
	int silence;
	int res;
	int digit = 0, fax_digit = 0, custom_freq_digit = 0;
	int x;
	short *shortdata;
	unsigned char *odata;
	int len;
	struct ast_frame *outf = NULL;

	if (!af) {
		return NULL;
	}
	if (af->frametype != AST_FRAME_VOICE) {
		return af;
	}

	odata = af->data.ptr;
	len = af->datalen;
	/* Make sure we have short data */
	if (ast_format_cache_is_slinear(af->subclass.format)) {
		shortdata = af->data.ptr;
		len = af->datalen / 2;
	} else if (ast_format_cmp(af->subclass.format, ast_format_ulaw) == AST_FORMAT_CMP_EQUAL) {
		shortdata = ast_alloca(af->datalen * 2);
		for (x = 0; x < len; x++) {
			shortdata[x] = AST_MULAW(odata[x]);
		}
	} else if (ast_format_cmp(af->subclass.format, ast_format_alaw) == AST_FORMAT_CMP_EQUAL) {
		shortdata = ast_alloca(af->datalen * 2);
		for (x = 0; x < len; x++) {
			shortdata[x] = AST_ALAW(odata[x]);
		}
	} else {
		/*Display warning only once. Otherwise you would get hundreds of warnings every second */
		if (dsp->display_inband_dtmf_warning) {
			ast_log(LOG_WARNING, "Inband DTMF is not supported on codec %s. Use RFC2833\n", ast_format_get_name(af->subclass.format));
		}
		dsp->display_inband_dtmf_warning = 0;
		return af;
	}

	/* Initially we do not want to mute anything */
	dsp->mute_fragments = 0;

	/* Need to run the silence detection stuff for silence suppression and busy detection */
	if ((dsp->features & DSP_FEATURE_SILENCE_SUPPRESS) || (dsp->features & DSP_FEATURE_BUSY_DETECT)) {
		res = __ast_dsp_silence_noise(dsp, shortdata, len, &silence, NULL, NULL);
	}

	if ((dsp->features & DSP_FEATURE_SILENCE_SUPPRESS) && silence) {
		memset(&dsp->f, 0, sizeof(dsp->f));
		dsp->f.frametype = AST_FRAME_NULL;
		ast_frfree(af);
		return ast_frisolate(&dsp->f);
	}
	if ((dsp->features & DSP_FEATURE_BUSY_DETECT) && ast_dsp_busydetect(dsp)) {
		ast_channel_softhangup_internal_flag_add(chan, AST_SOFTHANGUP_DEV);
		memset(&dsp->f, 0, sizeof(dsp->f));
		dsp->f.frametype = AST_FRAME_CONTROL;
		dsp->f.subclass.integer = AST_CONTROL_BUSY;
		ast_frfree(af);
		ast_debug(1, "Requesting Hangup because the busy tone was detected on channel %s\n", ast_channel_name(chan));
		return ast_frisolate(&dsp->f);
	}

	if ((dsp->features & DSP_FEATURE_FAX_DETECT)) {
		if ((dsp->faxmode & DSP_FAXMODE_DETECT_CNG) && tone_detect(dsp, &dsp->cng_tone_state, shortdata, len)) {
			fax_digit = 'f';
		}

		if ((dsp->faxmode & DSP_FAXMODE_DETECT_CED) && tone_detect(dsp, &dsp->ced_tone_state, shortdata, len)) {
			fax_digit = 'e';
		}
	}

	if ((dsp->features & DSP_FEATURE_FREQ_DETECT)) {
		if ((dsp->freqmode) && tone_detect(dsp, &dsp->cng_tone_state, shortdata, len)) {
			custom_freq_digit = 'q';
		}
	}

	if (dsp->features & (DSP_FEATURE_DIGIT_DETECT | DSP_FEATURE_BUSY_DETECT)) {
		if (dsp->digitmode & DSP_DIGITMODE_MF) {
			digit = mf_detect(dsp, &dsp->digit_state, shortdata, len, (dsp->digitmode & DSP_DIGITMODE_NOQUELCH) == 0, (dsp->digitmode & DSP_DIGITMODE_RELAXDTMF));
		} else {
			digit = dtmf_detect(dsp, &dsp->digit_state, shortdata, len, (dsp->digitmode & DSP_DIGITMODE_NOQUELCH) == 0, (dsp->digitmode & DSP_DIGITMODE_RELAXDTMF));
		}

		if (dsp->digit_state.current_digits) {
			int event = 0, event_len = 0;
			char event_digit = 0;

			if (!dsp->dtmf_began) {
				/* We have not reported DTMF_BEGIN for anything yet */

				if (dsp->features & DSP_FEATURE_DIGIT_DETECT) {
					event = AST_FRAME_DTMF_BEGIN;
					event_digit = dsp->digit_state.digits[0];
				}
				dsp->dtmf_began = 1;

			} else if (dsp->digit_state.current_digits > 1 || digit != dsp->digit_state.digits[0]) {
				/* Digit changed. This means digit we have reported with DTMF_BEGIN ended */
				if (dsp->features & DSP_FEATURE_DIGIT_DETECT) {
					event = AST_FRAME_DTMF_END;
					event_digit = dsp->digit_state.digits[0];
					event_len = dsp->digit_state.digitlen[0] * 1000 / dsp->sample_rate;
				}
				memmove(&dsp->digit_state.digits[0], &dsp->digit_state.digits[1], dsp->digit_state.current_digits);
				memmove(&dsp->digit_state.digitlen[0], &dsp->digit_state.digitlen[1], dsp->digit_state.current_digits * sizeof(dsp->digit_state.digitlen[0]));
				dsp->digit_state.current_digits--;
				dsp->dtmf_began = 0;

				if (dsp->features & DSP_FEATURE_BUSY_DETECT) {
					/* Reset Busy Detector as we have some confirmed activity */
					memset(dsp->historicsilence, 0, sizeof(dsp->historicsilence));
					memset(dsp->historicnoise, 0, sizeof(dsp->historicnoise));
					ast_debug(1, "DTMF Detected - Reset busydetector\n");
				}
			}

			if (event) {
				memset(&dsp->f, 0, sizeof(dsp->f));
				dsp->f.frametype = event;
				dsp->f.subclass.integer = event_digit;
				dsp->f.len = event_len;
				outf = &dsp->f;
				goto done;
			}
		}
	}

	if (fax_digit) {
		/* Fax was detected - digit is either 'f' or 'e' */

		memset(&dsp->f, 0, sizeof(dsp->f));
		dsp->f.frametype = AST_FRAME_DTMF;
		dsp->f.subclass.integer = fax_digit;
		outf = &dsp->f;
		goto done;
	}

	if (custom_freq_digit) {
		/* Custom frequency was detected - digit is 'q' */

		memset(&dsp->f, 0, sizeof(dsp->f));
		dsp->f.frametype = AST_FRAME_DTMF;
		dsp->f.subclass.integer = custom_freq_digit;
		outf = &dsp->f;
		goto done;
	}

	if ((dsp->features & DSP_FEATURE_CALL_PROGRESS)) {
		res = __ast_dsp_call_progress(dsp, shortdata, len);
		if (res) {
			switch (res) {
			case AST_CONTROL_ANSWER:
			case AST_CONTROL_BUSY:
			case AST_CONTROL_RINGING:
			case AST_CONTROL_CONGESTION:
			case AST_CONTROL_HANGUP:
				memset(&dsp->f, 0, sizeof(dsp->f));
				dsp->f.frametype = AST_FRAME_CONTROL;
				dsp->f.subclass.integer = res;
				dsp->f.src = "dsp_progress";
				if (chan) {
					ast_queue_frame(chan, &dsp->f);
				}
				break;
			default:
				ast_log(LOG_WARNING, "Don't know how to represent call progress message %d\n", res);
			}
		}
	} else if ((dsp->features & DSP_FEATURE_WAITDIALTONE)) {
		res = __ast_dsp_call_progress(dsp, shortdata, len);
	}

done:
	/* Mute fragment of the frame */
	for (x = 0; x < dsp->mute_fragments; x++) {
		memset(shortdata + dsp->mute_data[x].start, 0, sizeof(int16_t) * (dsp->mute_data[x].end - dsp->mute_data[x].start));
	}

	if (ast_format_cmp(af->subclass.format, ast_format_ulaw) == AST_FORMAT_CMP_EQUAL) {
		for (x = 0; x < len; x++) {
			odata[x] = AST_LIN2MU((unsigned short) shortdata[x]);
		}
	} else if (ast_format_cmp(af->subclass.format, ast_format_alaw) == AST_FORMAT_CMP_EQUAL) {
		for (x = 0; x < len; x++) {
			odata[x] = AST_LIN2A((unsigned short) shortdata[x]);
		}
	}

	if (outf) {
		if (chan) {
			ast_queue_frame(chan, af);
		}
		ast_frfree(af);
		return ast_frisolate(outf);
	} else {
		return af;
	}
}

static void ast_dsp_prog_reset(struct ast_dsp *dsp)
{
	int max = 0;
	int x;

	dsp->gsamp_size = modes[dsp->progmode].size;
	dsp->gsamps = 0;
	for (x = 0; x < FREQ_ARRAY_SIZE; x++) {
		if (modes[dsp->progmode].freqs[x]) {
			goertzel_init(&dsp->freqs[x], (float)modes[dsp->progmode].freqs[x], dsp->sample_rate);
			max = x + 1;
		}
	}
	dsp->freqcount = max;
	dsp->ringtimeout = 0;
}

unsigned int ast_dsp_get_sample_rate(const struct ast_dsp *dsp)
{
	return dsp->sample_rate;
}

static struct ast_dsp *__ast_dsp_new(unsigned int sample_rate)
{
	struct ast_dsp *dsp;

	if ((dsp = ast_calloc(1, sizeof(*dsp)))) {
		dsp->threshold = DEFAULT_THRESHOLD;
		dsp->features = DSP_FEATURE_SILENCE_SUPPRESS;
		dsp->busycount = DSP_HISTORY;
		dsp->digitmode = DSP_DIGITMODE_DTMF;
		dsp->faxmode = DSP_FAXMODE_DETECT_CNG;
		dsp->sample_rate = sample_rate;
		dsp->freqcount = 0;
		/* Initialize digit detector */
		ast_digit_detect_init(&dsp->digit_state, dsp->digitmode & DSP_DIGITMODE_MF, dsp->sample_rate);
		dsp->display_inband_dtmf_warning = 1;
		/* Initialize initial DSP progress detect parameters */
		ast_dsp_prog_reset(dsp);
		/* Initialize fax detector */
		ast_fax_detect_init(dsp);
	}
	return dsp;
}

struct ast_dsp *ast_dsp_new(void)
{
	return __ast_dsp_new(DEFAULT_SAMPLE_RATE);
}

struct ast_dsp *ast_dsp_new_with_rate(unsigned int sample_rate)
{
	return __ast_dsp_new(sample_rate);
}

void ast_dsp_set_features(struct ast_dsp *dsp, int features)
{
	dsp->features = features;
	if (!(features & DSP_FEATURE_DIGIT_DETECT)) {
		dsp->display_inband_dtmf_warning = 0;
	}
}


int ast_dsp_get_features(struct ast_dsp *dsp)
{
        return (dsp->features);
}


void ast_dsp_free(struct ast_dsp *dsp)
{
	ast_free(dsp);
}

void ast_dsp_set_threshold(struct ast_dsp *dsp, int threshold)
{
	dsp->threshold = threshold;
}

void ast_dsp_set_busy_count(struct ast_dsp *dsp, int cadences)
{
	if (cadences < 4) {
		cadences = 4;
	}
	if (cadences > DSP_HISTORY) {
		cadences = DSP_HISTORY;
	}
	dsp->busycount = cadences;
}

void ast_dsp_set_busy_pattern(struct ast_dsp *dsp, const struct ast_dsp_busy_pattern *cadence)
{
	dsp->busy_cadence = *cadence;
	ast_debug(1, "dsp busy pattern set to %d,%d,%d,%d\n", cadence->pattern[0], cadence->pattern[1], (cadence->length == 4) ? cadence->pattern[2] : 0, (cadence->length == 4) ? cadence->pattern[3] : 0);
}

void ast_dsp_digitreset(struct ast_dsp *dsp)
{
	int i;

	dsp->dtmf_began = 0;
	if (dsp->digitmode & DSP_DIGITMODE_MF) {
		mf_detect_state_t *s = &dsp->digit_state.td.mf;
		/* Reinitialise the detector for the next block */
		for (i = 0; i < 6; i++) {
			goertzel_reset(&s->tone_out[i]);
		}
		s->hits[4] = s->hits[3] = s->hits[2] = s->hits[1] = s->hits[0] = 0;
		s->current_hit = 0;
		s->current_sample = 0;
	} else {
		dtmf_detect_state_t *s = &dsp->digit_state.td.dtmf;
		/* Reinitialise the detector for the next block */
		for (i = 0; i < 4; i++) {
			goertzel_reset(&s->row_out[i]);
			goertzel_reset(&s->col_out[i]);
		}
		s->lasthit = 0;
		s->current_hit = 0;
		s->energy = 0.0;
		s->current_sample = 0;
		s->hits = 0;
		s->misses = 0;
	}

	dsp->digit_state.digits[0] = '\0';
	dsp->digit_state.current_digits = 0;
}

void ast_dsp_reset(struct ast_dsp *dsp)
{
	int x;

	dsp->totalsilence = 0;
	dsp->gsamps = 0;
	for (x = 0; x < 4; x++) {
		dsp->freqs[x].v2 = dsp->freqs[x].v3 = 0.0;
	}
	memset(dsp->historicsilence, 0, sizeof(dsp->historicsilence));
	memset(dsp->historicnoise, 0, sizeof(dsp->historicnoise));
	dsp->ringtimeout = 0;
}

int ast_dsp_set_digitmode(struct ast_dsp *dsp, int digitmode)
{
	int new;
	int old;

	old = dsp->digitmode & (DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_MUTEMAX);
	new = digitmode & (DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_MUTEMAX);
	if (old != new) {
		/* Must initialize structures if switching from MF to DTMF or vice-versa */
		ast_digit_detect_init(&dsp->digit_state, new & DSP_DIGITMODE_MF, dsp->sample_rate);
	}
	dsp->digitmode = digitmode;
	return 0;
}

int ast_dsp_set_freqmode(struct ast_dsp *dsp, int freq, int dur, int db, int squelch)
{
	if (freq > 0) {
		dsp->freqmode = 1;
		ast_freq_detect_init(dsp, freq, dur, db, squelch);
	} else {
		dsp->freqmode = 0;
	}
	return 0;
}

int ast_dsp_set_faxmode(struct ast_dsp *dsp, int faxmode)
{
	if (dsp->faxmode != faxmode) {
		dsp->faxmode = faxmode;
		ast_fax_detect_init(dsp);
	}
	return 0;
}

int ast_dsp_set_call_progress_zone(struct ast_dsp *dsp, char *zone)
{
	int x;

	for (x = 0; x < ARRAY_LEN(aliases); x++) {
		if (!strcasecmp(aliases[x].name, zone)) {
			dsp->progmode = aliases[x].mode;
			ast_dsp_prog_reset(dsp);
			return 0;
		}
	}
	return -1;
}

int ast_dsp_was_muted(struct ast_dsp *dsp)
{
	return (dsp->mute_fragments > 0);
}

int ast_dsp_get_tstate(struct ast_dsp *dsp)
{
	return dsp->tstate;
}

int ast_dsp_get_tcount(struct ast_dsp *dsp)
{
	return dsp->tcount;
}

static int _dsp_init(int reload)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	int cfg_threshold;
	float cfg_twist;

	if ((cfg = ast_config_load2(CONFIG_FILE_NAME, "dsp", config_flags)) == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	}

	thresholds[THRESHOLD_SILENCE] = DEFAULT_SILENCE_THRESHOLD;
	dtmf_normal_twist = DEF_DTMF_NORMAL_TWIST;
	dtmf_reverse_twist = DEF_DTMF_REVERSE_TWIST;
	relax_dtmf_normal_twist = DEF_RELAX_DTMF_NORMAL_TWIST;
	relax_dtmf_reverse_twist = DEF_RELAX_DTMF_REVERSE_TWIST;
        dtmf_hits_to_begin = DEF_DTMF_HITS_TO_BEGIN;
        dtmf_misses_to_end = DEF_DTMF_MISSES_TO_END;

	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEINVALID) {
		return 0;
	}

	for (v = ast_variable_browse(cfg, "default"); v; v = v->next) {
		if (!strcasecmp(v->name, "silencethreshold")) {
			if (sscanf(v->value, "%30d", &cfg_threshold) < 1) {
				ast_log(LOG_WARNING, "Unable to convert '%s' to a numeric value.\n", v->value);
			} else if (cfg_threshold < 0) {
				ast_log(LOG_WARNING, "Invalid silence threshold '%d' specified, using default\n", cfg_threshold);
			} else {
				thresholds[THRESHOLD_SILENCE] = cfg_threshold;
			}
		} else if (!strcasecmp(v->name, "dtmf_normal_twist")) {
			if (sscanf(v->value, "%30f", &cfg_twist) < 1) {
				ast_log(LOG_WARNING, "Unable to convert '%s' to a numeric value.\n", v->value);
			} else if ((cfg_twist < 2.0) || (cfg_twist > 100.0)) {		/* < 3.0dB or > 20dB */
				ast_log(LOG_WARNING, "Invalid dtmf_normal_twist value '%.2f' specified, using default of %.2f\n", cfg_twist, dtmf_normal_twist);
			} else {
				dtmf_normal_twist = cfg_twist;
			}
		} else if (!strcasecmp(v->name, "dtmf_reverse_twist")) {
			if (sscanf(v->value, "%30f", &cfg_twist) < 1) {
				ast_log(LOG_WARNING, "Unable to convert '%s' to a numeric value.\n", v->value);
			} else if ((cfg_twist < 2.0) || (cfg_twist > 100.0)) {		/* < 3.0dB or > 20dB */
				ast_log(LOG_WARNING, "Invalid dtmf_reverse_twist value '%.2f' specified, using default of %.2f\n", cfg_twist, dtmf_reverse_twist);
			} else {
				dtmf_reverse_twist = cfg_twist;
			}
		} else if (!strcasecmp(v->name, "relax_dtmf_normal_twist")) {
			if (sscanf(v->value, "%30f", &cfg_twist) < 1) {
				ast_log(LOG_WARNING, "Unable to convert '%s' to a numeric value.\n", v->value);
			} else if ((cfg_twist < 2.0) || (cfg_twist > 100.0)) {		/* < 3.0dB or > 20dB */
				ast_log(LOG_WARNING, "Invalid relax_dtmf_normal_twist value '%.2f' specified, using default of %.2f\n", cfg_twist, relax_dtmf_normal_twist);
			} else {
				relax_dtmf_normal_twist = cfg_twist;
			}
		} else if (!strcasecmp(v->name, "relax_dtmf_reverse_twist")) {
			if (sscanf(v->value, "%30f", &cfg_twist) < 1) {
				ast_log(LOG_WARNING, "Unable to convert '%s' to a numeric value.\n", v->value);
			} else if ((cfg_twist < 2.0) || (cfg_twist > 100.0)) {		/* < 3.0dB or > 20dB */
				ast_log(LOG_WARNING, "Invalid relax_dtmf_reverse_twist value '%.2f' specified, using default of %.2f\n", cfg_twist, relax_dtmf_reverse_twist);
			} else {
				relax_dtmf_reverse_twist = cfg_twist;
			}
		} else if (!strcasecmp(v->name, "dtmf_hits_to_begin")) {
			if (sscanf(v->value, "%30d", &cfg_threshold) < 1) {
				ast_log(LOG_WARNING, "Unable to convert '%s' to a numeric value.\n", v->value);
			} else if (cfg_threshold < 1) {		/* must be 1 or greater */
				ast_log(LOG_WARNING, "Invalid dtmf_hits_to_begin value '%d' specified, using default of %d\n", cfg_threshold, dtmf_hits_to_begin);
			} else {
				dtmf_hits_to_begin = cfg_threshold;
			}
		} else if (!strcasecmp(v->name, "dtmf_misses_to_end")) {
			if (sscanf(v->value, "%30d", &cfg_threshold) < 1) {
				ast_log(LOG_WARNING, "Unable to convert '%s' to a numeric value.\n", v->value);
			} else if (cfg_threshold < 1) {		/* must be 1 or greater */
				ast_log(LOG_WARNING, "Invalid dtmf_misses_to_end value '%d' specified, using default of %d\n", cfg_threshold, dtmf_misses_to_end);
			} else {
				dtmf_misses_to_end = cfg_threshold;
			}
		}
	}
	ast_config_destroy(cfg);

	return 0;
}

int ast_dsp_get_threshold_from_settings(enum threshold which)
{
	return thresholds[which];
}

#ifdef TEST_FRAMEWORK
static void test_tone_sample_gen(short *slin_buf, int samples, int rate, int freq, short amplitude)
{
	int idx;
	double sample_step = 2.0 * M_PI * freq / rate;/* radians per step */

	for (idx = 0; idx < samples; ++idx) {
		slin_buf[idx] = amplitude * sin(sample_step * idx);
	}
}
#endif

#ifdef TEST_FRAMEWORK
static void test_tone_sample_gen_add(short *slin_buf, int samples, int rate, int freq, short amplitude)
{
	int idx;
	double sample_step = 2.0 * M_PI * freq / rate;/* radians per step */

	for (idx = 0; idx < samples; ++idx) {
		slin_buf[idx] += amplitude * sin(sample_step * idx);
	}
}
#endif

#ifdef TEST_FRAMEWORK
static void test_dual_sample_gen(short *slin_buf, int samples, int rate, int f1, short a1, int f2, short a2)
{
	test_tone_sample_gen(slin_buf, samples, rate, f1, a1);
	test_tone_sample_gen_add(slin_buf, samples, rate, f2, a2);
}
#endif

#ifdef TEST_FRAMEWORK
#define TONE_AMPLITUDE_MAX	0x7fff	/* Max signed linear amplitude */
#define TONE_AMPLITUDE_MIN	80		/* Min signed linear amplitude detectable */

static int test_tone_amplitude_sweep(struct ast_test *test, struct ast_dsp *dsp, tone_detect_state_t *tone_state)
{
	short slin_buf[tone_state->block_size];
	int result;
	int idx;
	struct {
		short amp_val;
		int detect;
	} amp_tests[] = {
		{ .amp_val = TONE_AMPLITUDE_MAX,	.detect = 1, },
		{ .amp_val = 10000,					.detect = 1, },
		{ .amp_val = 1000,					.detect = 1, },
		{ .amp_val = 100,					.detect = 1, },
		{ .amp_val = TONE_AMPLITUDE_MIN,	.detect = 1, },
		{ .amp_val = 75,					.detect = 0, },
		{ .amp_val = 10,					.detect = 0, },
		{ .amp_val = 1,						.detect = 0, },
	};

	result = 0;

	for (idx = 0; idx < ARRAY_LEN(amp_tests); ++idx) {
		int detected;
		int duration;

		ast_debug(1, "Test %d Hz at amplitude %d\n",
			tone_state->freq, amp_tests[idx].amp_val);
		test_tone_sample_gen(slin_buf, tone_state->block_size, DEFAULT_SAMPLE_RATE,
			tone_state->freq, amp_tests[idx].amp_val);

		detected = 0;
		for (duration = 0; !detected && duration < tone_state->hits_required + 3; ++duration) {
			detected = tone_detect(dsp, tone_state, slin_buf, tone_state->block_size) ? 1 : 0;
		}
		if (amp_tests[idx].detect != detected) {
			/*
			 * Both messages are needed.  ast_debug for when figuring out
			 * what went wrong and the test update for normal output before
			 * you start debugging.  The different logging methods are not
			 * synchronized.
			 */
			ast_debug(1,
				"Test %d Hz at amplitude %d failed.  Detected: %s\n",
				tone_state->freq, amp_tests[idx].amp_val,
				detected ? "yes" : "no");
			ast_test_status_update(test,
				"Test %d Hz at amplitude %d failed.  Detected: %s\n",
				tone_state->freq, amp_tests[idx].amp_val,
				detected ? "yes" : "no");
			result = -1;
		}
		tone_state->hit_count = 0;
	}

	return result;
}
#endif

#ifdef TEST_FRAMEWORK
static int test_dtmf_amplitude_sweep(struct ast_test *test, struct ast_dsp *dsp, int digit_index)
{
	short slin_buf[DTMF_GSIZE];
	int result;
	int row;
	int column;
	int idx;
	struct {
		short amp_val;
		int digit;
	} amp_tests[] = {
		/*
		 * XXX Since there is no current DTMF level detection issue.  This test
		 * just checks the current detection levels.
		 */
		{ .amp_val = TONE_AMPLITUDE_MAX/2,	.digit = dtmf_positions[digit_index], },
		{ .amp_val = 10000,					.digit = dtmf_positions[digit_index], },
		{ .amp_val = 1000,					.digit = dtmf_positions[digit_index], },
		{ .amp_val = 500,					.digit = dtmf_positions[digit_index], },
		{ .amp_val = 250,					.digit = dtmf_positions[digit_index], },
		{ .amp_val = 200,					.digit = dtmf_positions[digit_index], },
		{ .amp_val = 180,					.digit = dtmf_positions[digit_index], },
		/* Various digits detect and not detect in this range */
		{ .amp_val = 170,					.digit = 0, },
		{ .amp_val = 100,					.digit = 0, },
		/*
		 * Amplitudes below TONE_AMPLITUDE_MIN start having questionable detection
		 * over quantization and background noise.
		 */
		{ .amp_val = TONE_AMPLITUDE_MIN,	.digit = 0, },
		{ .amp_val = 75,					.digit = 0, },
		{ .amp_val = 10,					.digit = 0, },
		{ .amp_val = 1,						.digit = 0, },
	};

	row = (digit_index >> 2) & 0x03;
	column = digit_index & 0x03;

	result = 0;

	for (idx = 0; idx < ARRAY_LEN(amp_tests); ++idx) {
		int digit;
		int duration;

		ast_debug(1, "Test '%c' at amplitude %d\n",
			dtmf_positions[digit_index], amp_tests[idx].amp_val);
		test_dual_sample_gen(slin_buf, ARRAY_LEN(slin_buf), DEFAULT_SAMPLE_RATE,
			(int) dtmf_row[row], amp_tests[idx].amp_val,
			(int) dtmf_col[column], amp_tests[idx].amp_val);

		digit = 0;
		for (duration = 0; !digit && duration < 3; ++duration) {
			digit = dtmf_detect(dsp, &dsp->digit_state, slin_buf, ARRAY_LEN(slin_buf),
				0, 0);
		}
		if (amp_tests[idx].digit != digit) {
			/*
			 * Both messages are needed.  ast_debug for when figuring out
			 * what went wrong and the test update for normal output before
			 * you start debugging.  The different logging methods are not
			 * synchronized.
			 */
			ast_debug(1,
				"Test '%c' at amplitude %d failed.  Detected Digit: '%c'\n",
				dtmf_positions[digit_index], amp_tests[idx].amp_val,
				digit ?: ' ');
			ast_test_status_update(test,
				"Test '%c' at amplitude %d failed.  Detected Digit: '%c'\n",
				dtmf_positions[digit_index], amp_tests[idx].amp_val,
				digit ?: ' ');
			result = -1;
		}
		ast_dsp_digitreset(dsp);
	}

	return result;
}
#endif

#ifdef TEST_FRAMEWORK
static int test_dtmf_twist_sweep(struct ast_test *test, struct ast_dsp *dsp, int digit_index)
{
	short slin_buf[DTMF_GSIZE];
	int result;
	int row;
	int column;
	int idx;
	struct {
		short amp_row;
		short amp_col;
		int digit;
	} twist_tests[] = {
		/*
		 * XXX Since there is no current DTMF twist detection issue.  This test
		 * just checks the current detection levels.
		 *
		 * Normal twist has the column higher than the row amplitude.
		 * Reverse twist is the other way.
		 */
		{ .amp_row = 1000 + 1800, .amp_col = 1000 +    0, .digit = 0, },
		{ .amp_row = 1000 + 1700, .amp_col = 1000 +    0, .digit = 0, },
		/* Various digits detect and not detect in this range */
		{ .amp_row = 1000 + 1400, .amp_col = 1000 +    0, .digit = dtmf_positions[digit_index], },
		{ .amp_row = 1000 + 1300, .amp_col = 1000 +    0, .digit = dtmf_positions[digit_index], },
		{ .amp_row = 1000 + 1200, .amp_col = 1000 +    0, .digit = dtmf_positions[digit_index], },
		{ .amp_row = 1000 + 1100, .amp_col = 1000 +    0, .digit = dtmf_positions[digit_index], },
		{ .amp_row = 1000 + 1000, .amp_col = 1000 +    0, .digit = dtmf_positions[digit_index], },
		{ .amp_row = 1000 +  100, .amp_col = 1000 +    0, .digit = dtmf_positions[digit_index], },
		{ .amp_row = 1000 +    0, .amp_col = 1000 +  100, .digit = dtmf_positions[digit_index], },
		{ .amp_row = 1000 +    0, .amp_col = 1000 +  200, .digit = dtmf_positions[digit_index], },
		{ .amp_row = 1000 +    0, .amp_col = 1000 +  300, .digit = dtmf_positions[digit_index], },
		{ .amp_row = 1000 +    0, .amp_col = 1000 +  400, .digit = dtmf_positions[digit_index], },
		{ .amp_row = 1000 +    0, .amp_col = 1000 +  500, .digit = dtmf_positions[digit_index], },
		{ .amp_row = 1000 +    0, .amp_col = 1000 +  550, .digit = dtmf_positions[digit_index], },
		/* Various digits detect and not detect in this range */
		{ .amp_row = 1000 +    0, .amp_col = 1000 +  650, .digit = 0, },
		{ .amp_row = 1000 +    0, .amp_col = 1000 +  700, .digit = 0, },
		{ .amp_row = 1000 +    0, .amp_col = 1000 +  800, .digit = 0, },
	};
	float save_normal_twist;
	float save_reverse_twist;

	save_normal_twist = dtmf_normal_twist;
	save_reverse_twist = dtmf_reverse_twist;
	dtmf_normal_twist = DEF_DTMF_NORMAL_TWIST;
	dtmf_reverse_twist = DEF_DTMF_REVERSE_TWIST;

	row = (digit_index >> 2) & 0x03;
	column = digit_index & 0x03;

	result = 0;

	for (idx = 0; idx < ARRAY_LEN(twist_tests); ++idx) {
		int digit;
		int duration;

		ast_debug(1, "Test '%c' twist row %d col %d amplitudes\n",
			dtmf_positions[digit_index],
			twist_tests[idx].amp_row, twist_tests[idx].amp_col);
		test_dual_sample_gen(slin_buf, ARRAY_LEN(slin_buf), DEFAULT_SAMPLE_RATE,
			(int) dtmf_row[row], twist_tests[idx].amp_row,
			(int) dtmf_col[column], twist_tests[idx].amp_col);

		digit = 0;
		for (duration = 0; !digit && duration < 3; ++duration) {
			digit = dtmf_detect(dsp, &dsp->digit_state, slin_buf, ARRAY_LEN(slin_buf),
				0, 0);
		}
		if (twist_tests[idx].digit != digit) {
			/*
			 * Both messages are needed.  ast_debug for when figuring out
			 * what went wrong and the test update for normal output before
			 * you start debugging.  The different logging methods are not
			 * synchronized.
			 */
			ast_debug(1,
				"Test '%c' twist row %d col %d amplitudes failed.  Detected Digit: '%c'\n",
				dtmf_positions[digit_index],
				twist_tests[idx].amp_row, twist_tests[idx].amp_col,
				digit ?: ' ');
			ast_test_status_update(test,
				"Test '%c' twist row %d col %d amplitudes failed.  Detected Digit: '%c'\n",
				dtmf_positions[digit_index],
				twist_tests[idx].amp_row, twist_tests[idx].amp_col,
				digit ?: ' ');
			result = -1;
		}
		ast_dsp_digitreset(dsp);
	}

	dtmf_normal_twist = save_normal_twist;
	dtmf_reverse_twist = save_reverse_twist;

	return result;
}
#endif

#ifdef TEST_FRAMEWORK
static int test_tone_freq_sweep(struct ast_test *test, struct ast_dsp *dsp, tone_detect_state_t *tone_state, short amplitude)
{
	short slin_buf[tone_state->block_size];
	int result;
	int freq;
	int lower_freq;
	int upper_freq;

	/* Calculate detection frequency range */
	lower_freq = tone_state->freq - 4;
	upper_freq = tone_state->freq + 4;

	result = 0;

	/* Sweep frequencies loop. */
	for (freq = 100; freq <= 3500; freq += 1) {
		int detected;
		int duration;
		int expect_detection;

		if (freq == tone_state->freq) {
			/* This case is done by the amplitude sweep. */
			continue;
		}

		expect_detection = (lower_freq <= freq && freq <= upper_freq) ? 1 : 0;

		ast_debug(1, "Test %d Hz detection given %d Hz tone at amplitude %d.  Range:%d-%d Expect detect: %s\n",
			tone_state->freq, freq, amplitude, lower_freq, upper_freq,
			expect_detection ? "yes" : "no");
		test_tone_sample_gen(slin_buf, tone_state->block_size, DEFAULT_SAMPLE_RATE, freq,
			amplitude);

		detected = 0;
		for (duration = 0; !detected && duration < tone_state->hits_required + 3; ++duration) {
			detected = tone_detect(dsp, tone_state, slin_buf, tone_state->block_size) ? 1 : 0;
		}
		if (expect_detection != detected) {
			/*
			 * Both messages are needed.  ast_debug for when figuring out
			 * what went wrong and the test update for normal output before
			 * you start debugging.  The different logging methods are not
			 * synchronized.
			 */
			ast_debug(1,
				"Test %d Hz detection given %d Hz tone at amplitude %d failed.  Range:%d-%d Detected: %s\n",
				tone_state->freq, freq, amplitude, lower_freq, upper_freq,
				detected ? "yes" : "no");
			ast_test_status_update(test,
				"Test %d Hz detection given %d Hz tone at amplitude %d failed.  Range:%d-%d Detected: %s\n",
				tone_state->freq, freq, amplitude, lower_freq, upper_freq,
				detected ? "yes" : "no");
			result = -1;
		}
		tone_state->hit_count = 0;
	}

	return result;
}
#endif

#ifdef TEST_FRAMEWORK
AST_TEST_DEFINE(test_dsp_fax_detect)
{
	struct ast_dsp *dsp;
	enum ast_test_result_state result;

	switch (cmd) {
	case TEST_INIT:
		info->name = "fax";
		info->category = "/main/dsp/";
		info->summary = "DSP fax tone detect unit test";
		info->description =
			"Tests fax tone detection code.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	dsp = ast_dsp_new();
	if (!dsp) {
		return AST_TEST_FAIL;
	}

	result = AST_TEST_PASS;

	/* Test CNG tone amplitude detection */
	if (test_tone_amplitude_sweep(test, dsp, &dsp->cng_tone_state)) {
		result = AST_TEST_FAIL;
	}

	/* Test CED tone amplitude detection */
	if (test_tone_amplitude_sweep(test, dsp, &dsp->ced_tone_state)) {
		result = AST_TEST_FAIL;
	}

	/* Test CNG tone frequency detection */
	if (test_tone_freq_sweep(test, dsp, &dsp->cng_tone_state, TONE_AMPLITUDE_MAX)) {
		result = AST_TEST_FAIL;
	}
	if (test_tone_freq_sweep(test, dsp, &dsp->cng_tone_state, TONE_AMPLITUDE_MIN)) {
		result = AST_TEST_FAIL;
	}

	/* Test CED tone frequency detection */
	if (test_tone_freq_sweep(test, dsp, &dsp->ced_tone_state, TONE_AMPLITUDE_MAX)) {
		result = AST_TEST_FAIL;
	}
	if (test_tone_freq_sweep(test, dsp, &dsp->ced_tone_state, TONE_AMPLITUDE_MIN)) {
		result = AST_TEST_FAIL;
	}

	ast_dsp_free(dsp);
	return result;
}
#endif

#ifdef TEST_FRAMEWORK
AST_TEST_DEFINE(test_dsp_dtmf_detect)
{
	int idx;
	struct ast_dsp *dsp;
	enum ast_test_result_state result;

	switch (cmd) {
	case TEST_INIT:
		info->name = "dtmf";
		info->category = "/main/dsp/";
		info->summary = "DSP DTMF detect unit test";
		info->description =
			"Tests DTMF detection code.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	dsp = ast_dsp_new();
	if (!dsp) {
		return AST_TEST_FAIL;
	}

	result = AST_TEST_PASS;

	for (idx = 0; dtmf_positions[idx]; ++idx) {
		if (test_dtmf_amplitude_sweep(test, dsp, idx)) {
			result = AST_TEST_FAIL;
		}
	}

	for (idx = 0; dtmf_positions[idx]; ++idx) {
		if (test_dtmf_twist_sweep(test, dsp, idx)) {
			result = AST_TEST_FAIL;
		}
	}

	ast_dsp_free(dsp);
	return result;
}
#endif

static int unload_module(void)
{
	AST_TEST_UNREGISTER(test_dsp_fax_detect);
	AST_TEST_UNREGISTER(test_dsp_dtmf_detect);

	return 0;
}

static int load_module(void)
{
	if (_dsp_init(0)) {
		return AST_MODULE_LOAD_FAILURE;
	}

	AST_TEST_REGISTER(test_dsp_fax_detect);
	AST_TEST_REGISTER(test_dsp_dtmf_detect);

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload_module(void)
{
	return _dsp_init(1);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "DSP",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_CORE,
	.requires = "extconfig",
);
