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

#ifndef O_BINARY
#define O_BINARY 0
#endif


/*--- no kb function unless DOS ---*/

#ifndef KB_OK
#ifdef __MSDOS__
#define KB_OK
#endif
#ifdef _CONSOLE
#define KB_OK
#endif
#endif

#ifdef NEED_KBHIT
#ifdef KB_OK
#ifdef _MSC_VER
#pragma warning(disable: 4032)
#endif
#include <conio.h>
#else
static int kbhit()
{
   return 0;
}
static int getch()
{
   return 0;
}
#endif
#endif

/*-- no pcm conversion to wave required 
 if short = 16 bits and little endian ---*/

/* mods 1/9/97 LITTLE_SHORT16 detect */

#ifndef LITTLE_SHORT16
  #ifdef __MSDOS__
    #undef LITTLE_SHORT16
    #define LITTLE_SHORT16
  #endif
  #ifdef WIN32
    #undef LITTLE_SHORT16
    #define LITTLE_SHORT16
  #endif
  #ifdef _M_IX86
    #undef LITTLE_SHORT16
    #define LITTLE_SHORT16
  #endif
#endif


// JDW //
//#ifdef LITTLE_SHORT16
//#define cvt_to_wave_init(a)
//#define cvt_to_wave(a, b)  b
//#else
//void cvt_to_wave_init(int bits);
//unsigned int cvt_to_wave(void *a, unsigned int b);
//
//#endif
#ifdef LITTLE_SHORT16
#define cvt_to_wave_init(a)
#define cvt_to_wave(a, b) b
#else
void cvt_to_wave_init(int);
unsigned int cvt_to_wave(unsigned char *,unsigned int);
#endif

int cvt_to_wave_test(void);
