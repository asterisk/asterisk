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

/* portable copy of eco\mhead.h */
/* mpeg audio header   */

typedef struct
{
   int sync;			/* 1 if valid sync */
   int id;
   int option;
   int prot;
   int br_index;
   int sr_index;
   int pad;
   int private_bit;
   int mode;
   int mode_ext;
   int cr;
   int original;
   int emphasis;
}
MPEG_HEAD;

/* portable mpeg audio decoder, decoder functions */
typedef struct
{
   int in_bytes;
   int out_bytes;
}
IN_OUT;


typedef struct
{
   int channels;
   int outvalues;
   long samprate;
   int bits;
   int framebytes;
   int type;
}
DEC_INFO;

typedef IN_OUT(*AUDIO_DECODE_ROUTINE) (void *mv, unsigned char *bs, signed short *pcm);
typedef IN_OUT(*DECODE_FUNCTION) (void *mv, unsigned char *bs, unsigned char *pcm);

struct _mpeg;

typedef struct _mpeg  MPEG;

typedef void (*SBT_FUNCTION_F) (MPEG *m, float *sample, short *pcm, int n);

/* main data bit buffer */
#define NBUF (8*1024)
#define BUF_TRIGGER (NBUF-1500)

typedef void (*XFORM_FUNCTION) (void *mv, void *pcm, int igr);

struct _mpeg
{
	struct {
		float look_c_value[18];	/* built by init */
		unsigned char *bs_ptr;
		unsigned long bitbuf;
		int bits;
		long bitval;
		int outbytes;
		int framebytes;
		int outvalues;
		int pad;
		int stereo_sb;
		DEC_INFO decinfo;		/* global for Layer III */
		int max_sb;
		int nsb_limit;
		int first_pass;
		int first_pass_L1;
		int bit_skip;
		int nbat[4];
		int bat[4][16];
		int ballo[64];		/* set by unpack_ba */
		unsigned int samp_dispatch[66];	/* set by unpack_ba */
		float c_value[64];	/* set by unpack_ba */
		unsigned int sf_dispatch[66];	/* set by unpack_ba */
		float sf_table[64];
		float cs_factor[3][64];
		float *sample;		/* global for use by Later 3 */
		signed char group3_table[32][3];
		signed char group5_table[128][3];
		signed short group9_table[1024][3];
		SBT_FUNCTION_F sbt;
		AUDIO_DECODE_ROUTINE audio_decode_routine ;
		float *cs_factorL1;
		float look_c_valueL1[16];
		int nbatL1;

	} cup;

	struct {
		/* cupl3.c */
		int nBand[2][22];		/* [long/short][cb] */
		int sfBandIndex[2][22];		/* [long/short][cb] */
		int mpeg25_flag;
		int iframe;
		int band_limit;
		int band_limit21;
		int band_limit12;
		int band_limit_nsb;
		int nsb_limit;
		int gaim_adjust;
		int id;
		int ncbl_mixed;
		int gain_adjust;
		int sr_index;
		int outvalues;
		int outbytes;
		int half_outbytes;
		int framebytes;
		int padframebytes;
		int crcbytes;
		int pad;
		int stereo_flag;
		int nchan;
		int ms_mode;
		int is_mode;
		unsigned int zero_level_pcm;
		CB_INFO cb_info[2][2];
		IS_SF_INFO is_sf_info;	/* MPEG-2 intensity stereo */ 
		unsigned char buf[NBUF];
		int buf_ptr0;
		int buf_ptr1;
		int main_pos_bit;
		SIDE_INFO side_info;
		SCALEFACT sf[2][2];	/* [gr][ch] */
		int nsamp[2][2];		/* must start = 0, for nsamp[igr_prev] */
		float yout[576];		/* hybrid out, sbt in */
		SAMPLE sample[2][2][576];
		SBT_FUNCTION_F sbt_L3;
		XFORM_FUNCTION Xform;
		DECODE_FUNCTION decode_function;
		/* msis.c */
		/*-- windows by block type --*/
		float win[4][36];
		float csa[8][2];		/* antialias */
		float lr[2][8][2];	/* [ms_mode 0/1][sf][left/right]  */
		float lr2[2][2][64][2];
		/* l3dq.c */
		float look_global[256 + 2 + 4];
		float look_scale[2][4][32];
#define ISMAX 32
		float look_pow[2 * ISMAX];
		float look_subblock[8];
		float re_buf[192][3];
	} cupl;
	struct {
		signed int vb_ptr;
		signed int vb2_ptr;
		float vbuf[512];
		float vbuf2[512];
   		int first_pass;
	} csbt;
	struct {
		float coef32[31];	/* 32 pt dct coefs */
	} cdct;
};

typedef int (*CVT_FUNCTION_8) (void *mv, unsigned char *pcm);

typedef struct
{
	struct {
		unsigned char look_u[8192];
        short pcm[2304];
        int ncnt;
        int ncnt1;
        int nlast;
        int ndeci;
        int kdeci;
		int first_pass;
        short xsave;
		CVT_FUNCTION_8 convert_routine;
	} dec;
	MPEG cupper;
}
MPEG8;

#include "itype.h"

typedef void (*SBT_FUNCTION) (SAMPLEINT * sample, short *pcm, int n);
typedef void (*UNPACK_FUNCTION) ();

typedef struct
{
	struct {
		DEC_INFO decinfo;
		int pad;
		int look_c_value[18];	/* built by init */
		int look_c_shift[18];	/* built by init */
		int outbytes;
		int framebytes;
		int outvalues;
		int max_sb;
		int stereo_sb;
		int nsb_limit;
		int bit_skip;
		int nbat[4];
		int bat[4][16];
		int ballo[64];		/* set by unpack_ba */
		unsigned int samp_dispatch[66];	/* set by unpack_ba */
		int c_value[64];		/* set by unpack_ba */
		int c_shift[64];		/* set by unpack_ba */
		unsigned int sf_dispatch[66];	/* set by unpack_ba */
		int sf_table[64];
		INT32 cs_factor[3][64];
		SAMPLEINT sample[2304];
		signed char group3_table[32][3];
		signed char group5_table[128][3];
		signed short group9_table[1024][3];
		int nsbt;
		SBT_FUNCTION sbt;
		UNPACK_FUNCTION unpack_routine;
		unsigned char *bs_ptr;
		UINT32 bitbuf;
		int bits;
		INT32 bitval;
		int first_pass;
		int first_pass_L1;
		int nbatL1;
		INT32 *cs_factorL1;
		int look_c_valueL1[16]; /* built by init */
		int look_c_shiftL1[16];	/* built by init */
	} iup;
}
MPEGI;

#ifdef __cplusplus
extern "C"
{
#endif


   void mpeg_init(MPEG *m);
   int head_info(unsigned char *buf, unsigned int n, MPEG_HEAD * h);
   int head_info2(unsigned char *buf,
	   unsigned int n, MPEG_HEAD * h, int *br);
	int head_info3(unsigned char *buf, unsigned int n, MPEG_HEAD *h, int*br, unsigned int *searchForward);
/* head_info returns framebytes > 0 for success */
/* audio_decode_init returns 1 for success, 0 for fail */
/* audio_decode returns in_bytes = 0 on sync loss */

   int audio_decode_init(MPEG *m, MPEG_HEAD * h, int framebytes_arg,
		   int reduction_code, int transform_code, int convert_code,
			 int freq_limit);
   void audio_decode_info(MPEG *m, DEC_INFO * info);
   IN_OUT audio_decode(MPEG *m, unsigned char *bs, short *pcm);

   void mpeg8_init(MPEG8 *m);
   int audio_decode8_init(MPEG8 *m, MPEG_HEAD * h, int framebytes_arg,
		   int reduction_code, int transform_code, int convert_code,
			  int freq_limit);
   void audio_decode8_info(MPEG8 *m, DEC_INFO * info);
   IN_OUT audio_decode8(MPEG8 *m, unsigned char *bs, short *pcmbuf);

/*-- integer decode --*/
   void i_mpeg_init(MPEGI *m);
   int i_audio_decode_init(MPEGI *m, MPEG_HEAD * h, int framebytes_arg,
		   int reduction_code, int transform_code, int convert_code,
			   int freq_limit);
   void i_audio_decode_info(MPEGI *m, DEC_INFO * info);
   IN_OUT i_audio_decode(MPEGI *m, unsigned char *bs, short *pcm);



#ifdef __cplusplus
}
#endif
