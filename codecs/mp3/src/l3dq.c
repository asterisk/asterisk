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

/****  quant.c  ***************************************************
  Layer III  dequant

  does reordering of short blocks

  mod 8/19/98 decode 22 sf bands

******************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <float.h>
#include <math.h>
#include <string.h>
#include "L3.h"
#include "mhead.h"
#include "protos.h"


/*----------
static struct  {
int l[23];
int s[14];} sfBandTable[3] =   
{{{0,4,8,12,16,20,24,30,36,44,52,62,74,90,110,134,162,196,238,288,342,418,576},
 {0,4,8,12,16,22,30,40,52,66,84,106,136,192}},
{{0,4,8,12,16,20,24,30,36,42,50,60,72,88,106,128,156,190,230,276,330,384,576},
 {0,4,8,12,16,22,28,38,50,64,80,100,126,192}},
{{0,4,8,12,16,20,24,30,36,44,54,66,82,102,126,156,194,240,296,364,448,550,576},
 {0,4,8,12,16,22,30,42,58,78,104,138,180,192}}};
----------*/

/*--------------------------------*/
static int pretab[2][22] =
{
   {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
   {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 3, 2, 0},
};


/* 8 bit plus 2 lookup x = pow(2.0, 0.25*(global_gain-210)) */
/* two extra slots to do 1/sqrt(2) scaling for ms */
/* 4 extra slots to do 1/2 scaling for cvt to mono */

/*-------- scaling lookup
x = pow(2.0, -0.5*(1+scalefact_scale)*scalefac + preemp)
look_scale[scalefact_scale][preemp][scalefac]
-----------------------*/
#if 0
typedef float LS[4][32];
#endif

/*--- iSample**(4/3) lookup, -32<=i<=31 ---*/

/*-- pow(2.0, -0.25*8.0*subblock_gain) --*/

/*-- reorder buffer ---*/
typedef float ARRAY3[3];


/*=============================================================*/
float *quant_init_global_addr(MPEG *m)
{
   return m->cupl.look_global;
}
/*-------------------------------------------------------------*/
LS *quant_init_scale_addr(MPEG *m)
{
   return m->cupl.look_scale;
}
/*-------------------------------------------------------------*/
float *quant_init_pow_addr(MPEG *m)
{
   return m->cupl.look_pow;
}
/*-------------------------------------------------------------*/
float *quant_init_subblock_addr(MPEG *m)
{
   return m->cupl.look_subblock;
}
/*=============================================================*/

#ifdef _MSC_VER
#pragma warning(disable: 4056)
#endif

void dequant(MPEG *m, SAMPLE Sample[], int *nsamp,
	     SCALEFACT * sf,
	     GR * gr,
	     CB_INFO * cb_info, int ncbl_mixed)
{
   int i, j;
   int cb, n, w;
   float x0, xs;
   float xsb[3];
   double tmp;
   int ncbl;
   int cbs0;
   ARRAY3 *buf;			/* short block reorder */
   int nbands;
   int i0;
   int non_zero;
   int cbmax[3];

   nbands = *nsamp;


   ncbl = 22;			/* long block cb end */
   cbs0 = 12;			/* short block cb start */
/* ncbl_mixed = 8 or 6  mpeg1 or 2 */
   if (gr->block_type == 2)
   {
      ncbl = 0;
      cbs0 = 0;
      if (gr->mixed_block_flag)
      {
	 ncbl = ncbl_mixed;
	 cbs0 = 3;
      }
   }
/* fill in cb_info -- */
   /* This doesn't seem used anywhere...
   cb_info->lb_type = gr->block_type;
   if (gr->block_type == 2)
      cb_info->lb_type;
   */
   cb_info->cbs0 = cbs0;
   cb_info->ncbl = ncbl;

   cbmax[2] = cbmax[1] = cbmax[0] = 0;
/* global gain pre-adjusted by 2 if ms_mode, 0 otherwise */
   x0 = m->cupl.look_global[(2 + 4) + gr->global_gain];
   i = 0;
/*----- long blocks ---*/
   for (cb = 0; cb < ncbl; cb++)
   {
      non_zero = 0;
      xs = x0 * m->cupl.look_scale[gr->scalefac_scale][pretab[gr->preflag][cb]][sf->l[cb]];
      n = m->cupl.nBand[0][cb];
      for (j = 0; j < n; j++, i++)
      {
	 if (Sample[i].s == 0)
	    Sample[i].x = 0.0F;
	 else
	 {
	    non_zero = 1;
	    if ((Sample[i].s >= (-ISMAX)) && (Sample[i].s < ISMAX))
	       Sample[i].x = xs * m->cupl.look_pow[ISMAX + Sample[i].s];
	    else
	    {
		float tmpConst = (float)(1.0/3.0);
	       tmp = (double) Sample[i].s;
	       Sample[i].x = (float) (xs * tmp * pow(fabs(tmp), tmpConst));
	    }
	 }
      }
      if (non_zero)
	 cbmax[0] = cb;
      if (i >= nbands)
	 break;
   }

   cb_info->cbmax = cbmax[0];
   cb_info->cbtype = 0;		// type = long

   if (cbs0 >= 12)
      return;
/*---------------------------
block type = 2  short blocks
----------------------------*/
   cbmax[2] = cbmax[1] = cbmax[0] = cbs0;
   i0 = i;			/* save for reorder */
   buf = m->cupl.re_buf;
   for (w = 0; w < 3; w++)
      xsb[w] = x0 * m->cupl.look_subblock[gr->subblock_gain[w]];
   for (cb = cbs0; cb < 13; cb++)
   {
      n = m->cupl.nBand[1][cb];
      for (w = 0; w < 3; w++)
      {
	 non_zero = 0;
	 xs = xsb[w] * m->cupl.look_scale[gr->scalefac_scale][0][sf->s[w][cb]];
	 for (j = 0; j < n; j++, i++)
	 {
	    if (Sample[i].s == 0)
	       buf[j][w] = 0.0F;
	    else
	    {
	       non_zero = 1;
	       if ((Sample[i].s >= (-ISMAX)) && (Sample[i].s < ISMAX))
		  buf[j][w] = xs * m->cupl.look_pow[ISMAX + Sample[i].s];
	       else
	       {
		  float tmpConst = (float)(1.0/3.0);
		  tmp = (double) Sample[i].s;
		  buf[j][w] = (float) (xs * tmp * pow(fabs(tmp), tmpConst));
	       }
	    }
	 }
	 if (non_zero)
	    cbmax[w] = cb;
      }
      if (i >= nbands)
	 break;
      buf += n;
   }


   memmove(&Sample[i0].x, &m->cupl.re_buf[0][0], sizeof(float) * (i - i0));

   *nsamp = i;			/* update nsamp */
   cb_info->cbmax_s[0] = cbmax[0];
   cb_info->cbmax_s[1] = cbmax[1];
   cb_info->cbmax_s[2] = cbmax[2];
   if (cbmax[1] > cbmax[0])
      cbmax[0] = cbmax[1];
   if (cbmax[2] > cbmax[0])
      cbmax[0] = cbmax[2];

   cb_info->cbmax = cbmax[0];
   cb_info->cbtype = 1;		/* type = short */


   return;
}

#ifdef _MSC_VER
#pragma warning(default: 4056)
#endif

/*-------------------------------------------------------------*/
