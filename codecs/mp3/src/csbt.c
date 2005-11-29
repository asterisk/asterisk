/*____________________________________________________________________________
	
	FreeAmp - The Free MP3 Player

        MP3 Decoder originally Copyright (C) 1995-1997 Xing Technology
        Corp.  http://www.xingtech.com

	Portions Copyright (C) 1998 EMusic.com

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

/****  csbt.c  ***************************************************

MPEG audio decoder, dct and window
portable C

1/7/96 mod for Layer III
  
******************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <float.h>
#include <math.h>
#include "L3.h"
#include "mhead.h"

void fdct32(MPEG *m, float *, float *);
void fdct32_dual(MPEG *m, float *, float *);
void fdct32_dual_mono(MPEG *m, float *, float *);
void fdct16(MPEG *m, float *, float *);
void fdct16_dual(MPEG *m, float *, float *);
void fdct16_dual_mono(MPEG *m, float *, float *);
void fdct8(MPEG *m, float *, float *);
void fdct8_dual(MPEG *m, float *, float *);
void fdct8_dual_mono(MPEG *m, float *, float *);

void window(float *, int , short *pcm);
void window_dual(MPEG *m, float *, int , short *pcm);
void window16(MPEG *m, float *, int , short *pcm);
void window16_dual(MPEG *m, float *, int , short *pcm);
void window8(MPEG *m, float *, int , short *pcm);
void window8_dual(MPEG *m, float *, int , short *pcm);
float *dct_coef_addr(MPEG *m);

/*-------------------------------------------------------------------------*/
/* circular window buffers */

/*======================================================================*/
static void gencoef(MPEG *m)		/* gen coef for N=32 (31 coefs) */
{
   int p, n, i, k;
   double t, pi;
   float *coef32;

   coef32 = dct_coef_addr(m);

   pi = 4.0 * atan(1.0);
   n = 16;
   k = 0;
   for (i = 0; i < 5; i++, n = n / 2)
   {

      for (p = 0; p < n; p++, k++)
      {
	 t = (pi / (4 * n)) * (2 * p + 1);
	 coef32[k] = (float) (0.50 / cos(t));
      }
   }
}
/*------------------------------------------------------------*/
void sbt_init(MPEG *m)
{
   int i;

   if (m->csbt.first_pass)
   {
      gencoef(m);
      m->csbt.first_pass = 0;
   }

/* clear window m->csbt.vbuf */
   for (i = 0; i < 512; i++)
   {
      m->csbt.vbuf[i] = 0.0F;
      m->csbt.vbuf2[i] = 0.0F;
   }
   m->csbt.vb2_ptr = m->csbt.vb_ptr = 0;

}
/*============================================================*/
/*============================================================*/
/*============================================================*/
void sbt_mono(MPEG *m, float *sample, short *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      fdct32(m, sample, m->csbt.vbuf + m->csbt.vb_ptr);
      window(m->csbt.vbuf, m->csbt.vb_ptr, pcm);
      sample += 64;
      m->csbt.vb_ptr = (m->csbt.vb_ptr - 32) & 511;
      pcm += 32;
   }

}
/*------------------------------------------------------------*/
void sbt_dual(MPEG *m, float *sample, short *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      fdct32_dual(m,sample, m->csbt.vbuf + m->csbt.vb_ptr);
      fdct32_dual(m,sample + 1, m->csbt.vbuf2 + m->csbt.vb_ptr);
      window_dual(m,m->csbt.vbuf, m->csbt.vb_ptr, pcm);
      window_dual(m,m->csbt.vbuf2, m->csbt.vb_ptr, pcm + 1);
      sample += 64;
      m->csbt.vb_ptr = (m->csbt.vb_ptr - 32) & 511;
      pcm += 64;
   }


}
/*------------------------------------------------------------*/
/* convert dual to mono */
void sbt_dual_mono(MPEG *m, float *sample, short *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      fdct32_dual_mono(m,sample, m->csbt.vbuf + m->csbt.vb_ptr);
      window(m->csbt.vbuf, m->csbt.vb_ptr, pcm);
      sample += 64;
      m->csbt.vb_ptr = (m->csbt.vb_ptr - 32) & 511;
      pcm += 32;
   }

}
/*------------------------------------------------------------*/
/* convert dual to left */
void sbt_dual_left(MPEG *m, float *sample, short *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      fdct32_dual(m,sample, m->csbt.vbuf + m->csbt.vb_ptr);
      window(m->csbt.vbuf, m->csbt.vb_ptr, pcm);
      sample += 64;
      m->csbt.vb_ptr = (m->csbt.vb_ptr - 32) & 511;
      pcm += 32;
   }
}
/*------------------------------------------------------------*/
/* convert dual to right */
void sbt_dual_right(MPEG *m, float *sample, short *pcm, int n)
{
   int i;

   sample++;			/* point to right chan */
   for (i = 0; i < n; i++)
   {
      fdct32_dual(m,sample, m->csbt.vbuf + m->csbt.vb_ptr);
      window(m->csbt.vbuf, m->csbt.vb_ptr, pcm);
      sample += 64;
      m->csbt.vb_ptr = (m->csbt.vb_ptr - 32) & 511;
      pcm += 32;
   }
}
/*------------------------------------------------------------*/
/*---------------- 16 pt sbt's  -------------------------------*/
/*------------------------------------------------------------*/
void sbt16_mono(MPEG *m, float *sample, short *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      fdct16(m,sample, m->csbt.vbuf + m->csbt.vb_ptr);
      window16(m,m->csbt.vbuf, m->csbt.vb_ptr, pcm);
      sample += 64;
      m->csbt.vb_ptr = (m->csbt.vb_ptr - 16) & 255;
      pcm += 16;
   }


}
/*------------------------------------------------------------*/
void sbt16_dual(MPEG *m, float *sample, short *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      fdct16_dual(m,sample, m->csbt.vbuf + m->csbt.vb_ptr);
      fdct16_dual(m,sample + 1, m->csbt.vbuf2 + m->csbt.vb_ptr);
      window16_dual(m,m->csbt.vbuf, m->csbt.vb_ptr, pcm);
      window16_dual(m,m->csbt.vbuf2, m->csbt.vb_ptr, pcm + 1);
      sample += 64;
      m->csbt.vb_ptr = (m->csbt.vb_ptr - 16) & 255;
      pcm += 32;
   }
}
/*------------------------------------------------------------*/
void sbt16_dual_mono(MPEG *m, float *sample, short *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      fdct16_dual_mono(m,sample, m->csbt.vbuf + m->csbt.vb_ptr);
      window16(m,m->csbt.vbuf, m->csbt.vb_ptr, pcm);
      sample += 64;
      m->csbt.vb_ptr = (m->csbt.vb_ptr - 16) & 255;
      pcm += 16;
   }
}
/*------------------------------------------------------------*/
void sbt16_dual_left(MPEG *m, float *sample, short *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      fdct16_dual(m,sample, m->csbt.vbuf + m->csbt.vb_ptr);
      window16(m,m->csbt.vbuf, m->csbt.vb_ptr, pcm);
      sample += 64;
      m->csbt.vb_ptr = (m->csbt.vb_ptr - 16) & 255;
      pcm += 16;
   }
}
/*------------------------------------------------------------*/
void sbt16_dual_right(MPEG *m, float *sample, short *pcm, int n)
{
   int i;

   sample++;
   for (i = 0; i < n; i++)
   {
      fdct16_dual(m,sample, m->csbt.vbuf + m->csbt.vb_ptr);
      window16(m,m->csbt.vbuf, m->csbt.vb_ptr, pcm);
      sample += 64;
      m->csbt.vb_ptr = (m->csbt.vb_ptr - 16) & 255;
      pcm += 16;
   }
}
/*------------------------------------------------------------*/
/*---------------- 8 pt sbt's  -------------------------------*/
/*------------------------------------------------------------*/
void sbt8_mono(MPEG *m, float *sample, short *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      fdct8(m,sample, m->csbt.vbuf + m->csbt.vb_ptr);
      window8(m,m->csbt.vbuf, m->csbt.vb_ptr, pcm);
      sample += 64;
      m->csbt.vb_ptr = (m->csbt.vb_ptr - 8) & 127;
      pcm += 8;
   }

}
/*------------------------------------------------------------*/
void sbt8_dual(MPEG *m, float *sample, short *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      fdct8_dual(m,sample, m->csbt.vbuf + m->csbt.vb_ptr);
      fdct8_dual(m,sample + 1, m->csbt.vbuf2 + m->csbt.vb_ptr);
      window8_dual(m,m->csbt.vbuf, m->csbt.vb_ptr, pcm);
      window8_dual(m,m->csbt.vbuf2, m->csbt.vb_ptr, pcm + 1);
      sample += 64;
      m->csbt.vb_ptr = (m->csbt.vb_ptr - 8) & 127;
      pcm += 16;
   }
}
/*------------------------------------------------------------*/
void sbt8_dual_mono(MPEG *m, float *sample, short *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      fdct8_dual_mono(m,sample, m->csbt.vbuf + m->csbt.vb_ptr);
      window8(m,m->csbt.vbuf, m->csbt.vb_ptr, pcm);
      sample += 64;
      m->csbt.vb_ptr = (m->csbt.vb_ptr - 8) & 127;
      pcm += 8;
   }
}
/*------------------------------------------------------------*/
void sbt8_dual_left(MPEG *m, float *sample, short *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      fdct8_dual(m,sample, m->csbt.vbuf + m->csbt.vb_ptr);
      window8(m,m->csbt.vbuf, m->csbt.vb_ptr, pcm);
      sample += 64;
      m->csbt.vb_ptr = (m->csbt.vb_ptr - 8) & 127;
      pcm += 8;
   }
}
/*------------------------------------------------------------*/
void sbt8_dual_right(MPEG *m, float *sample, short *pcm, int n)
{
   int i;

   sample++;
   for (i = 0; i < n; i++)
   {
      fdct8_dual(m,sample, m->csbt.vbuf + m->csbt.vb_ptr);
      window8(m,m->csbt.vbuf, m->csbt.vb_ptr, pcm);
      sample += 64;
      m->csbt.vb_ptr = (m->csbt.vb_ptr - 8) & 127;
      pcm += 8;
   }
}
/*------------------------------------------------------------*/
/*------------------------------------------------------------*/
#include "csbtb.c"		/* 8 bit output */
#include "csbtL3.c"		/* Layer III */
/*------------------------------------------------------------*/
