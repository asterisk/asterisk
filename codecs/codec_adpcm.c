/* codec_adpcm.c - translate between signed linear and Dialogic ADPCM
 * 
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Based on frompcm.c and topcm.c from the Emiliano MIPL browser/
 * interpreter.  See http://www.bsdtelephony.com.mx
 *
 * Copyright (c) 2001 Linux Support Services, Inc.  All rights reserved.
 *
 * Karl Sackett <krs@linux-support.net>, 2001-3-21
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/translate.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE   8096	/* size for the translation buffers */

static pthread_mutex_t localuser_lock = PTHREAD_MUTEX_INITIALIZER;
static int localusecnt = 0;

static char *tdesc = "Adaptive Differential PCM Coder/Decoder";

/* Sample frame data */

#include "slin_adpcm_ex.h"
#include "adpcm_slin_ex.h"

/*
 * Step size index shift table 
 */

static short indsft[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

/*
 * Step size table, where stpsz[i]=floor[16*(11/10)^i]
 */

static short stpsz[49] = {
  16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73,
  80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279,
  307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
  1060, 1166, 1282, 1411, 1552
};

/* 
 * Nibble to bit map
 */

static short nbl2bit[16][4] = {
  {1, 0, 0, 0}, {1, 0, 0, 1}, {1, 0, 1, 0}, {1, 0, 1, 1},
  {1, 1, 0, 0}, {1, 1, 0, 1}, {1, 1, 1, 0}, {1, 1, 1, 1},
  {-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1},
  {-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}
};

/*
 * Decode(encoded)
 *  Decodes the encoded nibble from the adpcm file.
 *
 * Results:
 *  Returns the encoded difference.
 *
 * Side effects:
 *  Sets the index to the step size table for the next encode.
 */

static inline short
decode (unsigned char encoded, short *ssindex)
{
  short diff, step;
  step = stpsz[*ssindex];
  diff = nbl2bit[encoded][0] * (step * nbl2bit[encoded][1] +
				(step >> 1) * nbl2bit[encoded][2] +
				(step >> 2) * nbl2bit[encoded][3] +
				(step >> 3));
  *ssindex = *ssindex + indsft[(encoded & 7)];
  if (*ssindex < 0)
    *ssindex = 0;
  else if (*ssindex > 48)
    *ssindex = 48;
  return (diff);
}

/*
 * Adpcm
 *  Takes a signed linear signal and encodes it as ADPCM
 *  For more information see http://support.dialogic.com/appnotes/adpcm.pdf
 *
 * Results:
 *  Foo.
 *
 * Side effects:
 *  signal gets updated with each pass.
 */

static inline unsigned char
adpcm (short csig, short *ssindex, short *signal)
{
  short diff, step;
  unsigned char encoded;
  step = stpsz[*ssindex];
  /* 
   * Clip csig if too large or too small
   */
   
  csig >>= 4;

  diff = csig - *signal;
  
  if (diff < 0)
    {
      encoded = 8;
      diff = -diff;
    }
  else
    encoded = 0;
  if (diff >= step)
    {
      encoded |= 4;
      diff -= step;
    }
  step >>= 1;
  if (diff >= step)
    {
      encoded |= 2;
      diff -= step;
    }
  step >>= 1;
  if (diff >= step)
    encoded |= 1;
    
  *signal += decode (encoded, ssindex);
  return (encoded);
}

/*
 * Private workspace for translating signed linear signals to ADPCM.
 */

struct adpcm_encoder_pvt
{
  struct ast_frame f;
  char offset[AST_FRIENDLY_OFFSET];   /* Space to build offset */
  short inbuf[BUFFER_SIZE];           /* Unencoded signed linear values */
  unsigned char outbuf[BUFFER_SIZE];  /* Encoded ADPCM, two nibbles to a word */
  short ssindex;
  short signal;
  int tail;
};

/*
 * Private workspace for translating ADPCM signals to signed linear.
 */

struct adpcm_decoder_pvt
{
  struct ast_frame f;
  char offset[AST_FRIENDLY_OFFSET];	/* Space to build offset */
  short outbuf[BUFFER_SIZE];	/* Decoded signed linear values */
  int tail;
};

/*
 * AdpcmToLin_New
 *  Create a new instance of adpcm_decoder_pvt.
 *
 * Results:
 *  Returns a pointer to the new instance.
 *
 * Side effects:
 *  None.
 */

static struct ast_translator_pvt *
adpcmtolin_new ()
{
  struct adpcm_decoder_pvt *tmp;
  tmp = malloc (sizeof (struct adpcm_decoder_pvt));
  if (tmp)
    {
	  memset(tmp, 0, sizeof(*tmp));
      tmp->tail = 0;
      localusecnt++;
      ast_update_use_count ();
    }
  return (struct ast_translator_pvt *) tmp;
}

/*
 * LinToAdpcm_New
 *  Create a new instance of adpcm_encoder_pvt.
 *
 * Results:
 *  Returns a pointer to the new instance.
 *
 * Side effects:
 *  None.
 */

static struct ast_translator_pvt *
lintoadpcm_new ()
{
  struct adpcm_encoder_pvt *tmp;
  tmp = malloc (sizeof (struct adpcm_encoder_pvt));
  if (tmp)
    {
	  memset(tmp, 0, sizeof(*tmp));
      localusecnt++;
      ast_update_use_count ();
      tmp->tail = 0;
    }
  return (struct ast_translator_pvt *) tmp;
}

/*
 * AdpcmToLin_FrameIn
 *  Fill an input buffer with packed 4-bit ADPCM values if there is room
 *  left.
 *
 * Results:
 *  Foo
 *
 * Side effects:
 *  tmp->tail is the number of packed values in the buffer.
 */

static int
adpcmtolin_framein (struct ast_translator_pvt *pvt, struct ast_frame *f)
{
  struct adpcm_decoder_pvt *tmp = (struct adpcm_decoder_pvt *) pvt;
  int x;
  short signal;
  short ssindex;
  unsigned char *b;

  if (f->datalen < 3) {
  	ast_log(LOG_WARNING, "Didn't have at least three bytes of input\n");
	return -1;
  }

  if ((f->datalen - 3) * 4 > sizeof(tmp->outbuf)) {
  	ast_log(LOG_WARNING, "Out of buffer space\n");
	return -1;
  }

  /* Reset ssindex and signal to frame's specified values */
  b = f->data;
  ssindex = b[0];
  if (ssindex < 0)
  	ssindex = 0;
  if (ssindex > 48)
    ssindex = 48;

  signal = (b[1] << 8) | b[2]; 
  
  for (x=3;x<f->datalen;x++) {
  	signal += decode(b[x] >> 4, &ssindex);
	if (signal > 2047)
		signal = 2047;
	if (signal < -2048)
		signal = -2048;
    tmp->outbuf[tmp->tail++] = signal << 4;
  	signal +=  decode(b[x] & 0x0f, &ssindex);
	if (signal > 2047)
		signal = 2047;
	if (signal < -2048)
		signal = -2048;
    tmp->outbuf[tmp->tail++] = signal << 4;
  }

  return 0;
}

/*
 * AdpcmToLin_FrameOut
 *  Convert 4-bit ADPCM encoded signals to 16-bit signed linear.
 *
 * Results:
 *  Converted signals are placed in tmp->f.data, tmp->f.datalen
 *  and tmp->f.timelen are calculated.
 *
 * Side effects:
 *  None.
 */

static struct ast_frame *
adpcmtolin_frameout (struct ast_translator_pvt *pvt)
{
  struct adpcm_decoder_pvt *tmp = (struct adpcm_decoder_pvt *) pvt;

  if (!tmp->tail)
    return NULL;

  tmp->f.frametype = AST_FRAME_VOICE;
  tmp->f.subclass = AST_FORMAT_SLINEAR;
  tmp->f.datalen = tmp->tail *2;
  tmp->f.timelen = tmp->tail / 8;
  tmp->f.mallocd = 0;
  tmp->f.offset = AST_FRIENDLY_OFFSET;
  tmp->f.src = __PRETTY_FUNCTION__;
  tmp->f.data = tmp->outbuf;
  tmp->tail = 0;
  return &tmp->f;
}

/*
 * LinToAdpcm_FrameIn
 *  Fill an input buffer with 16-bit signed linear PCM values.
 *
 * Results:
 *  None.
 *
 * Side effects:
 *  tmp->tail is number of signal values in the input buffer.
 */

static int
lintoadpcm_framein (struct ast_translator_pvt *pvt, struct ast_frame *f)
{
  struct adpcm_encoder_pvt *tmp = (struct adpcm_encoder_pvt *) pvt;

  if ((tmp->tail + f->datalen / 2) < (sizeof (tmp->inbuf) / 2))
    {
      memcpy (&tmp->inbuf[tmp->tail], f->data, f->datalen);
      tmp->tail += f->datalen / 2;
    }
  else
    {
      ast_log (LOG_WARNING, "Out of buffer space\n");
      return -1;
    }
  return 0;
}

/*
 * LinToAdpcm_FrameOut
 *  Convert a buffer of raw 16-bit signed linear PCM to a buffer
 *  of 4-bit ADPCM packed two to a byte (Big Endian).
 *
 * Results:
 *  Foo
 *
 * Side effects:
 *  Leftover inbuf data gets packed, tail gets updated.
 */

static struct ast_frame *
lintoadpcm_frameout (struct ast_translator_pvt *pvt)
{
  struct adpcm_encoder_pvt *tmp = (struct adpcm_encoder_pvt *) pvt;
  unsigned char adpcm0, adpcm1;
  int i_max, i;
  
  if (tmp->tail < 2) return NULL;


  i_max = (tmp->tail / 2) * 2;

  tmp->outbuf[0] = tmp->ssindex & 0xff;
  tmp->outbuf[1] = (tmp->signal >> 8) & 0xff;
  tmp->outbuf[2] = (tmp->signal & 0xff);

  for (i = 0; i < i_max; i+=2)
  {
    adpcm0 = adpcm(tmp->inbuf[i], &tmp->ssindex, &tmp->signal);
    adpcm1 = adpcm(tmp->inbuf[i+1], &tmp->ssindex, &tmp->signal);
    tmp->outbuf[i/2 + 3] = (adpcm0 << 4) | adpcm1;
  };


  tmp->f.frametype = AST_FRAME_VOICE;
  tmp->f.subclass = AST_FORMAT_ADPCM;
  tmp->f.timelen = i_max / 8;
  tmp->f.mallocd = 0;
  tmp->f.offset = AST_FRIENDLY_OFFSET;
  tmp->f.src = __PRETTY_FUNCTION__;
  tmp->f.data = tmp->outbuf;
  tmp->f.datalen = i_max / 2 + 3;

  /*
   * If there is a signal left over (there should be no more than
   * one) move it to the beginning of the input buffer.
   */

  if (tmp->tail == i_max)
    tmp->tail = 0;
  else
    {
      tmp->inbuf[0] = tmp->inbuf[tmp->tail];
      tmp->tail = 1;
    }
  return &tmp->f;
}


/*
 * AdpcmToLin_Sample
 */

static struct ast_frame *
adpcmtolin_sample ()
{
  static struct ast_frame f;
  f.frametype = AST_FRAME_VOICE;
  f.subclass = AST_FORMAT_ADPCM;
  f.datalen = sizeof (adpcm_slin_ex);
  f.timelen = sizeof(adpcm_slin_ex) / 4;
  f.mallocd = 0;
  f.offset = 0;
  f.src = __PRETTY_FUNCTION__;
  f.data = adpcm_slin_ex;
  return &f;
}

/*
 * LinToAdpcm_Sample
 */

static struct ast_frame *
lintoadpcm_sample ()
{
  static struct ast_frame f;
  f.frametype = AST_FRAME_VOICE;
  f.subclass = AST_FORMAT_SLINEAR;
  f.datalen = sizeof (slin_adpcm_ex);
  /* Assume 8000 Hz */
  f.timelen = sizeof (slin_adpcm_ex) / 16;
  f.mallocd = 0;
  f.offset = 0;
  f.src = __PRETTY_FUNCTION__;
  f.data = slin_adpcm_ex;
  return &f;
}

/*
 * Adpcm_Destroy
 *  Destroys a private workspace.
 *
 * Results:
 *  It's gone!
 *
 * Side effects:
 *  None.
 */

static void
adpcm_destroy (struct ast_translator_pvt *pvt)
{
  free (pvt);
  localusecnt--;
  ast_update_use_count ();
}

/*
 * The complete translator for ADPCMToLin.
 */

static struct ast_translator adpcmtolin = {
  "adpcmtolin",
  AST_FORMAT_ADPCM,
  AST_FORMAT_SLINEAR,
  adpcmtolin_new,
  adpcmtolin_framein,
  adpcmtolin_frameout,
  adpcm_destroy,
  /* NULL */
  adpcmtolin_sample
};

/*
 * The complete translator for LinToADPCM.
 */

static struct ast_translator lintoadpcm = {
  "lintoadpcm",
  AST_FORMAT_SLINEAR,
  AST_FORMAT_ADPCM,
  lintoadpcm_new,
  lintoadpcm_framein,
  lintoadpcm_frameout,
  adpcm_destroy,
  /* NULL */
  lintoadpcm_sample
};

int
unload_module (void)
{
  int res;
  pthread_mutex_lock (&localuser_lock);
  res = ast_unregister_translator (&lintoadpcm);
  if (!res)
    res = ast_unregister_translator (&adpcmtolin);
  if (localusecnt)
    res = -1;
  pthread_mutex_unlock (&localuser_lock);
  return res;
}

int
load_module (void)
{
  int res;
  res = ast_register_translator (&adpcmtolin);
  if (!res)
    res = ast_register_translator (&lintoadpcm);
  else
    ast_unregister_translator (&adpcmtolin);
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
