/* codec_g726.c - translate between signed linear and ITU G.726-32kbps
 * 
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Based on frompcm.c and topcm.c from the Emiliano MIPL browser/
 * interpreter.  See http://www.bsdtelephony.com.mx
 *
 * Copyright (c) 2004, Digium
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/lock.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/translate.h>
#include <asterisk/channel.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WANT_ASM
#include "log2comp.h"

/* define NOT_BLI to use a faster but not bit-level identical version */
/* #define NOT_BLI */

#if defined(NOT_BLI)
#	if defined(_MSC_VER)
typedef __int64 sint64;
#	elif defined(__GNUC__)
typedef long long sint64;
#	else
#		error 64-bit integer type is not defined for your compiler/platform
#	endif
#endif

#define BUFFER_SIZE   8096	/* size for the translation buffers */
#define BUF_SHIFT	5

AST_MUTEX_DEFINE_STATIC(localuser_lock);
static int localusecnt = 0;

static char *tdesc = "ITU G.726-32kbps G726 Transcoder";

/* Sample frame data */

#include "slin_g726_ex.h"
#include "g726_slin_ex.h"

/*
 * The following is the definition of the state structure
 * used by the G.721/G.723 encoder and decoder to preserve their internal
 * state between successive calls.  The meanings of the majority
 * of the state structure fields are explained in detail in the
 * CCITT Recommendation G.721.  The field names are essentially indentical
 * to variable names in the bit level description of the coding algorithm
 * included in this Recommendation.
 */
struct g726_state {
	long yl;	/* Locked or steady state step size multiplier. */
	int yu;		/* Unlocked or non-steady state step size multiplier. */
	int dms;	/* Short term energy estimate. */
	int dml;	/* Long term energy estimate. */
	int ap;		/* Linear weighting coefficient of 'yl' and 'yu'. */

	int a[2];	/* Coefficients of pole portion of prediction filter.
				 * stored as fixed-point 1==2^14 */
	int b[6];	/* Coefficients of zero portion of prediction filter.
				 * stored as fixed-point 1==2^14 */
	int pk[2];	/* Signs of previous two samples of a partially
			 * reconstructed signal.
			 */
	int dq[6];  /* Previous 6 samples of the quantized difference signal
				 * stored as fixed point 1==2^12,
				 * or in internal floating point format */
	int sr[2];	/* Previous 2 samples of the quantized difference signal
				 * stored as fixed point 1==2^12,
				 * or in internal floating point format */
	int td;	/* delayed tone detect, new in 1988 version */
};



static int qtab_721[7] = {-124, 80, 178, 246, 300, 349, 400};
/*
 * Maps G.721 code word to reconstructed scale factor normalized log
 * magnitude values.
 */
static int _dqlntab[16] = {-2048, 4, 135, 213, 273, 323, 373, 425,
				425, 373, 323, 273, 213, 135, 4, -2048};

/* Maps G.721 code word to log of scale factor multiplier. */
static int _witab[16] = {-12, 18, 41, 64, 112, 198, 355, 1122,
				1122, 355, 198, 112, 64, 41, 18, -12};
/*
 * Maps G.721 code words to a set of values whose long and short
 * term averages are computed and then compared to give an indication
 * how stationary (steady state) the signal is.
 */
static int _fitab[16] = {0, 0, 0, 0x200, 0x200, 0x200, 0x600, 0xE00,
				0xE00, 0x600, 0x200, 0x200, 0x200, 0, 0, 0};

/* Deprecated
static int power2[15] = {1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80,
			0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000};
*/

/*
 * g72x_init_state()
 *
 * This routine initializes and/or resets the g726_state structure
 * pointed to by 'state_ptr'.
 * All the initial state values are specified in the CCITT G.721 document.
 */
static void g726_init_state(struct g726_state *state_ptr)
{
	int		cnta;

	state_ptr->yl = 34816;
	state_ptr->yu = 544;
	state_ptr->dms = 0;
	state_ptr->dml = 0;
	state_ptr->ap = 0;
	for (cnta = 0; cnta < 2; cnta++)
	{
		state_ptr->a[cnta] = 0;
		state_ptr->pk[cnta] = 0;
#ifdef NOT_BLI
		state_ptr->sr[cnta] = 1;
#else
		state_ptr->sr[cnta] = 32;
#endif
	}
	for (cnta = 0; cnta < 6; cnta++)
	{
		state_ptr->b[cnta] = 0;
#ifdef NOT_BLI
		state_ptr->dq[cnta] = 1;
#else
		state_ptr->dq[cnta] = 32;
#endif
	}
	state_ptr->td = 0;
}

/*
 * quan()
 *
 * quantizes the input val against the table of integers.
 * It returns i if table[i - 1] <= val < table[i].
 *
 * Using linear search for simple coding.
 */
static int quan(int val, int *table, int size)
{
	int		i;

	for (i = 0; i < size && val >= *table; ++i, ++table)
		;
	return (i);
}

#ifdef NOT_BLI /* faster non-identical version */

/*
 * predictor_zero()
 *
 * computes the estimated signal from 6-zero predictor.
 *
 */
static int predictor_zero(struct g726_state *state_ptr)
{	/* divide by 2 is necessary here to handle negative numbers correctly */
	int i;
	sint64 sezi;
	for (sezi = 0, i = 0; i < 6; i++)			/* ACCUM */
		sezi += (sint64)state_ptr->b[i] * state_ptr->dq[i];
	return (int)(sezi >> 13) / 2 /* 2^14 */;
}

/*
 * predictor_pole()
 *
 * computes the estimated signal from 2-pole predictor.
 *
 */
static int predictor_pole(struct g726_state *state_ptr)
{	/* divide by 2 is necessary here to handle negative numbers correctly */
	return (int)(((sint64)state_ptr->a[1] * state_ptr->sr[1] +
	              (sint64)state_ptr->a[0] * state_ptr->sr[0]) >> 13) / 2 /* 2^14 */;
}

#else /* NOT_BLI - identical version */
/*
 * fmult()
 *
 * returns the integer product of the fixed-point number "an" (1==2^12) and
 * "floating point" representation (4-bit exponent, 6-bit mantessa) "srn".
 */
static int fmult(int an, int srn)
{
	int		anmag, anexp, anmant;
	int		wanexp, wanmant;
	int		retval;

	anmag = (an > 0) ? an : ((-an) & 0x1FFF);
	anexp = log2(anmag) - 5;
	anmant = (anmag == 0) ? 32 :
	    (anexp >= 0) ? anmag >> anexp : anmag << -anexp;
	wanexp = anexp + ((srn >> 6) & 0xF) - 13;

	wanmant = (anmant * (srn & 077) + 0x30) >> 4;
	retval = (wanexp >= 0) ? ((wanmant << wanexp) & 0x7FFF) :
	    (wanmant >> -wanexp);

	return (((an ^ srn) < 0) ? -retval : retval);
}

static int predictor_zero(struct g726_state *state_ptr)
{
	int		i;
	int		sezi;
	for (sezi = 0, i = 0; i < 6; i++)			/* ACCUM */
		sezi += fmult(state_ptr->b[i] >> 2, state_ptr->dq[i]);
	return sezi;
}

static int predictor_pole(struct g726_state *state_ptr)
{
	return (fmult(state_ptr->a[1] >> 2, state_ptr->sr[1]) +
			fmult(state_ptr->a[0] >> 2, state_ptr->sr[0]));
}

#endif /* NOT_BLI */

/*
 * step_size()
 *
 * computes the quantization step size of the adaptive quantizer.
 *
 */
static int step_size(struct g726_state *state_ptr)
{
	int		y;
	int		dif;
	int		al;

	if (state_ptr->ap >= 256)
		return (state_ptr->yu);
	else {
		y = state_ptr->yl >> 6;
		dif = state_ptr->yu - y;
		al = state_ptr->ap >> 2;
		if (dif > 0)
			y += (dif * al) >> 6;
		else if (dif < 0)
			y += (dif * al + 0x3F) >> 6;
		return (y);
	}
}

/*
 * quantize()
 *
 * Given a raw sample, 'd', of the difference signal and a
 * quantization step size scale factor, 'y', this routine returns the
 * ADPCM codeword to which that sample gets quantized.  The step
 * size scale factor division operation is done in the log base 2 domain
 * as a subtraction.
 */
static int quantize(
	int		d,	/* Raw difference signal sample */
	int		y,	/* Step size multiplier */
	int		*table,	/* quantization table */
	int		size)	/* table size of integers */
{
	int		dqm;	/* Magnitude of 'd' */
	int		exp;	/* Integer part of base 2 log of 'd' */
	int		mant;	/* Fractional part of base 2 log */
	int		dl;		/* Log of magnitude of 'd' */
	int		dln;	/* Step size scale factor normalized log */
	int		i;

	/*
	 * LOG
	 *
	 * Compute base 2 log of 'd', and store in 'dl'.
	 */
	dqm = abs(d);
	exp = log2(dqm);
	if (exp < 0)
		exp = 0;
	mant = ((dqm << 7) >> exp) & 0x7F;	/* Fractional portion. */
	dl = (exp << 7) | mant;

	/*
	 * SUBTB
	 *
	 * "Divide" by step size multiplier.
	 */
	dln = dl - (y >> 2);

	/*
	 * QUAN
	 *
	 * Obtain codword i for 'd'.
	 */
	i = quan(dln, table, size);
	if (d < 0)			/* take 1's complement of i */
		return ((size << 1) + 1 - i);
	else if (i == 0)		/* take 1's complement of 0 */
		return ((size << 1) + 1); /* new in 1988 */
	else
		return (i);
}

/*
 * reconstruct()
 *
 * Returns reconstructed difference signal 'dq' obtained from
 * codeword 'i' and quantization step size scale factor 'y'.
 * Multiplication is performed in log base 2 domain as addition.
 */
static int reconstruct(
	int		sign,	/* 0 for non-negative value */
	int		dqln,	/* G.72x codeword */
	int		y)	/* Step size multiplier */
{
	int		dql;	/* Log of 'dq' magnitude */
	int		dex;	/* Integer part of log */
	int		dqt;
	int		dq;	/* Reconstructed difference signal sample */

	dql = dqln + (y >> 2);	/* ADDA */

	if (dql < 0) {
#ifdef NOT_BLI
		return (sign) ? -1 : 1;
#else
		return (sign) ? -0x8000 : 0;
#endif
	} else {		/* ANTILOG */
		dex = (dql >> 7) & 15;
		dqt = 128 + (dql & 127);
#ifdef NOT_BLI
		dq = ((dqt << 19) >> (14 - dex));
		return (sign) ? -dq : dq;
#else
		dq = (dqt << 7) >> (14 - dex);
		return (sign) ? (dq - 0x8000) : dq;
#endif
	}
}

/*
 * update()
 *
 * updates the state variables for each output code
 */
static void update(
	int		code_size,	/* distinguish 723_40 with others */
	int		y,		/* quantizer step size */
	int		wi,		/* scale factor multiplier */
	int		fi,		/* for long/short term energies */
	int		dq,		/* quantized prediction difference */
	int		sr,		/* reconstructed signal */
	int		dqsez,		/* difference from 2-pole predictor */
	struct g726_state *state_ptr)	/* coder state pointer */
{
	int		cnt;
	int		mag;		/* Adaptive predictor, FLOAT A */
#ifndef NOT_BLI
	int		exp;
#endif
	int		a2p=0;		/* LIMC */
	int		a1ul;		/* UPA1 */
	int		pks1;		/* UPA2 */
	int		fa1;
	int		tr;			/* tone/transition detector */
	int		ylint, thr2, dqthr;
	int		ylfrac, thr1;
	int		pk0;

	pk0 = (dqsez < 0) ? 1 : 0;	/* needed in updating predictor poles */

#ifdef NOT_BLI
	mag = abs(dq / 0x1000); /* prediction difference magnitude */
#else
	mag = dq & 0x7FFF;		/* prediction difference magnitude */
#endif
	/* TRANS */
	ylint = state_ptr->yl >> 15;	/* exponent part of yl */
	ylfrac = (state_ptr->yl >> 10) & 0x1F;	/* fractional part of yl */
	thr1 = (32 + ylfrac) << ylint;		/* threshold */
	thr2 = (ylint > 9) ? 31 << 10 : thr1;	/* limit thr2 to 31 << 10 */
	dqthr = (thr2 + (thr2 >> 1)) >> 1;	/* dqthr = 0.75 * thr2 */
	if (state_ptr->td == 0)		/* signal supposed voice */
		tr = 0;
	else if (mag <= dqthr)		/* supposed data, but small mag */
		tr = 0;			/* treated as voice */
	else				/* signal is data (modem) */
		tr = 1;

	/*
	 * Quantizer scale factor adaptation.
	 */

	/* FUNCTW & FILTD & DELAY */
	/* update non-steady state step size multiplier */
	state_ptr->yu = y + ((wi - y) >> 5);

	/* LIMB */
	if (state_ptr->yu < 544)	/* 544 <= yu <= 5120 */
		state_ptr->yu = 544;
	else if (state_ptr->yu > 5120)
		state_ptr->yu = 5120;

	/* FILTE & DELAY */
	/* update steady state step size multiplier */
	state_ptr->yl += state_ptr->yu + ((-state_ptr->yl) >> 6);

	/*
	 * Adaptive predictor coefficients.
	 */
	if (tr == 1) {			/* reset a's and b's for modem signal */
		state_ptr->a[0] = 0;
		state_ptr->a[1] = 0;
		state_ptr->b[0] = 0;
		state_ptr->b[1] = 0;
		state_ptr->b[2] = 0;
		state_ptr->b[3] = 0;
		state_ptr->b[4] = 0;
		state_ptr->b[5] = 0;
	} else {			/* update a's and b's */
		pks1 = pk0 ^ state_ptr->pk[0];		/* UPA2 */

		/* update predictor pole a[1] */
		a2p = state_ptr->a[1] - (state_ptr->a[1] >> 7);
		if (dqsez != 0) {
			fa1 = (pks1) ? state_ptr->a[0] : -state_ptr->a[0];
			if (fa1 < -8191)	/* a2p = function of fa1 */
				a2p -= 0x100;
			else if (fa1 > 8191)
				a2p += 0xFF;
			else
				a2p += fa1 >> 5;

			if (pk0 ^ state_ptr->pk[1])
				/* LIMC */
				if (a2p <= -12160)
					a2p = -12288;
				else if (a2p >= 12416)
					a2p = 12288;
				else
					a2p -= 0x80;
			else if (a2p <= -12416)
				a2p = -12288;
			else if (a2p >= 12160)
				a2p = 12288;
			else
				a2p += 0x80;
		}

		/* TRIGB & DELAY */
		state_ptr->a[1] = a2p;

		/* UPA1 */
		/* update predictor pole a[0] */
		state_ptr->a[0] -= state_ptr->a[0] >> 8;
		if (dqsez != 0) {
			if (pks1 == 0)
				state_ptr->a[0] += 192;
			else
				state_ptr->a[0] -= 192;
		}
		/* LIMD */
		a1ul = 15360 - a2p;
		if (state_ptr->a[0] < -a1ul)
			state_ptr->a[0] = -a1ul;
		else if (state_ptr->a[0] > a1ul)
			state_ptr->a[0] = a1ul;

		/* UPB : update predictor zeros b[6] */
		for (cnt = 0; cnt < 6; cnt++) {
			if (code_size == 5)		/* for 40Kbps G.723 */
				state_ptr->b[cnt] -= state_ptr->b[cnt] >> 9;
			else			/* for G.721 and 24Kbps G.723 */
				state_ptr->b[cnt] -= state_ptr->b[cnt] >> 8;
			if (mag)
			{	/* XOR */
				if ((dq ^ state_ptr->dq[cnt]) >= 0)
					state_ptr->b[cnt] += 128;
				else
					state_ptr->b[cnt] -= 128;
			}
		}
	}

	for (cnt = 5; cnt > 0; cnt--)
		state_ptr->dq[cnt] = state_ptr->dq[cnt-1];
#ifdef NOT_BLI
	state_ptr->dq[0] = dq;
#else
	/* FLOAT A : convert dq[0] to 4-bit exp, 6-bit mantissa f.p. */
	if (mag == 0) {
		state_ptr->dq[0] = (dq >= 0) ? 0x20 : 0x20 - 0x400;
	} else {
		exp = log2(mag) + 1;
		state_ptr->dq[0] = (dq >= 0) ?
		    (exp << 6) + ((mag << 6) >> exp) :
		    (exp << 6) + ((mag << 6) >> exp) - 0x400;
	}
#endif

	state_ptr->sr[1] = state_ptr->sr[0];
#ifdef NOT_BLI
	state_ptr->sr[0] = sr;
#else
	/* FLOAT B : convert sr to 4-bit exp., 6-bit mantissa f.p. */
	if (sr == 0) {
		state_ptr->sr[0] = 0x20;
	} else if (sr > 0) {
		exp = log2(sr) + 1;
		state_ptr->sr[0] = (exp << 6) + ((sr << 6) >> exp);
	} else if (sr > -0x8000) {
		mag = -sr;
		exp = log2(mag) + 1;
		state_ptr->sr[0] =  (exp << 6) + ((mag << 6) >> exp) - 0x400;
	} else
		state_ptr->sr[0] = 0x20 - 0x400;
#endif

	/* DELAY A */
	state_ptr->pk[1] = state_ptr->pk[0];
	state_ptr->pk[0] = pk0;

	/* TONE */
	if (tr == 1)		/* this sample has been treated as data */
		state_ptr->td = 0;	/* next one will be treated as voice */
	else if (a2p < -11776)	/* small sample-to-sample correlation */
		state_ptr->td = 1;	/* signal may be data */
	else				/* signal is voice */
		state_ptr->td = 0;

	/*
	 * Adaptation speed control.
	 */
	state_ptr->dms += (fi - state_ptr->dms) >> 5;		/* FILTA */
	state_ptr->dml += (((fi << 2) - state_ptr->dml) >> 7);	/* FILTB */

	if (tr == 1)
		state_ptr->ap = 256;
	else if (y < 1536)					/* SUBTC */
		state_ptr->ap += (0x200 - state_ptr->ap) >> 4;
	else if (state_ptr->td == 1)
		state_ptr->ap += (0x200 - state_ptr->ap) >> 4;
	else if (abs((state_ptr->dms << 2) - state_ptr->dml) >=
	    (state_ptr->dml >> 3))
		state_ptr->ap += (0x200 - state_ptr->ap) >> 4;
	else
		state_ptr->ap += (-state_ptr->ap) >> 4;
}

/*
 * g726_decode()
 *
 * Description:
 *
 * Decodes a 4-bit code of G.726-32 encoded data of i and
 * returns the resulting linear PCM, A-law or u-law value.
 * return -1 for unknown out_coding value.
 */
static int g726_decode(int	i, struct g726_state *state_ptr)
{
	int		sezi, sez, se;	/* ACCUM */
	int		y;			/* MIX */
	int		sr;			/* ADDB */
	int		dq;
	int		dqsez;

	i &= 0x0f;			/* mask to get proper bits */
#ifdef NOT_BLI
	sezi = predictor_zero(state_ptr);
	sez = sezi;
	se = sezi + predictor_pole(state_ptr);	/* estimated signal */
#else
	sezi = predictor_zero(state_ptr);
	sez = sezi >> 1;
	se = (sezi + predictor_pole(state_ptr)) >> 1;	/* estimated signal */
#endif

	y = step_size(state_ptr);	/* dynamic quantizer step size */

	dq = reconstruct(i & 8, _dqlntab[i], y); /* quantized diff. */

#ifdef NOT_BLI
	sr = se + dq;				/* reconst. signal */
	dqsez = dq + sez;			/* pole prediction diff. */
#else
	sr = (dq < 0) ? se - (dq & 0x3FFF) : se + dq;	/* reconst. signal */
	dqsez = sr - se + sez;		/* pole prediction diff. */
#endif

	update(4, y, _witab[i] << 5, _fitab[i], dq, sr, dqsez, state_ptr);

#ifdef NOT_BLI
	return (sr >> 10);	/* sr was 26-bit dynamic range */
#else
	return (sr << 2);	/* sr was 14-bit dynamic range */
#endif
}

/*
 * g726_encode()
 *
 * Encodes the input vale of linear PCM, A-law or u-law data sl and returns
 * the resulting code. -1 is returned for unknown input coding value.
 */
static int g726_encode(int sl, struct g726_state *state_ptr)
{
	int		sezi, se, sez;		/* ACCUM */
	int		d;			/* SUBTA */
	int		sr;			/* ADDB */
	int		y;			/* MIX */
	int		dqsez;			/* ADDC */
	int		dq, i;

#ifdef NOT_BLI
	sl <<= 10;			/* 26-bit dynamic range */

	sezi = predictor_zero(state_ptr);
	sez = sezi;
	se = sezi + predictor_pole(state_ptr);	/* estimated signal */
#else
	sl >>= 2;			/* 14-bit dynamic range */

	sezi = predictor_zero(state_ptr);
	sez = sezi >> 1;
	se = (sezi + predictor_pole(state_ptr)) >> 1;	/* estimated signal */
#endif

	d = sl - se;				/* estimation difference */

	/* quantize the prediction difference */
	y = step_size(state_ptr);		/* quantizer step size */
#ifdef NOT_BLI
	d /= 0x1000;
#endif
	i = quantize(d, y, qtab_721, 7);	/* i = G726 code */

	dq = reconstruct(i & 8, _dqlntab[i], y);	/* quantized est diff */

#ifdef NOT_BLI
	sr = se + dq;				/* reconst. signal */
	dqsez = dq + sez;			/* pole prediction diff. */
#else
	sr = (dq < 0) ? se - (dq & 0x3FFF) : se + dq;	/* reconst. signal */
	dqsez = sr - se + sez;			/* pole prediction diff. */
#endif

	update(4, y, _witab[i] << 5, _fitab[i], dq, sr, dqsez, state_ptr);

	return (i);
}

/*
 * Private workspace for translating signed linear signals to G726.
 */

struct g726_encoder_pvt
{
  struct ast_frame f;
  char offset[AST_FRIENDLY_OFFSET];   /* Space to build offset */
  unsigned char outbuf[BUFFER_SIZE];  /* Encoded G726, two nibbles to a word */
  unsigned char next_flag;
  struct g726_state g726;
  int tail;
};

/*
 * Private workspace for translating G726 signals to signed linear.
 */

struct g726_decoder_pvt
{
  struct ast_frame f;
  char offset[AST_FRIENDLY_OFFSET];	/* Space to build offset */
  short outbuf[BUFFER_SIZE];	/* Decoded signed linear values */
  struct g726_state g726;
  int tail;
};

/*
 * G726ToLin_New
 *  Create a new instance of g726_decoder_pvt.
 *
 * Results:
 *  Returns a pointer to the new instance.
 *
 * Side effects:
 *  None.
 */

static struct ast_translator_pvt *
g726tolin_new (void)
{
  struct g726_decoder_pvt *tmp;
  tmp = malloc (sizeof (struct g726_decoder_pvt));
  if (tmp)
    {
	  memset(tmp, 0, sizeof(*tmp));
      tmp->tail = 0;
      localusecnt++;
	  g726_init_state(&tmp->g726);
      ast_update_use_count ();
    }
  return (struct ast_translator_pvt *) tmp;
}

/*
 * LinToG726_New
 *  Create a new instance of g726_encoder_pvt.
 *
 * Results:
 *  Returns a pointer to the new instance.
 *
 * Side effects:
 *  None.
 */

static struct ast_translator_pvt *
lintog726_new (void)
{
  struct g726_encoder_pvt *tmp;
  tmp = malloc (sizeof (struct g726_encoder_pvt));
  if (tmp)
    {
	  memset(tmp, 0, sizeof(*tmp));
      localusecnt++;
      tmp->tail = 0;
	  g726_init_state(&tmp->g726);
      ast_update_use_count ();
    }
  return (struct ast_translator_pvt *) tmp;
}

/*
 * G726ToLin_FrameIn
 *  Fill an input buffer with packed 4-bit G726 values if there is room
 *  left.
 *
 * Results:
 *  Foo
 *
 * Side effects:
 *  tmp->tail is the number of packed values in the buffer.
 */

static int
g726tolin_framein (struct ast_translator_pvt *pvt, struct ast_frame *f)
{
  struct g726_decoder_pvt *tmp = (struct g726_decoder_pvt *) pvt;
  unsigned char *b;
  int x;

  b = f->data;
  for (x=0;x<f->datalen;x++) {
  	if (tmp->tail >= BUFFER_SIZE) {
		ast_log(LOG_WARNING, "Out of buffer space!\n");
		return -1;
	}
	tmp->outbuf[tmp->tail++] = g726_decode((b[x] >> 4) & 0xf, &tmp->g726);
  	if (tmp->tail >= BUFFER_SIZE) {
		ast_log(LOG_WARNING, "Out of buffer space!\n");
		return -1;
	}
	tmp->outbuf[tmp->tail++] = g726_decode(b[x] & 0x0f, &tmp->g726);
  }

  return 0;
}

/*
 * G726ToLin_FrameOut
 *  Convert 4-bit G726 encoded signals to 16-bit signed linear.
 *
 * Results:
 *  Converted signals are placed in tmp->f.data, tmp->f.datalen
 *  and tmp->f.samples are calculated.
 *
 * Side effects:
 *  None.
 */

static struct ast_frame *
g726tolin_frameout (struct ast_translator_pvt *pvt)
{
  struct g726_decoder_pvt *tmp = (struct g726_decoder_pvt *) pvt;

  if (!tmp->tail)
    return NULL;

  tmp->f.frametype = AST_FRAME_VOICE;
  tmp->f.subclass = AST_FORMAT_SLINEAR;
  tmp->f.datalen = tmp->tail * 2;
  tmp->f.samples = tmp->tail;
  tmp->f.mallocd = 0;
  tmp->f.offset = AST_FRIENDLY_OFFSET;
  tmp->f.src = __PRETTY_FUNCTION__;
  tmp->f.data = tmp->outbuf;
  tmp->tail = 0;
  return &tmp->f;
}

/*
 * LinToG726_FrameIn
 *  Fill an input buffer with 16-bit signed linear PCM values.
 *
 * Results:
 *  None.
 *
 * Side effects:
 *  tmp->tail is number of signal values in the input buffer.
 */

static int
lintog726_framein (struct ast_translator_pvt *pvt, struct ast_frame *f)
{
  struct g726_encoder_pvt *tmp = (struct g726_encoder_pvt *) pvt;
  short *s = f->data;
  int samples = f->datalen / 2;
  int x;
  for (x=0;x<samples;x++) {
  	if (tmp->next_flag & 0x80) {
		if (tmp->tail >= BUFFER_SIZE) {
			ast_log(LOG_WARNING, "Out of buffer space\n");
			return -1;
		}
		tmp->outbuf[tmp->tail++] = ((tmp->next_flag & 0xf)<< 4) | g726_encode(s[x], &tmp->g726);
		tmp->next_flag = 0;
	} else {
		tmp->next_flag = 0x80 | g726_encode(s[x], &tmp->g726);
	}
  }
  return 0;
}

/*
 * LinToG726_FrameOut
 *  Convert a buffer of raw 16-bit signed linear PCM to a buffer
 *  of 4-bit G726 packed two to a byte (Big Endian).
 *
 * Results:
 *  Foo
 *
 * Side effects:
 *  Leftover inbuf data gets packed, tail gets updated.
 */

static struct ast_frame *
lintog726_frameout (struct ast_translator_pvt *pvt)
{
  struct g726_encoder_pvt *tmp = (struct g726_encoder_pvt *) pvt;
  
  if (!tmp->tail)
  	return NULL;
  tmp->f.frametype = AST_FRAME_VOICE;
  tmp->f.subclass = AST_FORMAT_G726;
  tmp->f.samples = tmp->tail * 2;
  tmp->f.mallocd = 0;
  tmp->f.offset = AST_FRIENDLY_OFFSET;
  tmp->f.src = __PRETTY_FUNCTION__;
  tmp->f.data = tmp->outbuf;
  tmp->f.datalen = tmp->tail;

  tmp->tail = 0;
  return &tmp->f;
}


/*
 * G726ToLin_Sample
 */

static struct ast_frame *
g726tolin_sample (void)
{
  static struct ast_frame f;
  f.frametype = AST_FRAME_VOICE;
  f.subclass = AST_FORMAT_G726;
  f.datalen = sizeof (g726_slin_ex);
  f.samples = sizeof(g726_slin_ex) * 2;
  f.mallocd = 0;
  f.offset = 0;
  f.src = __PRETTY_FUNCTION__;
  f.data = g726_slin_ex;
  return &f;
}

/*
 * LinToG726_Sample
 */

static struct ast_frame *
lintog726_sample (void)
{
  static struct ast_frame f;
  f.frametype = AST_FRAME_VOICE;
  f.subclass = AST_FORMAT_SLINEAR;
  f.datalen = sizeof (slin_g726_ex);
  /* Assume 8000 Hz */
  f.samples = sizeof (slin_g726_ex) / 2;
  f.mallocd = 0;
  f.offset = 0;
  f.src = __PRETTY_FUNCTION__;
  f.data = slin_g726_ex;
  return &f;
}

/*
 * G726_Destroy
 *  Destroys a private workspace.
 *
 * Results:
 *  It's gone!
 *
 * Side effects:
 *  None.
 */

static void
g726_destroy (struct ast_translator_pvt *pvt)
{
  free (pvt);
  localusecnt--;
  ast_update_use_count ();
}

/*
 * The complete translator for G726ToLin.
 */

static struct ast_translator g726tolin = {
  "g726tolin",
  AST_FORMAT_G726,
  AST_FORMAT_SLINEAR,
  g726tolin_new,
  g726tolin_framein,
  g726tolin_frameout,
  g726_destroy,
  /* NULL */
  g726tolin_sample
};

/*
 * The complete translator for LinToG726.
 */

static struct ast_translator lintog726 = {
  "lintog726",
  AST_FORMAT_SLINEAR,
  AST_FORMAT_G726,
  lintog726_new,
  lintog726_framein,
  lintog726_frameout,
  g726_destroy,
  /* NULL */
  lintog726_sample
};

int
unload_module (void)
{
  int res;
  ast_mutex_lock (&localuser_lock);
  res = ast_unregister_translator (&lintog726);
  if (!res)
    res = ast_unregister_translator (&g726tolin);
  if (localusecnt)
    res = -1;
  ast_mutex_unlock (&localuser_lock);
  return res;
}

int
load_module (void)
{
  int res;
  res = ast_register_translator (&g726tolin);
  if (!res)
    res = ast_register_translator (&lintog726);
  else
    ast_unregister_translator (&g726tolin);
  return res;
}

/*
 * Return a description of this module.
 */

char *
description (void)
{
  return tdesc;
}

int
usecount (void)
{
  int res;
  STANDARD_USECOUNT (res);
  return res;
}

char *
key ()
{
  return ASTERISK_GPL_KEY;
}
