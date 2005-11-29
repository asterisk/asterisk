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

/****  cupL3.c  ***************************************************
unpack Layer III


mod 8/18/97  bugfix crc problem

mod 10/9/97  add band_limit12 for short blocks

mod 10/22/97  zero buf_ptrs in init

mod 5/15/98 mpeg 2.5

mod 8/19/98 decode 22 sf bands

******************************************************************/

/*---------------------------------------
TO DO: Test mixed blocks (mixed long/short)
  No mixed blocks in mpeg-1 test stream being used for development

-----------------------------------------*/

#include <stdlib.h>
#include <stdio.h>
#include <float.h>
#include <math.h>
#include <memory.h>
#include <string.h>
#include <assert.h>
#include "L3.h"
#include "mhead.h"		/* mpeg header structure */
#include "jdw.h"
#include "protos.h"


/*====================================================================*/
static int mp_sr20_table[2][4] =
{{441, 480, 320, -999}, {882, 960, 640, -999}};
static int mp_br_tableL3[2][16] =
{{0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},	/* mpeg 2 */
 {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0}};

/*====================================================================*/

/*-- global band tables */
/*-- short portion is 3*x !! --*/

/*====================================================================*/

/*---------------------------------*/
/*---------------------------------*/
/*- sample union of int/float  sample[ch][gr][576] */
/* Sample is the same as cup.sample */

void sbt_dual_L3(MPEG *m, float *sample, short *pcm, int n);

IN_OUT L3audio_decode_MPEG1(void *mv, unsigned char *bs, unsigned char *pcm);
IN_OUT L3audio_decode_MPEG2(void *mv, unsigned char *bs, unsigned char *pcm);
/*
static DECODE_FUNCTION decode_function = L3audio_decode_MPEG1;
*/

/*====================================================================*/
/* get bits from bitstream in endian independent way */

BITDAT bitdat;			/* global for inline use by Huff */

/*------------- initialize bit getter -------------*/
static void bitget_init(unsigned char *buf)
{
   bitdat.bs_ptr0 = bitdat.bs_ptr = buf;
   bitdat.bits = 0;
   bitdat.bitbuf = 0;
}
/*------------- initialize bit getter -------------*/
static void bitget_init_end(unsigned char *buf_end)
{
   bitdat.bs_ptr_end = buf_end;
}
/*------------- get n bits from bitstream -------------*/
int bitget_bits_used()
{
   int n;			/* compute bits used from last init call */

   n = ((bitdat.bs_ptr - bitdat.bs_ptr0) << 3) - bitdat.bits;
   return n;
}
/*------------- check for n bits in bitbuf -------------*/
void bitget_check(int n)
{
   if (bitdat.bits < n)
   {
      while (bitdat.bits <= 24)
      {
	 bitdat.bitbuf = (bitdat.bitbuf << 8) | *bitdat.bs_ptr++;
	 bitdat.bits += 8;
      }
   }
}
/*------------- get n bits from bitstream -------------*/
unsigned int bitget(int n)
{
   unsigned int x;

   if (bitdat.bits < n)
   {				/* refill bit buf if necessary */
      while (bitdat.bits <= 24)
      {
	 bitdat.bitbuf = (bitdat.bitbuf << 8) | *bitdat.bs_ptr++;
	 bitdat.bits += 8;
      }
   }
   bitdat.bits -= n;
   x = bitdat.bitbuf >> bitdat.bits;
   bitdat.bitbuf -= x << bitdat.bits;
   return x;
}
/*------------- get 1 bit from bitstream -------------*/
unsigned int bitget_1bit()
{
   unsigned int x;

   if (bitdat.bits <= 0)
   {				/* refill bit buf if necessary */
      while (bitdat.bits <= 24)
      {
	 bitdat.bitbuf = (bitdat.bitbuf << 8) | *bitdat.bs_ptr++;
	 bitdat.bits += 8;
      }
   }
   bitdat.bits--;
   x = bitdat.bitbuf >> bitdat.bits;
   bitdat.bitbuf -= x << bitdat.bits;
   return x;
}
/*====================================================================*/
static void Xform_mono(void *mv, void *pcm, int igr)
{
   MPEG *m = mv;
   int igr_prev, n1, n2;

/*--- hybrid + sbt ---*/
   n1 = n2 = m->cupl.nsamp[igr][0];	/* total number bands */
   if (m->cupl.side_info.gr[igr][0].block_type == 2)
   {				/* long bands */
      n1 = 0;
      if (m->cupl.side_info.gr[igr][0].mixed_block_flag)
	 n1 = m->cupl.sfBandIndex[0][m->cupl.ncbl_mixed - 1];
   }
   if (n1 > m->cupl.band_limit)
      n1 = m->cupl.band_limit;
   if (n2 > m->cupl.band_limit)
      n2 = m->cupl.band_limit;
   igr_prev = igr ^ 1;

   m->cupl.nsamp[igr][0] = hybrid(m,m->cupl.sample[0][igr], m->cupl.sample[0][igr_prev],
	 m->cupl.yout, m->cupl.side_info.gr[igr][0].block_type, n1, n2, m->cupl.nsamp[igr_prev][0]);
   FreqInvert(m->cupl.yout, m->cupl.nsamp[igr][0]);
   m->cupl.sbt_L3(m,m->cupl.yout, pcm, 0);

}

/*--------------------------------------------------------------------*/
static void Xform_dual_right(void *mv, void *pcm, int igr)
{
   MPEG *m = mv;
   int igr_prev, n1, n2;

/*--- hybrid + sbt ---*/
   n1 = n2 = m->cupl.nsamp[igr][1];	/* total number bands */
   if (m->cupl.side_info.gr[igr][1].block_type == 2)
   {				/* long bands */
      n1 = 0;
      if (m->cupl.side_info.gr[igr][1].mixed_block_flag)
	 n1 = m->cupl.sfBandIndex[0][m->cupl.ncbl_mixed - 1];
   }
   if (n1 > m->cupl.band_limit)
      n1 = m->cupl.band_limit;
   if (n2 > m->cupl.band_limit)
      n2 = m->cupl.band_limit;
   igr_prev = igr ^ 1;
   m->cupl.nsamp[igr][1] = hybrid(m,m->cupl.sample[1][igr], m->cupl.sample[1][igr_prev],
	 m->cupl.yout, m->cupl.side_info.gr[igr][1].block_type, n1, n2, m->cupl.nsamp[igr_prev][1]);
   FreqInvert(m->cupl.yout, m->cupl.nsamp[igr][1]);
   m->cupl.sbt_L3(m,m->cupl.yout, pcm, 0);

}
/*--------------------------------------------------------------------*/
static void Xform_dual(void *mv, void *pcm, int igr)
{
   MPEG *m = mv;
   int ch;
   int igr_prev, n1, n2;

/*--- hybrid + sbt ---*/
   igr_prev = igr ^ 1;
   for (ch = 0; ch < m->cupl.nchan; ch++)
   {
      n1 = n2 = m->cupl.nsamp[igr][ch];	/* total number bands */
      if (m->cupl.side_info.gr[igr][ch].block_type == 2)
      {				/* long bands */
	 n1 = 0;
	 if (m->cupl.side_info.gr[igr][ch].mixed_block_flag)
	    n1 = m->cupl.sfBandIndex[0][m->cupl.ncbl_mixed - 1];
      }
      if (n1 > m->cupl.band_limit)
	 n1 = m->cupl.band_limit;
      if (n2 > m->cupl.band_limit)
	 n2 = m->cupl.band_limit;
      m->cupl.nsamp[igr][ch] = hybrid(m,m->cupl.sample[ch][igr], m->cupl.sample[ch][igr_prev],
       m->cupl.yout, m->cupl.side_info.gr[igr][ch].block_type, n1, n2, m->cupl.nsamp[igr_prev][ch]);
      FreqInvert(m->cupl.yout, m->cupl.nsamp[igr][ch]);
      m->cupl.sbt_L3(m,m->cupl.yout, pcm, ch);
   }

}
/*--------------------------------------------------------------------*/
static void Xform_dual_mono(void *mv, void *pcm, int igr)
{
   MPEG *m = mv;
   int igr_prev, n1, n2, n3;

/*--- hybrid + sbt ---*/
   igr_prev = igr ^ 1;
   if ((m->cupl.side_info.gr[igr][0].block_type == m->cupl.side_info.gr[igr][1].block_type)
       && (m->cupl.side_info.gr[igr][0].mixed_block_flag == 0)
       && (m->cupl.side_info.gr[igr][1].mixed_block_flag == 0))
   {

      n2 = m->cupl.nsamp[igr][0];	/* total number bands max of L R */
      if (n2 < m->cupl.nsamp[igr][1])
	 n2 = m->cupl.nsamp[igr][1];
      if (n2 > m->cupl.band_limit)
	 n2 = m->cupl.band_limit;
      n1 = n2;			/* n1 = number long bands */
      if (m->cupl.side_info.gr[igr][0].block_type == 2)
	 n1 = 0;
      sum_f_bands(m->cupl.sample[0][igr], m->cupl.sample[1][igr], n2);
      n3 = m->cupl.nsamp[igr][0] = hybrid(m,m->cupl.sample[0][igr], m->cupl.sample[0][igr_prev],
	 m->cupl.yout, m->cupl.side_info.gr[igr][0].block_type, n1, n2, m->cupl.nsamp[igr_prev][0]);
   }
   else
   {				/* transform and then sum (not tested - never happens in test) */
/*-- left chan --*/
      n1 = n2 = m->cupl.nsamp[igr][0];	/* total number bands */
      if (m->cupl.side_info.gr[igr][0].block_type == 2)
      {
	 n1 = 0;		/* long bands */
	 if (m->cupl.side_info.gr[igr][0].mixed_block_flag)
	    n1 = m->cupl.sfBandIndex[0][m->cupl.ncbl_mixed - 1];
      }
      n3 = m->cupl.nsamp[igr][0] = hybrid(m,m->cupl.sample[0][igr], m->cupl.sample[0][igr_prev],
	 m->cupl.yout, m->cupl.side_info.gr[igr][0].block_type, n1, n2, m->cupl.nsamp[igr_prev][0]);
/*-- right chan --*/
      n1 = n2 = m->cupl.nsamp[igr][1];	/* total number bands */
      if (m->cupl.side_info.gr[igr][1].block_type == 2)
      {
	 n1 = 0;		/* long bands */
	 if (m->cupl.side_info.gr[igr][1].mixed_block_flag)
	    n1 = m->cupl.sfBandIndex[0][m->cupl.ncbl_mixed - 1];
      }
      m->cupl.nsamp[igr][1] = hybrid_sum(m, m->cupl.sample[1][igr], m->cupl.sample[0][igr],
			     m->cupl.yout, m->cupl.side_info.gr[igr][1].block_type, n1, n2);
      if (n3 < m->cupl.nsamp[igr][1])
	 n1 = m->cupl.nsamp[igr][1];
   }

/*--------*/
   FreqInvert(m->cupl.yout, n3);
   m->cupl.sbt_L3(m,m->cupl.yout, pcm, 0);

}
/*--------------------------------------------------------------------*/
/*====================================================================*/
static int unpack_side_MPEG1(MPEG *m)
{
   int prot;
   int br_index;
   int igr, ch;
   int side_bytes;

/* decode partial header plus initial side info */
/* at entry bit getter points at id, sync skipped by caller */

   m->cupl.id = bitget(1);		/* id */
   bitget(2);			/* skip layer */
   prot = bitget(1);		/* bitget prot bit */
   br_index = bitget(4);
   m->cupl.sr_index = bitget(2);
   m->cupl.pad = bitget(1);
   bitget(1);			/* skip to mode */
   m->cupl.side_info.mode = bitget(2);	/* mode */
   m->cupl.side_info.mode_ext = bitget(2);	/* mode ext */

   if (m->cupl.side_info.mode != 1)
      m->cupl.side_info.mode_ext = 0;

/* adjust global gain in ms mode to avoid having to mult by 1/sqrt(2) */
   m->cupl.ms_mode = m->cupl.side_info.mode_ext >> 1;
   m->cupl.is_mode = m->cupl.side_info.mode_ext & 1;


   m->cupl.crcbytes = 0;
   if (prot)
      bitget(4);		/* skip to data */
   else
   {
      bitget(20);		/* skip crc */
      m->cupl.crcbytes = 2;
   }

   if (br_index > 0)		/* framebytes fixed for free format */
	{
      m->cupl.framebytes =
	 2880 * mp_br_tableL3[m->cupl.id][br_index] / mp_sr20_table[m->cupl.id][m->cupl.sr_index];
   }

   m->cupl.side_info.main_data_begin = bitget(9);
   if (m->cupl.side_info.mode == 3)
   {
      m->cupl.side_info.private_bits = bitget(5);
      m->cupl.nchan = 1;
      m->cupl.stereo_flag = 0;
      side_bytes = (4 + 17);
/*-- with header --*/
   }
   else
   {
      m->cupl.side_info.private_bits = bitget(3);
      m->cupl.nchan = 2;
      m->cupl.stereo_flag = 1;
      side_bytes = (4 + 32);
/*-- with header --*/
   }
   for (ch = 0; ch < m->cupl.nchan; ch++)
      m->cupl.side_info.scfsi[ch] = bitget(4);
/* this always 0 (both igr) for short blocks */

   for (igr = 0; igr < 2; igr++)
   {
      for (ch = 0; ch < m->cupl.nchan; ch++)
      {
	 m->cupl.side_info.gr[igr][ch].part2_3_length = bitget(12);
	 m->cupl.side_info.gr[igr][ch].big_values = bitget(9);
	 m->cupl.side_info.gr[igr][ch].global_gain = bitget(8) + m->cupl.gain_adjust;
	 if (m->cupl.ms_mode)
	    m->cupl.side_info.gr[igr][ch].global_gain -= 2;
	 m->cupl.side_info.gr[igr][ch].scalefac_compress = bitget(4);
	 m->cupl.side_info.gr[igr][ch].window_switching_flag = bitget(1);
	 if (m->cupl.side_info.gr[igr][ch].window_switching_flag)
	 {
	    m->cupl.side_info.gr[igr][ch].block_type = bitget(2);
	    m->cupl.side_info.gr[igr][ch].mixed_block_flag = bitget(1);
	    m->cupl.side_info.gr[igr][ch].table_select[0] = bitget(5);
	    m->cupl.side_info.gr[igr][ch].table_select[1] = bitget(5);
	    m->cupl.side_info.gr[igr][ch].subblock_gain[0] = bitget(3);
	    m->cupl.side_info.gr[igr][ch].subblock_gain[1] = bitget(3);
	    m->cupl.side_info.gr[igr][ch].subblock_gain[2] = bitget(3);
	  /* region count set in terms of long block cb's/bands */
	  /* r1 set so r0+r1+1 = 21 (lookup produces 576 bands ) */
	  /* if(window_switching_flag) always 36 samples in region0 */
	    m->cupl.side_info.gr[igr][ch].region0_count = (8 - 1);	/* 36 samples */
	    m->cupl.side_info.gr[igr][ch].region1_count = 20 - (8 - 1);
	 }
	 else
	 {
	    m->cupl.side_info.gr[igr][ch].mixed_block_flag = 0;
	    m->cupl.side_info.gr[igr][ch].block_type = 0;
	    m->cupl.side_info.gr[igr][ch].table_select[0] = bitget(5);
	    m->cupl.side_info.gr[igr][ch].table_select[1] = bitget(5);
	    m->cupl.side_info.gr[igr][ch].table_select[2] = bitget(5);
	    m->cupl.side_info.gr[igr][ch].region0_count = bitget(4);
	    m->cupl.side_info.gr[igr][ch].region1_count = bitget(3);
	 }
	 m->cupl.side_info.gr[igr][ch].preflag = bitget(1);
	 m->cupl.side_info.gr[igr][ch].scalefac_scale = bitget(1);
	 m->cupl.side_info.gr[igr][ch].count1table_select = bitget(1);
      }
   }



/* return  bytes in header + side info */
   return side_bytes;
}
/*====================================================================*/
static int unpack_side_MPEG2(MPEG *m, int igr)
{
   int prot;
   int br_index;
   int ch;
   int side_bytes;

/* decode partial header plus initial side info */
/* at entry bit getter points at id, sync skipped by caller */

   m->cupl.id = bitget(1);		/* id */
   bitget(2);			/* skip layer */
   prot = bitget(1);		/* bitget prot bit */
   br_index = bitget(4);
   m->cupl.sr_index = bitget(2);
   m->cupl.pad = bitget(1);
   bitget(1);			/* skip to mode */
   m->cupl.side_info.mode = bitget(2);	/* mode */
   m->cupl.side_info.mode_ext = bitget(2);	/* mode ext */

   if (m->cupl.side_info.mode != 1)
      m->cupl.side_info.mode_ext = 0;

/* adjust global gain in ms mode to avoid having to mult by 1/sqrt(2) */
   m->cupl.ms_mode = m->cupl.side_info.mode_ext >> 1;
   m->cupl.is_mode = m->cupl.side_info.mode_ext & 1;

   m->cupl.crcbytes = 0;
   if (prot)
      bitget(4);		/* skip to data */
   else
   {
      bitget(20);		/* skip crc */
      m->cupl.crcbytes = 2;
   }

   if (br_index > 0)
   {				/* framebytes fixed for free format */
      if (m->cupl.mpeg25_flag == 0)
      {
	 m->cupl.framebytes =
	    1440 * mp_br_tableL3[m->cupl.id][br_index] / mp_sr20_table[m->cupl.id][m->cupl.sr_index];
      }
      else
      {
	 m->cupl.framebytes =
	    2880 * mp_br_tableL3[m->cupl.id][br_index] / mp_sr20_table[m->cupl.id][m->cupl.sr_index];
       //if( sr_index == 2 ) return 0;  // fail mpeg25 8khz
      }
   }
   m->cupl.side_info.main_data_begin = bitget(8);
   if (m->cupl.side_info.mode == 3)
   {
      m->cupl.side_info.private_bits = bitget(1);
      m->cupl.nchan = 1;
      m->cupl.stereo_flag = 0;
      side_bytes = (4 + 9);
/*-- with header --*/
   }
   else
   {
      m->cupl.side_info.private_bits = bitget(2);
      m->cupl.nchan = 2;
      m->cupl.stereo_flag = 1;
      side_bytes = (4 + 17);
/*-- with header --*/
   }
   m->cupl.side_info.scfsi[1] = m->cupl.side_info.scfsi[0] = 0;


   for (ch = 0; ch < m->cupl.nchan; ch++)
   {
      m->cupl.side_info.gr[igr][ch].part2_3_length = bitget(12);
      m->cupl.side_info.gr[igr][ch].big_values = bitget(9);
      m->cupl.side_info.gr[igr][ch].global_gain = bitget(8) + m->cupl.gain_adjust;
      if (m->cupl.ms_mode)
	 m->cupl.side_info.gr[igr][ch].global_gain -= 2;
      m->cupl.side_info.gr[igr][ch].scalefac_compress = bitget(9);
      m->cupl.side_info.gr[igr][ch].window_switching_flag = bitget(1);
      if (m->cupl.side_info.gr[igr][ch].window_switching_flag)
      {
	 m->cupl.side_info.gr[igr][ch].block_type = bitget(2);
	 m->cupl.side_info.gr[igr][ch].mixed_block_flag = bitget(1);
	 m->cupl.side_info.gr[igr][ch].table_select[0] = bitget(5);
	 m->cupl.side_info.gr[igr][ch].table_select[1] = bitget(5);
	 m->cupl.side_info.gr[igr][ch].subblock_gain[0] = bitget(3);
	 m->cupl.side_info.gr[igr][ch].subblock_gain[1] = bitget(3);
	 m->cupl.side_info.gr[igr][ch].subblock_gain[2] = bitget(3);
       /* region count set in terms of long block cb's/bands  */
       /* r1 set so r0+r1+1 = 21 (lookup produces 576 bands ) */
       /* bt=1 or 3       54 samples */
       /* bt=2 mixed=0    36 samples */
       /* bt=2 mixed=1    54 (8 long sf) samples? or maybe 36 */
       /* region0 discussion says 54 but this would mix long */
       /* and short in region0 if scale factors switch */
       /* at band 36 (6 long scale factors) */
	 if ((m->cupl.side_info.gr[igr][ch].block_type == 2))
	 {
	    m->cupl.side_info.gr[igr][ch].region0_count = (6 - 1);	/* 36 samples */
	    m->cupl.side_info.gr[igr][ch].region1_count = 20 - (6 - 1);
	 }
	 else
	 {			/* long block type 1 or 3 */
	    m->cupl.side_info.gr[igr][ch].region0_count = (8 - 1);	/* 54 samples */
	    m->cupl.side_info.gr[igr][ch].region1_count = 20 - (8 - 1);
	 }
      }
      else
      {
	 m->cupl.side_info.gr[igr][ch].mixed_block_flag = 0;
	 m->cupl.side_info.gr[igr][ch].block_type = 0;
	 m->cupl.side_info.gr[igr][ch].table_select[0] = bitget(5);
	 m->cupl.side_info.gr[igr][ch].table_select[1] = bitget(5);
	 m->cupl.side_info.gr[igr][ch].table_select[2] = bitget(5);
	 m->cupl.side_info.gr[igr][ch].region0_count = bitget(4);
	 m->cupl.side_info.gr[igr][ch].region1_count = bitget(3);
      }
      m->cupl.side_info.gr[igr][ch].preflag = 0;
      m->cupl.side_info.gr[igr][ch].scalefac_scale = bitget(1);
      m->cupl.side_info.gr[igr][ch].count1table_select = bitget(1);
   }

/* return  bytes in header + side info */
   return side_bytes;
}
/*-----------------------------------------------------------------*/
static void unpack_main(MPEG *m, unsigned char *pcm, int igr)
{
   int ch;
   int bit0;
   int n1, n2, n3, n4, nn2, nn3;
   int nn4;
   int qbits;
   int m0;


   for (ch = 0; ch < m->cupl.nchan; ch++)
   {
      bitget_init(m->cupl.buf + (m->cupl.main_pos_bit >> 3));
      bit0 = (m->cupl.main_pos_bit & 7);
      if (bit0)
	 bitget(bit0);
      m->cupl.main_pos_bit += m->cupl.side_info.gr[igr][ch].part2_3_length;
      bitget_init_end(m->cupl.buf + ((m->cupl.main_pos_bit + 39) >> 3));
/*-- scale factors --*/
      if (m->cupl.id)
	 unpack_sf_sub_MPEG1(&m->cupl.sf[igr][ch],
			  &m->cupl.side_info.gr[igr][ch], m->cupl.side_info.scfsi[ch], igr);
      else
	 unpack_sf_sub_MPEG2(&m->cupl.sf[igr][ch],
			 &m->cupl.side_info.gr[igr][ch], m->cupl.is_mode & ch, &m->cupl.is_sf_info);
/*--- huff data ---*/
      n1 = m->cupl.sfBandIndex[0][m->cupl.side_info.gr[igr][ch].region0_count];
      n2 = m->cupl.sfBandIndex[0][m->cupl.side_info.gr[igr][ch].region0_count
			  + m->cupl.side_info.gr[igr][ch].region1_count + 1];
      n3 = m->cupl.side_info.gr[igr][ch].big_values;
      n3 = n3 + n3;


      if (n3 > m->cupl.band_limit)
	 n3 = m->cupl.band_limit;
      if (n2 > n3)
	 n2 = n3;
      if (n1 > n3)
	 n1 = n3;
      nn3 = n3 - n2;
      nn2 = n2 - n1;
      unpack_huff(m->cupl.sample[ch][igr], n1, m->cupl.side_info.gr[igr][ch].table_select[0]);
      unpack_huff(m->cupl.sample[ch][igr] + n1, nn2, m->cupl.side_info.gr[igr][ch].table_select[1]);
      unpack_huff(m->cupl.sample[ch][igr] + n2, nn3, m->cupl.side_info.gr[igr][ch].table_select[2]);
      qbits = m->cupl.side_info.gr[igr][ch].part2_3_length - (bitget_bits_used() - bit0);
      nn4 = unpack_huff_quad(m->cupl.sample[ch][igr] + n3, m->cupl.band_limit - n3, qbits,
			     m->cupl.side_info.gr[igr][ch].count1table_select);
      n4 = n3 + nn4;
      m->cupl.nsamp[igr][ch] = n4;
    //limit n4 or allow deqaunt to sf band 22
      if (m->cupl.side_info.gr[igr][ch].block_type == 2)
	 n4 = min(n4, m->cupl.band_limit12);
      else
	 n4 = min(n4, m->cupl.band_limit21);
      if (n4 < 576)
	 memset(m->cupl.sample[ch][igr] + n4, 0, sizeof(SAMPLE) * (576 - n4));
      if (bitdat.bs_ptr > bitdat.bs_ptr_end)
      {				// bad data overrun

	 memset(m->cupl.sample[ch][igr], 0, sizeof(SAMPLE) * (576));
      }
   }



/*--- dequant ---*/
   for (ch = 0; ch < m->cupl.nchan; ch++)
   {
      dequant(m,m->cupl.sample[ch][igr],
	      &m->cupl.nsamp[igr][ch],	/* nsamp updated for shorts */
	      &m->cupl.sf[igr][ch], &m->cupl.side_info.gr[igr][ch],
	      &m->cupl.cb_info[igr][ch], m->cupl.ncbl_mixed);
   }

/*--- ms stereo processing  ---*/
   if (m->cupl.ms_mode)
   {
      if (m->cupl.is_mode == 0)
      {
	 m0 = m->cupl.nsamp[igr][0];	/* process to longer of left/right */
	 if (m0 < m->cupl.nsamp[igr][1])
	    m0 = m->cupl.nsamp[igr][1];
      }
      else
      {				/* process to last cb in right */
	 m0 = m->cupl.sfBandIndex[m->cupl.cb_info[igr][1].cbtype][m->cupl.cb_info[igr][1].cbmax];
      }
      ms_process(m->cupl.sample[0][igr], m0);
   }

/*--- is stereo processing  ---*/
   if (m->cupl.is_mode)
   {
      if (m->cupl.id)
	 is_process_MPEG1(m, m->cupl.sample[0][igr], &m->cupl.sf[igr][1],
			  m->cupl.cb_info[igr], m->cupl.nsamp[igr][0], m->cupl.ms_mode);
      else
	 is_process_MPEG2(m,m->cupl.sample[0][igr], &m->cupl.sf[igr][1],
			  m->cupl.cb_info[igr], &m->cupl.is_sf_info,
			  m->cupl.nsamp[igr][0], m->cupl.ms_mode);
   }

/*-- adjust ms and is modes to max of left/right */
   if (m->cupl.side_info.mode_ext)
   {
      if (m->cupl.nsamp[igr][0] < m->cupl.nsamp[igr][1])
	 m->cupl.nsamp[igr][0] = m->cupl.nsamp[igr][1];
      else
	 m->cupl.nsamp[igr][1] = m->cupl.nsamp[igr][0];
   }

/*--- antialias ---*/
   for (ch = 0; ch < m->cupl.nchan; ch++)
   {
      if (m->cupl.cb_info[igr][ch].ncbl == 0)
	 continue;		/* have no long blocks */
      if (m->cupl.side_info.gr[igr][ch].mixed_block_flag)
	 n1 = 1;		/* 1 -> 36 samples */
      else
	 n1 = (m->cupl.nsamp[igr][ch] + 7) / 18;
      if (n1 > 31)
	 n1 = 31;
      antialias(m, m->cupl.sample[ch][igr], n1);
      n1 = 18 * n1 + 8;		/* update number of samples */
      if (n1 > m->cupl.nsamp[igr][ch])
	 m->cupl.nsamp[igr][ch] = n1;
   }



/*--- hybrid + sbt ---*/
   m->cupl.Xform(m, pcm, igr);


/*-- done --*/
}
/*--------------------------------------------------------------------*/
/*-----------------------------------------------------------------*/
IN_OUT L3audio_decode(void *mv, unsigned char *bs, unsigned char *pcm)
{
   MPEG *m = mv;
   return m->cupl.decode_function((MPEG *)mv, bs, pcm);
}

/*--------------------------------------------------------------------*/
IN_OUT L3audio_decode_MPEG1(void *mv, unsigned char *bs, unsigned char *pcm)
{
   MPEG *m = mv;
   int sync;
   IN_OUT in_out;
   int side_bytes;
   int nbytes;
   
   m->cupl.iframe++;

   bitget_init(bs);		/* initialize bit getter */
/* test sync */
   in_out.in_bytes = 0;		/* assume fail */
   in_out.out_bytes = 0;
   sync = bitget(12);

   if (sync != 0xFFF)
      return in_out;		/* sync fail */
/*-----------*/

/*-- unpack side info --*/
   side_bytes = unpack_side_MPEG1(m);
   m->cupl.padframebytes = m->cupl.framebytes + m->cupl.pad;
   in_out.in_bytes = m->cupl.padframebytes;

/*-- load main data and update buf pointer --*/
/*------------------------------------------- 
   if start point < 0, must just cycle decoder 
   if jumping into middle of stream, 
w---------------------------------------------*/
   m->cupl.buf_ptr0 = m->cupl.buf_ptr1 - m->cupl.side_info.main_data_begin;	/* decode start point */
   if (m->cupl.buf_ptr1 > BUF_TRIGGER)
   {				/* shift buffer */
      memmove(m->cupl.buf, m->cupl.buf + m->cupl.buf_ptr0, m->cupl.side_info.main_data_begin);
      m->cupl.buf_ptr0 = 0;
      m->cupl.buf_ptr1 = m->cupl.side_info.main_data_begin;
   }
   nbytes = m->cupl.padframebytes - side_bytes - m->cupl.crcbytes;

   // RAK: This is no bueno. :-(
	if (nbytes < 0 || nbytes > NBUF)
	{
	    in_out.in_bytes = 0;
		 return in_out;
   }

   memmove(m->cupl.buf + m->cupl.buf_ptr1, bs + side_bytes + m->cupl.crcbytes, nbytes);
   m->cupl.buf_ptr1 += nbytes;
/*-----------------------*/

   if (m->cupl.buf_ptr0 >= 0)
   {
// dump_frame(buf+buf_ptr0, 64);
      m->cupl.main_pos_bit = m->cupl.buf_ptr0 << 3;
      unpack_main(m,pcm, 0);
      unpack_main(m,pcm + m->cupl.half_outbytes, 1);
      in_out.out_bytes = m->cupl.outbytes;
   }
   else
   {
      memset(pcm, m->cupl.zero_level_pcm, m->cupl.outbytes);	/* fill out skipped frames */
      in_out.out_bytes = m->cupl.outbytes;
/* iframe--;  in_out.out_bytes = 0;  // test test */
   }

   return in_out;
}
/*--------------------------------------------------------------------*/
/*--------------------------------------------------------------------*/
IN_OUT L3audio_decode_MPEG2(void *mv, unsigned char *bs, unsigned char *pcm)
{
   MPEG *m = mv;
   int sync;
   IN_OUT in_out;
   int side_bytes;
   int nbytes;
   static int igr = 0;

   m->cupl.iframe++;


   bitget_init(bs);		/* initialize bit getter */
/* test sync */
   in_out.in_bytes = 0;		/* assume fail */
   in_out.out_bytes = 0;
   sync = bitget(12);

// if( sync != 0xFFF ) return in_out;       /* sync fail */

   m->cupl.mpeg25_flag = 0;
   if (sync != 0xFFF)
   {
      m->cupl.mpeg25_flag = 1;		/* mpeg 2.5 sync */
      if (sync != 0xFFE)
	 return in_out;		/* sync fail */
   }
/*-----------*/


/*-- unpack side info --*/
   side_bytes = unpack_side_MPEG2(m,igr);
   m->cupl.padframebytes = m->cupl.framebytes + m->cupl.pad;
   in_out.in_bytes = m->cupl.padframebytes;

   m->cupl.buf_ptr0 = m->cupl.buf_ptr1 - m->cupl.side_info.main_data_begin;	/* decode start point */
   if (m->cupl.buf_ptr1 > BUF_TRIGGER)
   {				/* shift buffer */
      memmove(m->cupl.buf, m->cupl.buf + m->cupl.buf_ptr0, m->cupl.side_info.main_data_begin);
      m->cupl.buf_ptr0 = 0;
      m->cupl.buf_ptr1 = m->cupl.side_info.main_data_begin;
   }
   nbytes = m->cupl.padframebytes - side_bytes - m->cupl.crcbytes;
   // RAK: This is no bueno. :-(
	if (nbytes < 0 || nbytes > NBUF)
	{
	    in_out.in_bytes = 0;
		 return in_out;
   }
   memmove(m->cupl.buf + m->cupl.buf_ptr1, bs + side_bytes + m->cupl.crcbytes, nbytes);
   m->cupl.buf_ptr1 += nbytes;
/*-----------------------*/

   if (m->cupl.buf_ptr0 >= 0)
   {
      m->cupl.main_pos_bit = m->cupl.buf_ptr0 << 3;
      unpack_main(m,pcm, igr);
      in_out.out_bytes = m->cupl.outbytes;
   }
   else
   {
      memset(pcm, m->cupl.zero_level_pcm, m->cupl.outbytes);	/* fill out skipped frames */
      in_out.out_bytes = m->cupl.outbytes;
// iframe--;  in_out.out_bytes = 0; return in_out;// test test */
   }



   igr = igr ^ 1;
   return in_out;
}
/*--------------------------------------------------------------------*/
/*--------------------------------------------------------------------*/
/*--------------------------------------------------------------------*/
static int const sr_table[8] =
{22050, 24000, 16000, 1,
 44100, 48000, 32000, 1};

static const struct
{
   int l[23];
   int s[14];
}
sfBandIndexTable[3][3] =
{
/* mpeg-2 */
   {
      {
	 {
	    0, 6, 12, 18, 24, 30, 36, 44, 54, 66, 80, 96, 116, 140, 168, 200, 238, 284, 336, 396, 464, 522, 576
	 }
	 ,
	 {
	    0, 4, 8, 12, 18, 24, 32, 42, 56, 74, 100, 132, 174, 192
	 }
      }
      ,
      {
	 {
	    0, 6, 12, 18, 24, 30, 36, 44, 54, 66, 80, 96, 114, 136, 162, 194, 232, 278, 332, 394, 464, 540, 576
	 }
	 ,
	 {
	    0, 4, 8, 12, 18, 26, 36, 48, 62, 80, 104, 136, 180, 192
	 }
      }
      ,
      {
	 {
	    0, 6, 12, 18, 24, 30, 36, 44, 54, 66, 80, 96, 116, 140, 168, 200, 238, 284, 336, 396, 464, 522, 576
	 }
	 ,
	 {
	    0, 4, 8, 12, 18, 26, 36, 48, 62, 80, 104, 134, 174, 192
	 }
      }
      ,
   }
   ,
/* mpeg-1 */
   {
      {
	 {
	    0, 4, 8, 12, 16, 20, 24, 30, 36, 44, 52, 62, 74, 90, 110, 134, 162, 196, 238, 288, 342, 418, 576
	 }
	 ,
	 {
	    0, 4, 8, 12, 16, 22, 30, 40, 52, 66, 84, 106, 136, 192
	 }
      }
      ,
      {
	 {
	    0, 4, 8, 12, 16, 20, 24, 30, 36, 42, 50, 60, 72, 88, 106, 128, 156, 190, 230, 276, 330, 384, 576
	 }
	 ,
	 {
	    0, 4, 8, 12, 16, 22, 28, 38, 50, 64, 80, 100, 126, 192
	 }
      }
      ,
      {
	 {
	    0, 4, 8, 12, 16, 20, 24, 30, 36, 44, 54, 66, 82, 102, 126, 156, 194, 240, 296, 364, 448, 550, 576
	 }
	 ,
	 {
	    0, 4, 8, 12, 16, 22, 30, 42, 58, 78, 104, 138, 180, 192
	 }
      }
   }
   ,

/* mpeg-2.5, 11 & 12 KHz seem ok, 8 ok */
   {
      {
	 {
	    0, 6, 12, 18, 24, 30, 36, 44, 54, 66, 80, 96, 116, 140, 168, 200, 238, 284, 336, 396, 464, 522, 576
	 }
	 ,
	 {
	    0, 4, 8, 12, 18, 26, 36, 48, 62, 80, 104, 134, 174, 192
	 }
      }
      ,
      {
	 {
	    0, 6, 12, 18, 24, 30, 36, 44, 54, 66, 80, 96, 116, 140, 168, 200, 238, 284, 336, 396, 464, 522, 576
	 }
	 ,
	 {
	    0, 4, 8, 12, 18, 26, 36, 48, 62, 80, 104, 134, 174, 192
	 }
      }
      ,
// this 8khz table, and only 8khz, from mpeg123)
      {
	 {
	    0, 12, 24, 36, 48, 60, 72, 88, 108, 132, 160, 192, 232, 280, 336, 400, 476, 566, 568, 570, 572, 574, 576
	 }
	 ,
	 {
	    0, 8, 16, 24, 36, 52, 72, 96, 124, 160, 162, 164, 166, 192
	 }
      }
      ,
   }
   ,
};


void sbt_mono_L3(MPEG *m, float *sample, signed short *pcm, int ch);
void sbt_dual_L3(MPEG *m, float *sample, signed short *pcm, int ch);
void sbt16_mono_L3(MPEG *m, float *sample, signed short *pcm, int ch);
void sbt16_dual_L3(MPEG *m, float *sample, signed short *pcm, int ch);
void sbt8_mono_L3(MPEG *m, float *sample, signed short *pcm, int ch);
void sbt8_dual_L3(MPEG *m, float *sample, signed short *pcm, int ch);

void sbtB_mono_L3(MPEG *m, float *sample, unsigned char *pcm, int ch);
void sbtB_dual_L3(MPEG *m, float *sample, unsigned char *pcm, int ch);
void sbtB16_mono_L3(MPEG *m, float *sample, unsigned char *pcm, int ch);
void sbtB16_dual_L3(MPEG *m, float *sample, unsigned char *pcm, int ch);
void sbtB8_mono_L3(MPEG *m, float *sample, unsigned char *pcm, int ch);
void sbtB8_dual_L3(MPEG *m, float *sample, unsigned char *pcm, int ch);



static SBT_FUNCTION_F sbt_table[2][3][2] =
{
{{ (SBT_FUNCTION_F) sbt_mono_L3,
   (SBT_FUNCTION_F) sbt_dual_L3 } ,
 { (SBT_FUNCTION_F) sbt16_mono_L3,
   (SBT_FUNCTION_F) sbt16_dual_L3 } ,
 { (SBT_FUNCTION_F) sbt8_mono_L3,
   (SBT_FUNCTION_F) sbt8_dual_L3 }} ,
/*-- 8 bit output -*/
{{ (SBT_FUNCTION_F) sbtB_mono_L3,
   (SBT_FUNCTION_F) sbtB_dual_L3 },
 { (SBT_FUNCTION_F) sbtB16_mono_L3,
   (SBT_FUNCTION_F) sbtB16_dual_L3 },
 { (SBT_FUNCTION_F) sbtB8_mono_L3,
   (SBT_FUNCTION_F) sbtB8_dual_L3 }}
};


void Xform_mono(void *mv, void *pcm, int igr);
void Xform_dual(void *mv, void *pcm, int igr);
void Xform_dual_mono(void *mv, void *pcm, int igr);
void Xform_dual_right(void *mv, void *pcm, int igr);

static XFORM_FUNCTION xform_table[5] =
{
   Xform_mono,
   Xform_dual,
   Xform_dual_mono,
   Xform_mono,			/* left */
   Xform_dual_right,
};
int L3table_init(MPEG *m);
void msis_init(MPEG *m);
void sbt_init(MPEG *m);
#if 0
typedef int iARRAY22[22];
#endif
iARRAY22 *quant_init_band_addr();
iARRAY22 *msis_init_band_addr();

/*---------------------------------------------------------*/
/* mpeg_head defined in mhead.h  frame bytes is without pad */
int L3audio_decode_init(void *mv, MPEG_HEAD * h, int framebytes_arg,
		   int reduction_code, int transform_code, int convert_code,
			int freq_limit)
{
   MPEG *m = mv;
   int i, j, k;
   // static int first_pass = 1;
   int samprate;
   int limit;
   int bit_code;
   int out_chans;

   m->cupl.buf_ptr0 = 0;
   m->cupl.buf_ptr1 = 0;

/* check if code handles */
   if (h->option != 1)
      return 0;			/* layer III only */

   if (h->id)
      m->cupl.ncbl_mixed = 8;		/* mpeg-1 */
   else
      m->cupl.ncbl_mixed = 6;		/* mpeg-2 */

   m->cupl.framebytes = framebytes_arg;

   transform_code = transform_code;	/* not used, asm compatability */
   bit_code = 0;
   if (convert_code & 8)
      bit_code = 1;
   convert_code = convert_code & 3;	/* higher bits used by dec8 freq cvt */
   if (reduction_code < 0)
      reduction_code = 0;
   if (reduction_code > 2)
      reduction_code = 2;
   if (freq_limit < 1000)
      freq_limit = 1000;


   samprate = sr_table[4 * h->id + h->sr_index];
   if ((h->sync & 1) == 0)
      samprate = samprate / 2;	// mpeg 2.5 
/*----- compute nsb_limit --------*/
   m->cupl.nsb_limit = (freq_limit * 64L + samprate / 2) / samprate;
/*- caller limit -*/
   limit = (32 >> reduction_code);
   if (limit > 8)
      limit--;
   if (m->cupl.nsb_limit > limit)
      m->cupl.nsb_limit = limit;
   limit = 18 * m->cupl.nsb_limit;

   k = h->id;
   if ((h->sync & 1) == 0)
      k = 2;			// mpeg 2.5 

   if (k == 1)
   {
      m->cupl.band_limit12 = 3 * sfBandIndexTable[k][h->sr_index].s[13];
      m->cupl.band_limit = m->cupl.band_limit21 = sfBandIndexTable[k][h->sr_index].l[22];
   }
   else
   {
      m->cupl.band_limit12 = 3 * sfBandIndexTable[k][h->sr_index].s[12];
      m->cupl.band_limit = m->cupl.band_limit21 = sfBandIndexTable[k][h->sr_index].l[21];
   }
   m->cupl.band_limit += 8;		/* allow for antialias */
   if (m->cupl.band_limit > limit)
      m->cupl.band_limit = limit;

   if (m->cupl.band_limit21 > m->cupl.band_limit)
      m->cupl.band_limit21 = m->cupl.band_limit;
   if (m->cupl.band_limit12 > m->cupl.band_limit)
      m->cupl.band_limit12 = m->cupl.band_limit;


   m->cupl.band_limit_nsb = (m->cupl.band_limit + 17) / 18;	/* limit nsb's rounded up */
/*----------------------------------------------*/
   m->cupl.gain_adjust = 0;		/* adjust gain e.g. cvt to mono sum channel */
   if ((h->mode != 3) && (convert_code == 1))
      m->cupl.gain_adjust = -4;

   m->cupl.outvalues = 1152 >> reduction_code;
   if (h->id == 0)
      m->cupl.outvalues /= 2;

   out_chans = 2;
   if (h->mode == 3)
      out_chans = 1;
   if (convert_code)
      out_chans = 1;

   m->cupl.sbt_L3 = sbt_table[bit_code][reduction_code][out_chans - 1];
   k = 1 + convert_code;
   if (h->mode == 3)
      k = 0;
   m->cupl.Xform = xform_table[k];


   m->cupl.outvalues *= out_chans;

   if (bit_code)
      m->cupl.outbytes = m->cupl.outvalues;
   else
      m->cupl.outbytes = sizeof(short) * m->cupl.outvalues;

   if (bit_code)
      m->cupl.zero_level_pcm = 128;	/* 8 bit output */
   else
      m->cupl.zero_level_pcm = 0;


   m->cup.decinfo.channels = out_chans;
   m->cup.decinfo.outvalues = m->cupl.outvalues;
   m->cup.decinfo.samprate = samprate >> reduction_code;
   if (bit_code)
      m->cup.decinfo.bits = 8;
   else
      m->cup.decinfo.bits = sizeof(short) * 8;

   m->cup.decinfo.framebytes = m->cupl.framebytes;
   m->cup.decinfo.type = 0;

   m->cupl.half_outbytes = m->cupl.outbytes / 2;
/*------------------------------------------*/

/*- init band tables --*/


   k = h->id;
   if ((h->sync & 1) == 0)
      k = 2;			// mpeg 2.5 

   for (i = 0; i < 22; i++)
      m->cupl.sfBandIndex[0][i] = sfBandIndexTable[k][h->sr_index].l[i + 1];
   for (i = 0; i < 13; i++)
      m->cupl.sfBandIndex[1][i] = 3 * sfBandIndexTable[k][h->sr_index].s[i + 1];
   for (i = 0; i < 22; i++)
      m->cupl.nBand[0][i] =
	 sfBandIndexTable[k][h->sr_index].l[i + 1]
	 - sfBandIndexTable[k][h->sr_index].l[i];
   for (i = 0; i < 13; i++)
      m->cupl.nBand[1][i] =
	 sfBandIndexTable[k][h->sr_index].s[i + 1]
	 - sfBandIndexTable[k][h->sr_index].s[i];


/* init tables */
   L3table_init(m);
/* init ms and is stereo modes */
   msis_init(m);

/*----- init sbt ---*/
   sbt_init(m);



/*--- clear buffers --*/
   for (i = 0; i < 576; i++)
      m->cupl.yout[i] = 0.0f;
   for (j = 0; j < 2; j++)
   {
      for (k = 0; k < 2; k++)
      {
	 for (i = 0; i < 576; i++)
	 {
	    m->cupl.sample[j][k][i].x = 0.0f;
	    m->cupl.sample[j][k][i].s = 0;
	 }
      }
   }

   if (h->id == 1)
      m->cupl.decode_function = L3audio_decode_MPEG1;
   else
      m->cupl.decode_function = L3audio_decode_MPEG2;

   return 1;
}
/*---------------------------------------------------------*/
/*==========================================================*/
void cup3_init(MPEG *m)
{
	m->cupl.sbt_L3 = sbt_dual_L3;
	m->cupl.Xform = Xform_dual;
	m->cupl.sbt_L3 = sbt_dual_L3;
	m->cupl.decode_function = L3audio_decode_MPEG1;
}
