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

/****  upsf.c  ***************************************************

Layer III 
    unpack scale factors



******************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <float.h>
#include <math.h>
#include "L3.h"

extern int iframe;

unsigned int bitget(int n);

/*------------------------------------------------------------*/
static const int slen_table[16][2] =
{
  {0, 0}, {0, 1},
  {0, 2}, {0, 3},
  {3, 0}, {1, 1},
  {1, 2}, {1, 3},
  {2, 1}, {2, 2},
  {2, 3}, {3, 1},
  {3, 2}, {3, 3},
  {4, 2}, {4, 3},
};

/* nr_table[size+3*is_right][block type 0,1,3  2, 2+mixed][4]  */
/* for bt=2 nr is count for group of 3 */
static const int nr_table[6][3][4] =
{
 {{6, 5, 5, 5},
  {3, 3, 3, 3},
  {6, 3, 3, 3}},

 {{6, 5, 7, 3},
  {3, 3, 4, 2},
  {6, 3, 4, 2}},

 {{11, 10, 0, 0},
  {6, 6, 0, 0},
  {6, 3, 6, 0}},			/* adjusted *//* 15, 18, 0, 0,   */
/*-intensity stereo right chan--*/
 {{7, 7, 7, 0},
  {4, 4, 4, 0},
  {6, 5, 4, 0}},

 {{6, 6, 6, 3},
  {4, 3, 3, 2},
  {6, 4, 3, 2}},

 {{8, 8, 5, 0},
  {5, 4, 3, 0},
  {6, 6, 3, 0}},
};

/*=============================================================*/
void unpack_sf_sub_MPEG1(SCALEFACT sf[],
			 GR * grdat,
			 int scfsi,	/* bit flag */
			 int gr)
{
   int sfb;
   int slen0, slen1;
   int block_type, mixed_block_flag, scalefac_compress;


   block_type = grdat->block_type;
   mixed_block_flag = grdat->mixed_block_flag;
   scalefac_compress = grdat->scalefac_compress;

   slen0 = slen_table[scalefac_compress][0];
   slen1 = slen_table[scalefac_compress][1];


   if (block_type == 2)
   {
      if (mixed_block_flag)
      {				/* mixed */
	 for (sfb = 0; sfb < 8; sfb++)
	    sf[0].l[sfb] = bitget(slen0);
	 for (sfb = 3; sfb < 6; sfb++)
	 {
	    sf[0].s[0][sfb] = bitget(slen0);
	    sf[0].s[1][sfb] = bitget(slen0);
	    sf[0].s[2][sfb] = bitget(slen0);
	 }
	 for (sfb = 6; sfb < 12; sfb++)
	 {
	    sf[0].s[0][sfb] = bitget(slen1);
	    sf[0].s[1][sfb] = bitget(slen1);
	    sf[0].s[2][sfb] = bitget(slen1);
	 }
	 return;
      }
      for (sfb = 0; sfb < 6; sfb++)
      {
	 sf[0].s[0][sfb] = bitget(slen0);
	 sf[0].s[1][sfb] = bitget(slen0);
	 sf[0].s[2][sfb] = bitget(slen0);
      }
      for (; sfb < 12; sfb++)
      {
	 sf[0].s[0][sfb] = bitget(slen1);
	 sf[0].s[1][sfb] = bitget(slen1);
	 sf[0].s[2][sfb] = bitget(slen1);
      }
      return;
   }

/* long blocks types 0 1 3, first granule */
   if (gr == 0)
   {
      for (sfb = 0; sfb < 11; sfb++)
	 sf[0].l[sfb] = bitget(slen0);
      for (; sfb < 21; sfb++)
	 sf[0].l[sfb] = bitget(slen1);
      return;
   }

/* long blocks 0, 1, 3, second granule */
   sfb = 0;
   if (scfsi & 8)
      for (; sfb < 6; sfb++)
	 sf[0].l[sfb] = sf[-2].l[sfb];
   else
      for (; sfb < 6; sfb++)
	 sf[0].l[sfb] = bitget(slen0);
   if (scfsi & 4)
      for (; sfb < 11; sfb++)
	 sf[0].l[sfb] = sf[-2].l[sfb];
   else
      for (; sfb < 11; sfb++)
	 sf[0].l[sfb] = bitget(slen0);
   if (scfsi & 2)
      for (; sfb < 16; sfb++)
	 sf[0].l[sfb] = sf[-2].l[sfb];
   else
      for (; sfb < 16; sfb++)
	 sf[0].l[sfb] = bitget(slen1);
   if (scfsi & 1)
      for (; sfb < 21; sfb++)
	 sf[0].l[sfb] = sf[-2].l[sfb];
   else
      for (; sfb < 21; sfb++)
	 sf[0].l[sfb] = bitget(slen1);



   return;
}
/*=============================================================*/
void unpack_sf_sub_MPEG2(SCALEFACT sf[],
			 GR * grdat,
			 int is_and_ch, IS_SF_INFO * sf_info)
{
   int sfb;
   int slen1, slen2, slen3, slen4;
   int nr1, nr2, nr3, nr4;
   int i, k;
   int preflag, intensity_scale;
   int block_type, mixed_block_flag, scalefac_compress;


   block_type = grdat->block_type;
   mixed_block_flag = grdat->mixed_block_flag;
   scalefac_compress = grdat->scalefac_compress;

   preflag = 0;
   intensity_scale = 0;		/* to avoid compiler warning */
   if (is_and_ch == 0)
   {
      if (scalefac_compress < 400)
      {
	 slen2 = scalefac_compress >> 4;
	 slen1 = slen2 / 5;
	 slen2 = slen2 % 5;
	 slen4 = scalefac_compress & 15;
	 slen3 = slen4 >> 2;
	 slen4 = slen4 & 3;
	 k = 0;
      }
      else if (scalefac_compress < 500)
      {
	 scalefac_compress -= 400;
	 slen2 = scalefac_compress >> 2;
	 slen1 = slen2 / 5;
	 slen2 = slen2 % 5;
	 slen3 = scalefac_compress & 3;
	 slen4 = 0;
	 k = 1;
      }
      else
      {
	 scalefac_compress -= 500;
	 slen1 = scalefac_compress / 3;
	 slen2 = scalefac_compress % 3;
	 slen3 = slen4 = 0;
	 if (mixed_block_flag)
	 {
	    slen3 = slen2;	/* adjust for long/short mix logic */
	    slen2 = slen1;
	 }
	 preflag = 1;
	 k = 2;
      }
   }
   else
   {				/* intensity stereo ch = 1 (right) */
      intensity_scale = scalefac_compress & 1;
      scalefac_compress >>= 1;
      if (scalefac_compress < 180)
      {
	 slen1 = scalefac_compress / 36;
	 slen2 = scalefac_compress % 36;
	 slen3 = slen2 % 6;
	 slen2 = slen2 / 6;
	 slen4 = 0;
	 k = 3 + 0;
      }
      else if (scalefac_compress < 244)
      {
	 scalefac_compress -= 180;
	 slen3 = scalefac_compress & 3;
	 scalefac_compress >>= 2;
	 slen2 = scalefac_compress & 3;
	 slen1 = scalefac_compress >> 2;
	 slen4 = 0;
	 k = 3 + 1;
      }
      else
      {
	 scalefac_compress -= 244;
	 slen1 = scalefac_compress / 3;
	 slen2 = scalefac_compress % 3;
	 slen3 = slen4 = 0;
	 k = 3 + 2;
      }
   }

   i = 0;
   if (block_type == 2)
      i = (mixed_block_flag & 1) + 1;
   nr1 = nr_table[k][i][0];
   nr2 = nr_table[k][i][1];
   nr3 = nr_table[k][i][2];
   nr4 = nr_table[k][i][3];


/* return is scale factor info (for right chan is mode) */
   if (is_and_ch)
   {
      sf_info->nr[0] = nr1;
      sf_info->nr[1] = nr2;
      sf_info->nr[2] = nr3;
      sf_info->slen[0] = slen1;
      sf_info->slen[1] = slen2;
      sf_info->slen[2] = slen3;
      sf_info->intensity_scale = intensity_scale;
   }
   grdat->preflag = preflag;	/* return preflag */

/*--------------------------------------*/
   if (block_type == 2)
   {
      if (mixed_block_flag)
      {				/* mixed */
	 if (slen1 != 0)	/* long block portion */
	    for (sfb = 0; sfb < 6; sfb++)
	       sf[0].l[sfb] = bitget(slen1);
	 else
	    for (sfb = 0; sfb < 6; sfb++)
	       sf[0].l[sfb] = 0;
	 sfb = 3;		/* start sfb for short */
      }
      else
      {				/* all short, initial short blocks */
	 sfb = 0;
	 if (slen1 != 0)
	    for (i = 0; i < nr1; i++, sfb++)
	    {
	       sf[0].s[0][sfb] = bitget(slen1);
	       sf[0].s[1][sfb] = bitget(slen1);
	       sf[0].s[2][sfb] = bitget(slen1);
	    }
	 else
	    for (i = 0; i < nr1; i++, sfb++)
	    {
	       sf[0].s[0][sfb] = 0;
	       sf[0].s[1][sfb] = 0;
	       sf[0].s[2][sfb] = 0;
	    }
      }
/* remaining short blocks */
      if (slen2 != 0)
	 for (i = 0; i < nr2; i++, sfb++)
	 {
	    sf[0].s[0][sfb] = bitget(slen2);
	    sf[0].s[1][sfb] = bitget(slen2);
	    sf[0].s[2][sfb] = bitget(slen2);
	 }
      else
	 for (i = 0; i < nr2; i++, sfb++)
	 {
	    sf[0].s[0][sfb] = 0;
	    sf[0].s[1][sfb] = 0;
	    sf[0].s[2][sfb] = 0;
	 }
      if (slen3 != 0)
	 for (i = 0; i < nr3; i++, sfb++)
	 {
	    sf[0].s[0][sfb] = bitget(slen3);
	    sf[0].s[1][sfb] = bitget(slen3);
	    sf[0].s[2][sfb] = bitget(slen3);
	 }
      else
	 for (i = 0; i < nr3; i++, sfb++)
	 {
	    sf[0].s[0][sfb] = 0;
	    sf[0].s[1][sfb] = 0;
	    sf[0].s[2][sfb] = 0;
	 }
      if (slen4 != 0)
	 for (i = 0; i < nr4; i++, sfb++)
	 {
	    sf[0].s[0][sfb] = bitget(slen4);
	    sf[0].s[1][sfb] = bitget(slen4);
	    sf[0].s[2][sfb] = bitget(slen4);
	 }
      else
	 for (i = 0; i < nr4; i++, sfb++)
	 {
	    sf[0].s[0][sfb] = 0;
	    sf[0].s[1][sfb] = 0;
	    sf[0].s[2][sfb] = 0;
	 }
      return;
   }


/* long blocks types 0 1 3 */
   sfb = 0;
   if (slen1 != 0)
      for (i = 0; i < nr1; i++, sfb++)
	 sf[0].l[sfb] = bitget(slen1);
   else
      for (i = 0; i < nr1; i++, sfb++)
	 sf[0].l[sfb] = 0;

   if (slen2 != 0)
      for (i = 0; i < nr2; i++, sfb++)
	 sf[0].l[sfb] = bitget(slen2);
   else
      for (i = 0; i < nr2; i++, sfb++)
	 sf[0].l[sfb] = 0;

   if (slen3 != 0)
      for (i = 0; i < nr3; i++, sfb++)
	 sf[0].l[sfb] = bitget(slen3);
   else
      for (i = 0; i < nr3; i++, sfb++)
	 sf[0].l[sfb] = 0;

   if (slen4 != 0)
      for (i = 0; i < nr4; i++, sfb++)
	 sf[0].l[sfb] = bitget(slen4);
   else
      for (i = 0; i < nr4; i++, sfb++)
	 sf[0].l[sfb] = 0;


}
/*-------------------------------------------------*/
