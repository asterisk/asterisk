/*____________________________________________________________________________
	
	FreeAmp - The Free MP3 Player

        MP3 Decoder originally Copyright (C) 1996-1997 Xing Technology
        Corp.  http://www.xingtech.com

	Portions Copyright (C) 1998-1999 Emusic.com

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

/****  L3.h  ***************************************************

  Layer III structures

  *** Layer III is 32 bit only          ***
  *** Layer III code assumes 32 bit int ***

******************************************************************/

#define GLOBAL_GAIN_SCALE (4*15)
/* #define GLOBAL_GAIN_SCALE 0 */


#ifdef _M_IX86
#define LITTLE_ENDIAN 1
#endif

#ifdef _M_ALPHA
#define LITTLE_ENDIAN 1
#endif

#ifdef sparc
#define LITTLE_ENDIAN 0
#endif

#ifndef LITTLE_ENDIAN
#error Layer III LITTLE_ENDIAN must be defined 0 or 1
#endif

/*-----------------------------------------------------------*/
/*---- huffman lookup tables ---*/
/* endian dependent !!! */
#if LITTLE_ENDIAN
typedef union
{
   int ptr;
   struct
   {
      unsigned char signbits;
      unsigned char x;
      unsigned char y;
      unsigned char purgebits;	// 0 = esc

   }
   b;
}
HUFF_ELEMENT;

#else /* big endian machines */
typedef union
{
   int ptr;			/* int must be 32 bits or more */
   struct
   {
      unsigned char purgebits;	// 0 = esc

      unsigned char y;
      unsigned char x;
      unsigned char signbits;
   }
   b;
}
HUFF_ELEMENT;

#endif
/*--------------------------------------------------------------*/
typedef struct
{
   unsigned int bitbuf;
   int bits;
   unsigned char *bs_ptr;
   unsigned char *bs_ptr0;
   unsigned char *bs_ptr_end;	// optional for overrun test

}
BITDAT;

/*-- side info ---*/
typedef struct
{
   int part2_3_length;
   int big_values;
   int global_gain;
   int scalefac_compress;
   int window_switching_flag;
   int block_type;
   int mixed_block_flag;
   int table_select[3];
   int subblock_gain[3];
   int region0_count;
   int region1_count;
   int preflag;
   int scalefac_scale;
   int count1table_select;
}
GR;
typedef struct
{
   int mode;
   int mode_ext;
/*---------------*/
   int main_data_begin;		/* beginning, not end, my spec wrong */
   int private_bits;
/*---------------*/
   int scfsi[2];		/* 4 bit flags [ch] */
   GR gr[2][2];			/* [gran][ch] */
}
SIDE_INFO;

/*-----------------------------------------------------------*/
/*-- scale factors ---*/
// check dimensions - need 21 long, 3*12 short
// plus extra for implicit sf=0 above highest cb
typedef struct
{
   int l[23];			/* [cb] */
   int s[3][13];		/* [window][cb] */
}
SCALEFACT;

/*-----------------------------------------------------------*/
typedef struct
{
   int cbtype;			/* long=0 short=1 */
   int cbmax;			/* max crit band */
//   int lb_type;			/* long block type 0 1 3 */
   int cbs0;			/* short band start index 0 3 12 (12=no shorts */
   int ncbl;			/* number long cb's 0 8 21 */
   int cbmax_s[3];		/* cbmax by individual short blocks */
}
CB_INFO;

/*-----------------------------------------------------------*/
/* scale factor infor for MPEG2 intensity stereo  */
typedef struct
{
   int nr[3];
   int slen[3];
   int intensity_scale;
}
IS_SF_INFO;

/*-----------------------------------------------------------*/
typedef union
{
   int s;
   float x;
}
SAMPLE;

/*-----------------------------------------------------------*/
