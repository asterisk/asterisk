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

/*----- iwinbq.c ---------------------------------------------------

portable c
mpeg1/2 Layer I/II audio decode

conditional include to iwinm.c

quick integer window - 8 bit output

mods 1/8/97 warnings

--------------------------------------------------------------*/
/*--------------------------------------------------------------------*/
void i_windowB(WININT * vbuf, int vb_ptr, unsigned char *pcm)
{
   int i, j;
   unsigned int si, bx;
   WINCOEF *coef;
   INT32 sum;

/*-- first 16 --*/
   si = (vb_ptr + (16 + 3 * 64)) & 511;
   bx = (si + (32 + 512 - 3 * 64 + 2 * 64)) & 511;
   coef = iwincoef;
   for (i = 0; i < 16; i++)
   {
      sum = -WINMULT(vbuf[bx], (*coef++));
      for (j = 0; j < 3; j++)
      {
	 bx = (bx + 64) & 511;
	 sum += WINMULT(vbuf[si], (*coef++));
	 si = (si + 64) & 511;
	 sum -= WINMULT(vbuf[bx], (*coef++));
      }
      si = (si + (5 * 64 + 1)) & 511;
      bx = (bx + (64 + 4 * 64 - 1)) & 511;
      sum >>= WINBITS;
      if (sum > 32767)
	 sum = 32767;
      else if (sum < -32768)
	 sum = -32768;
      *pcm++ = (unsigned char) ((sum >> 8) ^ 0x80);
   }
/*--  special case --*/
   bx = (bx + (512 - 64)) & 511;
   sum = WINMULT(vbuf[bx], (*coef++));
   for (j = 0; j < 3; j++)
   {
      bx = (bx + 64) & 511;
      sum += WINMULT(vbuf[bx], (*coef++));
   }
   sum >>= WINBITS;
   if (sum > 32767)
      sum = 32767;
   else if (sum < -32768)
      sum = -32768;
   *pcm++ = (unsigned char) ((sum >> 8) ^ 0x80);

/*-- last 15 --*/
   coef = iwincoef + 111;	/* back pass through coefs */
   si = (si + (512 - 3 * 64 + 2 * 64 - 1)) & 511;
   bx = (bx + (64 + 3 * 64 + 2 * 64 + 1)) & 511;
   for (i = 0; i < 15; i++)
   {
      sum = WINMULT(vbuf[si], (*coef--));
      for (j = 0; j < 3; j++)
      {
	 si = (si + 64) & 511;
	 sum += WINMULT(vbuf[bx], (*coef--));
	 bx = (bx + 64) & 511;
	 sum += WINMULT(vbuf[si], (*coef--));
      }
      si = (si + (64 - 1 + 4 * 64)) & 511;
      bx = (bx + (5 * 64 + 1)) & 511;
      sum >>= WINBITS;
      if (sum > 32767)
	 sum = 32767;
      else if (sum < -32768)
	 sum = -32768;
      *pcm++ = (unsigned char) ((sum >> 8) ^ 0x80);
   }
}
/*------------------------------------------------------------*/
void i_windowB_dual(WININT * vbuf, int vb_ptr, unsigned char *pcm)
{
/* dual window interleaves output */
   int i, j;
   unsigned int si, bx;
   WINCOEF *coef;
   INT32 sum;

/*-- first 16 --*/
   si = (vb_ptr + (16 + 3 * 64)) & 511;
   bx = (si + (32 + 512 - 3 * 64 + 2 * 64)) & 511;
   coef = iwincoef;
   for (i = 0; i < 16; i++)
   {
      sum = -WINMULT(vbuf[bx], (*coef++));
      for (j = 0; j < 3; j++)
      {
	 bx = (bx + 64) & 511;
	 sum += WINMULT(vbuf[si], (*coef++));
	 si = (si + 64) & 511;
	 sum -= WINMULT(vbuf[bx], (*coef++));
      }
      si = (si + (5 * 64 + 1)) & 511;
      bx = (bx + (64 + 4 * 64 - 1)) & 511;
      sum >>= WINBITS;
      if (sum > 32767)
	 sum = 32767;
      else if (sum < -32768)
	 sum = -32768;
      *pcm = (unsigned char) ((sum >> 8) ^ 0x80);
      pcm += 2;
   }
/*--  special case --*/
   bx = (bx + (512 - 64)) & 511;
   sum = WINMULT(vbuf[bx], (*coef++));
   for (j = 0; j < 3; j++)
   {
      bx = (bx + 64) & 511;
      sum += WINMULT(vbuf[bx], (*coef++));
   }
   sum >>= WINBITS;
   if (sum > 32767)
      sum = 32767;
   else if (sum < -32768)
      sum = -32768;
   *pcm = (unsigned char) ((sum >> 8) ^ 0x80);
   pcm += 2;

/*-- last 15 --*/
   coef = iwincoef + 111;	/* back pass through coefs */
   si = (si + (512 - 3 * 64 + 2 * 64 - 1)) & 511;
   bx = (bx + (64 + 3 * 64 + 2 * 64 + 1)) & 511;
   for (i = 0; i < 15; i++)
   {
      sum = WINMULT(vbuf[si], (*coef--));
      for (j = 0; j < 3; j++)
      {
	 si = (si + 64) & 511;
	 sum += WINMULT(vbuf[bx], (*coef--));
	 bx = (bx + 64) & 511;
	 sum += WINMULT(vbuf[si], (*coef--));
      }
      si = (si + (64 - 1 + 4 * 64)) & 511;
      bx = (bx + (5 * 64 + 1)) & 511;
      sum >>= WINBITS;
      if (sum > 32767)
	 sum = 32767;
      else if (sum < -32768)
	 sum = -32768;
      *pcm = (unsigned char) ((sum >> 8) ^ 0x80);
      pcm += 2;
   }
}
/*------------------------------------------------------------*/
void i_windowB_dual_right(WININT * vbuf, int vb_ptr, unsigned char *pcm)
{
/* right identical to dual, for asm  */
/* dual window interleaves output */
   int i, j;
   unsigned int si, bx;
   WINCOEF *coef;
   INT32 sum;

/*-- first 16 --*/
   si = (vb_ptr + (16 + 3 * 64)) & 511;
   bx = (si + (32 + 512 - 3 * 64 + 2 * 64)) & 511;
   coef = iwincoef;
   for (i = 0; i < 16; i++)
   {
      sum = -WINMULT(vbuf[bx], (*coef++));
      for (j = 0; j < 3; j++)
      {
	 bx = (bx + 64) & 511;
	 sum += WINMULT(vbuf[si], (*coef++));
	 si = (si + 64) & 511;
	 sum -= WINMULT(vbuf[bx], (*coef++));
      }
      si = (si + (5 * 64 + 1)) & 511;
      bx = (bx + (64 + 4 * 64 - 1)) & 511;
      sum >>= WINBITS;
      if (sum > 32767)
	 sum = 32767;
      else if (sum < -32768)
	 sum = -32768;
      *pcm = (unsigned char) ((sum >> 8) ^ 0x80);
      pcm += 2;
   }
/*--  special case --*/
   bx = (bx + (512 - 64)) & 511;
   sum = WINMULT(vbuf[bx], (*coef++));
   for (j = 0; j < 3; j++)
   {
      bx = (bx + 64) & 511;
      sum += WINMULT(vbuf[bx], (*coef++));
   }
   sum >>= WINBITS;
   if (sum > 32767)
      sum = 32767;
   else if (sum < -32768)
      sum = -32768;
   *pcm = (unsigned char) ((sum >> 8) ^ 0x80);
   pcm += 2;

/*-- last 15 --*/
   coef = iwincoef + 111;	/* back pass through coefs */
   si = (si + (512 - 3 * 64 + 2 * 64 - 1)) & 511;
   bx = (bx + (64 + 3 * 64 + 2 * 64 + 1)) & 511;
   for (i = 0; i < 15; i++)
   {
      sum = WINMULT(vbuf[si], (*coef--));
      for (j = 0; j < 3; j++)
      {
	 si = (si + 64) & 511;
	 sum += WINMULT(vbuf[bx], (*coef--));
	 bx = (bx + 64) & 511;
	 sum += WINMULT(vbuf[si], (*coef--));
      }
      si = (si + (64 - 1 + 4 * 64)) & 511;
      bx = (bx + (5 * 64 + 1)) & 511;
      sum >>= WINBITS;
      if (sum > 32767)
	 sum = 32767;
      else if (sum < -32768)
	 sum = -32768;
      *pcm = (unsigned char) ((sum >> 8) ^ 0x80);
      pcm += 2;
   }
}
/*------------------------------------------------------------*/
/*------------------------------------------------------------*/
/*------------------- 16 pt window ------------------------------*/
void i_windowB16(WININT * vbuf, int vb_ptr, unsigned char *pcm)
{
   int i, j;
   unsigned char si, bx;
   WINCOEF *coef;
   INT32 sum;


/*-- first 8 --*/
   si = (unsigned char) (vb_ptr + 8 + 3 * 32);
   bx = (unsigned char) (si + (16 + 256 - 3 * 32 + 2 * 32));
   coef = iwincoef;
   for (i = 0; i < 8; i++)
   {
      sum = -WINMULT(vbuf[bx], (*coef++));
      for (j = 0; j < 3; j++)
      {
	 bx += 32;
	 sum += WINMULT(vbuf[si], (*coef++));
	 si += 32;
	 sum -= WINMULT(vbuf[bx], (*coef++));
      }
      si += (5 * 32 + 1);
      bx += (32 + 4 * 32 - 1);
      coef += 7;
      sum >>= WINBITS;
      if (sum > 32767)
	 sum = 32767;
      else if (sum < -32768)
	 sum = -32768;
      *pcm++ = (unsigned char) ((sum >> 8) ^ 0x80);
   }
/*--  special case --*/
   bx += (256 - 32);
   sum = WINMULT(vbuf[bx], (*coef++));
   for (j = 0; j < 3; j++)
   {
      bx += 32;
      sum += WINMULT(vbuf[bx], (*coef++));
   }
   sum >>= WINBITS;
   if (sum > 32767)
      sum = 32767;
   else if (sum < -32768)
      sum = -32768;
   *pcm++ = (unsigned char) ((sum >> 8) ^ 0x80);

/*-- last 7 --*/
   coef = iwincoef + (111 - 7);	/* back pass through coefs */
   si += (256 + -3 * 32 + 2 * 32 - 1);
   bx += (32 + 3 * 32 + 2 * 32 + 1);
   for (i = 0; i < 7; i++)
   {
      sum = WINMULT(vbuf[si], (*coef--));
      for (j = 0; j < 3; j++)
      {
	 si += 32;
	 sum += WINMULT(vbuf[bx], (*coef--));
	 bx += 32;
	 sum += WINMULT(vbuf[si], (*coef--));
      }
      si += (32 - 1 + 4 * 32);
      bx += (5 * 32 + 1);
      coef -= 7;
      sum >>= WINBITS;
      if (sum > 32767)
	 sum = 32767;
      else if (sum < -32768)
	 sum = -32768;
      *pcm++ = (unsigned char) ((sum >> 8) ^ 0x80);
   }
}
/*--------------- 16 pt dual window (interleaved output) -----------------*/
void i_windowB16_dual(WININT * vbuf, int vb_ptr, unsigned char *pcm)
{
   int i, j;
   unsigned char si, bx;
   WINCOEF *coef;
   INT32 sum;


/*-- first 8 --*/
   si = (unsigned char) (vb_ptr + 8 + 3 * 32);
   bx = (unsigned char) (si + (16 + 256 - 3 * 32 + 2 * 32));
   coef = iwincoef;
   for (i = 0; i < 8; i++)
   {
      sum = -WINMULT(vbuf[bx], (*coef++));
      for (j = 0; j < 3; j++)
      {
	 bx += 32;
	 sum += WINMULT(vbuf[si], (*coef++));
	 si += 32;
	 sum -= WINMULT(vbuf[bx], (*coef++));
      }
      si += (5 * 32 + 1);
      bx += (32 + 4 * 32 - 1);
      coef += 7;
      sum >>= WINBITS;
      if (sum > 32767)
	 sum = 32767;
      else if (sum < -32768)
	 sum = -32768;
      *pcm = (unsigned char) ((sum >> 8) ^ 0x80);
      pcm += 2;
   }
/*--  special case --*/
   bx += (256 - 32);
   sum = WINMULT(vbuf[bx], (*coef++));
   for (j = 0; j < 3; j++)
   {
      bx += 32;
      sum += WINMULT(vbuf[bx], (*coef++));
   }
   sum >>= WINBITS;
   if (sum > 32767)
      sum = 32767;
   else if (sum < -32768)
      sum = -32768;
   *pcm = (unsigned char) ((sum >> 8) ^ 0x80);
   pcm += 2;

/*-- last 7 --*/
   coef = iwincoef + (111 - 7);	/* back pass through coefs */
   si += (256 + -3 * 32 + 2 * 32 - 1);
   bx += (32 + 3 * 32 + 2 * 32 + 1);
   for (i = 0; i < 7; i++)
   {
      sum = WINMULT(vbuf[si], (*coef--));
      for (j = 0; j < 3; j++)
      {
	 si += 32;
	 sum += WINMULT(vbuf[bx], (*coef--));
	 bx += 32;
	 sum += WINMULT(vbuf[si], (*coef--));
      }
      si += (32 - 1 + 4 * 32);
      bx += (5 * 32 + 1);
      coef -= 7;
      sum >>= WINBITS;
      if (sum > 32767)
	 sum = 32767;
      else if (sum < -32768)
	 sum = -32768;
      *pcm = (unsigned char) ((sum >> 8) ^ 0x80);
      pcm += 2;
   }
}
/*--------------- 16 pt dual window (interleaved output) -----------------*/
void i_windowB16_dual_right(WININT * vbuf, int vb_ptr, unsigned char *pcm)
{
/* right identical to dual, for asm */
   int i, j;
   unsigned char si, bx;
   WINCOEF *coef;
   INT32 sum;


/*-- first 8 --*/
   si = (unsigned char) (vb_ptr + 8 + 3 * 32);
   bx = (unsigned char) (si + (16 + 256 - 3 * 32 + 2 * 32));
   coef = iwincoef;
   for (i = 0; i < 8; i++)
   {
      sum = -WINMULT(vbuf[bx], (*coef++));
      for (j = 0; j < 3; j++)
      {
	 bx += 32;
	 sum += WINMULT(vbuf[si], (*coef++));
	 si += 32;
	 sum -= WINMULT(vbuf[bx], (*coef++));
      }
      si += (5 * 32 + 1);
      bx += (32 + 4 * 32 - 1);
      coef += 7;
      sum >>= WINBITS;
      if (sum > 32767)
	 sum = 32767;
      else if (sum < -32768)
	 sum = -32768;
      *pcm = (unsigned char) ((sum >> 8) ^ 0x80);
      pcm += 2;
   }
/*--  special case --*/
   bx += (256 - 32);
   sum = WINMULT(vbuf[bx], (*coef++));
   for (j = 0; j < 3; j++)
   {
      bx += 32;
      sum += WINMULT(vbuf[bx], (*coef++));
   }
   sum >>= WINBITS;
   if (sum > 32767)
      sum = 32767;
   else if (sum < -32768)
      sum = -32768;
   *pcm = (unsigned char) ((sum >> 8) ^ 0x80);
   pcm += 2;

/*-- last 7 --*/
   coef = iwincoef + (111 - 7);	/* back pass through coefs */
   si += (256 + -3 * 32 + 2 * 32 - 1);
   bx += (32 + 3 * 32 + 2 * 32 + 1);
   for (i = 0; i < 7; i++)
   {
      sum = WINMULT(vbuf[si], (*coef--));
      for (j = 0; j < 3; j++)
      {
	 si += 32;
	 sum += WINMULT(vbuf[bx], (*coef--));
	 bx += 32;
	 sum += WINMULT(vbuf[si], (*coef--));
      }
      si += (32 - 1 + 4 * 32);
      bx += (5 * 32 + 1);
      coef -= 7;
      sum >>= WINBITS;
      if (sum > 32767)
	 sum = 32767;
      else if (sum < -32768)
	 sum = -32768;
      *pcm = (unsigned char) ((sum >> 8) ^ 0x80);
      pcm += 2;
   }
}
/*------------------------------------------------------------*/
/*------------------- 8 pt window ------------------------------*/
void i_windowB8(WININT * vbuf, int vb_ptr, unsigned char *pcm)
{
   int i, j;
   unsigned int si, bx;
   WINCOEF *coef;
   INT32 sum;

/*-- first 4 --*/
   si = (vb_ptr + (4 + 3 * 16)) & 127;
   bx = (si + (8 + 128 - 3 * 16 + 2 * 16)) & 127;
   coef = iwincoef;
   for (i = 0; i < 4; i++)
   {
      sum = -WINMULT(vbuf[bx], (*coef++));
      for (j = 0; j < 3; j++)
      {
	 bx = (bx + 16) & 127;
	 sum += WINMULT(vbuf[si], (*coef++));
	 si = (si + 16) & 127;
	 sum -= WINMULT(vbuf[bx], (*coef++));
      }
      si = (si + (5 * 16 + 1)) & 127;
      bx = (bx + (16 + 4 * 16 - 1)) & 127;
      coef += (3 * 7);
      sum >>= WINBITS;
      if (sum > 32767)
	 sum = 32767;
      else if (sum < -32768)
	 sum = -32768;
      *pcm++ = (unsigned char) ((sum >> 8) ^ 0x80);
   }
/*--  special case --*/
   bx = (bx + (128 - 16)) & 127;
   sum = WINMULT(vbuf[bx], (*coef++));
   for (j = 0; j < 3; j++)
   {
      bx = (bx + 16) & 127;
      sum += WINMULT(vbuf[bx], (*coef++));
   }
   sum >>= WINBITS;
   if (sum > 32767)
      sum = 32767;
   else if (sum < -32768)
      sum = -32768;
   *pcm++ = (unsigned char) ((sum >> 8) ^ 0x80);

/*-- last 3 --*/
   coef = iwincoef + (111 - 3 * 7);	/* back pass through coefs */
   si = (si + (128 - 3 * 16 + 2 * 16 - 1)) & 127;
   bx = (bx + (16 + 3 * 16 + 2 * 16 + 1)) & 127;
   for (i = 0; i < 3; i++)
   {
      sum = WINMULT(vbuf[si], (*coef--));
      for (j = 0; j < 3; j++)
      {
	 si = (si + 16) & 127;
	 sum += WINMULT(vbuf[bx], (*coef--));
	 bx = (bx + 16) & 127;
	 sum += WINMULT(vbuf[si], (*coef--));
      }
      si = (si + (16 - 1 + 4 * 16)) & 127;
      bx = (bx + (5 * 16 + 1)) & 127;
      coef -= (3 * 7);
      sum >>= WINBITS;
      if (sum > 32767)
	 sum = 32767;
      else if (sum < -32768)
	 sum = -32768;
      *pcm++ = (unsigned char) ((sum >> 8) ^ 0x80);
   }
}
/*--------------- 8 pt dual window (interleaved output) --------------*/
void i_windowB8_dual(WININT * vbuf, int vb_ptr, unsigned char *pcm)
{
   int i, j;
   unsigned int si, bx;
   WINCOEF *coef;
   INT32 sum;

/*-- first 4 --*/
   si = (vb_ptr + (4 + 3 * 16)) & 127;
   bx = (si + (8 + 128 - 3 * 16 + 2 * 16)) & 127;
   coef = iwincoef;
   for (i = 0; i < 4; i++)
   {
      sum = -WINMULT(vbuf[bx], (*coef++));
      for (j = 0; j < 3; j++)
      {
	 bx = (bx + 16) & 127;
	 sum += WINMULT(vbuf[si], (*coef++));
	 si = (si + 16) & 127;
	 sum -= WINMULT(vbuf[bx], (*coef++));
      }
      si = (si + (5 * 16 + 1)) & 127;
      bx = (bx + (16 + 4 * 16 - 1)) & 127;
      coef += (3 * 7);
      sum >>= WINBITS;
      if (sum > 32767)
	 sum = 32767;
      else if (sum < -32768)
	 sum = -32768;
      *pcm = (unsigned char) ((sum >> 8) ^ 0x80);
      pcm += 2;
   }
/*--  special case --*/
   bx = (bx + (128 - 16)) & 127;
   sum = WINMULT(vbuf[bx], (*coef++));
   for (j = 0; j < 3; j++)
   {
      bx = (bx + 16) & 127;
      sum += WINMULT(vbuf[bx], (*coef++));
   }
   sum >>= WINBITS;
   if (sum > 32767)
      sum = 32767;
   else if (sum < -32768)
      sum = -32768;
   *pcm = (unsigned char) ((sum >> 8) ^ 0x80);
   pcm += 2;

/*-- last 3 --*/
   coef = iwincoef + (111 - 3 * 7);	/* back pass through coefs */
   si = (si + (128 - 3 * 16 + 2 * 16 - 1)) & 127;
   bx = (bx + (16 + 3 * 16 + 2 * 16 + 1)) & 127;
   for (i = 0; i < 3; i++)
   {
      sum = WINMULT(vbuf[si], (*coef--));
      for (j = 0; j < 3; j++)
      {
	 si = (si + 16) & 127;
	 sum += WINMULT(vbuf[bx], (*coef--));
	 bx = (bx + 16) & 127;
	 sum += WINMULT(vbuf[si], (*coef--));
      }
      si = (si + (16 - 1 + 4 * 16)) & 127;
      bx = (bx + (5 * 16 + 1)) & 127;
      coef -= (3 * 7);
      sum >>= WINBITS;
      if (sum > 32767)
	 sum = 32767;
      else if (sum < -32768)
	 sum = -32768;
      *pcm = (unsigned char) ((sum >> 8) ^ 0x80);
      pcm += 2;
   }
}
/*------------------------------------------------------------*/
/*--------------- 8 pt dual window (interleaved output) --------------*/
void i_windowB8_dual_right(WININT * vbuf, int vb_ptr, unsigned char *pcm)
{
   int i, j;
   unsigned int si, bx;
   WINCOEF *coef;
   INT32 sum;

/* right identical to dual, for asm */

/*-- first 4 --*/
   si = (vb_ptr + (4 + 3 * 16)) & 127;
   bx = (si + (8 + 128 - 3 * 16 + 2 * 16)) & 127;
   coef = iwincoef;
   for (i = 0; i < 4; i++)
   {
      sum = -WINMULT(vbuf[bx], (*coef++));
      for (j = 0; j < 3; j++)
      {
	 bx = (bx + 16) & 127;
	 sum += WINMULT(vbuf[si], (*coef++));
	 si = (si + 16) & 127;
	 sum -= WINMULT(vbuf[bx], (*coef++));
      }
      si = (si + (5 * 16 + 1)) & 127;
      bx = (bx + (16 + 4 * 16 - 1)) & 127;
      coef += (3 * 7);
      sum >>= WINBITS;
      if (sum > 32767)
	 sum = 32767;
      else if (sum < -32768)
	 sum = -32768;
      *pcm = (unsigned char) ((sum >> 8) ^ 0x80);
      pcm += 2;
   }
/*--  special case --*/
   bx = (bx + (128 - 16)) & 127;
   sum = WINMULT(vbuf[bx], (*coef++));
   for (j = 0; j < 3; j++)
   {
      bx = (bx + 16) & 127;
      sum += WINMULT(vbuf[bx], (*coef++));
   }
   sum >>= WINBITS;
   if (sum > 32767)
      sum = 32767;
   else if (sum < -32768)
      sum = -32768;
   *pcm = (unsigned char) ((sum >> 8) ^ 0x80);
   pcm += 2;

/*-- last 3 --*/
   coef = iwincoef + (111 - 3 * 7);	/* back pass through coefs */
   si = (si + (128 - 3 * 16 + 2 * 16 - 1)) & 127;
   bx = (bx + (16 + 3 * 16 + 2 * 16 + 1)) & 127;
   for (i = 0; i < 3; i++)
   {
      sum = WINMULT(vbuf[si], (*coef--));
      for (j = 0; j < 3; j++)
      {
	 si = (si + 16) & 127;
	 sum += WINMULT(vbuf[bx], (*coef--));
	 bx = (bx + 16) & 127;
	 sum += WINMULT(vbuf[si], (*coef--));
      }
      si = (si + (16 - 1 + 4 * 16)) & 127;
      bx = (bx + (5 * 16 + 1)) & 127;
      coef -= (3 * 7);
      sum >>= WINBITS;
      if (sum > 32767)
	 sum = 32767;
      else if (sum < -32768)
	 sum = -32768;
      *pcm = (unsigned char) ((sum >> 8) ^ 0x80);
      pcm += 2;
   }
}
/*--------------------------------------------------------*/
