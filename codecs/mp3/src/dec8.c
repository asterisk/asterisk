/*____________________________________________________________________________
	
	FreeAmp - The Free MP3 Player

        MP3 Decoder originally Copyright (C) 1995-1997 Xing Technology
        Corp.  http://www.xingtech.com

	Portions Copyright (C) 1998-1999 EMusic.com

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
	
	$Id$
____________________________________________________________________________*/

/****  dec8.c  ***************************************************


ANSI C
MPEG audio decoder Layer II only mpeg1 and mpeg2
output sample type and sample rate conversion
  decode mpeg to 8000Ks mono
  output 16 bit linear, 8 bit linear, or u-law


mod 6/29/95  bugfix in u-law table

mod 11/15/95 for Layer I

mod 1/7/97 minor mods for warnings  

******************************************************************/
/*****************************************************************

       MPEG audio software decoder portable ANSI c.
       Decodes all Layer II to 8000Ks mono pcm.
       Output selectable: 16 bit linear, 8 bit linear, u-law.

-------------------------------------
int audio_decode8_init(MPEG_HEAD *h, int framebytes_arg,
         int reduction_code, int transform_code, int convert_code,
         int freq_limit)

initilize decoder:
       return 0 = fail, not 0 = success

MPEG_HEAD *h    input, mpeg header info (returned by call to head_info)
framebytes      input, mpeg frame size (returned by call to head_info)
reduction_code  input, ignored
transform_code  input, ignored
convert_code    input, set convert_code = 4*bit_code + chan_code
                     bit_code:   1 = 16 bit linear pcm
                                 2 =  8 bit (unsigned) linear pcm
                                 3 = u-law (8 bits unsigned)
                     chan_code:  0 = convert two chan to mono
                                 1 = convert two chan to mono
                                 2 = convert two chan to left chan
                                 3 = convert two chan to right chan
freq_limit      input, ignored


---------------------------------
void audio_decode8_info( DEC_INFO *info)

information return:
          Call after audio_decode8_init.  See mhead.h for
          information returned in DEC_INFO structure.


---------------------------------
IN_OUT audio_decode8(unsigned char *bs, void *pcmbuf)

decode one mpeg audio frame:
bs        input, mpeg bitstream, must start with
          sync word.  Caution: may read up to 3 bytes
          beyond end of frame.
pcmbuf    output, pcm samples.

IN_OUT structure returns:
          Number bytes conceptually removed from mpeg bitstream.
          Returns 0 if sync loss.
          Number bytes of pcm output.  This may vary from frame
          to frame.

*****************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <float.h>
#include <string.h>
#include <math.h>
#include "L3.h"
#include "mhead.h"		/* mpeg header structure */





/*------------------------------------------*/
static int output_code;
static int convert(void *mv, unsigned char *pcm);
static int convert_8bit(void *mv, unsigned char *pcm);
static int convert_u(void *mv, unsigned char *pcm);
static CVT_FUNCTION_8 cvt_table[3] =
{
   convert,
   convert_8bit,
   convert_u,
};

void mpeg8_init(MPEG8 *m)
{
	memset(&m->dec, 0, sizeof(m->dec));
	m->dec.ncnt = 8 * 288;
	m->dec.ncnt1 = 8 * 287;
	m->dec.nlast = 287;
	m->dec.ndeci = 11;
	m->dec.kdeci = 8 * 288;
	m->dec.first_pass = 1;
}

/*====================================================================*/
IN_OUT audio_decode8(MPEG8 *m, unsigned char *bs, signed short *pcmbuf)
{
   IN_OUT x;

   x = audio_decode(&m->cupper, bs, m->dec.pcm);
   if (x.in_bytes <= 0)
      return x;
   x.out_bytes = m->dec.convert_routine(m, (void *) pcmbuf);

   return x;
}
/*--------------8Ks 16 bit pcm --------------------------------*/
static int convert(void *mv, unsigned char y0[])
{
	MPEG8 *m = mv;
   int i, k;
   long alpha;
   short *y;

   y = (short *) y0;
   k = 0;
   if (m->dec.kdeci < m->dec.ncnt)
   {
      alpha = m->dec.kdeci & 7;
      y[k++] = (short) (m->dec.xsave + ((alpha * (m->dec.pcm[0] - m->dec.xsave)) >> 3));
      m->dec.kdeci += m->dec.ndeci;
   }
   m->dec.kdeci -= m->dec.ncnt;
   for (; m->dec.kdeci < m->dec.ncnt1; m->dec.kdeci += m->dec.ndeci)
   {
      i = m->dec.kdeci >> 3;
      alpha = m->dec.kdeci & 7;
      y[k++] = (short) (m->dec.pcm[i] + ((alpha * (m->dec.pcm[i + 1] - m->dec.pcm[i])) >> 3));
   }
   m->dec.xsave = m->dec.pcm[m->dec.nlast];

/* printf("\n k out = %4d", k);   */

   return sizeof(short) * k;
}
/*----------------8Ks 8 bit unsigned pcm ---------------------------*/
static int convert_8bit(void *mv, unsigned char y[])
{
   MPEG8 *m = mv;
   int i, k;
   long alpha;

   k = 0;
   if (m->dec.kdeci < m->dec.ncnt)
   {
      alpha = m->dec.kdeci & 7;
      y[k++] = (unsigned char) (((m->dec.xsave + ((alpha * (m->dec.pcm[0] - m->dec.xsave)) >> 3)) >> 8) + 128);
      m->dec.kdeci += m->dec.ndeci;
   }
   m->dec.kdeci -= m->dec.ncnt;
   for (; m->dec.kdeci < m->dec.ncnt1; m->dec.kdeci += m->dec.ndeci)
   {
      i = m->dec.kdeci >> 3;
      alpha = m->dec.kdeci & 7;
      y[k++] = (unsigned char) (((m->dec.pcm[i] + ((alpha * (m->dec.pcm[i + 1] - m->dec.pcm[i])) >> 3)) >> 8) + 128);
   }
   m->dec.xsave = m->dec.pcm[m->dec.nlast];

/* printf("\n k out = %4d", k);   */

   return k;
}
/*--------------8Ks u-law --------------------------------*/
static int convert_u(void *mv, unsigned char y[])
{
   MPEG8 *m = mv;
   int i, k;
   long alpha;
   unsigned char *look;

   look = m->dec.look_u + 4096;

   k = 0;
   if (m->dec.kdeci < m->dec.ncnt)
   {
      alpha = m->dec.kdeci & 7;
      y[k++] = look[(m->dec.xsave + ((alpha * (m->dec.pcm[0] - m->dec.xsave)) >> 3)) >> 3];
      m->dec.kdeci += m->dec.ndeci;
   }
   m->dec.kdeci -= m->dec.ncnt;
   for (; m->dec.kdeci < m->dec.ncnt1; m->dec.kdeci += m->dec.ndeci)
   {
      i = m->dec.kdeci >> 3;
      alpha = m->dec.kdeci & 7;
      y[k++] = look[(m->dec.pcm[i] + ((alpha * (m->dec.pcm[i + 1] - m->dec.pcm[i])) >> 3)) >> 3];
   }
   m->dec.xsave = m->dec.pcm[m->dec.nlast];

/* printf("\n k out = %4d", k);   */

   return k;
}
/*--------------------------------------------------------------------*/
static int ucomp3(int x)	/* re analog devices CCITT G.711 */
{
   int s, p, y, t, u, u0, sign;

   sign = 0;
   if (x < 0)
   {
      x = -x;
      sign = 0x0080;
   }
   if (x > 8031)
      x = 8031;
   x += 33;
   t = x;
   for (s = 0; s < 15; s++)
   {
      if (t & 0x4000)
	 break;
      t <<= 1;
   }
   y = x << s;
   p = (y >> 10) & 0x0f;	/* position */
   s = 9 - s;			/* segment */
   u0 = (((s << 4) | p) & 0x7f) | sign;
   u = u0 ^ 0xff;

   return u;
}
/*------------------------------------------------------------------*/
static void table_init(MPEG8 *m)
{
   int i;

   for (i = -4096; i < 4096; i++)
      m->dec.look_u[4096 + i] = (unsigned char) (ucomp3(2 * i));

}
/*-------------------------------------------------------------------*/
int audio_decode8_init(MPEG8 *m, MPEG_HEAD * h, int framebytes_arg,
		   int reduction_code, int transform_code, int convert_code,
		       int freq_limit)
{
   int istat;
   int outvals;
   static int sr_table[2][4] =
   {{22, 24, 16, 0}, {44, 48, 32, 0}};

   if (m->dec.first_pass)
   {
      table_init(m);
      m->dec.first_pass = 0;
   }

   if ((h->sync & 1) == 0)
      return 0;			// fail mpeg 2.5

   output_code = convert_code >> 2;
   if (output_code < 1)
      output_code = 1;		/* 1= 16bit 2 = 8bit 3 = u */
   if (output_code > 3)
      output_code = 3;		/* 1= 16bit 2 = 8bit 3 = u */

   convert_code = convert_code & 3;
   if (convert_code <= 0)
      convert_code = 1;		/* always cvt to mono */

   reduction_code = 1;
   if (h->id)
      reduction_code = 2;

/* select convert routine */
   m->dec.convert_routine = cvt_table[output_code - 1];

/* init decimation/convert routine */
/*-- MPEG-2 layer III --*/
   if ((h->option == 1) && h->id == 0)
      outvals = 576 >> reduction_code;
   else if (h->option == 3)
      outvals = 384 >> reduction_code;
/*-- layer I --*/
   else
      outvals = 1152 >> reduction_code;
   m->dec.ncnt = 8 * outvals;
   m->dec.ncnt1 = 8 * (outvals - 1);
   m->dec.nlast = outvals - 1;
   m->dec.ndeci = sr_table[h->id][h->sr_index] >> reduction_code;
   m->dec.kdeci = 8 * outvals;
/* printf("\n outvals %d", outvals);  */

   freq_limit = 3200;
   istat = audio_decode_init(&m->cupper, h, framebytes_arg,
			     reduction_code, transform_code, convert_code,
			     freq_limit);


   return istat;
}
/*-----------------------------------------------------------------*/
void audio_decode8_info(MPEG8 *m, DEC_INFO * info)
{

   audio_decode_info(&m->cupper, info);
   info->samprate = 8000;
   if (output_code != 1)
      info->bits = 8;
   if (output_code == 3)
      info->type = 10;
}
