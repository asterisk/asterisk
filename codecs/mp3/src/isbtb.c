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

/****  isbtb.c  ***************************************************

include to isbt.c


MPEG audio decoder, integer dct and window, 8 bit output


******************************************************************/
/* asm is quick only, c code does not need separate window for right */
/* full is opposite of quick */
#ifdef  FULL_INTEGER
#define i_windowB_dual_right   i_windowB_dual
#define i_windowB16_dual_right i_windowB16_dual
#define i_windowB8_dual_right  i_windowB8_dual
#endif

void i_windowB(WININT * vbuf, int vb_ptr, unsigned char *pcm);
void i_windowB_dual(WININT * vbuf, int vb_ptr, unsigned char *pcm);
void i_windowB_dual_right(WININT * vbuf, int vb_ptr, unsigned char *pcm);
void i_windowB16(WININT * vbuf, int vb_ptr, unsigned char *pcm);
void i_windowB16_dual(WININT * vbuf, int vb_ptr, unsigned char *pcm);
void i_windowB16_dual_right(WININT * vbuf, int vb_ptr, unsigned char *pcm);
void i_windowB8(WININT * vbuf, int vb_ptr, unsigned char *pcm);
void i_windowB8_dual(WININT * vbuf, int vb_ptr, unsigned char *pcm);
void i_windowB8_dual_right(WININT * vbuf, int vb_ptr, unsigned char *pcm);



/*==============================================================*/
/*==============================================================*/
/*==============================================================*/
void i_sbtB_mono(SAMPLEINT * sample, unsigned char *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      i_dct32(sample, vbuf + vb_ptr);
      i_windowB(vbuf, vb_ptr, pcm);
      sample += 64;
      vb_ptr = (vb_ptr - 32) & 511;
      pcm += 32;
   }

}
/*------------------------------------------------------------*/
void i_sbtB_dual(SAMPLEINT * sample, unsigned char *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      i_dct32_dual(sample, vbuf + vb_ptr);
      i_dct32_dual(sample + 1, vbuf2 + vb_ptr);
      i_windowB_dual(vbuf, vb_ptr, pcm);
      i_windowB_dual_right(vbuf2, vb_ptr, pcm + 1);
      sample += 64;
      vb_ptr = (vb_ptr - 32) & 511;
      pcm += 64;
   }
}
/*------------------------------------------------------------*/
/* convert dual to mono */
void i_sbtB_dual_mono(SAMPLEINT * sample, unsigned char *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      i_dct32_dual_mono(sample, vbuf + vb_ptr);
      i_windowB(vbuf, vb_ptr, pcm);
      sample += 64;
      vb_ptr = (vb_ptr - 32) & 511;
      pcm += 32;
   }
}
/*------------------------------------------------------------*/
/* convert dual to left */
void i_sbtB_dual_left(SAMPLEINT * sample, unsigned char *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      i_dct32_dual(sample, vbuf + vb_ptr);
      i_windowB(vbuf, vb_ptr, pcm);
      sample += 64;
      vb_ptr = (vb_ptr - 32) & 511;
      pcm += 32;
   }
}
/*------------------------------------------------------------*/
/* convert dual to right */
void i_sbtB_dual_right(SAMPLEINT * sample, unsigned char *pcm, int n)
{
   int i;

   sample++;			/* point to right chan */
   for (i = 0; i < n; i++)
   {
      i_dct32_dual(sample, vbuf + vb_ptr);
      i_windowB(vbuf, vb_ptr, pcm);
      sample += 64;
      vb_ptr = (vb_ptr - 32) & 511;
      pcm += 32;
   }
}
/*------------------------------------------------------------*/
/*---------------- 16 pt sbt's  -------------------------------*/
/*------------------------------------------------------------*/
void i_sbtB16_mono(SAMPLEINT * sample, unsigned char *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      i_dct16(sample, vbuf + vb_ptr);
      i_windowB16(vbuf, vb_ptr, pcm);
      sample += 64;
      vb_ptr = (vb_ptr - 16) & 255;
      pcm += 16;
   }

}
/*------------------------------------------------------------*/
void i_sbtB16_dual(SAMPLEINT * sample, unsigned char *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      i_dct16_dual(sample, vbuf + vb_ptr);
      i_dct16_dual(sample + 1, vbuf2 + vb_ptr);
      i_windowB16_dual(vbuf, vb_ptr, pcm);
      i_windowB16_dual_right(vbuf2, vb_ptr, pcm + 1);
      sample += 64;
      vb_ptr = (vb_ptr - 16) & 255;
      pcm += 32;
   }

}
/*------------------------------------------------------------*/
void i_sbtB16_dual_mono(SAMPLEINT * sample, unsigned char *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      i_dct16_dual_mono(sample, vbuf + vb_ptr);
      i_windowB16(vbuf, vb_ptr, pcm);
      sample += 64;
      vb_ptr = (vb_ptr - 16) & 255;
      pcm += 16;
   }
}
/*------------------------------------------------------------*/
void i_sbtB16_dual_left(SAMPLEINT * sample, unsigned char *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      i_dct16_dual(sample, vbuf + vb_ptr);
      i_windowB16(vbuf, vb_ptr, pcm);
      sample += 64;
      vb_ptr = (vb_ptr - 16) & 255;
      pcm += 16;
   }
}
/*------------------------------------------------------------*/
void i_sbtB16_dual_right(SAMPLEINT * sample, unsigned char *pcm, int n)
{
   int i;

   sample++;
   for (i = 0; i < n; i++)
   {
      i_dct16_dual(sample, vbuf + vb_ptr);
      i_windowB16(vbuf, vb_ptr, pcm);
      sample += 64;
      vb_ptr = (vb_ptr - 16) & 255;
      pcm += 16;
   }
}
/*------------------------------------------------------------*/
/*---------------- 8 pt sbt's  -------------------------------*/
/*------------------------------------------------------------*/
void i_sbtB8_mono(SAMPLEINT * sample, unsigned char *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      i_dct8(sample, vbuf + vb_ptr);
      i_windowB8(vbuf, vb_ptr, pcm);
      sample += 64;
      vb_ptr = (vb_ptr - 8) & 127;
      pcm += 8;
   }

}
/*------------------------------------------------------------*/
void i_sbtB8_dual(SAMPLEINT * sample, unsigned char *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      i_dct8_dual(sample, vbuf + vb_ptr);
      i_dct8_dual(sample + 1, vbuf2 + vb_ptr);
      i_windowB8_dual(vbuf, vb_ptr, pcm);
      i_windowB8_dual_right(vbuf2, vb_ptr, pcm + 1);
      sample += 64;
      vb_ptr = (vb_ptr - 8) & 127;
      pcm += 16;
   }
}
/*------------------------------------------------------------*/
void i_sbtB8_dual_mono(SAMPLEINT * sample, unsigned char *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      i_dct8_dual_mono(sample, vbuf + vb_ptr);
      i_windowB8(vbuf, vb_ptr, pcm);
      sample += 64;
      vb_ptr = (vb_ptr - 8) & 127;
      pcm += 8;
   }
}
/*------------------------------------------------------------*/
void i_sbtB8_dual_left(SAMPLEINT * sample, unsigned char *pcm, int n)
{
   int i;

   for (i = 0; i < n; i++)
   {
      i_dct8_dual(sample, vbuf + vb_ptr);
      i_windowB8(vbuf, vb_ptr, pcm);
      sample += 64;
      vb_ptr = (vb_ptr - 8) & 127;
      pcm += 8;
   }
}
/*------------------------------------------------------------*/
void i_sbtB8_dual_right(SAMPLEINT * sample, unsigned char *pcm, int n)
{
   int i;

   sample++;
   for (i = 0; i < n; i++)
   {
      i_dct8_dual(sample, vbuf + vb_ptr);
      i_windowB8(vbuf, vb_ptr, pcm);
      sample += 64;
      vb_ptr = (vb_ptr - 8) & 127;
      pcm += 8;
   }
}
/*------------------------------------------------------------*/
