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

/*---------------------------------------------------------------

mpeg Layer II audio decoder, integer version
variable type control

-----------------------------------------------------------------*/
/*-----------------------------------------------------------------
Variable types can have a large impact on performance.  If the
native type int is 32 bit or better, setting all variables to the
native int is probably the best bet.  Machines with fast floating
point handware will probably run faster with the floating point
version of this decoder.

On 16 bit machines, use the native 16 bit int where possible
with special consideration given to the multiplies used in
the dct and window (see below).


The code uses the type INT32 when 32 or more bits are required.
Use the native int if possible.
Signed types are required for all but DCTCOEF which may be unsigned.

THe major parts of the decoder are: bit stream unpack (iup.c),
dct (cidct.c), and window (iwinq.c).  The compute time relationship
is usually  unpack < dct < window.

-------------------------------------------------------------------*/

/*-------------- dct cidct.c -------------------------------------------
dct input is type SAMPLEINT, output is WININT

DCTCOEF:  dct coefs, 16 or more bits required
DCTBITS:  fractional bits in dct coefs. Coefs are unsigned in
          the range 0.50 to 10.2.  DCTBITS=10 is a compromise
          between precision and the possibility of overflowing
          intermediate results.


DCTSATURATE:  If set, saturates dct output to 16 bit.
              Dct output may overflow if WININT is 16 bit, overflow
              is rare, but very noisy.  Define to 1 for 16 bit WININT.
              Define to 0 otherwise.

The multiply used in the dct (routine forward_bf in cidct.c) requires
the multiplication of a 32 bit variable by a 16 bit unsigned coef
to produce a signed 32 bit result. On 16 bit machines this could be
faster than a full 32x32 multiply.

------------------------------------------------------------------*/
/*-------------- WINDOW iwinq.c ---------------------------------------
window input is type WININT, output is short (16 bit pcm audio)

window coefs WINBITS fractional bits,
        coefs are signed range in -1.15 to 0.96.
        WINBITS=14 is maximum for 16 bit signed representation.
        Some CPU's will multiply faster with fewer bits.
        WINBITS less that 8 may cause noticeable quality loss.

WINMULT defines the multiply used in the window (iwinq.c)
WINMULT must produce a 32 bit (or better) result, although both
multipliers may be 16 bit.  A 16x16-->32 multiply may offer
a performance advantage if the compiler can be coerced into
doing the right thing.
------------------------------------------------------------------*/
/*-- settings for MS C++ 4.0 flat 32 bit (long=int=32bit) --*/
/*-- asm replacement modules must use these settings ---*/

typedef long INT32;
typedef unsigned long UINT32;

typedef int SAMPLEINT;

typedef int DCTCOEF;

#define DCTBITS 10
#define DCTSATURATE 0

typedef int WININT;
typedef int WINCOEF;

#define WINBITS 10
#define WINMULT(x,coef)  ((x)*(coef))
