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

/****  csbtL3.c  ***************************************************

layer III

  include to  csbt.c

******************************************************************/
/*============================================================*/
/*============ Layer III =====================================*/
/*============================================================*/
void sbt_mono_L3(MPEG *m, float *sample, short *pcm, int ch)
{
   int i;

   ch = 0;
   for (i = 0; i < 18; i++)
   {
      fdct32(m,sample, m->csbt.vbuf + m->csbt.vb_ptr);
      window(m->csbt.vbuf, m->csbt.vb_ptr, pcm);
      sample += 32;
      m->csbt.vb_ptr = (m->csbt.vb_ptr - 32) & 511;
      pcm += 32;
   }

}
/*------------------------------------------------------------*/
void sbt_dual_L3(MPEG *m, float *sample, short *pcm, int ch)
{
   int i;

   if (ch == 0)
      for (i = 0; i < 18; i++)
      {
	 fdct32(m,sample, m->csbt.vbuf + m->csbt.vb_ptr);
	 window_dual(m,m->csbt.vbuf, m->csbt.vb_ptr, pcm);
	 sample += 32;
	 m->csbt.vb_ptr = (m->csbt.vb_ptr - 32) & 511;
	 pcm += 64;
      }
   else
      for (i = 0; i < 18; i++)
      {
	 fdct32(m,sample, m->csbt.vbuf2 + m->csbt.vb2_ptr);
	 window_dual(m,m->csbt.vbuf2, m->csbt.vb2_ptr, pcm + 1);
	 sample += 32;
	 m->csbt.vb2_ptr = (m->csbt.vb2_ptr - 32) & 511;
	 pcm += 64;
      }


}
/*------------------------------------------------------------*/
/*------------------------------------------------------------*/
/*---------------- 16 pt sbt's  -------------------------------*/
/*------------------------------------------------------------*/
void sbt16_mono_L3(MPEG *m, float *sample, short *pcm, int ch)
{
   int i;

   ch = 0;
   for (i = 0; i < 18; i++)
   {
      fdct16(m,sample, m->csbt.vbuf + m->csbt.vb_ptr);
      window16(m,m->csbt.vbuf, m->csbt.vb_ptr, pcm);
      sample += 32;
      m->csbt.vb_ptr = (m->csbt.vb_ptr - 16) & 255;
      pcm += 16;
   }


}
/*------------------------------------------------------------*/
void sbt16_dual_L3(MPEG *m, float *sample, short *pcm, int ch)
{
   int i;


   if (ch == 0)
   {
      for (i = 0; i < 18; i++)
      {
	 fdct16(m,sample, m->csbt.vbuf + m->csbt.vb_ptr);
	 window16_dual(m,m->csbt.vbuf, m->csbt.vb_ptr, pcm);
	 sample += 32;
	 m->csbt.vb_ptr = (m->csbt.vb_ptr - 16) & 255;
	 pcm += 32;
      }
   }
   else
   {
      for (i = 0; i < 18; i++)
      {
	 fdct16(m,sample, m->csbt.vbuf2 + m->csbt.vb2_ptr);
	 window16_dual(m,m->csbt.vbuf2, m->csbt.vb2_ptr, pcm + 1);
	 sample += 32;
	 m->csbt.vb2_ptr = (m->csbt.vb2_ptr - 16) & 255;
	 pcm += 32;
      }
   }

}
/*------------------------------------------------------------*/
/*---------------- 8 pt sbt's  -------------------------------*/
/*------------------------------------------------------------*/
void sbt8_mono_L3(MPEG *m, float *sample, short *pcm, int ch)
{
   int i;

   ch = 0;
   for (i = 0; i < 18; i++)
   {
      fdct8(m,sample, m->csbt.vbuf + m->csbt.vb_ptr);
      window8(m,m->csbt.vbuf, m->csbt.vb_ptr, pcm);
      sample += 32;
      m->csbt.vb_ptr = (m->csbt.vb_ptr - 8) & 127;
      pcm += 8;
   }

}
/*------------------------------------------------------------*/
void sbt8_dual_L3(MPEG *m, float *sample, short *pcm, int ch)
{
   int i;

   if (ch == 0)
   {
      for (i = 0; i < 18; i++)
      {
	 fdct8(m,sample, m->csbt.vbuf + m->csbt.vb_ptr);
	 window8_dual(m,m->csbt.vbuf, m->csbt.vb_ptr, pcm);
	 sample += 32;
	 m->csbt.vb_ptr = (m->csbt.vb_ptr - 8) & 127;
	 pcm += 16;
      }
   }
   else
   {
      for (i = 0; i < 18; i++)
      {
	 fdct8(m,sample, m->csbt.vbuf2 + m->csbt.vb2_ptr);
	 window8_dual(m,m->csbt.vbuf2, m->csbt.vb2_ptr, pcm + 1);
	 sample += 32;
	 m->csbt.vb2_ptr = (m->csbt.vb2_ptr - 8) & 127;
	 pcm += 16;
      }
   }



}
/*------------------------------------------------------------*/
/*------- 8 bit output ---------------------------------------*/
/*------------------------------------------------------------*/
void sbtB_mono_L3(MPEG *m, float *sample, unsigned char *pcm, int ch)
{
   int i;

   ch = 0;
   for (i = 0; i < 18; i++)
   {
      fdct32(m,sample, m->csbt.vbuf + m->csbt.vb_ptr);
      windowB(m,m->csbt.vbuf, m->csbt.vb_ptr, pcm);
      sample += 32;
      m->csbt.vb_ptr = (m->csbt.vb_ptr - 32) & 511;
      pcm += 32;
   }

}
/*------------------------------------------------------------*/
void sbtB_dual_L3(MPEG *m, float *sample, unsigned char *pcm, int ch)
{
   int i;

   if (ch == 0)
      for (i = 0; i < 18; i++)
      {
	 fdct32(m,sample, m->csbt.vbuf + m->csbt.vb_ptr);
	 windowB_dual(m,m->csbt.vbuf, m->csbt.vb_ptr, pcm);
	 sample += 32;
	 m->csbt.vb_ptr = (m->csbt.vb_ptr - 32) & 511;
	 pcm += 64;
      }
   else
      for (i = 0; i < 18; i++)
      {
	 fdct32(m,sample, m->csbt.vbuf2 + m->csbt.vb2_ptr);
	 windowB_dual(m,m->csbt.vbuf2, m->csbt.vb2_ptr, pcm + 1);
	 sample += 32;
	 m->csbt.vb2_ptr = (m->csbt.vb2_ptr - 32) & 511;
	 pcm += 64;
      }

}
/*------------------------------------------------------------*/
/*------------------------------------------------------------*/
/*---------------- 16 pt sbtB's  -------------------------------*/
/*------------------------------------------------------------*/
void sbtB16_mono_L3(MPEG *m, float *sample, unsigned char *pcm, int ch)
{
   int i;

   ch = 0;
   for (i = 0; i < 18; i++)
   {
      fdct16(m,sample, m->csbt.vbuf + m->csbt.vb_ptr);
      windowB16(m,m->csbt.vbuf, m->csbt.vb_ptr, pcm);
      sample += 32;
      m->csbt.vb_ptr = (m->csbt.vb_ptr - 16) & 255;
      pcm += 16;
   }


}
/*------------------------------------------------------------*/
void sbtB16_dual_L3(MPEG *m, float *sample, unsigned char *pcm, int ch)
{
   int i;

   if (ch == 0)
   {
      for (i = 0; i < 18; i++)
      {
	 fdct16(m,sample, m->csbt.vbuf + m->csbt.vb_ptr);
	 windowB16_dual(m,m->csbt.vbuf, m->csbt.vb_ptr, pcm);
	 sample += 32;
	 m->csbt.vb_ptr = (m->csbt.vb_ptr - 16) & 255;
	 pcm += 32;
      }
   }
   else
   {
      for (i = 0; i < 18; i++)
      {
	 fdct16(m,sample, m->csbt.vbuf2 + m->csbt.vb2_ptr);
	 windowB16_dual(m,m->csbt.vbuf2, m->csbt.vb2_ptr, pcm + 1);
	 sample += 32;
	 m->csbt.vb2_ptr = (m->csbt.vb2_ptr - 16) & 255;
	 pcm += 32;
      }
   }

}
/*------------------------------------------------------------*/
/*---------------- 8 pt sbtB's  -------------------------------*/
/*------------------------------------------------------------*/
void sbtB8_mono_L3(MPEG *m, float *sample, unsigned char *pcm, int ch)
{
   int i;

   ch = 0;
   for (i = 0; i < 18; i++)
   {
      fdct8(m,sample, m->csbt.vbuf + m->csbt.vb_ptr);
      windowB8(m,m->csbt.vbuf, m->csbt.vb_ptr, pcm);
      sample += 32;
      m->csbt.vb_ptr = (m->csbt.vb_ptr - 8) & 127;
      pcm += 8;
   }

}
/*------------------------------------------------------------*/
void sbtB8_dual_L3(MPEG *m, float *sample, unsigned char *pcm, int ch)
{
   int i;

   if (ch == 0)
   {
      for (i = 0; i < 18; i++)
      {
	 fdct8(m,sample, m->csbt.vbuf + m->csbt.vb_ptr);
	 windowB8_dual(m,m->csbt.vbuf, m->csbt.vb_ptr, pcm);
	 sample += 32;
	 m->csbt.vb_ptr = (m->csbt.vb_ptr - 8) & 127;
	 pcm += 16;
      }
   }
   else
   {
      for (i = 0; i < 18; i++)
      {
	 fdct8(m,sample, m->csbt.vbuf2 + m->csbt.vb2_ptr);
	 windowB8_dual(m,m->csbt.vbuf2, m->csbt.vb2_ptr, pcm + 1);
	 sample += 32;
	 m->csbt.vb2_ptr = (m->csbt.vb2_ptr - 8) & 127;
	 pcm += 16;
      }
   }

}
/*------------------------------------------------------------*/
