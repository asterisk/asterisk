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

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <math.h>

#include "asterisk/frame.h"
#include "asterisk/channel.h"
#include "asterisk/dsp.h"
#include "asterisk/ulaw.h"
#include "asterisk/alaw.h"
#include "asterisk/utils.h"
#include "asterisk/options.h"

/*! Number of goertzels for progress detect */
enum gsamp_size {
	GSAMP_SIZE_NA = 183,			/*!< North America - 350, 440, 480, 620, 950, 1400, 1800 Hz */
	GSAMP_SIZE_CR = 188,			/*!< Costa Rica, Brazil - Only care about 425 Hz */
	GSAMP_SIZE_UK = 160 			/*!< UK disconnect goertzel feed - should trigger 400hz */
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
	HZ_400 = 0
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

static struct progress {
	enum gsamp_size size;
	int freqs[7];
} modes[] = {
	{ GSAMP_SIZE_NA, { 350, 440, 480, 620, 950, 1400, 1800 } },	/*!< North America */
	{ GSAMP_SIZE_CR, { 425 } },                                	/*!< Costa Rica, Brazil */
	{ GSAMP_SIZE_UK, { 400 } },                                	/*!< UK */
};

#define DEFAULT_THRESHOLD	512

enum busy_detect {
	BUSY_PERCENT = 10,   	/*!< The percentage difference between the two last silence periods */
	BUSY_PAT_PERCENT = 7,	/*!< The percentage difference between measured and actual pattern */
	BUSY_THRESHOLD = 100,	/*!< Max number of ms difference between max and min times in busy */
	BUSY_MIN = 75,       	/*!< Busy must be at least 80 ms in half-cadence */
	BUSY_MAX =3100       	/*!< Busy can't be longer than 3100 ms in half-cadence */
};

/*! Remember last 15 units */
#define DSP_HISTORY 		15

#define TONE_THRESH		10.0	/*!< How much louder the tone should be than channel energy */
#define TONE_MIN_THRESH 	1e8	/*!< How much tone there should be at least to attempt */

/*! All THRESH_XXX values are in GSAMP_SIZE chunks (us = 22ms) */
enum gsamp_thresh {
	THRESH_RING = 8,        	/*!< Need at least 150ms ring to accept */
	THRESH_TALK = 2,        	/*!< Talk detection does not work continuously */
	THRESH_BUSY = 4,        	/*!< Need at least 80ms to accept */
	THRESH_CONGESTION = 4,  	/*!< Need at least 80ms to accept */
	THRESH_HANGUP = 60,     	/*!< Need at least 1300ms to accept hangup */
	THRESH_RING2ANSWER = 300	/*!< Timeout from start of ring to answer (about 6600 ms) */
};

#define	MAX_DTMF_DIGITS		128

/* Basic DTMF specs:
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
#define FAX_THRESHOLD		8.0e7
#define FAX_2ND_HARMONIC	2.0     /* 4dB */
#define DTMF_NORMAL_TWIST	6.3     /* 8dB */
#ifdef	RADIO_RELAX
#define DTMF_REVERSE_TWIST          ((digitmode & DSP_DIGITMODE_RELAXDTMF) ? 6.5 : 2.5)     /* 4dB normal */
#else
#define DTMF_REVERSE_TWIST          ((digitmode & DSP_DIGITMODE_RELAXDTMF) ? 4.0 : 2.5)     /* 4dB normal */
#endif
#define DTMF_RELATIVE_PEAK_ROW	6.3     /* 8dB */
#define DTMF_RELATIVE_PEAK_COL	6.3     /* 8dB */
#define DTMF_2ND_HARMONIC_ROW       ((digitmode & DSP_DIGITMODE_RELAXDTMF) ? 1.7 : 2.5)     /* 4dB normal */
#define DTMF_2ND_HARMONIC_COL	63.1    /* 18dB */
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
#define FAX_TONE_CNG_DURATION	500
#define FAX_TONE_CNG_DB		16

/* This signal may be sent by the Terminating FAX machine anywhere between
 * 1.8 to 2.5 seconds AFTER answering the call.  The CED signal consists
 * of a 2100 Hz tone that is from 2.6 to 4 seconds in duration.
*/
#define FAX_TONE_CED_FREQ	2100
#define FAX_TONE_CED_DURATION	2600
#define FAX_TONE_CED_DB		16

#define SAMPLE_RATE		8000

/* How many samples a frame has.  This constant is used when calculating
 * Goertzel block size for tone_detect.  It is only important if we want to
 * remove (squelch) the tone. In this case it is important to have block
 * size not to exceed size of voice frame.  Otherwise by the moment the tone
 * is detected it is too late to squelch it from previous frames.
 */
#define SAMPLES_IN_FRAME	160


typedef struct {
	int v2;
	int v3;
	int chunky;
	int fac;
	int samples;
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

	int hits_required;	/* How many successive blocks with tone we are looking for */
	float threshold;	/* Energy of the tone relative to energy from all other signals to consider a hit */

	int hit_count;		/* How many successive blocks we consider tone present */
	int last_hit;		/* Indicates if the last processed block was a hit */

} tone_detect_state_t;

typedef struct
{
	goertzel_state_t row_out[4];
	goertzel_state_t col_out[4];
	int lasthit;
	int current_hit;
	float energy;
	int current_sample;
} dtmf_detect_state_t;

typedef struct
{
	goertzel_state_t tone_out[6];
	int current_hit;
	int hits[5];
	int current_sample;
} mf_detect_state_t;

typedef struct
{
	char digits[MAX_DTMF_DIGITS + 1];
	int current_digits;
	int detected_digits;
	int lost_digits;

	union {
		dtmf_detect_state_t dtmf;
		mf_detect_state_t mf;
	} td;
} digit_detect_state_t;

static float dtmf_row[] =
{
	697.0,  770.0,  852.0,  941.0
};
static float dtmf_col[] =
{
	1209.0, 1336.0, 1477.0, 1633.0
};

static float mf_tones[] =
{
	700.0, 900.0, 1100.0, 1300.0, 1500.0, 1700.0
};

static char dtmf_positions[] = "123A" "456B" "789C" "*0#D";

static char bell_mf_positions[] = "1247C-358A--69*---0B----#";

static inline void goertzel_sample(goertzel_state_t *s, short sample)
{
	int v1;
	
	v1 = s->v2;
	s->v2 = s->v3;
	
	s->v3 = (s->fac * s->v2) >> 15;
	s->v3 = s->v3 - v1 + (sample >> s->chunky);
	if (abs(s->v3) > 32768) {
		s->chunky++;
		s->v3 = s->v3 >> 1;
		s->v2 = s->v2 >> 1;
		v1 = v1 >> 1;
	}
}

static inline void goertzel_update(goertzel_state_t *s, short *samps, int count)
{
	int i;
	
	for (i=0;i<count;i++) 
		goertzel_sample(s, samps[i]);
}


static inline float goertzel_result(goertzel_state_t *s)
{
	goertzel_result_t r;
	r.value = (s->v3 * s->v3) + (s->v2 * s->v2);
	r.value -= ((s->v2 * s->v3) >> 15) * s->fac;
	r.power = s->chunky * 2;
	return (float)r.value * (float)(1 << r.power);
}

static inline void goertzel_init(goertzel_state_t *s, float freq, int samples)
{
	s->v2 = s->v3 = s->chunky = 0.0;
	s->fac = (int)(32768.0 * 2.0 * cos(2.0 * M_PI * freq / SAMPLE_RATE));
	s->samples = samples;
}

static inline void goertzel_reset(goertzel_state_t *s)
{
	s->v2 = s->v3 = s->chunky = 0.0;
}

struct ast_dsp {
	struct ast_frame f;
	int threshold;
	int totalsilence;
	int totalnoise;
	int features;
	int ringtimeout;
	int busymaybe;
	int busycount;
	int busy_tonelength;
	int busy_quietlength;
	int historicnoise[DSP_HISTORY];
	int historicsilence[DSP_HISTORY];
	goertzel_state_t freqs[7];
	int freqcount;
	int gsamps;
	enum gsamp_size gsamp_size;
	enum prog_mode progmode;
	int tstate;
	int tcount;
	int digitmode;
	int faxmode;
	int thinkdigit;
	float genergy;
	digit_detect_state_t digit_state;
	tone_detect_state_t cng_tone_state;
	tone_detect_state_t ced_tone_state;
};

static void ast_tone_detect_init(tone_detect_state_t *s, int freq, int duration, int amp)
{
	int duration_samples;
	float x;
	int periods_in_block;

	s->freq = freq;

	/* Desired tone duration in samples */
	duration_samples = duration * SAMPLE_RATE / 1000;
	/* We want to allow 10% deviation of tone duration */
	duration_samples = duration_samples * 9 / 10;

	/* If we want to remove tone, it is important to have block size not
	   to exceed frame size. Otherwise by the moment tone is detected it is too late
 	   to squelch it from previous frames */
	s->block_size = SAMPLES_IN_FRAME;

	periods_in_block = s->block_size * freq / SAMPLE_RATE;

	/* Make sure we will have at least 5 periods at target frequency for analisys.
	   This may make block larger than expected packet and will make squelching impossible
	   but at least we will be detecting the tone */
	if (periods_in_block < 5)
		periods_in_block = 5;

	/* Now calculate final block size. It will contain integer number of periods */
	s->block_size = periods_in_block * SAMPLE_RATE / freq;

	/* tone_detect is currently only used to detect fax tones and we
	   do not need suqlching the fax tones */
	s->squelch = 0;

	/* Account for the first and the last block to be incomplete
	   and thus no tone will be detected in them */
	s->hits_required = (duration_samples - (s->block_size - 1)) / s->block_size;

	goertzel_init(&s->tone, freq, s->block_size);

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
	ast_tone_detect_init(&s->cng_tone_state, FAX_TONE_CNG_FREQ, FAX_TONE_CNG_DURATION, FAX_TONE_CNG_DB);
	ast_tone_detect_init(&s->ced_tone_state, FAX_TONE_CED_FREQ, FAX_TONE_CED_DURATION, FAX_TONE_CED_DB);
}

static void ast_dtmf_detect_init (dtmf_detect_state_t *s)
{
	int i;

	s->lasthit = 0;
	s->current_hit = 0;
	for (i = 0;  i < 4;  i++) {
		goertzel_init (&s->row_out[i], dtmf_row[i], 102);
		goertzel_init (&s->col_out[i], dtmf_col[i], 102);
		s->energy = 0.0;
	}
	s->current_sample = 0;
}

static void ast_mf_detect_init (mf_detect_state_t *s)
{
	int i;
	s->hits[0] = s->hits[1] = s->hits[2] = s->hits[3] = s->hits[4] = 0;
	for (i = 0;  i < 6;  i++) {
		goertzel_init (&s->tone_out[i], mf_tones[i], 160);
	}
	s->current_sample = 0;
	s->current_hit = 0;
}

static void ast_digit_detect_init(digit_detect_state_t *s, int mf)
{
	s->current_digits = 0;
	s->detected_digits = 0;
	s->lost_digits = 0;
	s->digits[0] = '\0';

	if (mf)
		ast_mf_detect_init(&s->td.mf);
	else
		ast_dtmf_detect_init(&s->td.dtmf);
}

static int tone_detect(tone_detect_state_t *s, int16_t *amp, int samples, 
		 int *writeback)
{
	float tone_energy;
	int i;
	int hit = 0;
	int limit;
	int res = 0;
	int16_t *ptr;

	while (1) {
		/* Process in blocks. */
		limit = (samples < s->samples_pending) ? samples : s->samples_pending;

		for (i = limit, ptr = amp ; i > 0; i--, ptr++) {
			/* signed 32 bit int should be enough to suqare any possible signed 16 bit value */
			s->energy += (int32_t) *ptr * (int32_t) *ptr;

			goertzel_sample(&s->tone, *ptr);
		}

		s->samples_pending -= limit;

		if (s->samples_pending) {
			/* Finished incomplete (last) block */
			if (s->last_hit && s->squelch) {
				/* If we had a hit last time, go ahead and clear this out since likely it
				   will be another hit */
				memset(amp, 0, sizeof(*amp) * limit);
				if (writeback)
					*writeback = 1;
			}
			break;
		}


		tone_energy = goertzel_result(&s->tone);

		/* Scale to make comparable */
		tone_energy *= 2.0;
		s->energy *= s->block_size;

		ast_debug(10, "tone %d, Ew=%f, Et=%f, s/n=%10.2f\n", s->freq, tone_energy, s->energy, tone_energy / (s->energy - tone_energy));
		hit = 0;
		if (tone_energy > s->energy * s->threshold) {

			ast_debug(10, "Hit! count=%d\n", s->hit_count);
			hit = 1;

			if (s->squelch) {
				/* Zero out frame data */
				memset(amp, 0, sizeof(*amp) * limit);
				if (writeback)
					*writeback = 1;
			}
		}

		if (s->hit_count)
			s->hit_count++;

		if (hit == s->last_hit) {
			if (!hit) {
				/* Two successive misses. Tone ended */
				s->hit_count = 0;
			} else if (!s->hit_count) {
				s->hit_count++;
			}

		}

		if (s->hit_count == s->hits_required) {
			ast_debug(1, "%d Hz done detected\n", s->freq);
			res = 1;
		}

		s->last_hit = hit;

		/* Reset for the next block */
		goertzel_reset(&s->tone);

		/* Advance to the next block */
		s->energy = 0.0;
		s->samples_pending = s->block_size;

		amp += limit;
		samples -= limit;
	}

	return res;
}

static void store_digit(digit_detect_state_t *s, char digit)
{
	s->detected_digits++;
	if (s->current_digits < MAX_DTMF_DIGITS) {
		s->digits[s->current_digits++] = digit;
		s->digits[s->current_digits] = '\0';
	} else {
		ast_log(LOG_WARNING, "Digit lost due to full buffer\n");
		s->lost_digits++;
	}
}

static int dtmf_detect(digit_detect_state_t *s, int16_t amp[], int samples, 
		 int digitmode, int *writeback)
{
	float row_energy[4];
	float col_energy[4];
	float famp;
	int i;
	int j;
	int sample;
	int best_row;
	int best_col;
	int hit;
	int limit;

	hit = 0;
	for (sample = 0;  sample < samples;  sample = limit) {
		/* 102 is optimised to meet the DTMF specs. */
		if ((samples - sample) >= (102 - s->td.dtmf.current_sample))
			limit = sample + (102 - s->td.dtmf.current_sample);
		else
			limit = samples;
		/* The following unrolled loop takes only 35% (rough estimate) of the 
		   time of a rolled loop on the machine on which it was developed */
		for (j = sample; j < limit; j++) {
			famp = amp[j];
			s->td.dtmf.energy += famp*famp;
			/* With GCC 2.95, the following unrolled code seems to take about 35%
			   (rough estimate) as long as a neat little 0-3 loop */
			goertzel_sample(s->td.dtmf.row_out, amp[j]);
			goertzel_sample(s->td.dtmf.col_out, amp[j]);
			goertzel_sample(s->td.dtmf.row_out + 1, amp[j]);
			goertzel_sample(s->td.dtmf.col_out + 1, amp[j]);
			goertzel_sample(s->td.dtmf.row_out + 2, amp[j]);
			goertzel_sample(s->td.dtmf.col_out + 2, amp[j]);
			goertzel_sample(s->td.dtmf.row_out + 3, amp[j]);
			goertzel_sample(s->td.dtmf.col_out + 3, amp[j]);
		}
		s->td.dtmf.current_sample += (limit - sample);
		if (s->td.dtmf.current_sample < 102) {
			if (hit && !((digitmode & DSP_DIGITMODE_NOQUELCH))) {
				/* If we had a hit last time, go ahead and clear this out since likely it
				   will be another hit */
				for (i=sample;i<limit;i++) 
					amp[i] = 0;
				*writeback = 1;
			}
			continue;
		}
		/* We are at the end of a DTMF detection block */
		/* Find the peak row and the peak column */
		row_energy[0] = goertzel_result (&s->td.dtmf.row_out[0]);
		col_energy[0] = goertzel_result (&s->td.dtmf.col_out[0]);

		for (best_row = best_col = 0, i = 1;  i < 4;  i++) {
			row_energy[i] = goertzel_result (&s->td.dtmf.row_out[i]);
			if (row_energy[i] > row_energy[best_row])
				best_row = i;
			col_energy[i] = goertzel_result (&s->td.dtmf.col_out[i]);
			if (col_energy[i] > col_energy[best_col])
				best_col = i;
		}
		hit = 0;
		/* Basic signal level test and the twist test */
		if (row_energy[best_row] >= DTMF_THRESHOLD && 
		    col_energy[best_col] >= DTMF_THRESHOLD &&
		    col_energy[best_col] < row_energy[best_row]*DTMF_REVERSE_TWIST &&
		    col_energy[best_col]*DTMF_NORMAL_TWIST > row_energy[best_row]) {
			/* Relative peak test */
			for (i = 0;  i < 4;  i++) {
				if ((i != best_col &&
				    col_energy[i]*DTMF_RELATIVE_PEAK_COL > col_energy[best_col]) ||
				    (i != best_row 
				     && row_energy[i]*DTMF_RELATIVE_PEAK_ROW > row_energy[best_row])) {
					break;
				}
			}
			/* ... and fraction of total energy test */
			if (i >= 4 &&
			    (row_energy[best_row] + col_energy[best_col]) > DTMF_TO_TOTAL_ENERGY*s->td.dtmf.energy) {
				/* Got a hit */
				hit = dtmf_positions[(best_row << 2) + best_col];
				if (!(digitmode & DSP_DIGITMODE_NOQUELCH)) {
					/* Zero out frame data if this is part DTMF */
					for (i=sample;i<limit;i++) 
						amp[i] = 0;
					*writeback = 1;
				}
			}
		} 

		/* The logic in the next test is:
		   For digits we need two successive identical clean detects, with
		   something different preceeding it. This can work with
		   back to back differing digits. More importantly, it
		   can work with nasty phones that give a very wobbly start
		   to a digit */
		if (hit != s->td.dtmf.current_hit) {
			if (hit && s->td.dtmf.lasthit == hit) {
				s->td.dtmf.current_hit = hit;
				store_digit(s, hit);
			} else if (s->td.dtmf.lasthit != s->td.dtmf.current_hit) {
				s->td.dtmf.current_hit = 0;
			}
		}
		s->td.dtmf.lasthit = hit;

		/* Reinitialise the detector for the next block */
		for (i = 0;  i < 4;  i++) {
			goertzel_reset(&s->td.dtmf.row_out[i]);
			goertzel_reset(&s->td.dtmf.col_out[i]);
		}
		s->td.dtmf.energy = 0.0;
		s->td.dtmf.current_sample = 0;
	}
	return (s->td.dtmf.current_hit);	/* return the debounced hit */
}

/* MF goertzel size */
#define MF_GSIZE 120

static int mf_detect(digit_detect_state_t *s, int16_t amp[],
	int samples, int digitmode, int *writeback)
{
	float energy[6];
	int best;
	int second_best;
	float famp;
	int i;
	int j;
	int sample;
	int hit;
	int limit;

	hit = 0;
	for (sample = 0;  sample < samples;  sample = limit) {
		/* 80 is optimised to meet the MF specs. */
		if ((samples - sample) >= (MF_GSIZE - s->td.mf.current_sample))
			limit = sample + (MF_GSIZE - s->td.mf.current_sample);
		else
			limit = samples;
		/* The following unrolled loop takes only 35% (rough estimate) of the 
		   time of a rolled loop on the machine on which it was developed */
		for (j = sample;  j < limit;  j++) {
			famp = amp[j];
			/* With GCC 2.95, the following unrolled code seems to take about 35%
			   (rough estimate) as long as a neat little 0-3 loop */
			goertzel_sample(s->td.mf.tone_out, amp[j]);
			goertzel_sample(s->td.mf.tone_out + 1, amp[j]);
			goertzel_sample(s->td.mf.tone_out + 2, amp[j]);
			goertzel_sample(s->td.mf.tone_out + 3, amp[j]);
			goertzel_sample(s->td.mf.tone_out + 4, amp[j]);
			goertzel_sample(s->td.mf.tone_out + 5, amp[j]);
		}
		s->td.mf.current_sample += (limit - sample);
		if (s->td.mf.current_sample < MF_GSIZE) {
			if (hit && !((digitmode & DSP_DIGITMODE_NOQUELCH))) {
				/* If we had a hit last time, go ahead and clear this out since likely it
				   will be another hit */
				for (i=sample;i<limit;i++) 
					amp[i] = 0;
				*writeback = 1;
			}
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
		for (i=2;i<6;i++) {
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
	            && energy[best]*BELL_MF_TWIST > energy[second_best]) {
			/* Relative peak test */
			hit = -1;
			for (i=0;i<6;i++) {
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
			best = best*5 + second_best - 1;
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
		/* Reinitialise the detector for the next block */
		for (i = 0;  i < 6;  i++)
			goertzel_reset(&s->td.mf.tone_out[i]);
		s->td.mf.current_sample = 0;
	}

	return (s->td.mf.current_hit); /* return the debounced hit */
}

static int __ast_dsp_digitdetect(struct ast_dsp *dsp, short *s, int len, int *writeback)
{
	int res = 0;
	
	if ((dsp->features & DSP_FEATURE_DTMF_DETECT) && (dsp->digitmode & DSP_DIGITMODE_MF))
		res = mf_detect(&dsp->digit_state, s, len, dsp->digitmode & DSP_DIGITMODE_RELAXDTMF, writeback);
	else if (dsp->features & DSP_FEATURE_DTMF_DETECT)
		res = dtmf_detect(&dsp->digit_state, s, len, dsp->digitmode & DSP_DIGITMODE_RELAXDTMF, writeback);

	if ((dsp->features & DSP_FEATURE_FAX_DETECT) && (dsp->faxmode & DSP_FAXMODE_DETECT_CNG)) {
		if (tone_detect(&dsp->cng_tone_state, s, len, NULL)) {
			store_digit(&dsp->digit_state, 'f');
			res = 'f';
		}
	}

	if ((dsp->features & DSP_FEATURE_FAX_DETECT) && (dsp->faxmode & DSP_FAXMODE_DETECT_CED)) {
		if (tone_detect(&dsp->ced_tone_state, s, len, NULL)) {
			store_digit(&dsp->digit_state, 'e');
			res = 'e';
		}
	}

	return res;
}

int ast_dsp_digitdetect(struct ast_dsp *dsp, struct ast_frame *inf)
{
	short *s;
	int len;
	int ign=0;

	if (inf->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Can't check call progress of non-voice frames\n");
		return 0;
	}
	if (inf->subclass != AST_FORMAT_SLINEAR) {
		ast_log(LOG_WARNING, "Can only check call progress in signed-linear frames\n");
		return 0;
	}
	s = inf->data;
	len = inf->datalen / 2;
	return __ast_dsp_digitdetect(dsp, s, len, &ign);
}

static inline int pair_there(float p1, float p2, float i1, float i2, float e)
{
	/* See if p1 and p2 are there, relative to i1 and i2 and total energy */
	/* Make sure absolute levels are high enough */
	if ((p1 < TONE_MIN_THRESH) || (p2 < TONE_MIN_THRESH))
		return 0;
	/* Amplify ignored stuff */
	i2 *= TONE_THRESH;
	i1 *= TONE_THRESH;
	e *= TONE_THRESH;
	/* Check first tone */
	if ((p1 < i1) || (p1 < i2) || (p1 < e))
		return 0;
	/* And second */
	if ((p2 < i1) || (p2 < i2) || (p2 < e))
		return 0;
	/* Guess it's there... */
	return 1;
}

int ast_dsp_getdigits(struct ast_dsp *dsp, char *buf, int max)
{
	if (max > dsp->digit_state.current_digits)
		max = dsp->digit_state.current_digits;
	if (max > 0) {
		memcpy(buf, dsp->digit_state.digits, max);
		memmove(dsp->digit_state.digits, dsp->digit_state.digits + max, dsp->digit_state.current_digits - max);
		dsp->digit_state.current_digits -= max;
	}
	buf[max] = '\0';
	return  max;
}

static int __ast_dsp_call_progress(struct ast_dsp *dsp, short *s, int len)
{
	int x;
	int y;
	int pass;
	int newstate = DSP_TONE_STATE_SILENCE;
	int res = 0;
	while (len) {
		/* Take the lesser of the number of samples we need and what we have */
		pass = len;
		if (pass > dsp->gsamp_size - dsp->gsamps) 
			pass = dsp->gsamp_size - dsp->gsamps;
		for (x=0;x<pass;x++) {
			for (y=0;y<dsp->freqcount;y++) 
				goertzel_sample(&dsp->freqs[y], s[x]);
			dsp->genergy += s[x] * s[x];
		}
		s += pass;
		dsp->gsamps += pass;
		len -= pass;
		if (dsp->gsamps == dsp->gsamp_size) {
			float hz[7];
			for (y=0;y<7;y++)
				hz[y] = goertzel_result(&dsp->freqs[y]);
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
					if (dsp->tstate == DSP_TONE_STATE_SPECIAL1)
						newstate = DSP_TONE_STATE_SPECIAL2;
				} else if (hz[HZ_1800] > TONE_MIN_THRESH * TONE_THRESH) {
					if (dsp->tstate == DSP_TONE_STATE_SPECIAL2)
						newstate = DSP_TONE_STATE_SPECIAL3;
				} else if (dsp->genergy > TONE_MIN_THRESH * TONE_THRESH) {
					newstate = DSP_TONE_STATE_TALKING;
				} else
					newstate = DSP_TONE_STATE_SILENCE;
				break;
			case PROG_MODE_CR:
				if (hz[HZ_425] > TONE_MIN_THRESH * TONE_THRESH) {
					newstate = DSP_TONE_STATE_RINGING;
				} else if (dsp->genergy > TONE_MIN_THRESH * TONE_THRESH) {
					newstate = DSP_TONE_STATE_TALKING;
				} else
					newstate = DSP_TONE_STATE_SILENCE;
				break;
			case PROG_MODE_UK:
				if (hz[HZ_400] > TONE_MIN_THRESH * TONE_THRESH) {
					newstate = DSP_TONE_STATE_HUNGUP;
				}
				break;
			default:
				ast_log(LOG_WARNING, "Can't process in unknown prog mode '%d'\n", dsp->progmode);
			}
			if (newstate == dsp->tstate) {
				dsp->tcount++;
				if (dsp->ringtimeout)
					dsp->ringtimeout++;
				switch (dsp->tstate) {
					case DSP_TONE_STATE_RINGING:
						if ((dsp->features & DSP_PROGRESS_RINGING) &&
						    (dsp->tcount==THRESH_RING)) {
							res = AST_CONTROL_RINGING;
							dsp->ringtimeout= 1;
						}
						break;
					case DSP_TONE_STATE_BUSY:
						if ((dsp->features & DSP_PROGRESS_BUSY) &&
						    (dsp->tcount==THRESH_BUSY)) {
							res = AST_CONTROL_BUSY;
							dsp->features &= ~DSP_FEATURE_CALL_PROGRESS;
						}
						break;
					case DSP_TONE_STATE_TALKING:
						if ((dsp->features & DSP_PROGRESS_TALK) &&
						    (dsp->tcount==THRESH_TALK)) {
							res = AST_CONTROL_ANSWER;
							dsp->features &= ~DSP_FEATURE_CALL_PROGRESS;
						}
						break;
					case DSP_TONE_STATE_SPECIAL3:
						if ((dsp->features & DSP_PROGRESS_CONGESTION) &&
						    (dsp->tcount==THRESH_CONGESTION)) {
							res = AST_CONTROL_CONGESTION;
							dsp->features &= ~DSP_FEATURE_CALL_PROGRESS;
						}
						break;
					case DSP_TONE_STATE_HUNGUP:
						if ((dsp->features & DSP_FEATURE_CALL_PROGRESS) &&
						    (dsp->tcount==THRESH_HANGUP)) {
							res = AST_CONTROL_HANGUP;
							dsp->features &= ~DSP_FEATURE_CALL_PROGRESS;
						}
						break;
				}
				if (dsp->ringtimeout==THRESH_RING2ANSWER) {
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
			for (x=0;x<7;x++)
				dsp->freqs[x].v2 = dsp->freqs[x].v3 = 0.0;
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
	if (inf->subclass != AST_FORMAT_SLINEAR) {
		ast_log(LOG_WARNING, "Can only check call progress in signed-linear frames\n");
		return 0;
	}
	return __ast_dsp_call_progress(dsp, inf->data, inf->datalen / 2);
}

static int __ast_dsp_silence(struct ast_dsp *dsp, short *s, int len, int *totalsilence)
{
	int accum;
	int x;
	int res = 0;

	if (!len)
		return 0;
	accum = 0;
	for (x=0;x<len; x++) 
		accum += abs(s[x]);
	accum /= len;
	if (accum < dsp->threshold) {
		/* Silent */
		dsp->totalsilence += len/8;
		if (dsp->totalnoise) {
			/* Move and save history */
			memmove(dsp->historicnoise + DSP_HISTORY - dsp->busycount, dsp->historicnoise + DSP_HISTORY - dsp->busycount +1, dsp->busycount*sizeof(dsp->historicnoise[0]));
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
		dsp->totalnoise += len/8;
		if (dsp->totalsilence) {
			int silence1 = dsp->historicsilence[DSP_HISTORY - 1];
			int silence2 = dsp->historicsilence[DSP_HISTORY - 2];
			/* Move and save history */
			memmove(dsp->historicsilence + DSP_HISTORY - dsp->busycount, dsp->historicsilence + DSP_HISTORY - dsp->busycount + 1, dsp->busycount*sizeof(dsp->historicsilence[0]));
			dsp->historicsilence[DSP_HISTORY - 1] = dsp->totalsilence;
			/* check if the previous sample differs only by BUSY_PERCENT from the one before it */
			if (silence1 < silence2) {
				if (silence1 + silence1*BUSY_PERCENT/100 >= silence2)
					dsp->busymaybe = 1;
				else 
					dsp->busymaybe = 0;
			} else {
				if (silence1 - silence1*BUSY_PERCENT/100 <= silence2)
					dsp->busymaybe = 1;
				else 
					dsp->busymaybe = 0;
			}
		}
		dsp->totalsilence = 0;
	}
	if (totalsilence)
		*totalsilence = dsp->totalsilence;
	return res;
}

int ast_dsp_busydetect(struct ast_dsp *dsp)
{
	int res = 0, x;
#ifndef BUSYDETECT_TONEONLY
	int avgsilence = 0, hitsilence = 0;
#endif
	int avgtone = 0, hittone = 0;
	if (!dsp->busymaybe)
		return res;
	for (x=DSP_HISTORY - dsp->busycount;x<DSP_HISTORY;x++) {
#ifndef BUSYDETECT_TONEONLY
		avgsilence += dsp->historicsilence[x];
#endif
		avgtone += dsp->historicnoise[x];
	}
#ifndef BUSYDETECT_TONEONLY
	avgsilence /= dsp->busycount;
#endif
	avgtone /= dsp->busycount;
	for (x=DSP_HISTORY - dsp->busycount;x<DSP_HISTORY;x++) {
#ifndef BUSYDETECT_TONEONLY
		if (avgsilence > dsp->historicsilence[x]) {
			if (avgsilence - (avgsilence*BUSY_PERCENT/100) <= dsp->historicsilence[x])
				hitsilence++;
		} else {
			if (avgsilence + (avgsilence*BUSY_PERCENT/100) >= dsp->historicsilence[x])
				hitsilence++;
		}
#endif
		if (avgtone > dsp->historicnoise[x]) {
			if (avgtone - (avgtone*BUSY_PERCENT/100) <= dsp->historicnoise[x])
				hittone++;
		} else {
			if (avgtone + (avgtone*BUSY_PERCENT/100) >= dsp->historicnoise[x])
				hittone++;
		}
	}
#ifndef BUSYDETECT_TONEONLY
	if ((hittone >= dsp->busycount - 1) && (hitsilence >= dsp->busycount - 1) && 
	    (avgtone >= BUSY_MIN && avgtone <= BUSY_MAX) && 
	    (avgsilence >= BUSY_MIN && avgsilence <= BUSY_MAX)) {
#else
	if ((hittone >= dsp->busycount - 1) && (avgtone >= BUSY_MIN && avgtone <= BUSY_MAX)) {
#endif
#ifdef BUSYDETECT_COMPARE_TONE_AND_SILENCE
		if (avgtone > avgsilence) {
			if (avgtone - avgtone*BUSY_PERCENT/100 <= avgsilence)
				res = 1;
		} else {
			if (avgtone + avgtone*BUSY_PERCENT/100 >= avgsilence)
				res = 1;
		}
#else
		res = 1;
#endif
	}
	/* If we know the expected busy tone length, check we are in the range */
	if (res && (dsp->busy_tonelength > 0)) {
		if (abs(avgtone - dsp->busy_tonelength) > (dsp->busy_tonelength*BUSY_PAT_PERCENT/100)) {
#ifdef BUSYDETECT_DEBUG
			ast_debug(5, "busy detector: avgtone of %d not close enough to desired %d\n",
				avgtone, dsp->busy_tonelength);
#endif
			res = 0;
		}
	}
#ifndef BUSYDETECT_TONEONLY
	/* If we know the expected busy tone silent-period length, check we are in the range */
	if (res && (dsp->busy_quietlength > 0)) {
		if (abs(avgsilence - dsp->busy_quietlength) > (dsp->busy_quietlength*BUSY_PAT_PERCENT/100)) {
#ifdef BUSYDETECT_DEBUG
		ast_debug(5, "busy detector: avgsilence of %d not close enough to desired %d\n",
			avgsilence, dsp->busy_quietlength);
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

int ast_dsp_silence(struct ast_dsp *dsp, struct ast_frame *f, int *totalsilence)
{
	short *s;
	int len;
	
	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Can't calculate silence on a non-voice frame\n");
		return 0;
	}
	if (f->subclass != AST_FORMAT_SLINEAR) {
		ast_log(LOG_WARNING, "Can only calculate silence on signed-linear frames :(\n");
		return 0;
	}
	s = f->data;
	len = f->datalen/2;
	return __ast_dsp_silence(dsp, s, len, totalsilence);
}

struct ast_frame *ast_dsp_process(struct ast_channel *chan, struct ast_dsp *dsp, struct ast_frame *af)
{
	int silence;
	int res;
	int digit;
	int x;
	short *shortdata;
	unsigned char *odata;
	int len;
	int writeback = 0;

#define FIX_INF(inf) do { \
		if (writeback) { \
			switch (inf->subclass) { \
			case AST_FORMAT_SLINEAR: \
				break; \
			case AST_FORMAT_ULAW: \
				for (x=0;x<len;x++) \
					odata[x] = AST_LIN2MU((unsigned short)shortdata[x]); \
				break; \
			case AST_FORMAT_ALAW: \
				for (x=0;x<len;x++) \
					odata[x] = AST_LIN2A((unsigned short)shortdata[x]); \
				break; \
			} \
		} \
	} while(0) 

	if (!af)
		return NULL;
	if (af->frametype != AST_FRAME_VOICE)
		return af;
	odata = af->data;
	len = af->datalen;
	/* Make sure we have short data */
	switch (af->subclass) {
	case AST_FORMAT_SLINEAR:
		shortdata = af->data;
		len = af->datalen / 2;
		break;
	case AST_FORMAT_ULAW:
		shortdata = alloca(af->datalen * 2);
		for (x = 0;x < len; x++) 
			shortdata[x] = AST_MULAW(odata[x]);
		break;
	case AST_FORMAT_ALAW:
		shortdata = alloca(af->datalen * 2);
		for (x = 0; x < len; x++) 
			shortdata[x] = AST_ALAW(odata[x]);
		break;
	default:
		ast_log(LOG_WARNING, "Inband DTMF is not supported on codec %s. Use RFC2833\n", ast_getformatname(af->subclass));
		return af;
	}
	silence = __ast_dsp_silence(dsp, shortdata, len, NULL);
	if ((dsp->features & DSP_FEATURE_SILENCE_SUPPRESS) && silence) {
		memset(&dsp->f, 0, sizeof(dsp->f));
		dsp->f.frametype = AST_FRAME_NULL;
		ast_frfree(af);
		ast_set_flag(&dsp->f, AST_FRFLAG_FROM_DSP);
		return &dsp->f;
	}
	if ((dsp->features & DSP_FEATURE_BUSY_DETECT) && ast_dsp_busydetect(dsp)) {
		chan->_softhangup |= AST_SOFTHANGUP_DEV;
		memset(&dsp->f, 0, sizeof(dsp->f));
		dsp->f.frametype = AST_FRAME_CONTROL;
		dsp->f.subclass = AST_CONTROL_BUSY;
		ast_frfree(af);
		ast_debug(1, "Requesting Hangup because the busy tone was detected on channel %s\n", chan->name);
		ast_set_flag(&dsp->f, AST_FRFLAG_FROM_DSP);
		return &dsp->f;
	}
	if (((dsp->features & DSP_FEATURE_DTMF_DETECT) || (dsp->features & DSP_FEATURE_FAX_DETECT))) {
		digit = __ast_dsp_digitdetect(dsp, shortdata, len, &writeback);
#if 0
		if (digit)
			printf("Performing digit detection returned %d, digitmode is %d\n", digit, dsp->digitmode);
#endif
		if (dsp->digitmode & (DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_MUTEMAX)) {
			if (!dsp->thinkdigit) {
				if (digit) {
					/* Looks like we might have something.  
					 * Request a conference mute for the moment */
					memset(&dsp->f, 0, sizeof(dsp->f));
					dsp->f.frametype = AST_FRAME_DTMF;
					dsp->f.subclass = 'm';
					dsp->thinkdigit = 'x';
					FIX_INF(af);
					if (chan)
						ast_queue_frame(chan, af);
					ast_frfree(af);
					ast_set_flag(&dsp->f, AST_FRFLAG_FROM_DSP);
					return &dsp->f;
				}
			} else {
				if (digit) {
					/* Thought we saw one last time.  Pretty sure we really have now */
					if ((dsp->thinkdigit != 'x') && (dsp->thinkdigit != digit)) {
						/* If we found a digit, and we're changing digits, go
						   ahead and send this one, but DON'T stop confmute because
						   we're detecting something else, too... */
						memset(&dsp->f, 0, sizeof(dsp->f));
						dsp->f.frametype = AST_FRAME_DTMF_END;
						dsp->f.subclass = dsp->thinkdigit;
						FIX_INF(af);
						if (chan)
							ast_queue_frame(chan, af);
						ast_frfree(af);
					} else {
						dsp->thinkdigit = digit;
						memset(&dsp->f, 0, sizeof(dsp->f));
						dsp->f.frametype = AST_FRAME_DTMF_BEGIN;
						dsp->f.subclass = dsp->thinkdigit;
						FIX_INF(af);
						if (chan)
							ast_queue_frame(chan, af);
						ast_frfree(af);
					}
					ast_set_flag(&dsp->f, AST_FRFLAG_FROM_DSP);
					return &dsp->f;
				} else {
					memset(&dsp->f, 0, sizeof(dsp->f));
					if (dsp->thinkdigit != 'x') {
						/* If we found a digit, send it now */
						dsp->f.frametype = AST_FRAME_DTMF_END;
						dsp->f.subclass = dsp->thinkdigit;
						dsp->thinkdigit = 0;
					} else {
						dsp->f.frametype = AST_FRAME_DTMF;
						dsp->f.subclass = 'u';
						dsp->thinkdigit = 0;
					}
					FIX_INF(af);
					if (chan)
						ast_queue_frame(chan, af);
					ast_frfree(af);
					ast_set_flag(&dsp->f, AST_FRFLAG_FROM_DSP);
					return &dsp->f;
				}
			}
		} else if (dsp->digit_state.current_digits > 1 ||
			(dsp->digit_state.current_digits == 1 && digit != dsp->digit_state.digits[0])) {
			/* Since we basically generate DTMF_END frames we do it only when a digit
			   has finished. */

			memset(&dsp->f, 0, sizeof(dsp->f));
			dsp->f.frametype = AST_FRAME_DTMF;
			dsp->f.subclass = dsp->digit_state.digits[0];
			memmove(dsp->digit_state.digits, dsp->digit_state.digits + 1, dsp->digit_state.current_digits);
			dsp->digit_state.current_digits--;
			FIX_INF(af);
			if (chan)
				ast_queue_frame(chan, af);
			ast_frfree(af);
			ast_set_flag(&dsp->f, AST_FRFLAG_FROM_DSP);
			return &dsp->f;
		}
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
				dsp->f.subclass = res;
				dsp->f.src = "dsp_progress";
				if (chan) 
					ast_queue_frame(chan, &dsp->f);
				break;
			default:
				ast_log(LOG_WARNING, "Don't know how to represent call progress message %d\n", res);
			}
		}
	}
	FIX_INF(af);
	return af;
}

static void ast_dsp_prog_reset(struct ast_dsp *dsp)
{
	int max = 0;
	int x;
	
	dsp->gsamp_size = modes[dsp->progmode].size;
	dsp->gsamps = 0;
	for (x = 0; x < ARRAY_LEN(modes[dsp->progmode].freqs); x++) {
		if (modes[dsp->progmode].freqs[x]) {
			goertzel_init(&dsp->freqs[x], (float)modes[dsp->progmode].freqs[x], dsp->gsamp_size);
			max = x + 1;
		}
	}
	dsp->freqcount = max;
	dsp->ringtimeout= 0;
}

struct ast_dsp *ast_dsp_new(void)
{
	struct ast_dsp *dsp;
	
	if ((dsp = ast_calloc(1, sizeof(*dsp)))) {		
		dsp->threshold = DEFAULT_THRESHOLD;
		dsp->features = DSP_FEATURE_SILENCE_SUPPRESS;
		dsp->busycount = DSP_HISTORY;
		dsp->digitmode = DSP_DIGITMODE_DTMF;
		dsp->faxmode = DSP_FAXMODE_DETECT_CNG;
		/* Initialize digit detector */
		ast_digit_detect_init(&dsp->digit_state, dsp->digitmode & DSP_DIGITMODE_MF);
		/* Initialize initial DSP progress detect parameters */
		ast_dsp_prog_reset(dsp);
		/* Initialize fax detector */
		ast_fax_detect_init(dsp);
	}
	return dsp;
}

void ast_dsp_set_features(struct ast_dsp *dsp, int features)
{
	dsp->features = features;
}

void ast_dsp_free(struct ast_dsp *dsp)
{
	if (ast_test_flag(&dsp->f, AST_FRFLAG_FROM_DSP)) {
		/* If this flag is still set, that means that the dsp's destruction 
		 * been torn down, while we still have a frame out there being used.
		 * When ast_frfree() gets called on that frame, this ast_trans_pvt
		 * will get destroyed, too. */

		/* Set the magic flag to destroy the dsp when the frame is freed */
		dsp->freqcount = -1;

		return;
	}
	ast_free(dsp);
}

void ast_dsp_set_threshold(struct ast_dsp *dsp, int threshold)
{
	dsp->threshold = threshold;
}

void ast_dsp_set_busy_count(struct ast_dsp *dsp, int cadences)
{
	if (cadences < 4)
		cadences = 4;
	if (cadences > DSP_HISTORY)
		cadences = DSP_HISTORY;
	dsp->busycount = cadences;
}

void ast_dsp_set_busy_pattern(struct ast_dsp *dsp, int tonelength, int quietlength)
{
	dsp->busy_tonelength = tonelength;
	dsp->busy_quietlength = quietlength;
	ast_debug(1, "dsp busy pattern set to %d,%d\n", tonelength, quietlength);
}

void ast_dsp_digitreset(struct ast_dsp *dsp)
{
	int i;
	
	dsp->thinkdigit = 0;
	if (dsp->digitmode & DSP_DIGITMODE_MF) {
		mf_detect_state_t *s = &dsp->digit_state.td.mf;
		/* Reinitialise the detector for the next block */
		for (i = 0;  i < 6;  i++) {
			goertzel_reset(&s->tone_out[i]);
		}
		s->hits[4] = s->hits[3] = s->hits[2] = s->hits[1] = s->hits[0] = s->current_hit = 0;
		s->current_sample = 0;
	} else {
		dtmf_detect_state_t *s = &dsp->digit_state.td.dtmf;
		/* Reinitialise the detector for the next block */
		for (i = 0;  i < 4;  i++) {
			goertzel_reset(&s->row_out[i]);
			goertzel_reset(&s->col_out[i]);
		}
		s->lasthit = s->current_hit = 0;
		s->energy = 0.0;
		s->current_sample = 0;
	}

	dsp->digit_state.digits[0] = '\0';
	dsp->digit_state.current_digits = 0;
}

void ast_dsp_reset(struct ast_dsp *dsp)
{
	int x;
	
	dsp->totalsilence = 0;
	dsp->gsamps = 0;
	for (x=0;x<4;x++)
		dsp->freqs[x].v2 = dsp->freqs[x].v3 = 0.0;
	memset(dsp->historicsilence, 0, sizeof(dsp->historicsilence));
	memset(dsp->historicnoise, 0, sizeof(dsp->historicnoise));	
	dsp->ringtimeout= 0;
}

int ast_dsp_digitmode(struct ast_dsp *dsp, int digitmode)
{
	int new;
	int old;
	
	old = dsp->digitmode & (DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_MUTEMAX);
	new = digitmode & (DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_MUTEMAX);
	if (old != new) {
		/* Must initialize structures if switching from MF to DTMF or vice-versa */
		ast_digit_detect_init(&dsp->digit_state, new & DSP_DIGITMODE_MF);
	}
	dsp->digitmode = digitmode;
	return 0;
}

int ast_dsp_set_faxmode(struct ast_dsp *dsp, int faxmode)
{
	if (dsp->faxmode != faxmode) {
		ast_fax_detect_init(dsp);
	}
	dsp->faxmode = faxmode;
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

int ast_dsp_get_tstate(struct ast_dsp *dsp) 
{
	return dsp->tstate;
}

int ast_dsp_get_tcount(struct ast_dsp *dsp) 
{
	return dsp->tcount;
}

void ast_dsp_frame_freed(struct ast_frame *fr)
{
	struct ast_dsp *dsp;

	ast_clear_flag(fr, AST_FRFLAG_FROM_DSP);

	dsp = (struct ast_dsp *) (((char *) fr) - offsetof(struct ast_dsp, f));

	if (dsp->freqcount != -1)
		return;
	
	ast_dsp_free(dsp);
}
