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

/*---- wcvt.c --------------------------------------------

conditional inclusion to wavep.c

pcm conversion to wave format
  for big endians or when sizeof(short) > 16 bits

mod 1/9/97 warnings

-----------------------------------------------------------*/
static int bytes_per_samp = 1;
static int big_ender;
static int cvt_flag;

/*-------------------------------------------------------*/
void cvt_to_wave_init(int bits)
{

   big_ender = 1;
   if ((*(unsigned char *) &big_ender) == 1)
      big_ender = 0;

/*--- printf("\n big_ender = %d", big_ender );  ---*/

   if (bits == 8)
      bytes_per_samp = 1;
   else
      bytes_per_samp = sizeof(short);


   cvt_flag = 0;
   if (bits > 8)
   {
      if (big_ender)
	 cvt_flag = 1;
      cvt_flag |= (sizeof(short) > 2);
   }

}
/*-------------------------------------------------------*/
unsigned int cvt_to_wave(unsigned char *pcm, unsigned int bytes_in)
{
   unsigned int i, k;
   unsigned int nsamp;
   short tmp;
   unsigned short *w;

// printf("\n wave convert");

   if (cvt_flag == 0)
      return bytes_in;
/*-- no conversion required --*/

   nsamp = bytes_in / bytes_per_samp;
   w = (unsigned short *) pcm;
   for (i = 0, k = 0; i < nsamp; i++, k += 2)
   {
      tmp = w[i];
      pcm[k] = (unsigned char) tmp;
      pcm[k + 1] = (unsigned char) (tmp >> 8);
   }

   return (nsamp << 1);
/*--- return bytes out ---*/
}
/*-------------------------------------------------------*/
