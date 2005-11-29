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

/****  iupL1.c  ***************************************************

MPEG audio decoder Layer I mpeg1 and mpeg2
should be portable ANSI C, should be endian independent

icupL1 integer version of cupL1.c

******************************************************************/
/*======================================================================*/
/* Read Only */
static int bat_bit_masterL1[] =
{
   0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

/*======================================================================*/
static void unpack_baL1(MPEGI *m)
{
   int k;
   int nstereo;
   int n;

   m->iup.bit_skip = 0;
   nstereo = m->iup.stereo_sb;
   for (k = 0; k < m->iup.nbatL1; k++)
   {
      mac_load_check(4);
      n = m->iup.ballo[k] = m->iup.samp_dispatch[k] = mac_load(4);
      if (k >= m->iup.nsb_limit)
	 m->iup.bit_skip += bat_bit_masterL1[m->iup.samp_dispatch[k]];
      m->iup.c_value[k] = m->iup.look_c_valueL1[n];
      m->iup.c_shift[k] = m->iup.look_c_shiftL1[n];
      if (--nstereo < 0)
      {
	 m->iup.ballo[k + 1] = m->iup.ballo[k];
	 m->iup.samp_dispatch[k] += 15;	/* flag as joint */
	 m->iup.samp_dispatch[k + 1] = m->iup.samp_dispatch[k];	/* flag for sf */
	 m->iup.c_value[k + 1] = m->iup.c_value[k];
	 m->iup.c_shift[k + 1] = m->iup.c_shift[k];
	 k++;
      }
   }
   m->iup.samp_dispatch[m->iup.nsb_limit] = 31;	/* terminate the dispatcher with skip */
   m->iup.samp_dispatch[k] = 30;	/* terminate the dispatcher */

}
/*-------------------------------------------------------------------------*/
static void unpack_sfL1(MPEGI *m)	/* unpack scale factor */
{				/* combine dequant and scale factors */
   int i, n;
   INT32 tmp;			/* only reason tmp is 32 bit is to get 32 bit mult result */

   for (i = 0; i < m->iup.nbatL1; i++)
   {
      if (m->iup.ballo[i])
      {
	 mac_load_check(6);
	 tmp = m->iup.c_value[i];
	 n = m->iup.c_shift[i];
	 m->iup.cs_factorL1[i] = (tmp * m->iup.sf_table[mac_load(6)]) >> n;
      }
   }

/*-- done --*/
}
/*-------------------------------------------------------------------------*/
#define UNPACKL1_N(n)  s[k]     =  (m->iup.cs_factorL1[k]*(load(m,n)-((1 << (n-1)) -1)))>>(n-1);   \
    goto dispatch;
#define UNPACKL1J_N(n) tmp        =  (load(m,n)-((1 << (n-1)) -1));                 \
    s[k]       =  (m->iup.cs_factorL1[k]*tmp)>>(n-1);               \
    s[k+1]     =  (m->iup.cs_factorL1[k+1]*tmp)>>(n-1);             \
    k++;       /* skip right chan dispatch */                \
    goto dispatch;
/*-------------------------------------------------------------------------*/
static void unpack_sampL1(MPEGI *m)	/* unpack samples */
{
   int j, k;
   SAMPLEINT *s;
   INT32 tmp;

   s = m->iup.sample;
   for (j = 0; j < 12; j++)
   {
      k = -1;
    dispatch:switch (m->iup.samp_dispatch[++k])
      {
	 case 0:
	    s[k] = 0;
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
	    s[k + 1] = s[k] = 0;
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
	    skip(m, m->iup.bit_skip);
	 case 30:
	    s += 64;
      }				/* end switch */
   }				/* end j loop */

/*-- done --*/
}
/*-------------------------------------------------------------------------*/
static void unpackL1(MPEGI *m)
{
   int prot;

/* at entry bit getter points at id, sync skipped by caller */

   load(m,3);			/* skip id and option (checked by init) */
   prot = load(m,1);		/* load prot bit */
   load(m,6);			/* skip to pad */
   m->iup.pad = load(m,1) << 2;
   load(m,1);			/* skip to mode */
   m->iup.stereo_sb = look_joint[load(m,4)];
   if (prot)
      load(m,4);			/* skip to data */
   else
      load(m,20);			/* skip crc */

   unpack_baL1(m);		/* unpack bit allocation */
   unpack_sfL1(m);		/* unpack scale factor */
   unpack_sampL1(m);		/* unpack samples */


}
/*-------------------------------------------------------------------------*/

#ifdef _MSC_VER
#pragma warning(disable: 4056)
#endif

int i_audio_decode_initL1(MPEGI *m, MPEG_HEAD * h, int framebytes_arg,
		   int reduction_code, int transform_code, int convert_code,
			  int freq_limit)
{
   int i, k;
   long samprate;
   int limit;
   int stepbit;
   long step;
   int bit_code;

/*--- sf table built by Layer II init ---*/

   if (m->iup.first_pass_L1)
   {
      stepbit = 2;
      step = 4;
      for (i = 1; i < 16; i++)
      {
	 m->iup.look_c_valueL1[i] = (int) (32768.0 * 2.0 / (step - 1));
	 m->iup.look_c_shiftL1[i] = 16 - stepbit;
	 stepbit++;
	 step <<= 1;
      }
      m->iup.first_pass_L1 = 0;
   }

   m->iup.unpack_routine = unpackL1;


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


   m->iup.framebytes = framebytes_arg;
/* check if code handles */
   if (h->option != 3)
      return 0;			/* layer I only */

   m->iup.nbatL1 = 32;
   m->iup.max_sb = m->iup.nbatL1;
/*----- compute nsb_limit --------*/
   samprate = sr_table[4 * h->id + h->sr_index];
   m->iup.nsb_limit = (freq_limit * 64L + samprate / 2) / samprate;
/*- caller limit -*/
/*---- limit = 0.94*(32>>reduction_code);  ----*/
   limit = (32 >> reduction_code);
   if (limit > 8)
      limit--;
   if (m->iup.nsb_limit > limit)
      m->iup.nsb_limit = limit;
   if (m->iup.nsb_limit > m->iup.max_sb)
      m->iup.nsb_limit = m->iup.max_sb;

   m->iup.outvalues = 384 >> reduction_code;
   if (h->mode != 3)
   {				/* adjust for 2 channel modes */
      m->iup.nbatL1 *= 2;
      m->iup.max_sb *= 2;
      m->iup.nsb_limit *= 2;
   }

/* set sbt function */
   m->iup.nsbt = 12;
   k = 1 + convert_code;
   if (h->mode == 3)
   {
      k = 0;
   }
   m->iup.sbt = sbt_table[bit_code][reduction_code][k];
   m->iup.outvalues *= out_chans[k];
   if (bit_code != 0)
      m->iup.outbytes = m->iup.outvalues;
   else
      m->iup.outbytes = sizeof(short) * m->iup.outvalues;

   m->iup.decinfo.channels = out_chans[k];
   m->iup.decinfo.outvalues = m->iup.outvalues;
   m->iup.decinfo.samprate = samprate >> reduction_code;
   if (bit_code != 0)
      m->iup.decinfo.bits = 8;
   else
      m->iup.decinfo.bits = sizeof(short) * 8;

   m->iup.decinfo.framebytes = m->iup.framebytes;
   m->iup.decinfo.type = 0;


/* clear sample buffer, unused sub bands must be 0 */
   for (i = 0; i < 768; i++)
      m->iup.sample[i] = 0;


/* init sub-band transform */
   i_sbt_init();

   return 1;
}

#ifdef _MSC_VER
#pragma warning(default: 4056)
#endif

/*---------------------------------------------------------*/
