/* codec_adpcm.c - translate between signed linear and Dialogic ADPCM
 * 
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Based on frompcm.c and topcm.c from the Emiliano MIPL browser/
 * interpreter.  See http://www.bsdtelephony.com.mx
 *
 * Copyright (c) 2001 - 2005 Digium, Inc.
 * All rights reserved.
 *
 * Karl Sackett <krs@linux-support.net>, 2001-03-21
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include "asterisk/lock.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/options.h"
#include "asterisk/translate.h"
#include "asterisk/channel.h"
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* define NOT_BLI to use a faster but not bit-level identical version */
/* #define NOT_BLI */

#define BUFFER_SIZE   8096	/* size for the translation buffers */

AST_MUTEX_DEFINE_STATIC(localuser_lock);
static int localusecnt = 0;

static char *tdesc = "Adaptive Differential PCM Coder/Decoder";

static int useplc = 0;

/* Sample frame data */

#include "slin_adpcm_ex.h"
#include "adpcm_slin_ex.h"

/*
 * Step size index shift table 
 */

static int indsft[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

/*
 * Step size table, where stpsz[i]=floor[16*(11/10)^i]
 */

static int stpsz[49] = {
  16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73,
  80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279,
  307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
  1060, 1166, 1282, 1411, 1552
};

/*
 * Decoder/Encoder state
 *   States for both encoder and decoder are synchronized
 */
struct adpcm_state {
	int ssindex;
	int signal;
	int zero_count;
	int next_flag;
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
decode(int encoded, struct adpcm_state* state)
{
	int diff;
	int step;
	int sign;

	step = stpsz[state->ssindex];

	sign = encoded & 0x08;
	encoded &= 0x07;
#ifdef NOT_BLI
	diff = (((encoded << 1) + 1) * step) >> 3;
#else /* BLI code */
	diff = step >> 3;
	if (encoded & 4) diff += step;
	if (encoded & 2) diff += step >> 1;
	if (encoded & 1) diff += step >> 2;
	if ((encoded >> 1) & step & 0x1)
		diff++;
#endif
	if (sign)
		diff = -diff;

	if (state->next_flag & 0x1)
		state->signal -= 8;
	else if (state->next_flag & 0x2)
		state->signal += 8;

	state->signal += diff;

	if (state->signal > 2047)
		state->signal = 2047;
	else if (state->signal < -2047)
		state->signal = -2047;

	state->next_flag = 0;

#ifdef AUTO_RETURN
	if (encoded)
		state->zero_count = 0;
	else if (++(state->zero_count) == 24)
	{
		state->zero_count = 0;
		if (state->signal > 0)
			state->next_flag = 0x1;
		else if (state->signal < 0)
			state->next_flag = 0x2;
	}
#endif

	state->ssindex += indsft[encoded];
	if (state->ssindex < 0)
		state->ssindex = 0;
	else if (state->ssindex > 48)
		state->ssindex = 48;

	return state->signal << 4;
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

static inline int
adpcm(short csig, struct adpcm_state* state)
{
	int diff;
	int step;
	int encoded;

	/* 
	* Clip csig if too large or too small
	*/
	csig >>= 4;

	step = stpsz[state->ssindex];
	diff = csig - state->signal;

#ifdef NOT_BLI
	if (diff < 0)
	{
		encoded = (-diff << 2) / step;
		if (encoded > 7)
			encoded = 7;
		encoded |= 0x08;
	}
	else
	{
		encoded = (diff << 2) / step;
		if (encoded > 7)
			encoded = 7;
	}
#else /* BLI code */
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
#endif /* NOT_BLI */

	/* feedback to state */
	decode(encoded, state);
	
	return encoded;
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
  struct adpcm_state state;
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
  struct adpcm_state state;
  int tail;
  plc_state_t plc;
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
adpcmtolin_new (void)
{
  struct adpcm_decoder_pvt *tmp;
  tmp = malloc (sizeof (struct adpcm_decoder_pvt));
  if (tmp)
    {
	  memset(tmp, 0, sizeof(*tmp));
      tmp->tail = 0;
      plc_init(&tmp->plc);
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
lintoadpcm_new (void)
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
 *  Take an input buffer with packed 4-bit ADPCM values and put decoded PCM in outbuf, 
 *  if there is room left.
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
  unsigned char *b;

  if(f->datalen == 0) { /* perform PLC with nominal framesize of 20ms/160 samples */
        if((tmp->tail + 160) > sizeof(tmp->outbuf) / 2) {
            ast_log(LOG_WARNING, "Out of buffer space\n");
            return -1;
        }
        if(useplc) {
	  plc_fillin(&tmp->plc, tmp->outbuf+tmp->tail, 160);
	  tmp->tail += 160;
	}
        return 0;
  }

  if (f->datalen * 4 + tmp->tail * 2 > sizeof(tmp->outbuf)) {
  	ast_log(LOG_WARNING, "Out of buffer space\n");
	return -1;
  }

  b = f->data;

  for (x=0;x<f->datalen;x++) {
	tmp->outbuf[tmp->tail++] = decode((b[x] >> 4) & 0xf, &tmp->state);
	tmp->outbuf[tmp->tail++] = decode(b[x] & 0x0f, &tmp->state);
  }

  if(useplc) plc_rx(&tmp->plc, tmp->outbuf+tmp->tail-f->datalen*2, f->datalen*2);

  return 0;
}

/*
 * AdpcmToLin_FrameOut
 *  Convert 4-bit ADPCM encoded signals to 16-bit signed linear.
 *
 * Results:
 *  Converted signals are placed in tmp->f.data, tmp->f.datalen
 *  and tmp->f.samples are calculated.
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
  tmp->f.samples = tmp->tail;
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
  int i_max, i;
  
  if (tmp->tail < 2) return NULL;


  i_max = tmp->tail & ~1; /* atomic size is 2 samples */

  /* What is this, state debugging? should be #ifdef'd then
  tmp->outbuf[0] = tmp->ssindex & 0xff;
  tmp->outbuf[1] = (tmp->signal >> 8) & 0xff;
  tmp->outbuf[2] = (tmp->signal & 0xff);
  tmp->outbuf[3] = tmp->zero_count;
  tmp->outbuf[4] = tmp->next_flag;
  */
  for (i = 0; i < i_max; i+=2)
  {
    tmp->outbuf[i/2] =
      (adpcm(tmp->inbuf[i  ], &tmp->state) << 4) |
	  (adpcm(tmp->inbuf[i+1], &tmp->state)     );
  };


  tmp->f.frametype = AST_FRAME_VOICE;
  tmp->f.subclass = AST_FORMAT_ADPCM;
  tmp->f.samples = i_max;
  tmp->f.mallocd = 0;
  tmp->f.offset = AST_FRIENDLY_OFFSET;
  tmp->f.src = __PRETTY_FUNCTION__;
  tmp->f.data = tmp->outbuf;
  tmp->f.datalen = i_max / 2;

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
adpcmtolin_sample (void)
{
  static struct ast_frame f;
  f.frametype = AST_FRAME_VOICE;
  f.subclass = AST_FORMAT_ADPCM;
  f.datalen = sizeof (adpcm_slin_ex);
  f.samples = sizeof(adpcm_slin_ex) * 2;
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
lintoadpcm_sample (void)
{
  static struct ast_frame f;
  f.frametype = AST_FRAME_VOICE;
  f.subclass = AST_FORMAT_SLINEAR;
  f.datalen = sizeof (slin_adpcm_ex);
  /* Assume 8000 Hz */
  f.samples = sizeof (slin_adpcm_ex) / 2;
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

static void 
parse_config(void)
{
  struct ast_config *cfg;
  struct ast_variable *var;
  if ((cfg = ast_config_load("codecs.conf"))) {
    if ((var = ast_variable_browse(cfg, "plc"))) {
      while (var) {
       if (!strcasecmp(var->name, "genericplc")) {
         useplc = ast_true(var->value) ? 1 : 0;
         if (option_verbose > 2)
           ast_verbose(VERBOSE_PREFIX_3 "codec_adpcm: %susing generic PLC\n", useplc ? "" : "not ");
       }
       var = var->next;
      }
    }
    ast_config_destroy(cfg);
  }
}

int
reload(void)
{
  parse_config();
  return 0;
}

int
unload_module (void)
{
  int res;
  ast_mutex_lock (&localuser_lock);
  res = ast_unregister_translator (&lintoadpcm);
  if (!res)
    res = ast_unregister_translator (&adpcmtolin);
  if (localusecnt)
    res = -1;
  ast_mutex_unlock (&localuser_lock);
  return res;
}

int
load_module (void)
{
  int res;
  parse_config();
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
