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

/****  cwin.c  ***************************************************

include to cwinm.c

MPEG audio decoder, float window routines
portable C

******************************************************************/

#ifdef ASM_X86
extern void window_mpg_asm(float *a, int b, short *c);
extern void window_dual_asm(float *a, int b, short *c);
extern void window16_asm(float *a, int b, short *c);
extern void window16_dual_asm(float *a, int b, short *c);
extern void window8_asm(float *a, int b, short *c);
extern void window8_dual_asm(float *a, int b, short *c);
#endif /* ASM_X86 */

/*-------------------------------------------------------------------------*/
void window(float *vbuf, int vb_ptr, short *pcm)
{
#ifdef ASM_X86
   window_mpg_asm(vbuf, vb_ptr, pcm);
#else
   int i, j;
   int si, bx;
   float *coef;
   float sum;
   long tmp;

   si = vb_ptr + 16;
   bx = (si + 32) & 511;
   coef = wincoef;

/*-- first 16 --*/
   for (i = 0; i < 16; i++)
   {
      sum = 0.0F;
      for (j = 0; j < 8; j++)
      {
	 sum += (*coef++) * vbuf[si];
	 si = (si + 64) & 511;
	 sum -= (*coef++) * vbuf[bx];
	 bx = (bx + 64) & 511;
      }
      si++;
      bx--;
      tmp = (long) sum;
      if (tmp > 32767)
	 tmp = 32767;
      else if (tmp < -32768)
	 tmp = -32768;
      *pcm++ = tmp;
   }
/*--  special case --*/
   sum = 0.0F;
   for (j = 0; j < 8; j++)
   {
      sum += (*coef++) * vbuf[bx];
      bx = (bx + 64) & 511;
   }
   tmp = (long) sum;
   if (tmp > 32767)
      tmp = 32767;
   else if (tmp < -32768)
      tmp = -32768;
   *pcm++ = tmp;
/*-- last 15 --*/
   coef = wincoef + 255;	/* back pass through coefs */
   for (i = 0; i < 15; i++)
   {
      si--;
      bx++;
      sum = 0.0F;
      for (j = 0; j < 8; j++)
      {
	 sum += (*coef--) * vbuf[si];
	 si = (si + 64) & 511;
	 sum += (*coef--) * vbuf[bx];
	 bx = (bx + 64) & 511;
      }
      tmp = (long) sum;
      if (tmp > 32767)
	 tmp = 32767;
      else if (tmp < -32768)
	 tmp = -32768;
      *pcm++ = tmp;
   }
#endif
}



/*------------------------------------------------------------*/
#ifndef ASM_X86_OLD
void window_dual(float *vbuf, int vb_ptr, short *pcm)
{
#ifdef ASM_X86
   window_dual_asm(vbuf, vb_ptr, pcm);
#else
   int i, j;			/* dual window interleaves output */
   int si, bx;
   float *coef;
   float sum;
   long tmp;

   si = vb_ptr + 16;
   bx = (si + 32) & 511;
   coef = wincoef;

/*-- first 16 --*/
   for (i = 0; i < 16; i++)
   {
      sum = 0.0F;
      for (j = 0; j < 8; j++)
      {
	 sum += (*coef++) * vbuf[si];
	 si = (si + 64) & 511;
	 sum -= (*coef++) * vbuf[bx];
	 bx = (bx + 64) & 511;
      }
      si++;
      bx--;
      tmp = (long) sum;
      if (tmp > 32767)
	 tmp = 32767;
      else if (tmp < -32768)
	 tmp = -32768;
      *pcm = tmp;
      pcm += 2;
   }
/*--  special case --*/
   sum = 0.0F;
   for (j = 0; j < 8; j++)
   {
      sum += (*coef++) * vbuf[bx];
      bx = (bx + 64) & 511;
   }
   tmp = (long) sum;
   if (tmp > 32767)
      tmp = 32767;
   else if (tmp < -32768)
      tmp = -32768;
   *pcm = tmp;
   pcm += 2;
/*-- last 15 --*/
   coef = wincoef + 255;	/* back pass through coefs */
   for (i = 0; i < 15; i++)
   {
      si--;
      bx++;
      sum = 0.0F;
      for (j = 0; j < 8; j++)
      {
	 sum += (*coef--) * vbuf[si];
	 si = (si + 64) & 511;
	 sum += (*coef--) * vbuf[bx];
	 bx = (bx + 64) & 511;
      }
      tmp = (long) sum;
      if (tmp > 32767)
	 tmp = 32767;
      else if (tmp < -32768)
	 tmp = -32768;
      *pcm = tmp;
      pcm += 2;
   }
#endif
}
#endif	/* ndef ASM_X86_OLD */
/*------------------------------------------------------------*/
/*------------------- 16 pt window ------------------------------*/
void window16(float *vbuf, int vb_ptr, short *pcm)
{
#ifdef ASM_X86
   window16_asm(vbuf, vb_ptr, pcm);
#else
   int i, j;
   unsigned char si, bx;
   float *coef;
   float sum;
   long tmp;

   si = vb_ptr + 8;
   bx = si + 16;
   coef = wincoef;

/*-- first 8 --*/
   for (i = 0; i < 8; i++)
   {
      sum = 0.0F;
      for (j = 0; j < 8; j++)
      {
	 sum += (*coef++) * vbuf[si];
	 si += 32;
	 sum -= (*coef++) * vbuf[bx];
	 bx += 32;
      }
      si++;
      bx--;
      coef += 16;
      tmp = (long) sum;
      if (tmp > 32767)
	 tmp = 32767;
      else if (tmp < -32768)
	 tmp = -32768;
      *pcm++ = tmp;
   }
/*--  special case --*/
   sum = 0.0F;
   for (j = 0; j < 8; j++)
   {
      sum += (*coef++) * vbuf[bx];
      bx += 32;
   }
   tmp = (long) sum;
   if (tmp > 32767)
      tmp = 32767;
   else if (tmp < -32768)
      tmp = -32768;
   *pcm++ = tmp;
/*-- last 7 --*/
   coef = wincoef + 255;	/* back pass through coefs */
   for (i = 0; i < 7; i++)
   {
      coef -= 16;
      si--;
      bx++;
      sum = 0.0F;
      for (j = 0; j < 8; j++)
      {
	 sum += (*coef--) * vbuf[si];
	 si += 32;
	 sum += (*coef--) * vbuf[bx];
	 bx += 32;
      }
      tmp = (long) sum;
      if (tmp > 32767)
	 tmp = 32767;
      else if (tmp < -32768)
	 tmp = -32768;
      *pcm++ = tmp;
   }
#endif
}
/*--------------- 16 pt dual window (interleaved output) -----------------*/
void window16_dual(float *vbuf, int vb_ptr, short *pcm)
{
#ifdef ASM_X86
   window16_dual_asm(vbuf, vb_ptr, pcm);
#else
   int i, j;
   unsigned char si, bx;
   float *coef;
   float sum;
   long tmp;

   si = vb_ptr + 8;
   bx = si + 16;
   coef = wincoef;

/*-- first 8 --*/
   for (i = 0; i < 8; i++)
   {
      sum = 0.0F;
      for (j = 0; j < 8; j++)
      {
	 sum += (*coef++) * vbuf[si];
	 si += 32;
	 sum -= (*coef++) * vbuf[bx];
	 bx += 32;
      }
      si++;
      bx--;
      coef += 16;
      tmp = (long) sum;
      if (tmp > 32767)
	 tmp = 32767;
      else if (tmp < -32768)
	 tmp = -32768;
      *pcm = tmp;
      pcm += 2;
   }
/*--  special case --*/
   sum = 0.0F;
   for (j = 0; j < 8; j++)
   {
      sum += (*coef++) * vbuf[bx];
      bx += 32;
   }
   tmp = (long) sum;
   if (tmp > 32767)
      tmp = 32767;
   else if (tmp < -32768)
      tmp = -32768;
   *pcm = tmp;
   pcm += 2;
/*-- last 7 --*/
   coef = wincoef + 255;	/* back pass through coefs */
   for (i = 0; i < 7; i++)
   {
      coef -= 16;
      si--;
      bx++;
      sum = 0.0F;
      for (j = 0; j < 8; j++)
      {
	 sum += (*coef--) * vbuf[si];
	 si += 32;
	 sum += (*coef--) * vbuf[bx];
	 bx += 32;
      }
      tmp = (long) sum;
      if (tmp > 32767)
	 tmp = 32767;
      else if (tmp < -32768)
	 tmp = -32768;
      *pcm = tmp;
      pcm += 2;
   }
#endif
}
/*------------------- 8 pt window ------------------------------*/
void window8(float *vbuf, int vb_ptr, short *pcm)
{
#ifdef ASM_X86
   window8_asm(vbuf, vb_ptr, pcm);
#else
   int i, j;
   int si, bx;
   float *coef;
   float sum;
   long tmp;

   si = vb_ptr + 4;
   bx = (si + 8) & 127;
   coef = wincoef;

/*-- first 4 --*/
   for (i = 0; i < 4; i++)
   {
      sum = 0.0F;
      for (j = 0; j < 8; j++)
      {
	 sum += (*coef++) * vbuf[si];
	 si = (si + 16) & 127;
	 sum -= (*coef++) * vbuf[bx];
	 bx = (bx + 16) & 127;
      }
      si++;
      bx--;
      coef += 48;
      tmp = (long) sum;
      if (tmp > 32767)
	 tmp = 32767;
      else if (tmp < -32768)
	 tmp = -32768;
      *pcm++ = tmp;
   }
/*--  special case --*/
   sum = 0.0F;
   for (j = 0; j < 8; j++)
   {
      sum += (*coef++) * vbuf[bx];
      bx = (bx + 16) & 127;
   }
   tmp = (long) sum;
   if (tmp > 32767)
      tmp = 32767;
   else if (tmp < -32768)
      tmp = -32768;
   *pcm++ = tmp;
/*-- last 3 --*/
   coef = wincoef + 255;	/* back pass through coefs */
   for (i = 0; i < 3; i++)
   {
      coef -= 48;
      si--;
      bx++;
      sum = 0.0F;
      for (j = 0; j < 8; j++)
      {
	 sum += (*coef--) * vbuf[si];
	 si = (si + 16) & 127;
	 sum += (*coef--) * vbuf[bx];
	 bx = (bx + 16) & 127;
      }
      tmp = (long) sum;
      if (tmp > 32767)
	 tmp = 32767;
      else if (tmp < -32768)
	 tmp = -32768;
      *pcm++ = tmp;
   }
#endif
}
/*--------------- 8 pt dual window (interleaved output) -----------------*/
void window8_dual(float *vbuf, int vb_ptr, short *pcm)
{
#ifdef ASM_X86
   window8_dual_asm(vbuf, vb_ptr, pcm);
#else
   int i, j;
   int si, bx;
   float *coef;
   float sum;
   long tmp;

   si = vb_ptr + 4;
   bx = (si + 8) & 127;
   coef = wincoef;

/*-- first 4 --*/
   for (i = 0; i < 4; i++)
   {
      sum = 0.0F;
      for (j = 0; j < 8; j++)
      {
	 sum += (*coef++) * vbuf[si];
	 si = (si + 16) & 127;
	 sum -= (*coef++) * vbuf[bx];
	 bx = (bx + 16) & 127;
      }
      si++;
      bx--;
      coef += 48;
      tmp = (long) sum;
      if (tmp > 32767)
	 tmp = 32767;
      else if (tmp < -32768)
	 tmp = -32768;
      *pcm = tmp;
      pcm += 2;
   }
/*--  special case --*/
   sum = 0.0F;
   for (j = 0; j < 8; j++)
   {
      sum += (*coef++) * vbuf[bx];
      bx = (bx + 16) & 127;
   }
   tmp = (long) sum;
   if (tmp > 32767)
      tmp = 32767;
   else if (tmp < -32768)
      tmp = -32768;
   *pcm = tmp;
   pcm += 2;
/*-- last 3 --*/
   coef = wincoef + 255;	/* back pass through coefs */
   for (i = 0; i < 3; i++)
   {
      coef -= 48;
      si--;
      bx++;
      sum = 0.0F;
      for (j = 0; j < 8; j++)
      {
	 sum += (*coef--) * vbuf[si];
	 si = (si + 16) & 127;
	 sum += (*coef--) * vbuf[bx];
	 bx = (bx + 16) & 127;
      }
      tmp = (long) sum;
      if (tmp > 32767)
	 tmp = 32767;
      else if (tmp < -32768)
	 tmp = -32768;
      *pcm = tmp;
      pcm += 2;
   }
#endif
}
/*------------------------------------------------------------*/
