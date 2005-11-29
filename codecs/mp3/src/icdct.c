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

/****  icdct.c  ***************************************************


MPEG audio decoder, dct
portable C    integer dct

mod 1/8/97 warnings

******************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <float.h>
#include <math.h>
#include "itype.h"

/*-------------------------------------------------------------------*/
static DCTCOEF coef32[32];	/* 32 pt dct coefs */




#define forward_bf idx_forward_bf
/*--- #define forward_bf ptr_forward_bf  ---*/
/*------------------------------------------------------------*/
DCTCOEF *i_dct_coef_addr()
{
   return coef32;
}
/*------------------------------------------------------------*/
static void idx_forward_bf(int m, int n, INT32 x[], INT32 f[], DCTCOEF coef[])
{
   int i, j, n2;
   int p, q, p0, k;

   p0 = 0;
   n2 = n >> 1;
   for (i = 0; i < m; i++, p0 += n)
   {
      k = 0;
      p = p0;
      q = p + n - 1;
      for (j = 0; j < n2; j++, p++, q--, k++)
      {
	 f[p] = x[p] + x[q];
	 f[n2 + p] = ((x[p] - x[q]) * coef[k]) >> DCTBITS;
      }
   }
}
/*------------------------------------------------------------*/
/*--
static void ptr_forward_bf(int m, int n, INT32 x[], INT32 f[], DCTCOEF coef[])
{
int i, j, n2;
DCTCOEF *c;
INT32 *y;

n2 = n >> 1;
for(i=0; i<m; i++) {
   c = coef;
   y = x+n;
   for(j=0; j<n2; j++) {
        *f = *x + *--y;
        *((f++)+n2) = ( (*x++ - *y) * (*c++) ) >> DCTBITS;
        }
   f+=n2;
   x+=n2;
}
}
---*/
/*------------------------------------------------------------*/
static void forward_bfm(int m, INT32 x[], INT32 f[])
{
   int i;
   int p;

/*--- special case last fwd stage ----*/
   for (p = 0, i = 0; i < m; i++, p += 2)
   {
      f[p] = x[p] + x[p + 1];
      f[p + 1] = ((x[p] - x[p + 1]) * coef32[30]) >> DCTBITS;
   }
}
/*------------------------------------------------------------*/
static void back_bf(int m, int n, INT32 x[], INT32 f[])
{
   int i, j, n2, n21;
   int p, q, p0;

   p0 = 0;
   n2 = n >> 1;
   n21 = n2 - 1;
   for (i = 0; i < m; i++, p0 += n)
   {
      p = p0;
      q = p0;
      for (j = 0; j < n2; j++, p += 2, q++)
	 f[p] = x[q];
      p = p0 + 1;
      for (j = 0; j < n21; j++, p += 2, q++)
	 f[p] = x[q] + x[q + 1];
      f[p] = x[q];
   }
}
/*------------------------------------------------------------*/
static void back_bf0(int n, INT32 x[], WININT f[])
{
   int p, q;

   n--;
#if DCTSATURATE
   for (p = 0, q = 0; p < n; p += 2, q++)
   {
      tmp = x[q];
      if (tmp > 32767)
	 tmp = 32767;
      else if (tmp < -32768)
	 tmp = -32768;
      f[p] = tmp;
   }
   for (p = 1; q < n; p += 2, q++)
   {
      tmp = x[q] + x[q + 1];
      if (tmp > 32767)
	 tmp = 32767;
      else if (tmp < -32768)
	 tmp = -32768;
      f[p] = tmp;
   }
   tmp = x[q];
   if (tmp > 32767)
      tmp = 32767;
   else if (tmp < -32768)
      tmp = -32768;
   f[p] = tmp;
#else
   for (p = 0, q = 0; p < n; p += 2, q++)
      f[p] = x[q];
   for (p = 1; q < n; p += 2, q++)
      f[p] = x[q] + x[q + 1];
   f[p] = x[q];
#endif

}
/*------------------------------------------------------------*/
void i_dct32(SAMPLEINT x[], WININT c[])
{
   INT32 a[32];			/* ping pong buffers */
   INT32 b[32];
   int p, q;

/* special first stage */
   for (p = 0, q = 31; p < 16; p++, q--)
   {
      a[p] = (INT32) x[p] + x[q];
      a[16 + p] = (coef32[p] * ((INT32) x[p] - x[q])) >> DCTBITS;
   }

   forward_bf(2, 16, a, b, coef32 + 16);
   forward_bf(4, 8, b, a, coef32 + 16 + 8);
   forward_bf(8, 4, a, b, coef32 + 16 + 8 + 4);
   forward_bfm(16, b, a);
   back_bf(8, 4, a, b);
   back_bf(4, 8, b, a);
   back_bf(2, 16, a, b);
   back_bf0(32, b, c);
}
/*------------------------------------------------------------*/
void i_dct32_dual(SAMPLEINT x[], WININT c[])
{
   INT32 a[32];			/* ping pong buffers */
   INT32 b[32];
   int p, pp, qq;

/* special first stage for dual chan (interleaved x) */
   pp = 0;
   qq = 2 * 31;
   for (p = 0; p < 16; p++, pp += 2, qq -= 2)
   {
      a[p] = (INT32) x[pp] + x[qq];
      a[16 + p] = (coef32[p] * ((INT32) x[pp] - x[qq])) >> DCTBITS;
   }
   forward_bf(2, 16, a, b, coef32 + 16);
   forward_bf(4, 8, b, a, coef32 + 16 + 8);
   forward_bf(8, 4, a, b, coef32 + 16 + 8 + 4);
   forward_bfm(16, b, a);
   back_bf(8, 4, a, b);
   back_bf(4, 8, b, a);
   back_bf(2, 16, a, b);
   back_bf0(32, b, c);
}
/*---------------convert dual to mono------------------------------*/
void i_dct32_dual_mono(SAMPLEINT x[], WININT c[])
{
   INT32 a[32];			/* ping pong buffers */
   INT32 b[32];
   INT32 t1, t2;
   int p, pp, qq;

/* special first stage  */
   pp = 0;
   qq = 2 * 31;
   for (p = 0; p < 16; p++, pp += 2, qq -= 2)
   {
      t1 = ((INT32) x[pp] + x[pp + 1]);
      t2 = ((INT32) x[qq] + x[qq + 1]);
      a[p] = (t1 + t2) >> 1;
      a[16 + p] = coef32[p] * (t1 - t2) >> (DCTBITS + 1);
   }
   forward_bf(2, 16, a, b, coef32 + 16);
   forward_bf(4, 8, b, a, coef32 + 16 + 8);
   forward_bf(8, 4, a, b, coef32 + 16 + 8 + 4);
   forward_bfm(16, b, a);
   back_bf(8, 4, a, b);
   back_bf(4, 8, b, a);
   back_bf(2, 16, a, b);
   back_bf0(32, b, c);
}
/*------------------------------------------------------------*/
/*---------------- 16 pt dct -------------------------------*/
void i_dct16(SAMPLEINT x[], WININT c[])
{
   INT32 a[16];			/* ping pong buffers */
   INT32 b[16];
   int p, q;

/* special first stage (drop highest sb) */
   a[0] = x[0];
   a[8] = (a[0] * coef32[16]) >> DCTBITS;
   for (p = 1, q = 14; p < 8; p++, q--)
   {
      a[p] = (INT32) x[p] + x[q];
      a[8 + p] = (((INT32) x[p] - x[q]) * coef32[16 + p]) >> DCTBITS;
   }
   forward_bf(2, 8, a, b, coef32 + 16 + 8);
   forward_bf(4, 4, b, a, coef32 + 16 + 8 + 4);
   forward_bfm(8, a, b);
   back_bf(4, 4, b, a);
   back_bf(2, 8, a, b);
   back_bf0(16, b, c);
}
/*------------------------------------------------------------*/
/*---------------- 16 pt dct dual chan---------------------*/
void i_dct16_dual(SAMPLEINT x[], WININT c[])
{
   int p, pp, qq;
   INT32 a[16];			/* ping pong buffers */
   INT32 b[16];

/* special first stage for interleaved input */
   a[0] = x[0];
   a[8] = (coef32[16] * a[0]) >> DCTBITS;
   pp = 2;
   qq = 2 * 14;
   for (p = 1; p < 8; p++, pp += 2, qq -= 2)
   {
      a[p] = (INT32) x[pp] + x[qq];
      a[8 + p] = (coef32[16 + p] * ((INT32) x[pp] - x[qq])) >> DCTBITS;
   }
   forward_bf(2, 8, a, b, coef32 + 16 + 8);
   forward_bf(4, 4, b, a, coef32 + 16 + 8 + 4);
   forward_bfm(8, a, b);
   back_bf(4, 4, b, a);
   back_bf(2, 8, a, b);
   back_bf0(16, b, c);
}
/*------------------------------------------------------------*/
/*---------------- 16 pt dct dual to mono-------------------*/
void i_dct16_dual_mono(SAMPLEINT x[], WININT c[])
{
   INT32 a[16];			/* ping pong buffers */
   INT32 b[16];
   INT32 t1, t2;
   int p, pp, qq;

/* special first stage  */
   a[0] = ((INT32) x[0] + x[1]) >> 1;
   a[8] = (coef32[16] * a[0]) >> DCTBITS;
   pp = 2;
   qq = 2 * 14;
   for (p = 1; p < 8; p++, pp += 2, qq -= 2)
   {
      t1 = (INT32) x[pp] + x[pp + 1];
      t2 = (INT32) x[qq] + x[qq + 1];
      a[p] = (t1 + t2) >> 1;
      a[8 + p] = (coef32[16 + p] * (t1 - t2)) >> (DCTBITS + 1);
   }
   forward_bf(2, 8, a, b, coef32 + 16 + 8);
   forward_bf(4, 4, b, a, coef32 + 16 + 8 + 4);
   forward_bfm(8, a, b);
   back_bf(4, 4, b, a);
   back_bf(2, 8, a, b);
   back_bf0(16, b, c);
}
/*------------------------------------------------------------*/
/*---------------- 8 pt dct -------------------------------*/
void i_dct8(SAMPLEINT x[], WININT c[])
{
   int p, q;
   INT32 a[8];			/* ping pong buffers */
   INT32 b[8];

/* special first stage  */

   for (p = 0, q = 7; p < 4; p++, q--)
   {
      b[p] = (INT32) x[p] + x[q];
      b[4 + p] = (coef32[16 + 8 + p] * ((INT32) x[p] - x[q])) >> DCTBITS;
   }

   forward_bf(2, 4, b, a, coef32 + 16 + 8 + 4);
   forward_bfm(4, a, b);
   back_bf(2, 4, b, a);
   back_bf0(8, a, c);
}
/*------------------------------------------------------------*/
/*---------------- 8 pt dct dual chan---------------------*/
void i_dct8_dual(SAMPLEINT x[], WININT c[])
{
   int p, pp, qq;
   INT32 a[8];			/* ping pong buffers */
   INT32 b[8];

/* special first stage for interleaved input */
   for (p = 0, pp = 0, qq = 14; p < 4; p++, pp += 2, qq -= 2)
   {
      b[p] = (INT32) x[pp] + x[qq];
      b[4 + p] = (coef32[16 + 8 + p] * ((INT32) x[pp] - x[qq])) >> DCTBITS;
   }
   forward_bf(2, 4, b, a, coef32 + 16 + 8 + 4);
   forward_bfm(4, a, b);
   back_bf(2, 4, b, a);
   back_bf0(8, a, c);
}
/*------------------------------------------------------------*/
/*---------------- 8 pt dct dual to mono---------------------*/
void i_dct8_dual_mono(SAMPLEINT x[], WININT c[])
{
   int p, pp, qq;
   INT32 a[8];			/* ping pong buffers */
   INT32 b[8];
   INT32 t1, t2;

/* special first stage  */
   for (p = 0, pp = 0, qq = 14; p < 4; p++, pp += 2, qq -= 2)
   {
      t1 = (INT32) x[pp] + x[pp + 1];
      t2 = (INT32) x[qq] + x[qq + 1];
      b[p] = (t1 + t2) >> 1;
      b[4 + p] = (coef32[16 + 8 + p] * (t1 - t2)) >> (DCTBITS + 1);
   }
   forward_bf(2, 4, b, a, coef32 + 16 + 8 + 4);
   forward_bfm(4, a, b);
   back_bf(2, 4, b, a);
   back_bf0(8, a, c);
}
/*------------------------------------------------------------*/
