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

/****  isbt.c  ***************************************************

MPEG audio decoder, dct and window
portable C       integer version of csbt.c

mods 11/15/95 for Layer I

mods 1/7/97 warnings

******************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <float.h>
#include <math.h>
#include "itype.h"


#ifdef _MSC_VER
#pragma warning(disable: 4244)
#pragma warning(disable: 4056)
#endif



/* asm is quick only, c code does not need separate window for right */
/* full is opposite of quick */
#ifdef  FULL_INTEGER
#define i_window_dual_right   i_window_dual
#define i_window16_dual_right i_window16_dual
#define i_window8_dual_right  i_window8_dual
#endif


void i_dct32(SAMPLEINT * sample, WININT * vbuf);
void i_dct32_dual(SAMPLEINT * sample, WININT * vbuf);
void i_dct32_dual_mono(SAMPLEINT * sample, WININT * vbuf);

void i_dct16(SAMPLEINT * sample, WININT * vbuf);
void i_dct16_dual(SAMPLEINT * sample, WININT * vbuf);
void i_dct16_dual_mono(SAMPLEINT * sample, WININT * vbuf);

void i_dct8(SAMPLEINT * sample, WININT * vbuf);
void i_dct8_dual(SAMPLEINT * sample, WININT * vbuf);
void i_dct8_dual_mono(SAMPLEINT * sample, WININT * vbuf);


void i_window(WININT * vbuf, int vb_ptr, short *pcm);
void i_window_dual(WININT * vbuf, int vb_ptr, short *pcm);
void i_window_dual_right(WININT * vbuf, int vb_ptr, short *pcm);

void i_window16(WININT * vbuf, int vb_ptr, short *pcm);
void i_window16_dual(WININT * vbuf, int vb_ptr, short *pcm);
void i_window16_dual_right(WININT * vbuf, int vb_ptr, short *pcm);

void i_window8(WININT * vbuf, int vb_ptr, short *pcm);
void i_window8_dual(WININT * vbuf, int vb_ptr, short *pcm);
void i_window8_dual_right(WININT * vbuf, int vb_ptr, short *pcm);

/*--------------------------------------------------------------------*/
/*--  floating point window coefs  ---*/
/*--  for integer-quick window, table used to generate integer coefs --*/
static float wincoef[264] =
{
#include "tableawd.h"
};

/* circular window buffers */
/* extern windows because of asm */
static signed int vb_ptr;

// static WININT vbuf[512];
//static WININT vbuf2[512];
extern WININT vbuf[512];
extern WININT vbuf2[512];

DCTCOEF *i_dct_coef_addr();

/*======================================================================*/
static void gencoef()		/* gen coef for N=32 */
{
   int p, n, i, k;
   double t, pi;
   DCTCOEF *coef32;

   coef32 = i_dct_coef_addr();


   pi = 4.0 * atan(1.0);
   n = 16;
   k = 0;
   for (i = 0; i < 5; i++, n = n / 2)
   {
      for (p = 0; p < n; p++, k++)
      {
	 t = (pi / (4 * n)) * (2 * p + 1);
	 coef32[k] = (1 << DCTBITS) * (0.50 / cos(t)) + 0.5;
      }
   }
}
/*------------------------------------------------------------*/
WINCOEF *i_wincoef_addr();
static void genwincoef_q()	/* gen int window coefs from floating table */
{
   int i, j, k, m;
   float x;
   WINCOEF *iwincoef;

   iwincoef = i_wincoef_addr();


/*--- coefs generated inline for quick window ---*/
/*-- quick uses only 116 coefs --*/

   k = 0;
   m = 0;
   for (i = 0; i < 16; i++)
   {
      k += 5;
      for (j = 0; j < 7; j++)
      {
	 x = (1 << WINBITS) * wincoef[k++];
	 if (x > 0.0)
	    x += 0.5;
	 else
	    x -= 0.5;
	 iwincoef[m++] = x;
      }
      k += 4;
   }
   k++;
   for (j = 0; j < 4; j++)
   {
      x = (1 << WINBITS) * wincoef[k++];
      if (x > 0.0)
	 x += 0.5;
      else
	 x -= 0.5;
      iwincoef[m++] = x;
   }
}
/*------------------------------------------------------------*/
static void genwincoef()	/* gen int window coefs from floating table */
{
   int i;
   float x;
   WINCOEF *iwincoef;
   WINCOEF *i_wincoef_addr();

   iwincoef = i_wincoef_addr();

   for (i = 0; i < 264; i++)
   {
      x = (1 << WINBITS) * wincoef[i];
      if (x > 0.0)
	 x += 0.5;
      else
	 x -= 0.5;
      iwincoef[i] = x;
   }
}
/*------------------------------------------------------------*/
void i_sbt_init()
{
   int i;
   static int first_pass = 1;

#ifdef FULL_INTEGER
   static int full_integer = 1;

#else
   static int full_integer = 0;

#endif

   if (first_pass)
   {
      gencoef();
      if (full_integer)
	 genwincoef();
      else
	 genwincoef_q();
      first_pass = 0;
   }

/* clear window vbuf */
   for (i = 0; i < 512; i++)
      vbuf[i] = vbuf2[i] = 0;
   vb_ptr = 0;

}
/*==============================================================*/
/*==============================================================*/
/*==============================================================*/
void i_sbt_mono(SAMPLEINT * sample, short *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      i_dct32(sample, vbuf + vb_ptr);
      i_window(vbuf, vb_ptr, pcm);
      sample += 64;
      vb_ptr = (vb_ptr - 32) & 511;
      pcm += 32;
   }

}
/*------------------------------------------------------------*/
void i_sbt_dual(SAMPLEINT * sample, short *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      i_dct32_dual(sample, vbuf + vb_ptr);
      i_dct32_dual(sample + 1, vbuf2 + vb_ptr);
      i_window_dual(vbuf, vb_ptr, pcm);
      i_window_dual_right(vbuf2, vb_ptr, pcm + 1);
      sample += 64;
      vb_ptr = (vb_ptr - 32) & 511;
      pcm += 64;
   }
}
/*------------------------------------------------------------*/
/* convert dual to mono */
void i_sbt_dual_mono(SAMPLEINT * sample, short *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      i_dct32_dual_mono(sample, vbuf + vb_ptr);
      i_window(vbuf, vb_ptr, pcm);
      sample += 64;
      vb_ptr = (vb_ptr - 32) & 511;
      pcm += 32;
   }
}
/*------------------------------------------------------------*/
/* convert dual to left */
void i_sbt_dual_left(SAMPLEINT * sample, short *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      i_dct32_dual(sample, vbuf + vb_ptr);
      i_window(vbuf, vb_ptr, pcm);
      sample += 64;
      vb_ptr = (vb_ptr - 32) & 511;
      pcm += 32;
   }
}
/*------------------------------------------------------------*/
/* convert dual to right */
void i_sbt_dual_right(SAMPLEINT * sample, short *pcm, int n)
{
   int i;

   sample++;			/* point to right chan */
   for (i = 0; i < n; i++)
   {
      i_dct32_dual(sample, vbuf + vb_ptr);
      i_window(vbuf, vb_ptr, pcm);
      sample += 64;
      vb_ptr = (vb_ptr - 32) & 511;
      pcm += 32;
   }
}
/*------------------------------------------------------------*/
/*---------------- 16 pt sbt's  -------------------------------*/
/*------------------------------------------------------------*/
void i_sbt16_mono(SAMPLEINT * sample, short *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      i_dct16(sample, vbuf + vb_ptr);
      i_window16(vbuf, vb_ptr, pcm);
      sample += 64;
      vb_ptr = (vb_ptr - 16) & 255;
      pcm += 16;
   }

}
/*------------------------------------------------------------*/
void i_sbt16_dual(SAMPLEINT * sample, short *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      i_dct16_dual(sample, vbuf + vb_ptr);
      i_dct16_dual(sample + 1, vbuf2 + vb_ptr);
      i_window16_dual(vbuf, vb_ptr, pcm);
      i_window16_dual_right(vbuf2, vb_ptr, pcm + 1);
      sample += 64;
      vb_ptr = (vb_ptr - 16) & 255;
      pcm += 32;
   }

}
/*------------------------------------------------------------*/
void i_sbt16_dual_mono(SAMPLEINT * sample, short *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      i_dct16_dual_mono(sample, vbuf + vb_ptr);
      i_window16(vbuf, vb_ptr, pcm);
      sample += 64;
      vb_ptr = (vb_ptr - 16) & 255;
      pcm += 16;
   }
}
/*------------------------------------------------------------*/
void i_sbt16_dual_left(SAMPLEINT * sample, short *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      i_dct16_dual(sample, vbuf + vb_ptr);
      i_window16(vbuf, vb_ptr, pcm);
      sample += 64;
      vb_ptr = (vb_ptr - 16) & 255;
      pcm += 16;
   }
}
/*------------------------------------------------------------*/
void i_sbt16_dual_right(SAMPLEINT * sample, short *pcm, int n)
{
   int i;

   sample++;
   for (i = 0; i < n; i++)
   {
      i_dct16_dual(sample, vbuf + vb_ptr);
      i_window16(vbuf, vb_ptr, pcm);
      sample += 64;
      vb_ptr = (vb_ptr - 16) & 255;
      pcm += 16;
   }
}
/*------------------------------------------------------------*/
/*---------------- 8 pt sbt's  -------------------------------*/
/*------------------------------------------------------------*/
void i_sbt8_mono(SAMPLEINT * sample, short *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      i_dct8(sample, vbuf + vb_ptr);
      i_window8(vbuf, vb_ptr, pcm);
      sample += 64;
      vb_ptr = (vb_ptr - 8) & 127;
      pcm += 8;
   }

}
/*------------------------------------------------------------*/
void i_sbt8_dual(SAMPLEINT * sample, short *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      i_dct8_dual(sample, vbuf + vb_ptr);
      i_dct8_dual(sample + 1, vbuf2 + vb_ptr);
      i_window8_dual(vbuf, vb_ptr, pcm);
      i_window8_dual_right(vbuf2, vb_ptr, pcm + 1);
      sample += 64;
      vb_ptr = (vb_ptr - 8) & 127;
      pcm += 16;
   }
}
/*------------------------------------------------------------*/
void i_sbt8_dual_mono(SAMPLEINT * sample, short *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      i_dct8_dual_mono(sample, vbuf + vb_ptr);
      i_window8(vbuf, vb_ptr, pcm);
      sample += 64;
      vb_ptr = (vb_ptr - 8) & 127;
      pcm += 8;
   }
}
/*------------------------------------------------------------*/
void i_sbt8_dual_left(SAMPLEINT * sample, short *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      i_dct8_dual(sample, vbuf + vb_ptr);
      i_window8(vbuf, vb_ptr, pcm);
      sample += 64;
      vb_ptr = (vb_ptr - 8) & 127;
      pcm += 8;
   }
}
/*------------------------------------------------------------*/
void i_sbt8_dual_right(SAMPLEINT * sample, short *pcm, int n)
{
   int i;

   sample++;
   for (i = 0; i < n; i++)
   {
      i_dct8_dual(sample, vbuf + vb_ptr);
      i_window8(vbuf, vb_ptr, pcm);
      sample += 64;
      vb_ptr = (vb_ptr - 8) & 127;
      pcm += 8;
   }
}
/*------------------------------------------------------------*/
/*--- 8 bit output ----------------*/
#include "isbtb.c"
/*----------------------------------*/

#ifdef _MSC_VER
#pragma warning(default: 4244)
#pragma warning(default: 4056)
#endif
