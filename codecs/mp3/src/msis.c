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

/****  msis.c  ***************************************************
  Layer III  
 antialias, ms and is stereo precessing

**** is_process assumes never switch 
      from short to long in is region *****
     
is_process does ms or stereo in "forbidded sf regions"
    //ms_mode = 0 
    lr[0][i][0] = 1.0f;
    lr[0][i][1] = 0.0f;
    // ms_mode = 1, in is bands is routine does ms processing 
    lr[1][i][0] = 1.0f;
    lr[1][i][1] = 1.0f;

******************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <float.h>
#include <math.h>
#include "L3.h"
#include "mhead.h"

/* The functions are different than their prototypes, by design */

typedef float ARRAY2[2];
typedef float ARRAY8_2[8][2];


/* nBand[0] = long, nBand[1] = short */

/* intensity stereo */
/* if ms mode quant pre-scales all values by 1.0/sqrt(2.0) ms_mode in table
   compensates   */


/* intensity stereo MPEG2 */
/* lr2[intensity_scale][ms_mode][sflen_offset+sf][left/right] */
typedef float ARRAY2_64_2[2][64][2];
typedef float ARRAY64_2[64][2];

#ifdef ASM_X86
extern void antialias_asm(float x[], int n);
#endif /* ASM_X86 */


/*===============================================================*/
ARRAY2 *alias_init_addr(MPEG *m)
{
   return m->cupl.csa;
}
/*-----------------------------------------------------------*/
ARRAY8_2 *msis_init_addr(MPEG *m)
{
/*-------
pi = 4.0*atan(1.0);
t = pi/12.0;
for(i=0;i<7;i++) {
    s = sin(i*t);
    c = cos(i*t);
    // ms_mode = 0 
    m->cupl.lr[0][i][0] = (float)(s/(s+c));
    m->cupl.lr[0][i][1] = (float)(c/(s+c));
    // ms_mode = 1 
    m->cupl.lr[1][i][0] = (float)(sqrt(2.0)*(s/(s+c)));
    m->cupl.lr[1][i][1] = (float)(sqrt(2.0)*(c/(s+c)));
}
//sf = 7 
//ms_mode = 0 
m->cupl.lr[0][i][0] = 1.0f;
m->cupl.lr[0][i][1] = 0.0f;
// ms_mode = 1, in is bands is routine does ms processing 
m->cupl.lr[1][i][0] = 1.0f;
m->cupl.lr[1][i][1] = 1.0f;
------------*/

   return m->cupl.lr;
}
/*-------------------------------------------------------------*/
ARRAY2_64_2 *msis_init_addr_MPEG2(MPEG *m)
{
   return m->cupl.lr2;
}
/*===============================================================*/
void antialias(MPEG *m, float x[], int n)
{
#ifdef ASM_X86
   antialias_asm(x, n);
#else
   int i, k;
   float a, b;

   for (k = 0; k < n; k++)
   {
      for (i = 0; i < 8; i++)
      {
	 a = x[17 - i];
	 b = x[18 + i];
	 x[17 - i] = a * m->cupl.csa[i][0] - b * m->cupl.csa[i][1];
	 x[18 + i] = b * m->cupl.csa[i][0] + a * m->cupl.csa[i][1];
      }
      x += 18;
   }
#endif
}
/*===============================================================*/
void ms_process(float x[][1152], int n)		/* sum-difference stereo */
{
   int i;
   float xl, xr;

/*-- note: sqrt(2) done scaling by dequant ---*/
   for (i = 0; i < n; i++)
   {
      xl = x[0][i] + x[1][i];
      xr = x[0][i] - x[1][i];
      x[0][i] = xl;
      x[1][i] = xr;
   }
   return;
}
/*===============================================================*/
void is_process_MPEG1(MPEG *vm, float x[][1152],	/* intensity stereo */
		      SCALEFACT * sf,
		      CB_INFO cb_info[2],	/* [ch] */
		      int nsamp, int ms_mode)
{
   int i, j, n, cb, w;
   float fl, fr;
   int m;
   int isf;
   float fls[3], frs[3];
   int cb0;


   cb0 = cb_info[1].cbmax;	/* start at end of right */
   i = vm->cupl.sfBandIndex[cb_info[1].cbtype][cb0];
   cb0++;
   m = nsamp - i;		/* process to len of left */

   if (cb_info[1].cbtype)
      goto short_blocks;
/*------------------------*/
/* long_blocks: */
   for (cb = cb0; cb < 21; cb++)
   {
      isf = sf->l[cb];
      n = vm->cupl.nBand[0][cb];
      fl = vm->cupl.lr[ms_mode][isf][0];
      fr = vm->cupl.lr[ms_mode][isf][1];
      for (j = 0; j < n; j++, i++)
      {
	 if (--m < 0)
	    goto exit;
	 x[1][i] = fr * x[0][i];
	 x[0][i] = fl * x[0][i];
      }
   }
   return;
/*------------------------*/
 short_blocks:
   for (cb = cb0; cb < 12; cb++)
   {
      for (w = 0; w < 3; w++)
      {
	 isf = sf->s[w][cb];
	 fls[w] = vm->cupl.lr[ms_mode][isf][0];
	 frs[w] = vm->cupl.lr[ms_mode][isf][1];
      }
      n = vm->cupl.nBand[1][cb];
      for (j = 0; j < n; j++)
      {
	 m -= 3;
	 if (m < 0)
	    goto exit;
	 x[1][i] = frs[0] * x[0][i];
	 x[0][i] = fls[0] * x[0][i];
	 x[1][1 + i] = frs[1] * x[0][1 + i];
	 x[0][1 + i] = fls[1] * x[0][1 + i];
	 x[1][2 + i] = frs[2] * x[0][2 + i];
	 x[0][2 + i] = fls[2] * x[0][2 + i];
	 i += 3;
      }
   }

 exit:
   return;
}
/*===============================================================*/
void is_process_MPEG2(MPEG *vm, float x[][1152],	/* intensity stereo */
		      SCALEFACT * sf,
		      CB_INFO cb_info[2],	/* [ch] */
		      IS_SF_INFO * is_sf_info,
		      int nsamp, int ms_mode)
{
   int i, j, k, n, cb, w;
   float fl, fr;
   int m;
   int isf;
   int il[21];
   int tmp;
   int r;
   ARRAY2 *lr;
   int cb0, cb1;

   lr = vm->cupl.lr2[is_sf_info->intensity_scale][ms_mode];

   if (cb_info[1].cbtype)
      goto short_blocks;

/*------------------------*/
/* long_blocks: */
   cb0 = cb_info[1].cbmax;	/* start at end of right */
   i = vm->cupl.sfBandIndex[0][cb0];
   m = nsamp - i;		/* process to len of left */
/* gen sf info */
   for (k = r = 0; r < 3; r++)
   {
      tmp = (1 << is_sf_info->slen[r]) - 1;
      for (j = 0; j < is_sf_info->nr[r]; j++, k++)
	 il[k] = tmp;
   }
   for (cb = cb0 + 1; cb < 21; cb++)
   {
      isf = il[cb] + sf->l[cb];
      fl = lr[isf][0];
      fr = lr[isf][1];
      n = vm->cupl.nBand[0][cb];
      for (j = 0; j < n; j++, i++)
      {
	 if (--m < 0)
	    goto exit;
	 x[1][i] = fr * x[0][i];
	 x[0][i] = fl * x[0][i];
      }
   }
   return;
/*------------------------*/
 short_blocks:

   for (k = r = 0; r < 3; r++)
   {
      tmp = (1 << is_sf_info->slen[r]) - 1;
      for (j = 0; j < is_sf_info->nr[r]; j++, k++)
	 il[k] = tmp;
   }

   for (w = 0; w < 3; w++)
   {
      cb0 = cb_info[1].cbmax_s[w];	/* start at end of right */
      i = vm->cupl.sfBandIndex[1][cb0] + w;
      cb1 = cb_info[0].cbmax_s[w];	/* process to end of left */

      for (cb = cb0 + 1; cb <= cb1; cb++)
      {
	 isf = il[cb] + sf->s[w][cb];
	 fl = lr[isf][0];
	 fr = lr[isf][1];
	 n = vm->cupl.nBand[1][cb];
	 for (j = 0; j < n; j++)
	 {
	    x[1][i] = fr * x[0][i];
	    x[0][i] = fl * x[0][i];
	    i += 3;
	 }
      }

   }

 exit:
   return;
}
/*===============================================================*/
