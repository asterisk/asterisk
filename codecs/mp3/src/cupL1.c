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

/****  cupL1.c  ***************************************************

MPEG audio decoder Layer I mpeg1 and mpeg2

include to clup.c


******************************************************************/
/*======================================================================*/


/* Read Only */
static int bat_bit_masterL1[] =
{
   0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

/*======================================================================*/
static void unpack_baL1(MPEG *m)
{
   int j;
   int nstereo;

   m->cup.bit_skip = 0;
   nstereo = m->cup.stereo_sb;

   for (j = 0; j < m->cup.nbatL1; j++)
   {
      mac_load_check(4);
      m->cup.ballo[j] = m->cup.samp_dispatch[j] = mac_load(4);
      if (j >= m->cup.nsb_limit)
	 m->cup.bit_skip += bat_bit_masterL1[m->cup.samp_dispatch[j]];
      m->cup.c_value[j] = m->cup.look_c_valueL1[m->cup.samp_dispatch[j]];
      if (--nstereo < 0)
      {
	 m->cup.ballo[j + 1] = m->cup.ballo[j];
	 m->cup.samp_dispatch[j] += 15;	/* flag as joint */
	 m->cup.samp_dispatch[j + 1] = m->cup.samp_dispatch[j];	/* flag for sf */
	 m->cup.c_value[j + 1] = m->cup.c_value[j];
	 j++;
      }
   }
/*-- terminate with bit skip and end --*/
   m->cup.samp_dispatch[m->cup.nsb_limit] = 31;
   m->cup.samp_dispatch[j] = 30;
}
/*-------------------------------------------------------------------------*/
static void unpack_sfL1(MPEG *m)	/* unpack scale factor */
{				/* combine dequant and scale factors */
   int i;

   for (i = 0; i < m->cup.nbatL1; i++)
   {
      if (m->cup.ballo[i])
      {
	 mac_load_check(6);
	 m->cup.cs_factorL1[i] = m->cup.c_value[i] * m->cup.sf_table[mac_load(6)];
      }
   }
/*-- done --*/
}
/*-------------------------------------------------------------------------*/
#define UNPACKL1_N(n) s[k]     =  m->cup.cs_factorL1[k]*(load(m,n)-((1 << (n-1)) -1));  \
    goto dispatch;
#define UNPACKL1J_N(n) tmp        =  (load(m,n)-((1 << (n-1)) -1));                 \
    s[k]       =  m->cup.cs_factorL1[k]*tmp;                        \
    s[k+1]     =  m->cup.cs_factorL1[k+1]*tmp;                      \
    k++;                                                     \
    goto dispatch;
/*-------------------------------------------------------------------------*/
static void unpack_sampL1(MPEG *m)	/* unpack samples */
{
   int j, k;
   float *s;
   long tmp;

   s = m->cup.sample;
   for (j = 0; j < 12; j++)
   {
      k = -1;
    dispatch:switch (m->cup.samp_dispatch[++k])
      {
	 case 0:
	    s[k] = 0.0F;
	    goto dispatch;
	 case 1:
	    UNPACKL1_N(2)	/*  3 levels */
	 case 2:
	    UNPACKL1_N(3)	/*  7 levels */
	 case 3:
	    UNPACKL1_N(4)	/* 15 levels */
	 case 4:
	    UNPACKL1_N(5)	/* 31 levels */
	 case 5:
	    UNPACKL1_N(6)	/* 63 levels */
	 case 6:
	    UNPACKL1_N(7)	/* 127 levels */
	 case 7:
	    UNPACKL1_N(8)	/* 255 levels */
	 case 8:
	    UNPACKL1_N(9)	/* 511 levels */
	 case 9:
	    UNPACKL1_N(10)	/* 1023 levels */
	 case 10:
	    UNPACKL1_N(11)	/* 2047 levels */
	 case 11:
	    UNPACKL1_N(12)	/* 4095 levels */
	 case 12:
	    UNPACKL1_N(13)	/* 8191 levels */
	 case 13:
	    UNPACKL1_N(14)	/* 16383 levels */
	 case 14:
	    UNPACKL1_N(15)	/* 32767 levels */
/* -- joint ---- */
	 case 15 + 0:
	    s[k + 1] = s[k] = 0.0F;
	    k++;		/* skip right chan dispatch */
	    goto dispatch;
/* -- joint ---- */
	 case 15 + 1:
	    UNPACKL1J_N(2)	/*  3 levels */
	 case 15 + 2:
	    UNPACKL1J_N(3)	/*  7 levels */
	 case 15 + 3:
	    UNPACKL1J_N(4)	/* 15 levels */
	 case 15 + 4:
	    UNPACKL1J_N(5)	/* 31 levels */
	 case 15 + 5:
	    UNPACKL1J_N(6)	/* 63 levels */
	 case 15 + 6:
	    UNPACKL1J_N(7)	/* 127 levels */
	 case 15 + 7:
	    UNPACKL1J_N(8)	/* 255 levels */
	 case 15 + 8:
	    UNPACKL1J_N(9)	/* 511 levels */
	 case 15 + 9:
	    UNPACKL1J_N(10)	/* 1023 levels */
	 case 15 + 10:
	    UNPACKL1J_N(11)	/* 2047 levels */
	 case 15 + 11:
	    UNPACKL1J_N(12)	/* 4095 levels */
	 case 15 + 12:
	    UNPACKL1J_N(13)	/* 8191 levels */
	 case 15 + 13:
	    UNPACKL1J_N(14)	/* 16383 levels */
	 case 15 + 14:
	    UNPACKL1J_N(15)	/* 32767 levels */

/* -- end of dispatch -- */
	 case 31:
	    skip(m,m->cup.bit_skip);
	 case 30:
	    s += 64;
      }				/* end switch */
   }				/* end j loop */

/*-- done --*/
}
/*-------------------------------------------------------------------*/
IN_OUT L1audio_decode(void *mv, unsigned char *bs, signed short *pcm)
{
   MPEG *m = mv;
   int sync, prot;
   IN_OUT in_out;

   load_init(m, bs);		/* initialize bit getter */
/* test sync */
   in_out.in_bytes = 0;		/* assume fail */
   in_out.out_bytes = 0;
   sync = load(m,12);
   if (sync != 0xFFF)
      return in_out;		/* sync fail */


   load(m,3);			/* skip id and option (checked by init) */
   prot = load(m,1);		/* load prot bit */
   load(m,6);			/* skip to pad */
   m->cup.pad = (load(m,1)) << 2;
   load(m,1);			/* skip to mode */
   m->cup.stereo_sb = look_joint[load(m,4)];
   if (prot)
      load(m,4);			/* skip to data */
   else
      load(m,20);			/* skip crc */

   unpack_baL1(m);		/* unpack bit allocation */
   unpack_sfL1(m);		/* unpack scale factor */
   unpack_sampL1(m);		/* unpack samples */

   m->cup.sbt(m, m->cup.sample, pcm, 12);
/*-----------*/
   in_out.in_bytes = m->cup.framebytes + m->cup.pad;
   in_out.out_bytes = m->cup.outbytes;

   return in_out;
}
/*-------------------------------------------------------------------------*/
int L1audio_decode_init(MPEG *m, MPEG_HEAD * h, int framebytes_arg,
		   int reduction_code, int transform_code, int convert_code,
			int freq_limit)
{
   int i, k;
   long samprate;
   int limit;
   long step;
   int bit_code;

/*--- sf init done by layer II init ---*/
   if (m->cup.first_pass_L1)
   {
      for (step = 4, i = 1; i < 16; i++, step <<= 1)
	 m->cup.look_c_valueL1[i] = (float) (2.0 / (step - 1));
      m->cup.first_pass_L1 = 0;
   }

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


   m->cup.framebytes = framebytes_arg;
/* check if code handles */
   if (h->option != 3)
      return 0;			/* layer I only */

   m->cup.nbatL1 = 32;
   m->cup.max_sb = m->cup.nbatL1;
/*----- compute nsb_limit --------*/
   samprate = sr_table[4 * h->id + h->sr_index];
   m->cup.nsb_limit = (freq_limit * 64L + samprate / 2) / samprate;
/*- caller limit -*/
/*---- limit = 0.94*(32>>reduction_code);  ----*/
   limit = (32 >> reduction_code);
   if (limit > 8)
      limit--;
   if (m->cup.nsb_limit > limit)
      m->cup.nsb_limit = limit;
   if (m->cup.nsb_limit > m->cup.max_sb)
      m->cup.nsb_limit = m->cup.max_sb;

   m->cup.outvalues = 384 >> reduction_code;
   if (h->mode != 3)
   {				/* adjust for 2 channel modes */
      m->cup.nbatL1 *= 2;
      m->cup.max_sb *= 2;
      m->cup.nsb_limit *= 2;
   }

/* set sbt function */
   k = 1 + convert_code;
   if (h->mode == 3)
   {
      k = 0;
   }
   m->cup.sbt = sbt_table[bit_code][reduction_code][k];
   m->cup.outvalues *= out_chans[k];

   if (bit_code)
      m->cup.outbytes = m->cup.outvalues;
   else
      m->cup.outbytes = sizeof(short) * m->cup.outvalues;

   m->cup.decinfo.channels = out_chans[k];
   m->cup.decinfo.outvalues = m->cup.outvalues;
   m->cup.decinfo.samprate = samprate >> reduction_code;
   if (bit_code)
      m->cup.decinfo.bits = 8;
   else
      m->cup.decinfo.bits = sizeof(short) * 8;

   m->cup.decinfo.framebytes = m->cup.framebytes;
   m->cup.decinfo.type = 0;


/* clear sample buffer, unused sub bands must be 0 */
   for (i = 0; i < 768; i++)
      m->cup.sample[i] = 0.0F;


/* init sub-band transform */
   sbt_init();

   return 1;
}
/*---------------------------------------------------------*/
