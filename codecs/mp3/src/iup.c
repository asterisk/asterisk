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

/****  iup.c  ***************************************************

MPEG audio decoder Layer I/II, mpeg1 and mpeg2
should be portable ANSI C, should be endian independent

icup integer version of cup.c



mod 10/18/95   mod grouped sample unpack for:
               Overflow possible in grouped if native int is 16 bit.
               Rare occurance.  16x16-->32 mult needed.

mods 11/15/95 for Layer I


1/5/95    Quick fix,  cs_factor can overflow int16 (why?).  Typed to int32.

mods 1/7/97 warnings

******************************************************************/
/******************************************************************

       MPEG audio software decoder portable ANSI c.
       Decodes all Layer II to 16 bit linear pcm.
       Optional stereo to mono conversion.  Optional
       output sample rate conversion to half or quarter of
       native mpeg rate.

-------------------------------------
int i_audio_decode_init(MPEG *m, MPEG_HEAD *h, int framebytes_arg,
         int reduction_code, int transform_code, int convert_code,
         int freq_limit)

initilize decoder:
       return 0 = fail, not 0 = success

MPEG_HEAD *h    input, mpeg header info (returned by call to head_info)
framebytes      input, mpeg frame size (returned by call to head_info)
reduction_code  input, sample rate reduction code
                    0 = full rate
                    1 = half rate
                    0 = quarter rate

transform_code  input, ignored
convert_code    input, channel conversion
                  convert_code:  0 = two chan output
                                 1 = convert two chan to mono
                                 2 = convert two chan to left chan
                                 3 = convert two chan to right chan
freq_limit      input, limits bandwidth of pcm output to specified
                frequency.  Special use. Set to 24000 for normal use.


---------------------------------
void i_audio_decode_info( MPEG *m, DEC_INFO *info)

information return:
          Call after audio_decode_init.  See mhead.h for
          information returned in DEC_INFO structure.


---------------------------------
IN_OUT i_audio_decode(MPEG *m, unsigned char *bs, void *pcmbuf)

decode one mpeg audio frame:
bs        input, mpeg bitstream, must start with
          sync word.  Caution: may read up to 3 bytes
          beyond end of frame.
pcmbuf    output, pcm samples.

IN_OUT structure returns:
          Number bytes conceptually removed from mpeg bitstream.
          Returns 0 if sync loss.
          Number bytes of pcm output.

*******************************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <float.h>
#include <math.h>
#include "L3.h"
#include "mhead.h"		/* mpeg header structure */
#include "jdw.h"

/*-------------------------------------------------------
NOTE:  Decoder may read up to three bytes beyond end of
frame.  Calling application must ensure that this does
not cause a memory access violation (protection fault)
---------------------------------------------------------*/


#ifdef _MSC_VER
#pragma warning(disable: 4709)
#endif


/* Okay to be global -- is read/only */
static int look_joint[16] =
{				/* lookup stereo sb's by mode+ext */
   64, 64, 64, 64,		/* stereo */
   2 * 4, 2 * 8, 2 * 12, 2 * 16,	/* joint */
   64, 64, 64, 64,		/* dual */
   32, 32, 32, 32,		/* mono */
};

/* Okay to be global -- is read/only */
static int bat_bit_master[] =
{
   0, 5, 7, 9, 10, 12, 15, 18, 21, 24, 27, 30, 33, 36, 39, 42, 45, 48};

void i_sbt_mono(SAMPLEINT * sample, short *pcm, int n);
void i_sbt_dual(SAMPLEINT * sample, short *pcm, int n);

static void unpack();

/*------------- initialize bit getter -------------*/
static void load_init(MPEGI *m, unsigned char *buf)
{
   m->iup.bs_ptr = buf;
   m->iup.bits = 0;
   m->iup.bitbuf = 0;
}
/*------------- get n bits from bitstream -------------*/
static INT32 load(MPEGI *m, int n)
{
   UINT32 x;

   if (m->iup.bits < n)
   {				/* refill bit buf if necessary */
      while (m->iup.bits <= 24)
      {
	 m->iup.bitbuf = (m->iup.bitbuf << 8) | *m->iup.bs_ptr++;
	 m->iup.bits += 8;
      }
   }
   m->iup.bits -= n;
   x = m->iup.bitbuf >> m->iup.bits;
   m->iup.bitbuf -= x << m->iup.bits;
   return x;
}
/*------------- skip over n bits in bitstream -------------*/
static void skip(MPEGI *m, int n)
{
   int k;

   if (m->iup.bits < n)
   {
      n -= m->iup.bits;
      k = n >> 3;
/*--- bytes = n/8 --*/
      m->iup.bs_ptr += k;
      n -= k << 3;
      m->iup.bitbuf = *m->iup.bs_ptr++;
      m->iup.bits = 8;
   }
   m->iup.bits -= n;
   m->iup.bitbuf -= (m->iup.bitbuf >> m->iup.bits) << m->iup.bits;
}
/*--------------------------------------------------------------*/
#define mac_load_check(n)                     \
   if( m->iup.bits < (n) ) {                           \
          while( m->iup.bits <= 24 ) {               \
             m->iup.bitbuf = (m->iup.bitbuf << 8) | *m->iup.bs_ptr++;  \
             m->iup.bits += 8;                       \
          }                                   \
   }
/*--------------------------------------------------------------*/
#define mac_load(n)                    \
       ( m->iup.bits -= n,                    \
         m->iup.bitval = m->iup.bitbuf >> m->iup.bits,      \
         m->iup.bitbuf -= m->iup.bitval << m->iup.bits,     \
         m->iup.bitval )
/*======================================================================*/
static void unpack_ba(MPEGI *m)
{
   int i, j, k;
   static int nbit[4] =
   {4, 4, 3, 2};
   int nstereo;
   int n;

   m->iup.bit_skip = 0;
   nstereo = m->iup.stereo_sb;
   k = 0;
   for (i = 0; i < 4; i++)
   {
      for (j = 0; j < m->iup.nbat[i]; j++, k++)
      {
	 mac_load_check(4);
	 n = m->iup.ballo[k] = m->iup.samp_dispatch[k] = m->iup.bat[i][mac_load(nbit[i])];
	 if (k >= m->iup.nsb_limit)
	    m->iup.bit_skip += bat_bit_master[m->iup.samp_dispatch[k]];
	 m->iup.c_value[k] = m->iup.look_c_value[n];
	 m->iup.c_shift[k] = m->iup.look_c_shift[n];
	 if (--nstereo < 0)
	 {
	    m->iup.ballo[k + 1] = m->iup.ballo[k];
	    m->iup.samp_dispatch[k] += 18;	/* flag as joint */
	    m->iup.samp_dispatch[k + 1] = m->iup.samp_dispatch[k];	/* flag for sf */
	    m->iup.c_value[k + 1] = m->iup.c_value[k];
	    m->iup.c_shift[k + 1] = m->iup.c_shift[k];
	    k++;
	    j++;
	 }
      }
   }
   m->iup.samp_dispatch[m->iup.nsb_limit] = 37;	/* terminate the dispatcher with skip */
   m->iup.samp_dispatch[k] = 36;	/* terminate the dispatcher */

}
/*-------------------------------------------------------------------------*/
static void unpack_sfs(MPEGI *m)	/* unpack scale factor selectors */
{
   int i;

   for (i = 0; i < m->iup.max_sb; i++)
   {
      mac_load_check(2);
      if (m->iup.ballo[i])
	 m->iup.sf_dispatch[i] = mac_load(2);
      else
	 m->iup.sf_dispatch[i] = 4;	/* no allo */
   }
   m->iup.sf_dispatch[i] = 5;		/* terminate dispatcher */
}
/*-------------------------------------------------------------------------*/
/*--- multiply note -------------------------------------------------------*/
/*--- 16bit x 16bit mult --> 32bit >> 15 --> 16 bit  or better  -----------*/
static void unpack_sf(MPEGI *m)		/* unpack scale factor */
{				/* combine dequant and scale factors */
   int i, n;
   INT32 tmp;			/* only reason tmp is 32 bit is to get 32 bit mult result */

   i = -1;
 dispatch:switch (m->iup.sf_dispatch[++i])
   {
      case 0:			/* 3 factors 012 */
	 mac_load_check(18);
	 tmp = m->iup.c_value[i];
	 n = m->iup.c_shift[i];
	 m->iup.cs_factor[0][i] = (tmp * m->iup.sf_table[mac_load(6)]) >> n;
	 m->iup.cs_factor[1][i] = (tmp * m->iup.sf_table[mac_load(6)]) >> n;
	 m->iup.cs_factor[2][i] = (tmp * m->iup.sf_table[mac_load(6)]) >> n;
	 goto dispatch;
      case 1:			/* 2 factors 002 */
	 mac_load_check(12);
	 tmp = m->iup.c_value[i];
	 n = m->iup.c_shift[i];
	 m->iup.cs_factor[1][i] = m->iup.cs_factor[0][i] =
	    (tmp * m->iup.sf_table[mac_load(6)]) >> n;
	 m->iup.cs_factor[2][i] = (tmp * m->iup.sf_table[mac_load(6)]) >> n;
	 goto dispatch;
      case 2:			/* 1 factor 000 */
	 mac_load_check(6);
	 tmp = m->iup.c_value[i];
	 n = m->iup.c_shift[i];
	 m->iup.cs_factor[2][i] = m->iup.cs_factor[1][i] = m->iup.cs_factor[0][i] =
	    (tmp * m->iup.sf_table[mac_load(6)]) >> n;
	 goto dispatch;
      case 3:			/* 2 factors 022 */
	 mac_load_check(12);
	 tmp = m->iup.c_value[i];
	 n = m->iup.c_shift[i];
	 m->iup.cs_factor[0][i] = (tmp * m->iup.sf_table[mac_load(6)]) >> n;
	 m->iup.cs_factor[2][i] = m->iup.cs_factor[1][i] =
	    (tmp * m->iup.sf_table[mac_load(6)]) >> n;
	 goto dispatch;
      case 4:			/* no allo */
	 goto dispatch;
      case 5:			/* all done */
	 ;
   }				/* end switch */
}
/*-------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------*/
/*--- unpack multiply note ------------------------------------------------*/
/*--- 16bit x 16bit mult --> 32bit  or better required---------------------*/
#define UNPACK_N(n)                                          \
    s[k]     =  ((m->iup.cs_factor[i][k]*(load(m,n)-((1 << (n-1)) -1)))>>(n-1));   \
    s[k+64]  =  ((m->iup.cs_factor[i][k]*(load(m,n)-((1 << (n-1)) -1)))>>(n-1));   \
    s[k+128] =  ((m->iup.cs_factor[i][k]*(load(m,n)-((1 << (n-1)) -1)))>>(n-1));   \
    goto dispatch;
#define UNPACK_N2(n)                                             \
    mac_load_check(3*n);                                         \
    s[k]     =  (m->iup.cs_factor[i][k]*(mac_load(n)-((1 << (n-1)) -1)))>>(n-1);   \
    s[k+64]  =  (m->iup.cs_factor[i][k]*(mac_load(n)-((1 << (n-1)) -1)))>>(n-1);   \
    s[k+128] =  (m->iup.cs_factor[i][k]*(mac_load(n)-((1 << (n-1)) -1)))>>(n-1);   \
    goto dispatch;
#define UNPACK_N3(n)                                             \
    mac_load_check(2*n);                                         \
    s[k]     =  (m->iup.cs_factor[i][k]*(mac_load(n)-((1 << (n-1)) -1)))>>(n-1);   \
    s[k+64]  =  (m->iup.cs_factor[i][k]*(mac_load(n)-((1 << (n-1)) -1)))>>(n-1);   \
    mac_load_check(n);                                           \
    s[k+128] =  (m->iup.cs_factor[i][k]*(mac_load(n)-((1 << (n-1)) -1)))>>(n-1);   \
    goto dispatch;
#define UNPACKJ_N(n)                                         \
    tmp        =  (load(m,n)-((1 << (n-1)) -1));                 \
    s[k]       =  (m->iup.cs_factor[i][k]*tmp)>>(n-1);                       \
    s[k+1]     =  (m->iup.cs_factor[i][k+1]*tmp)>>(n-1);                     \
    tmp        =  (load(m,n)-((1 << (n-1)) -1));                 \
    s[k+64]    =  (m->iup.cs_factor[i][k]*tmp)>>(n-1);                       \
    s[k+64+1]  =  (m->iup.cs_factor[i][k+1]*tmp)>>(n-1);                     \
    tmp        =  (load(m,n)-((1 << (n-1)) -1));                 \
    s[k+128]   =  (m->iup.cs_factor[i][k]*tmp)>>(n-1);                       \
    s[k+128+1] =  (m->iup.cs_factor[i][k+1]*tmp)>>(n-1);                     \
    k++;       /* skip right chan dispatch */                \
    goto dispatch;
/*-------------------------------------------------------------------------*/
static void unpack_samp(MPEGI *m)	/* unpack samples */
{
   int i, j, k;
   SAMPLEINT *s;
   int n;
   INT32 tmp;

   s = m->iup.sample;
   for (i = 0; i < 3; i++)
   {				/* 3 groups of scale factors */
      for (j = 0; j < 4; j++)
      {
	 k = -1;
       dispatch:switch (m->iup.samp_dispatch[++k])
	 {
	    case 0:
	       s[k + 128] = s[k + 64] = s[k] = 0;
	       goto dispatch;
	    case 1:		/* 3 levels grouped 5 bits */
	       mac_load_check(5);
	       n = mac_load(5);
	       s[k] = ((INT32) m->iup.cs_factor[i][k] * m->iup.group3_table[n][0]) >> 1;
	       s[k + 64] = ((INT32) m->iup.cs_factor[i][k] * m->iup.group3_table[n][1]) >> 1;
	       s[k + 128] = ((INT32) m->iup.cs_factor[i][k] * m->iup.group3_table[n][2]) >> 1;
	       goto dispatch;
	    case 2:		/* 5 levels grouped 7 bits */
	       mac_load_check(7);
	       n = mac_load(7);
	       s[k] = ((INT32) m->iup.cs_factor[i][k] * m->iup.group5_table[n][0]) >> 2;
	       s[k + 64] = ((INT32) m->iup.cs_factor[i][k] * m->iup.group5_table[n][1]) >> 2;
	       s[k + 128] = ((INT32) m->iup.cs_factor[i][k] * m->iup.group5_table[n][2]) >> 2;
	       goto dispatch;
	    case 3:
	       UNPACK_N2(3)	/* 7 levels */
	    case 4:		/* 9 levels grouped 10 bits */
	       mac_load_check(10);
	       n = mac_load(10);
	       s[k] = ((INT32) m->iup.cs_factor[i][k] * m->iup.group9_table[n][0]) >> 3;
	       s[k + 64] = ((INT32) m->iup.cs_factor[i][k] * m->iup.group9_table[n][1]) >> 3;
	       s[k + 128] = ((INT32) m->iup.cs_factor[i][k] * m->iup.group9_table[n][2]) >> 3;
	       goto dispatch;
	    case 5:
	       UNPACK_N2(4)	/* 15 levels */
	    case 6:
	       UNPACK_N2(5)	/* 31 levels */
	    case 7:
	       UNPACK_N2(6)	/* 63 levels */
	    case 8:
	       UNPACK_N2(7)	/* 127 levels */
	    case 9:
	       UNPACK_N2(8)	/* 255 levels */
	    case 10:
	       UNPACK_N3(9)	/* 511 levels */
	    case 11:
	       UNPACK_N3(10)	/* 1023 levels */
	    case 12:
	       UNPACK_N3(11)	/* 2047 levels */
	    case 13:
	       UNPACK_N3(12)	/* 4095 levels */
	    case 14:
	       UNPACK_N(13)	/* 8191 levels */
	    case 15:
	       UNPACK_N(14)	/* 16383 levels */
	    case 16:
	       UNPACK_N(15)	/* 32767 levels */
	    case 17:
	       UNPACK_N(16)	/* 65535 levels */
/* -- joint ---- */
	    case 18 + 0:
	       s[k + 128 + 1] = s[k + 128] = s[k + 64 + 1] = s[k + 64] = s[k + 1] = s[k] = 0;
	       k++;		/* skip right chan dispatch */
	       goto dispatch;
	    case 18 + 1:	/* 3 levels grouped 5 bits */
	       n = load(m,5);
	       s[k] = ((INT32) m->iup.cs_factor[i][k] * m->iup.group3_table[n][0]) >> 1;
	       s[k + 1] = ((INT32) m->iup.cs_factor[i][k + 1] * m->iup.group3_table[n][0]) >> 1;
	       s[k + 64] = ((INT32) m->iup.cs_factor[i][k] * m->iup.group3_table[n][1]) >> 1;
	       s[k + 64 + 1] = ((INT32) m->iup.cs_factor[i][k + 1] * m->iup.group3_table[n][1]) >> 1;
	       s[k + 128] = ((INT32) m->iup.cs_factor[i][k] * m->iup.group3_table[n][2]) >> 1;
	       s[k + 128 + 1] = ((INT32) m->iup.cs_factor[i][k + 1] * m->iup.group3_table[n][2]) >> 1;
	       k++;		/* skip right chan dispatch */
	       goto dispatch;
	    case 18 + 2:	/* 5 levels grouped 7 bits */
	       n = load(m,7);
	       s[k] = ((INT32) m->iup.cs_factor[i][k] * m->iup.group5_table[n][0]) >> 2;
	       s[k + 1] = ((INT32) m->iup.cs_factor[i][k + 1] * m->iup.group5_table[n][0]) >> 2;
	       s[k + 64] = ((INT32) m->iup.cs_factor[i][k] * m->iup.group5_table[n][1]) >> 2;
	       s[k + 64 + 1] = ((INT32) m->iup.cs_factor[i][k + 1] * m->iup.group5_table[n][1]) >> 2;
	       s[k + 128] = ((INT32) m->iup.cs_factor[i][k] * m->iup.group5_table[n][2]) >> 2;
	       s[k + 128 + 1] = ((INT32) m->iup.cs_factor[i][k + 1] * m->iup.group5_table[n][2]) >> 2;
	       k++;		/* skip right chan dispatch */
	       goto dispatch;
	    case 18 + 3:
	       UNPACKJ_N(3)	/* 7 levels */
	    case 18 + 4:	/* 9 levels grouped 10 bits */
	       n = load(m,10);
	       s[k] = ((INT32) m->iup.cs_factor[i][k] * m->iup.group9_table[n][0]) >> 3;
	       s[k + 1] = ((INT32) m->iup.cs_factor[i][k + 1] * m->iup.group9_table[n][0]) >> 3;
	       s[k + 64] = ((INT32) m->iup.cs_factor[i][k] * m->iup.group9_table[n][1]) >> 3;
	       s[k + 64 + 1] = ((INT32) m->iup.cs_factor[i][k + 1] * m->iup.group9_table[n][1]) >> 3;
	       s[k + 128] = ((INT32) m->iup.cs_factor[i][k] * m->iup.group9_table[n][2]) >> 3;
	       s[k + 128 + 1] = ((INT32) m->iup.cs_factor[i][k + 1] * m->iup.group9_table[n][2]) >> 3;
	       k++;		/* skip right chan dispatch */
	       goto dispatch;
	    case 18 + 5:
	       UNPACKJ_N(4)	/* 15 levels */
	    case 18 + 6:
	       UNPACKJ_N(5)	/* 31 levels */
	    case 18 + 7:
	       UNPACKJ_N(6)	/* 63 levels */
	    case 18 + 8:
	       UNPACKJ_N(7)	/* 127 levels */
	    case 18 + 9:
	       UNPACKJ_N(8)	/* 255 levels */
	    case 18 + 10:
	       UNPACKJ_N(9)	/* 511 levels */
	    case 18 + 11:
	       UNPACKJ_N(10)	/* 1023 levels */
	    case 18 + 12:
	       UNPACKJ_N(11)	/* 2047 levels */
	    case 18 + 13:
	       UNPACKJ_N(12)	/* 4095 levels */
	    case 18 + 14:
	       UNPACKJ_N(13)	/* 8191 levels */
	    case 18 + 15:
	       UNPACKJ_N(14)	/* 16383 levels */
	    case 18 + 16:
	       UNPACKJ_N(15)	/* 32767 levels */
	    case 18 + 17:
	       UNPACKJ_N(16)	/* 65535 levels */
/* -- end of dispatch -- */
	    case 37:
	       skip(m, m->iup.bit_skip);
	    case 36:
	       s += 3 * 64;
	 }			/* end switch */
      }				/* end j loop */
   }				/* end i loop */


}
/*-------------------------------------------------------------------------*/
static void unpack(MPEGI *m)
{
   int prot;

/* at entry bit getter points at id, sync skipped by caller */

   load(m,3);			/* skip id and option (checked by init) */
   prot = load(m,1);		/* load prot bit */
   load(m,6);			/* skip to pad */
   m->iup.pad = load(m,1);
   load(m,1);			/* skip to mode */
   m->iup.stereo_sb = look_joint[load(m,4)];
   if (prot)
      load(m,4);			/* skip to data */
   else
      load(m,20);			/* skip crc */

   unpack_ba(m);			/* unpack bit allocation */
   unpack_sfs(m);		/* unpack scale factor selectors */
   unpack_sf(m);			/* unpack scale factor */
   unpack_samp(m);		/* unpack samples */


}
/*-------------------------------------------------------------------------*/
IN_OUT i_audio_decode(MPEGI *m, unsigned char *bs, signed short *pcm)
{
   int sync;
   IN_OUT in_out;

   load_init(m,bs);		/* initialize bit getter */
/* test sync */
   in_out.in_bytes = 0;		/* assume fail */
   in_out.out_bytes = 0;
   sync = load(m,12);

   if (sync != 0xFFF)
      return in_out;		/* sync fail */
/*-----------*/
   m->iup.unpack_routine();


   m->iup.sbt(m->iup.sample, pcm, m->iup.nsbt);
/*-----------*/
   in_out.in_bytes = m->iup.framebytes + m->iup.pad;
   in_out.out_bytes = m->iup.outbytes;
   return in_out;
}
/*-------------------------------------------------------------------------*/
#include "iupini.c"		/* initialization */
#include "iupL1.c"		/* Layer 1 */
/*-------------------------------------------------------------------------*/
