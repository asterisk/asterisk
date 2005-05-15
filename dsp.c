/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Convenience Signal Processing routines
 * 
 * Copyright (C) 2002, Digium
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License.
 *
 * Goertzel routines are borrowed from Steve Underwood's tremendous work on the
 * DTMF detector.
 *
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

#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <stdio.h>

#include "asterisk/frame.h"
#include "asterisk/channel.h"
#include "asterisk/logger.h"
#include "asterisk/dsp.h"
#include "asterisk/ulaw.h"
#include "asterisk/alaw.h"

/* Number of goertzels for progress detect */
#define GSAMP_SIZE_NA 183			/* North America - 350, 440, 480, 620, 950, 1400, 1800 Hz */
#define GSAMP_SIZE_CR 188			/* Costa Rica, Brazil - Only care about 425 Hz */

#define PROG_MODE_NA		0
#define PROG_MODE_CR		1	

/* For US modes */
#define HZ_350  0
#define HZ_440  1
#define HZ_480  2
#define HZ_620  3
#define HZ_950  4
#define HZ_1400 5
#define HZ_1800 6

/* For CR/BR modes */
#define HZ_425	0

static struct progalias {
	char *name;
	int mode;
} aliases[] = {
	{ "us", PROG_MODE_NA },
	{ "ca", PROG_MODE_NA },
	{ "cr", PROG_MODE_CR },
	{ "br", PROG_MODE_CR },
};

static struct progress {
	int size;
	int freqs[7];
} modes[] = {
	{ GSAMP_SIZE_NA, { 350, 440, 480, 620, 950, 1400, 1800 } },	/* North America */
	{ GSAMP_SIZE_CR, { 425 } },
};

#define DEFAULT_THRESHOLD	512

#define BUSY_PERCENT		10	/* The percentage diffrence between the two last silence periods */
#define BUSY_THRESHOLD		100	/* Max number of ms difference between max and min times in busy */
#define BUSY_MIN		75	/* Busy must be at least 80 ms in half-cadence */
#define BUSY_MAX		1100	/* Busy can't be longer than 1100 ms in half-cadence */

/* Remember last 15 units */
#define DSP_HISTORY 		15

/* Define if you want the fax detector -- NOT RECOMMENDED IN -STABLE */
#define FAX_DETECT

#define TONE_THRESH		10.0	/* How much louder the tone should be than channel energy */
#define TONE_MIN_THRESH 	1e8	/* How much tone there should be at least to attempt */
#define COUNT_THRESH		3	/* Need at least 50ms of stuff to count it */

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

#ifdef OLD_DSP_ROUTINES
#define MF_THRESHOLD		8.0e7
#define MF_NORMAL_TWIST		5.3     /* 8dB */
#define MF_REVERSE_TWIST	4.0     /* was 2.5 */
#define MF_RELATIVE_PEAK	5.3     /* 8dB */
#define MF_2ND_HARMONIC		1.7	/* was 2.5  */
#else
#define BELL_MF_THRESHOLD	1.6e9
#define BELL_MF_TWIST		4.0     /* 6dB */
#define BELL_MF_RELATIVE_PEAK	12.6    /* 11dB */
#endif

typedef struct {
	float v2;
	float v3;
	float fac;
#ifndef OLD_DSP_ROUTINES
	int samples;
#endif	
} goertzel_state_t;

typedef struct
{
	goertzel_state_t row_out[4];
	goertzel_state_t col_out[4];
#ifdef FAX_DETECT
	goertzel_state_t fax_tone;
#endif
#ifdef OLD_DSP_ROUTINES
	goertzel_state_t row_out2nd[4];
	goertzel_state_t col_out2nd[4];
#ifdef FAX_DETECT
	goertzel_state_t fax_tone2nd;    
#endif
	int hit1;
	int hit2;
	int hit3;
	int hit4;
#else
	int hits[3];
#endif	
	int mhit;
	float energy;
	int current_sample;

	char digits[MAX_DTMF_DIGITS + 1];
	
	int current_digits;
	int detected_digits;
	int lost_digits;
	int digit_hits[16];
#ifdef FAX_DETECT
	int fax_hits;
#endif
} dtmf_detect_state_t;

typedef struct
{
	goertzel_state_t tone_out[6];
	int mhit;
#ifdef OLD_DSP_ROUTINES
	int hit1;
	int hit2;
	int hit3;
	int hit4;
	goertzel_state_t tone_out2nd[6];
	float energy;
#else
	int hits[5];
#endif
	int current_sample;
	
	char digits[MAX_DTMF_DIGITS + 1];

	int current_digits;
	int detected_digits;
	int lost_digits;
#ifdef FAX_DETECT
	int fax_hits;
#endif
} mf_detect_state_t;

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

#ifdef FAX_DETECT
static float fax_freq = 1100.0;
#endif

static char dtmf_positions[] = "123A" "456B" "789C" "*0#D";

#ifdef OLD_DSP_ROUTINES
static char mf_hit[6][6] = {
	/*  700 + */ {   0, '1', '2', '4', '7', 'C' },
	/*  900 + */ { '1',   0, '3', '5', '8', 'A' },
	/* 1100 + */ { '2', '3',   0, '6', '9', '*' },
	/* 1300 + */ { '4', '5', '6',   0, '0', 'B' },
	/* 1500 + */ { '7', '8', '9', '0',  0, '#' },
	/* 1700 + */ { 'C', 'A', '*', 'B', '#',  0  },
};
#else
static char bell_mf_positions[] = "1247C-358A--69*---0B----#";
#endif

static inline void goertzel_sample(goertzel_state_t *s, short sample)
{
	float v1;
	float fsamp  = sample;
	
	v1 = s->v2;
	s->v2 = s->v3;
	s->v3 = s->fac * s->v2 - v1 + fsamp;
}

static inline void goertzel_update(goertzel_state_t *s, short *samps, int count)
{
	int i;
	
	for (i=0;i<count;i++) 
		goertzel_sample(s, samps[i]);
}


static inline float goertzel_result(goertzel_state_t *s)
{
	return s->v3 * s->v3 + s->v2 * s->v2 - s->v2 * s->v3 * s->fac;
}

static inline void goertzel_init(goertzel_state_t *s, float freq, int samples)
{
	s->v2 = s->v3 = 0.0;
	s->fac = 2.0 * cos(2.0 * M_PI * (freq / 8000.0));
#ifndef OLD_DSP_ROUTINES
	s->samples = samples;
#endif
}

static inline void goertzel_reset(goertzel_state_t *s)
{
	s->v2 = s->v3 = 0.0;
}

struct ast_dsp {
	struct ast_frame f;
	int threshold;
	int totalsilence;
	int totalnoise;
	int features;
	int busymaybe;
	int busycount;
	int historicnoise[DSP_HISTORY];
	int historicsilence[DSP_HISTORY];
	goertzel_state_t freqs[7];
	int freqcount;
	int gsamps;
	int gsamp_size;
	int progmode;
	int tstate;
	int tcount;
	int digitmode;
	int thinkdigit;
	float genergy;
	union {
		dtmf_detect_state_t dtmf;
		mf_detect_state_t mf;
	} td;
};

static void ast_dtmf_detect_init (dtmf_detect_state_t *s)
{
	int i;

#ifdef OLD_DSP_ROUTINES
	s->hit1 = 
	s->mhit = 
	s->hit3 =
	s->hit4 = 
	s->hit2 = 0;
#else
	s->hits[0] = s->hits[1] = s->hits[2] = 0;
#endif
	for (i = 0;  i < 4;  i++) {
		goertzel_init (&s->row_out[i], dtmf_row[i], 102);
		goertzel_init (&s->col_out[i], dtmf_col[i], 102);
#ifdef OLD_DSP_ROUTINES
		goertzel_init (&s->row_out2nd[i], dtmf_row[i] * 2.0, 102);
		goertzel_init (&s->col_out2nd[i], dtmf_col[i] * 2.0, 102);
#endif	
		s->energy = 0.0;
	}
#ifdef FAX_DETECT
	/* Same for the fax dector */
	goertzel_init (&s->fax_tone, fax_freq, 102);

#ifdef OLD_DSP_ROUTINES
	/* Same for the fax dector 2nd harmonic */
	goertzel_init (&s->fax_tone2nd, fax_freq * 2.0, 102);
#endif	
#endif /* FAX_DETECT */
	s->current_sample = 0;
	s->detected_digits = 0;
	s->current_digits = 0;
	memset(&s->digits, 0, sizeof(s->digits));
	s->lost_digits = 0;
	s->digits[0] = '\0';
}

static void ast_mf_detect_init (mf_detect_state_t *s)
{
	int i;
#ifdef OLD_DSP_ROUTINES
	s->hit1 = 
	s->hit2 = 0;
#else	
	s->hits[0] = s->hits[1] = s->hits[2] = s->hits[3] = s->hits[4] = 0;
#endif
	for (i = 0;  i < 6;  i++) {
		goertzel_init (&s->tone_out[i], mf_tones[i], 160);
#ifdef OLD_DSP_ROUTINES
		goertzel_init (&s->tone_out2nd[i], mf_tones[i] * 2.0, 160);
		s->energy = 0.0;
#endif
	}
	s->current_digits = 0;
	memset(&s->digits, 0, sizeof(s->digits));
	s->current_sample = 0;
	s->detected_digits = 0;
	s->lost_digits = 0;
	s->digits[0] = '\0';
	s->mhit = 0;
}

static int dtmf_detect (dtmf_detect_state_t *s, int16_t amp[], int samples, 
		 int digitmode, int *writeback, int faxdetect)
{
	float row_energy[4];
	float col_energy[4];
#ifdef FAX_DETECT
	float fax_energy;
#ifdef OLD_DSP_ROUTINES
	float fax_energy_2nd;
#endif	
#endif /* FAX_DETECT */
	float famp;
	float v1;
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
		if ((samples - sample) >= (102 - s->current_sample))
			limit = sample + (102 - s->current_sample);
		else
			limit = samples;
#if defined(USE_3DNOW)
		_dtmf_goertzel_update (s->row_out, amp + sample, limit - sample);
		_dtmf_goertzel_update (s->col_out, amp + sample, limit - sample);
#ifdef OLD_DSP_ROUTINES
		_dtmf_goertzel_update (s->row_out2nd, amp + sample, limit2 - sample);
		_dtmf_goertzel_update (s->col_out2nd, amp + sample, limit2 - sample);
#endif		
		/* XXX Need to fax detect for 3dnow too XXX */
		#warning "Fax Support Broken"
#else
		/* The following unrolled loop takes only 35% (rough estimate) of the 
		   time of a rolled loop on the machine on which it was developed */
		for (j=sample;j<limit;j++) {
			famp = amp[j];
			s->energy += famp*famp;
			/* With GCC 2.95, the following unrolled code seems to take about 35%
			   (rough estimate) as long as a neat little 0-3 loop */
			v1 = s->row_out[0].v2;
			s->row_out[0].v2 = s->row_out[0].v3;
			s->row_out[0].v3 = s->row_out[0].fac*s->row_out[0].v2 - v1 + famp;
			v1 = s->col_out[0].v2;
			s->col_out[0].v2 = s->col_out[0].v3;
			s->col_out[0].v3 = s->col_out[0].fac*s->col_out[0].v2 - v1 + famp;
			v1 = s->row_out[1].v2;
			s->row_out[1].v2 = s->row_out[1].v3;
			s->row_out[1].v3 = s->row_out[1].fac*s->row_out[1].v2 - v1 + famp;
			v1 = s->col_out[1].v2;
			s->col_out[1].v2 = s->col_out[1].v3;
			s->col_out[1].v3 = s->col_out[1].fac*s->col_out[1].v2 - v1 + famp;
			v1 = s->row_out[2].v2;
			s->row_out[2].v2 = s->row_out[2].v3;
			s->row_out[2].v3 = s->row_out[2].fac*s->row_out[2].v2 - v1 + famp;
			v1 = s->col_out[2].v2;
			s->col_out[2].v2 = s->col_out[2].v3;
			s->col_out[2].v3 = s->col_out[2].fac*s->col_out[2].v2 - v1 + famp;
			v1 = s->row_out[3].v2;
			s->row_out[3].v2 = s->row_out[3].v3;
			s->row_out[3].v3 = s->row_out[3].fac*s->row_out[3].v2 - v1 + famp;
			v1 = s->col_out[3].v2;
			s->col_out[3].v2 = s->col_out[3].v3;
			s->col_out[3].v3 = s->col_out[3].fac*s->col_out[3].v2 - v1 + famp;
#ifdef FAX_DETECT
			/* Update fax tone */
			v1 = s->fax_tone.v2;
			s->fax_tone.v2 = s->fax_tone.v3;
			s->fax_tone.v3 = s->fax_tone.fac*s->fax_tone.v2 - v1 + famp;
#endif /* FAX_DETECT */
#ifdef OLD_DSP_ROUTINES
			v1 = s->col_out2nd[0].v2;
			s->col_out2nd[0].v2 = s->col_out2nd[0].v3;
			s->col_out2nd[0].v3 = s->col_out2nd[0].fac*s->col_out2nd[0].v2 - v1 + famp;
			v1 = s->row_out2nd[0].v2;
			s->row_out2nd[0].v2 = s->row_out2nd[0].v3;
			s->row_out2nd[0].v3 = s->row_out2nd[0].fac*s->row_out2nd[0].v2 - v1 + famp;
			v1 = s->col_out2nd[1].v2;
			s->col_out2nd[1].v2 = s->col_out2nd[1].v3;
			s->col_out2nd[1].v3 = s->col_out2nd[1].fac*s->col_out2nd[1].v2 - v1 + famp;
			v1 = s->row_out2nd[1].v2;
			s->row_out2nd[1].v2 = s->row_out2nd[1].v3;
			s->row_out2nd[1].v3 = s->row_out2nd[1].fac*s->row_out2nd[1].v2 - v1 + famp;
			v1 = s->col_out2nd[2].v2;
			s->col_out2nd[2].v2 = s->col_out2nd[2].v3;
			s->col_out2nd[2].v3 = s->col_out2nd[2].fac*s->col_out2nd[2].v2 - v1 + famp;
			v1 = s->row_out2nd[2].v2;
			s->row_out2nd[2].v2 = s->row_out2nd[2].v3;
			s->row_out2nd[2].v3 = s->row_out2nd[2].fac*s->row_out2nd[2].v2 - v1 + famp;
			v1 = s->col_out2nd[3].v2;
			s->col_out2nd[3].v2 = s->col_out2nd[3].v3;
			s->col_out2nd[3].v3 = s->col_out2nd[3].fac*s->col_out2nd[3].v2 - v1 + famp;
			v1 = s->row_out2nd[3].v2;
			s->row_out2nd[3].v2 = s->row_out2nd[3].v3;
			s->row_out2nd[3].v3 = s->row_out2nd[3].fac*s->row_out2nd[3].v2 - v1 + famp;
#ifdef FAX_DETECT
			/* Update fax tone */            
			v1 = s->fax_tone.v2;
			s->fax_tone2nd.v2 = s->fax_tone2nd.v3;
			s->fax_tone2nd.v3 = s->fax_tone2nd.fac*s->fax_tone2nd.v2 - v1 + famp;
#endif /* FAX_DETECT */
#endif
		}
#endif
		s->current_sample += (limit - sample);
		if (s->current_sample < 102) {
			if (hit && !((digitmode & DSP_DIGITMODE_NOQUELCH))) {
				/* If we had a hit last time, go ahead and clear this out since likely it
				   will be another hit */
				for (i=sample;i<limit;i++) 
					amp[i] = 0;
				*writeback = 1;
			}
			continue;
		}
#ifdef FAX_DETECT
		/* Detect the fax energy, too */
		fax_energy = goertzel_result(&s->fax_tone);
#endif
		/* We are at the end of a DTMF detection block */
		/* Find the peak row and the peak column */
		row_energy[0] = goertzel_result (&s->row_out[0]);
		col_energy[0] = goertzel_result (&s->col_out[0]);

		for (best_row = best_col = 0, i = 1;  i < 4;  i++) {
			row_energy[i] = goertzel_result (&s->row_out[i]);
			if (row_energy[i] > row_energy[best_row])
				best_row = i;
			col_energy[i] = goertzel_result (&s->col_out[i]);
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
#ifdef OLD_DSP_ROUTINES
			/* ... and second harmonic test */
			if (i >= 4 && 
			    (row_energy[best_row] + col_energy[best_col]) > 42.0*s->energy &&
                	    goertzel_result(&s->col_out2nd[best_col])*DTMF_2ND_HARMONIC_COL < col_energy[best_col]
			    && goertzel_result(&s->row_out2nd[best_row])*DTMF_2ND_HARMONIC_ROW < row_energy[best_row]) {
#else
			/* ... and fraction of total energy test */
			if (i >= 4 &&
			    (row_energy[best_row] + col_energy[best_col]) > DTMF_TO_TOTAL_ENERGY*s->energy) {
#endif
				/* Got a hit */
				hit = dtmf_positions[(best_row << 2) + best_col];
				if (!(digitmode & DSP_DIGITMODE_NOQUELCH)) {
					/* Zero out frame data if this is part DTMF */
					for (i=sample;i<limit;i++) 
						amp[i] = 0;
					*writeback = 1;
				}
				/* Look for two successive similar results */
				/* The logic in the next test is:
				   We need two successive identical clean detects, with
				   something different preceeding it. This can work with
				   back to back differing digits. More importantly, it
				   can work with nasty phones that give a very wobbly start
				   to a digit */
#ifdef OLD_DSP_ROUTINES
				if (hit == s->hit3  &&  s->hit3 != s->hit2) {
					s->mhit = hit;
					s->digit_hits[(best_row << 2) + best_col]++;
					s->detected_digits++;
					if (s->current_digits < MAX_DTMF_DIGITS) {
						s->digits[s->current_digits++] = hit;
						s->digits[s->current_digits] = '\0';
					} else {
						s->lost_digits++;
					}
				}
#else				
				if (hit == s->hits[2]  &&  hit != s->hits[1]  &&  hit != s->hits[0]) {
					s->mhit = hit;
					s->digit_hits[(best_row << 2) + best_col]++;
					s->detected_digits++;
					if (s->current_digits < MAX_DTMF_DIGITS) {
						s->digits[s->current_digits++] = hit;
						s->digits[s->current_digits] = '\0';
					} else {
						s->lost_digits++;
					}
				}
#endif
			}
		} 
#ifdef FAX_DETECT
		if (!hit && (fax_energy >= FAX_THRESHOLD) && 
			(fax_energy >= DTMF_TO_TOTAL_ENERGY*s->energy) &&
			(faxdetect)) {
#if 0
			printf("Fax energy/Second Harmonic: %f\n", fax_energy);
#endif					
			/* XXX Probably need better checking than just this the energy XXX */
			hit = 'f';
			s->fax_hits++;
		} else {
			if (s->fax_hits > 5) {
				hit = 'f';
				s->mhit = 'f';
				s->detected_digits++;
				if (s->current_digits < MAX_DTMF_DIGITS) {
					s->digits[s->current_digits++] = hit;
					s->digits[s->current_digits] = '\0';
				} else {
					s->lost_digits++;
				}
			}
			s->fax_hits = 0;
		}
#endif /* FAX_DETECT */
#ifdef OLD_DSP_ROUTINES
		s->hit1 = s->hit2;
		s->hit2 = s->hit3;
		s->hit3 = hit;
#else
		s->hits[0] = s->hits[1];
		s->hits[1] = s->hits[2];
		s->hits[2] = hit;
#endif		
		/* Reinitialise the detector for the next block */
		for (i = 0;  i < 4;  i++) {
			goertzel_reset(&s->row_out[i]);
			goertzel_reset(&s->col_out[i]);
#ifdef OLD_DSP_ROUTINES
			goertzel_reset(&s->row_out2nd[i]);
			goertzel_reset(&s->col_out2nd[i]);
#endif			
		}
#ifdef FAX_DETECT
		goertzel_reset (&s->fax_tone);
#ifdef OLD_DSP_ROUTINES
		goertzel_reset (&s->fax_tone2nd);
#endif			
#endif
		s->energy = 0.0;
		s->current_sample = 0;
	}
	if ((!s->mhit) || (s->mhit != hit)) {
		s->mhit = 0;
		return(0);
	}
	return (hit);
}

/* MF goertzel size */
#ifdef OLD_DSP_ROUTINES
#define	MF_GSIZE 160
#else
#define MF_GSIZE 120
#endif

static int mf_detect (mf_detect_state_t *s, int16_t amp[],
                 int samples, int digitmode, int *writeback)
{
#ifdef OLD_DSP_ROUTINES
	float tone_energy[6];
	int best1;
	int best2;
	float max;
	int sofarsogood;
#else
	float energy[6];
	int best;
	int second_best;
#endif
	float famp;
	float v1;
	int i;
	int j;
	int sample;
	int hit;
	int limit;

	hit = 0;
	for (sample = 0;  sample < samples;  sample = limit) {
		/* 80 is optimised to meet the MF specs. */
		if ((samples - sample) >= (MF_GSIZE - s->current_sample))
			limit = sample + (MF_GSIZE - s->current_sample);
		else
			limit = samples;
#if defined(USE_3DNOW)
		_dtmf_goertzel_update (s->row_out, amp + sample, limit - sample);
		_dtmf_goertzel_update (s->col_out, amp + sample, limit - sample);
#ifdef OLD_DSP_ROUTINES
		_dtmf_goertzel_update (s->row_out2nd, amp + sample, limit2 - sample);
		_dtmf_goertzel_update (s->col_out2nd, amp + sample, limit2 - sample);
#endif
		/* XXX Need to fax detect for 3dnow too XXX */
		#warning "Fax Support Broken"
#else
		/* The following unrolled loop takes only 35% (rough estimate) of the 
		   time of a rolled loop on the machine on which it was developed */
		for (j = sample;  j < limit;  j++) {
			famp = amp[j];
#ifdef OLD_DSP_ROUTINES
			s->energy += famp*famp;
#endif
			/* With GCC 2.95, the following unrolled code seems to take about 35%
			   (rough estimate) as long as a neat little 0-3 loop */
			v1 = s->tone_out[0].v2;
			s->tone_out[0].v2 = s->tone_out[0].v3;
			s->tone_out[0].v3 = s->tone_out[0].fac*s->tone_out[0].v2 - v1 + famp;
			v1 = s->tone_out[1].v2;
			s->tone_out[1].v2 = s->tone_out[1].v3;
			s->tone_out[1].v3 = s->tone_out[1].fac*s->tone_out[1].v2 - v1 + famp;
			v1 = s->tone_out[2].v2;
			s->tone_out[2].v2 = s->tone_out[2].v3;
			s->tone_out[2].v3 = s->tone_out[2].fac*s->tone_out[2].v2 - v1 + famp;
			v1 = s->tone_out[3].v2;
			s->tone_out[3].v2 = s->tone_out[3].v3;
			s->tone_out[3].v3 = s->tone_out[3].fac*s->tone_out[3].v2 - v1 + famp;
			v1 = s->tone_out[4].v2;
			s->tone_out[4].v2 = s->tone_out[4].v3;
			s->tone_out[4].v3 = s->tone_out[4].fac*s->tone_out[4].v2 - v1 + famp;
			v1 = s->tone_out[5].v2;
			s->tone_out[5].v2 = s->tone_out[5].v3;
			s->tone_out[5].v3 = s->tone_out[5].fac*s->tone_out[5].v2 - v1 + famp;
#ifdef OLD_DSP_ROUTINES
			v1 = s->tone_out2nd[0].v2;
			s->tone_out2nd[0].v2 = s->tone_out2nd[0].v3;
			s->tone_out2nd[0].v3 = s->tone_out2nd[0].fac*s->tone_out2nd[0].v2 - v1 + famp;
			v1 = s->tone_out2nd[1].v2;
			s->tone_out2nd[1].v2 = s->tone_out2nd[1].v3;
			s->tone_out2nd[1].v3 = s->tone_out2nd[1].fac*s->tone_out2nd[1].v2 - v1 + famp;
			v1 = s->tone_out2nd[2].v2;
			s->tone_out2nd[2].v2 = s->tone_out2nd[2].v3;
			s->tone_out2nd[2].v3 = s->tone_out2nd[2].fac*s->tone_out2nd[2].v2 - v1 + famp;
			v1 = s->tone_out2nd[3].v2;
			s->tone_out2nd[3].v2 = s->tone_out2nd[3].v3;
			s->tone_out2nd[3].v3 = s->tone_out2nd[3].fac*s->tone_out2nd[3].v2 - v1 + famp;
			v1 = s->tone_out2nd[4].v2;
			s->tone_out2nd[4].v2 = s->tone_out2nd[4].v3;
			s->tone_out2nd[4].v3 = s->tone_out2nd[4].fac*s->tone_out2nd[2].v2 - v1 + famp;
			v1 = s->tone_out2nd[3].v2;
			s->tone_out2nd[5].v2 = s->tone_out2nd[6].v3;
			s->tone_out2nd[5].v3 = s->tone_out2nd[6].fac*s->tone_out2nd[3].v2 - v1 + famp;
#endif
		}
#endif
		s->current_sample += (limit - sample);
		if (s->current_sample < MF_GSIZE) {
			if (hit && !((digitmode & DSP_DIGITMODE_NOQUELCH))) {
				/* If we had a hit last time, go ahead and clear this out since likely it
				   will be another hit */
				for (i=sample;i<limit;i++) 
					amp[i] = 0;
				*writeback = 1;
			}
			continue;
		}
#ifdef OLD_DSP_ROUTINES		
		/* We're at the end of an MF detection block.  Go ahead and calculate
		   all the energies. */
		for (i=0;i<6;i++) {
			tone_energy[i] = goertzel_result(&s->tone_out[i]);
		}
		/* Find highest */
		best1 = 0;
		max = tone_energy[0];
		for (i=1;i<6;i++) {
			if (tone_energy[i] > max) {
				max = tone_energy[i];
				best1 = i;
			}
		}

		/* Find 2nd highest */
		if (best1) {
			max = tone_energy[0];
			best2 = 0;
		} else {
			max = tone_energy[1];
			best2 = 1;
		}

		for (i=0;i<6;i++) {
			if (i == best1) continue;
			if (tone_energy[i] > max) {
				max = tone_energy[i];
				best2 = i;
			}
		}
		hit = 0;
		if (best1 != best2) 
			sofarsogood=1;
		else 
			sofarsogood=0;
		/* Check for relative energies */
		for (i=0;i<6;i++) {
			if (i == best1) 
				continue;
			if (i == best2) 
				continue;
			if (tone_energy[best1] < tone_energy[i] * MF_RELATIVE_PEAK) {
				sofarsogood = 0;
				break;
			}
			if (tone_energy[best2] < tone_energy[i] * MF_RELATIVE_PEAK) {
				sofarsogood = 0;
				break;
			}
		}
		
		if (sofarsogood) {
			/* Check for 2nd harmonic */
			if (goertzel_result(&s->tone_out2nd[best1]) * MF_2ND_HARMONIC > tone_energy[best1]) 
				sofarsogood = 0;
			else if (goertzel_result(&s->tone_out2nd[best2]) * MF_2ND_HARMONIC > tone_energy[best2])
				sofarsogood = 0;
		}
		if (sofarsogood) {
			hit = mf_hit[best1][best2];
			if (!(digitmode & DSP_DIGITMODE_NOQUELCH)) {
				/* Zero out frame data if this is part DTMF */
				for (i=sample;i<limit;i++) 
					amp[i] = 0;
				*writeback = 1;
			}
			/* Look for two consecutive clean hits */
			if ((hit == s->hit3) && (s->hit3 != s->hit2)) {
				s->mhit = hit;
				s->detected_digits++;
				if (s->current_digits < MAX_DTMF_DIGITS - 2) {
					s->digits[s->current_digits++] = hit;
					s->digits[s->current_digits] = '\0';
				} else {
					s->lost_digits++;
				}
			}
		}
		
		s->hit1 = s->hit2;
		s->hit2 = s->hit3;
		s->hit3 = hit;
		/* Reinitialise the detector for the next block */
		for (i = 0;  i < 6;  i++) {
			goertzel_reset(&s->tone_out[i]);
			goertzel_reset(&s->tone_out2nd[i]);
		}
		s->energy = 0.0;
		s->current_sample = 0;
	}
#else
		/* We're at the end of an MF detection block.  */
		/* Find the two highest energies. The spec says to look for
		   two tones and two tones only. Taking this literally -ie
		   only two tones pass the minimum threshold - doesn't work
		   well. The sinc function mess, due to rectangular windowing
		   ensure that! Find the two highest energies and ensure they
		   are considerably stronger than any of the others. */
		energy[0] = goertzel_result(&s->tone_out[0]);
		energy[1] = goertzel_result(&s->tone_out[1]);
		if (energy[0] > energy[1]) {
			best = 0;
			second_best = 1;
		} else {
			best = 1;
			second_best = 0;
		}
		/*endif*/
		for (i=2;i<6;i++) {
			energy[i] = goertzel_result(&s->tone_out[i]);
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
			if (hit == s->hits[4] && hit == s->hits[3] &&
			   ((hit != '*' && hit != s->hits[2] && hit != s->hits[1])||
			    (hit == '*' && hit == s->hits[2] && hit != s->hits[1] && 
			    hit != s->hits[0]))) {
				s->detected_digits++;
				if (s->current_digits < MAX_DTMF_DIGITS) {
					s->digits[s->current_digits++] = hit;
					s->digits[s->current_digits] = '\0';
				} else {
					s->lost_digits++;
				}
			}
		} else {
			hit = 0;
		}
		s->hits[0] = s->hits[1];
		s->hits[1] = s->hits[2];
		s->hits[2] = s->hits[3];
		s->hits[3] = s->hits[4];
		s->hits[4] = hit;
		/* Reinitialise the detector for the next block */
		for (i = 0;  i < 6;  i++)
			goertzel_reset(&s->tone_out[i]);
		s->current_sample = 0;
	}
#endif	
	if ((!s->mhit) || (s->mhit != hit)) {
		s->mhit = 0;
		return(0);
	}
	return (hit);
}

static int __ast_dsp_digitdetect(struct ast_dsp *dsp, short *s, int len, int *writeback)
{
	int res;
	
	if (dsp->digitmode & DSP_DIGITMODE_MF)
		res = mf_detect(&dsp->td.mf, s, len, dsp->digitmode & DSP_DIGITMODE_RELAXDTMF, writeback);
	else
		res = dtmf_detect(&dsp->td.dtmf, s, len, dsp->digitmode & DSP_DIGITMODE_RELAXDTMF, writeback, dsp->features & DSP_FEATURE_FAX_DETECT);
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

int ast_dsp_getdigits (struct ast_dsp *dsp, char *buf, int max)
{
	if (dsp->digitmode & DSP_DIGITMODE_MF) {
		if (max > dsp->td.mf.current_digits)
			max = dsp->td.mf.current_digits;
		if (max > 0) {
			memcpy(buf, dsp->td.mf.digits, max);
			memmove(dsp->td.mf.digits, dsp->td.mf.digits + max, dsp->td.mf.current_digits - max);
			dsp->td.mf.current_digits -= max;
		}
		buf[max] = '\0';
		return  max;
	} else {
		if (max > dsp->td.dtmf.current_digits)
			max = dsp->td.dtmf.current_digits;
		if (max > 0) {
			memcpy (buf, dsp->td.dtmf.digits, max);
			memmove (dsp->td.dtmf.digits, dsp->td.dtmf.digits + max, dsp->td.dtmf.current_digits - max);
			dsp->td.dtmf.current_digits -= max;
		}
		buf[max] = '\0';
		return  max;
	}
}

static int __ast_dsp_call_progress(struct ast_dsp *dsp, short *s, int len)
{
	int x;
	int y;
	int pass;
	int newstate = DSP_TONE_STATE_SILENCE;
	int res = 0;
	while(len) {
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
#if 0
			printf("\n350:     425:     440:     480:     620:     950:     1400:    1800:    Energy:   \n");
			printf("%.2e %.2e %.2e %.2e %.2e %.2e %.2e %.2e %.2e\n", 
				hz[HZ_350], hz[HZ_425], hz[HZ_440], hz[HZ_480], hz[HZ_620], hz[HZ_950], hz[HZ_1400], hz[HZ_1800], dsp->genergy);
#endif
			switch(dsp->progmode) {
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
			default:
				ast_log(LOG_WARNING, "Can't process in unknown prog mode '%d'\n", dsp->progmode);
			}
			if (newstate == dsp->tstate) {
				dsp->tcount++;
				if (dsp->tcount == COUNT_THRESH) {
					if ((dsp->features & DSP_PROGRESS_BUSY) && 
					    dsp->tstate == DSP_TONE_STATE_BUSY) {
						res = AST_CONTROL_BUSY;
						dsp->features &= ~DSP_FEATURE_CALL_PROGRESS;
					} else if ((dsp->features & DSP_PROGRESS_TALK) && 
						   dsp->tstate == DSP_TONE_STATE_TALKING) {
						res = AST_CONTROL_ANSWER;
						dsp->features &= ~DSP_FEATURE_CALL_PROGRESS;
					} else if ((dsp->features & DSP_PROGRESS_RINGING) && 
						   dsp->tstate == DSP_TONE_STATE_RINGING)
						res = AST_CONTROL_RINGING;
					else if ((dsp->features & DSP_PROGRESS_CONGESTION) && 
						 dsp->tstate == DSP_TONE_STATE_SPECIAL3) {
						res = AST_CONTROL_CONGESTION;
						dsp->features &= ~DSP_FEATURE_CALL_PROGRESS;
					}
				}
			} else {
#if 0
				printf("Newstate: %d\n", newstate);
#endif
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
#if 0
	if (res)
		printf("Returning %d\n", res);
#endif		
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
		dsp->totalnoise += len/8;
		if (dsp->totalsilence) {
			int silence1 = dsp->historicsilence[DSP_HISTORY - 1];
			int silence2 = dsp->historicsilence[DSP_HISTORY - 2];
			/* Move and save history */
			memmove(dsp->historicsilence + DSP_HISTORY - dsp->busycount, dsp->historicsilence + DSP_HISTORY - dsp->busycount + 1, dsp->busycount*sizeof(dsp->historicsilence[0]));
			dsp->historicsilence[DSP_HISTORY - 1] = dsp->totalsilence;
			/* check if the previous sample differs only by BUSY_PERCENT from the one before it */
			if (silence1 < silence2) {
				if (silence1 + silence1/BUSY_PERCENT >= silence2)
					dsp->busymaybe = 1;
				else 
					dsp->busymaybe = 0;
			} else {
				if (silence1 - silence1/BUSY_PERCENT <= silence2)
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
#ifdef BUSYDETECT_MARTIN
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
			if (avgsilence - (avgsilence / BUSY_PERCENT) <= dsp->historicsilence[x])
				hitsilence++;
		} else {
			if (avgsilence + (avgsilence / BUSY_PERCENT) >= dsp->historicsilence[x])
				hitsilence++;
		}
#endif
		if (avgtone > dsp->historicnoise[x]) {
			if (avgtone - (avgtone / BUSY_PERCENT) <= dsp->historicnoise[x])
				hittone++;
		} else {
			if (avgtone + (avgtone / BUSY_PERCENT) >= dsp->historicnoise[x])
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
#ifdef BUSYDETECT_TONEONLY
#error You cant use BUSYDETECT_TONEONLY together with BUSYDETECT_COMPARE_TONE_AND_SILENCE
#endif
		if (avgtone > avgsilence) {
			if (avgtone - avgtone/(BUSY_PERCENT*2) <= avgsilence)
				res = 1;
		} else {
			if (avgtone + avgtone/(BUSY_PERCENT*2) >= avgsilence)
				res = 1;
		}
#else
		res = 1;
#endif
	}
#if 0
	if (res)
		ast_log(LOG_NOTICE, "detected busy, avgtone: %d, avgsilence %d\n", avgtone, avgsilence);
#endif
	return res;
}
#endif

#ifdef BUSYDETECT
int ast_dsp_busydetect(struct ast_dsp *dsp)
{
	int x;
	int res = 0;
	int max, min;

#if 0
	if (dsp->busy_hits > 5);
	return 0;
#endif
	if (dsp->busymaybe) {
#if 0
		printf("Maybe busy!\n");
#endif		
		dsp->busymaybe = 0;
		min = 9999;
		max = 0;
		for (x=DSP_HISTORY - dsp->busycount;x<DSP_HISTORY;x++) {
#if 0
			printf("Silence: %d, Noise: %d\n", dsp->historicsilence[x], dsp->historicnoise[x]);
#endif			
			if (dsp->historicsilence[x] < min)
				min = dsp->historicsilence[x];
			if (dsp->historicnoise[x] < min)
				min = dsp->historicnoise[x];
			if (dsp->historicsilence[x] > max)
				max = dsp->historicsilence[x];
			if (dsp->historicnoise[x] > max)
				max = dsp->historicnoise[x];
		}
		if ((max - min < BUSY_THRESHOLD) && (max < BUSY_MAX) && (min > BUSY_MIN)) {
#if 0
			printf("Busy!\n");
#endif			
			res = 1;
		}
#if 0
		printf("Min: %d, max: %d\n", min, max);
#endif		
	}
	return res;
}
#endif

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
			switch(inf->subclass) { \
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
	switch(af->subclass) {
	case AST_FORMAT_SLINEAR:
		shortdata = af->data;
		len = af->datalen / 2;
		break;
	case AST_FORMAT_ULAW:
		shortdata = alloca(af->datalen * 2);
		if (!shortdata) {
			ast_log(LOG_WARNING, "Unable to allocate stack space for data: %s\n", strerror(errno));
			return af;
		}
		for (x=0;x<len;x++) 
			shortdata[x] = AST_MULAW(odata[x]);
		break;
	case AST_FORMAT_ALAW:
		shortdata = alloca(af->datalen * 2);
		if (!shortdata) {
			ast_log(LOG_WARNING, "Unable to allocate stack space for data: %s\n", strerror(errno));
			return af;
		}
		for (x=0;x<len;x++) 
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
		return &dsp->f;
	}
	if ((dsp->features & DSP_FEATURE_BUSY_DETECT) && ast_dsp_busydetect(dsp)) {
		chan->_softhangup |= AST_SOFTHANGUP_DEV;
		memset(&dsp->f, 0, sizeof(dsp->f));
		dsp->f.frametype = AST_FRAME_CONTROL;
		dsp->f.subclass = AST_CONTROL_BUSY;
		ast_log(LOG_DEBUG, "Requesting Hangup because the busy tone was detected on channel %s\n", chan->name);
		return &dsp->f;
	}
	if ((dsp->features & DSP_FEATURE_DTMF_DETECT)) {
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
					return &dsp->f;
				}
			} else {
				if (digit) {
					/* Thought we saw one last time.  Pretty sure we really have now */
					if (dsp->thinkdigit) {
						if ((dsp->thinkdigit != 'x') && (dsp->thinkdigit != digit)) {
							/* If we found a digit, and we're changing digits, go
							   ahead and send this one, but DON'T stop confmute because
							   we're detecting something else, too... */
							memset(&dsp->f, 0, sizeof(dsp->f));
							dsp->f.frametype = AST_FRAME_DTMF;
							dsp->f.subclass = dsp->thinkdigit;
							FIX_INF(af);
							if (chan)
								ast_queue_frame(chan, af);
							ast_frfree(af);
						}
						dsp->thinkdigit = digit;
						return &dsp->f;
					}
					dsp->thinkdigit = digit;
				} else {
					if (dsp->thinkdigit) {
						memset(&dsp->f, 0, sizeof(dsp->f));
						if (dsp->thinkdigit != 'x') {
							/* If we found a digit, send it now */
							dsp->f.frametype = AST_FRAME_DTMF;
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
						return &dsp->f;
					}
				}
			}
		} else if (!digit) {
			/* Only check when there is *not* a hit... */
			if (dsp->digitmode & DSP_DIGITMODE_MF) {
				if (dsp->td.mf.current_digits) {
					memset(&dsp->f, 0, sizeof(dsp->f));
					dsp->f.frametype = AST_FRAME_DTMF;
					dsp->f.subclass = dsp->td.mf.digits[0];
					memmove(dsp->td.mf.digits, dsp->td.mf.digits + 1, dsp->td.mf.current_digits);
					dsp->td.mf.current_digits--;
					FIX_INF(af);
					if (chan)
						ast_queue_frame(chan, af);
					ast_frfree(af);
					return &dsp->f;
				}
			} else {
				if (dsp->td.dtmf.current_digits) {
					memset(&dsp->f, 0, sizeof(dsp->f));
					dsp->f.frametype = AST_FRAME_DTMF;
					dsp->f.subclass = dsp->td.dtmf.digits[0];
					memmove(dsp->td.dtmf.digits, dsp->td.dtmf.digits + 1, dsp->td.dtmf.current_digits);
					dsp->td.dtmf.current_digits--;
					FIX_INF(af);
					if (chan)
						ast_queue_frame(chan, af);
					ast_frfree(af);
					return &dsp->f;
				}
			}
		}
	}
	if ((dsp->features & DSP_FEATURE_CALL_PROGRESS)) {
		res = __ast_dsp_call_progress(dsp, shortdata, len);
		if (res) {
			switch(res) {
			case AST_CONTROL_ANSWER:
			case AST_CONTROL_BUSY:
			case AST_CONTROL_RINGING:
			case AST_CONTROL_CONGESTION:
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
	for (x=0;x<sizeof(modes[dsp->progmode].freqs) / sizeof(modes[dsp->progmode].freqs[0]);x++) {
		if (modes[dsp->progmode].freqs[x]) {
			goertzel_init(&dsp->freqs[x], (float)modes[dsp->progmode].freqs[x], dsp->gsamp_size);
			max = x + 1;
		}
	}
	dsp->freqcount = max;
}

struct ast_dsp *ast_dsp_new(void)
{
	struct ast_dsp *dsp;

	dsp = malloc(sizeof(struct ast_dsp));
	if (dsp) {
		memset(dsp, 0, sizeof(struct ast_dsp));
		dsp->threshold = DEFAULT_THRESHOLD;
		dsp->features = DSP_FEATURE_SILENCE_SUPPRESS;
		dsp->busycount = DSP_HISTORY;
		/* Initialize DTMF detector */
		ast_dtmf_detect_init(&dsp->td.dtmf);
		/* Initialize initial DSP progress detect parameters */
		ast_dsp_prog_reset(dsp);
	}
	return dsp;
}

void ast_dsp_set_features(struct ast_dsp *dsp, int features)
{
	dsp->features = features;
}

void ast_dsp_free(struct ast_dsp *dsp)
{
	free(dsp);
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

void ast_dsp_digitreset(struct ast_dsp *dsp)
{
	int i;
	
	dsp->thinkdigit = 0;
	if (dsp->digitmode & DSP_DIGITMODE_MF) {
		memset(dsp->td.mf.digits, 0, sizeof(dsp->td.mf.digits));
		dsp->td.mf.current_digits = 0;
		/* Reinitialise the detector for the next block */
		for (i = 0;  i < 6;  i++) {
			goertzel_reset(&dsp->td.mf.tone_out[i]);
#ifdef OLD_DSP_ROUTINES
			goertzel_reset(&dsp->td.mf.tone_out2nd[i]);
#endif			
		}
#ifdef OLD_DSP_ROUTINES
		dsp->td.mf.energy = 0.0;
		dsp->td.mf.hit1 = dsp->td.mf.hit2 = dsp->td.mf.hit3 = dsp->td.mf.hit4 = dsp->td.mf.mhit = 0;
#else
		dsp->td.mf.hits[4] = dsp->td.mf.hits[3] = dsp->td.mf.hits[2] = dsp->td.mf.hits[1] = dsp->td.mf.hits[0] = dsp->td.mf.mhit = 0;
#endif		
		dsp->td.mf.current_sample = 0;
	} else {
		memset(dsp->td.dtmf.digits, 0, sizeof(dsp->td.dtmf.digits));
		dsp->td.dtmf.current_digits = 0;
		/* Reinitialise the detector for the next block */
		for (i = 0;  i < 4;  i++) {
			goertzel_reset(&dsp->td.dtmf.row_out[i]);
			goertzel_reset(&dsp->td.dtmf.col_out[i]);
#ifdef OLD_DSP_ROUTINES
			goertzel_reset(&dsp->td.dtmf.row_out2nd[i]);
			goertzel_reset(&dsp->td.dtmf.col_out2nd[i]);
#endif			
		}
#ifdef FAX_DETECT
		goertzel_reset (&dsp->td.dtmf.fax_tone);
#endif
#ifdef OLD_DSP_ROUTINES
#ifdef FAX_DETECT
		goertzel_reset (&dsp->td.dtmf.fax_tone2nd);
#endif
		dsp->td.dtmf.hit1 = dsp->td.dtmf.hit2 = dsp->td.dtmf.hit3 = dsp->td.dtmf.hit4 = dsp->td.dtmf.mhit = 0;
#else
		dsp->td.dtmf.hits[2] = dsp->td.dtmf.hits[1] = dsp->td.dtmf.hits[0] =  dsp->td.dtmf.mhit = 0;
#endif		
		dsp->td.dtmf.energy = 0.0;
		dsp->td.dtmf.current_sample = 0;
	}
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
}

int ast_dsp_digitmode(struct ast_dsp *dsp, int digitmode)
{
	int new;
	int old;
	
	old = dsp->digitmode & (DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_MUTEMAX);
	new = digitmode & (DSP_DIGITMODE_DTMF | DSP_DIGITMODE_MF | DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_MUTEMAX);
	if (old != new) {
		/* Must initialize structures if switching from MF to DTMF or vice-versa */
		if (new & DSP_DIGITMODE_MF)
			ast_mf_detect_init(&dsp->td.mf);
		else
			ast_dtmf_detect_init(&dsp->td.dtmf);
	}
	dsp->digitmode = digitmode;
	return 0;
}

int ast_dsp_set_call_progress_zone(struct ast_dsp *dsp, char *zone)
{
	int x;
	
	for (x=0;x<sizeof(aliases) / sizeof(aliases[0]);x++) {
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
