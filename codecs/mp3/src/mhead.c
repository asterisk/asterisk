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

/*------------ mhead.c ----------------------------------------------
  mpeg audio
  extract info from mpeg header
  portable version (adapted from c:\eco\mhead.c

  add Layer III

  mods 6/18/97 re mux restart, 32 bit ints

  mod 5/7/98 parse mpeg 2.5

---------------------------------------------------------------------*/
#include <stdlib.h>
#include <stdio.h>
#include <float.h>
#include <math.h>
#include "L3.h"
#include "mhead.h"		/* mpeg header structure */

static int mp_br_table[2][16] =
{{0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
 {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0}};
static int mp_sr20_table[2][4] =
{{441, 480, 320, -999}, {882, 960, 640, -999}};

static int mp_br_tableL1[2][16] =
{{0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0},/* mpeg2 */
 {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0}};

static int mp_br_tableL3[2][16] =
{{0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},      /* mpeg 2 */
 {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0}};



static int find_sync(unsigned char *buf, int n);
static int sync_scan(unsigned char *buf, int n, int i0);
static int sync_test(unsigned char *buf, int n, int isync, int padbytes);

// jdw
extern unsigned int g_jdw_additional;

/*--------------------------------------------------------------*/
int head_info(unsigned char *buf, unsigned int n, MPEG_HEAD * h)
{
   int framebytes;
   int mpeg25_flag;
  
   if (n > 10000)
      n = 10000;		/* limit scan for free format */



   h->sync = 0;
   //if ((buf[0] == 0xFF) && ((buf[1] & 0xF0) == 0xF0))
   if ((buf[0] == 0xFF) && ((buf[0+1] & 0xF0) == 0xF0))
   {
      mpeg25_flag = 0;		// mpeg 1 & 2

   }
   else if ((buf[0] == 0xFF) && ((buf[0+1] & 0xF0) == 0xE0))
   {
      mpeg25_flag = 1;		// mpeg 2.5

   }
   else
      return 0;			// sync fail

   h->sync = 1;
   if (mpeg25_flag)
      h->sync = 2;		//low bit clear signals mpeg25 (as in 0xFFE)

   h->id = (buf[0+1] & 0x08) >> 3;
   h->option = (buf[0+1] & 0x06) >> 1;
   h->prot = (buf[0+1] & 0x01);

   h->br_index = (buf[0+2] & 0xf0) >> 4;
   h->sr_index = (buf[0+2] & 0x0c) >> 2;
   h->pad = (buf[0+2] & 0x02) >> 1;
   h->private_bit = (buf[0+2] & 0x01);
   h->mode = (buf[0+3] & 0xc0) >> 6;
   h->mode_ext = (buf[0+3] & 0x30) >> 4;
   h->cr = (buf[0+3] & 0x08) >> 3;
   h->original = (buf[0+3] & 0x04) >> 2;
   h->emphasis = (buf[0+3] & 0x03);


// if( mpeg25_flag ) {
 //    if( h->sr_index == 2 ) return 0;   // fail 8khz
 //}


/* compute framebytes for Layer I, II, III */
   if (h->option < 1)
      return 0;
   if (h->option > 3)
      return 0;

   framebytes = 0;

   if (h->br_index > 0)
   {
      if (h->option == 3)
      {				/* layer I */
	 framebytes =
	    240 * mp_br_tableL1[h->id][h->br_index]
	    / mp_sr20_table[h->id][h->sr_index];
	 framebytes = 4 * framebytes;
      }
      else if (h->option == 2)
      {				/* layer II */
	 framebytes =
	    2880 * mp_br_table[h->id][h->br_index]
	    / mp_sr20_table[h->id][h->sr_index];
      }
      else if (h->option == 1)
      {				/* layer III */
	 if (h->id)
	 {			// mpeg1

	    framebytes =
	       2880 * mp_br_tableL3[h->id][h->br_index]
	       / mp_sr20_table[h->id][h->sr_index];
	 }
	 else
	 {			// mpeg2

	    if (mpeg25_flag)
	    {			// mpeg2.2

	       framebytes =
		  2880 * mp_br_tableL3[h->id][h->br_index]
		  / mp_sr20_table[h->id][h->sr_index];
	    }
	    else
	    {
	       framebytes =
		  1440 * mp_br_tableL3[h->id][h->br_index]
		  / mp_sr20_table[h->id][h->sr_index];
	    }
	 }
      }
   }
   else
      framebytes = find_sync(buf, n);	/* free format */

  return framebytes;
}

int head_info3(unsigned char *buf, unsigned int n, MPEG_HEAD *h, int *br, unsigned int *searchForward) {
	unsigned int pBuf = 0;
	// jdw insertion...
   while ((pBuf < n) && !((buf[pBuf] == 0xFF) && 
          ((buf[pBuf+1] & 0xF0) == 0xF0 || (buf[pBuf+1] & 0xF0) == 0xE0))) 
   {
		pBuf++;
   }

   if (pBuf == n) return 0;

   *searchForward = pBuf;
   return head_info2(&(buf[pBuf]),n,h,br);

}

/*--------------------------------------------------------------*/
int head_info2(unsigned char *buf, unsigned int n, MPEG_HEAD * h, int *br)
{
   int framebytes;

/*---  return br (in bits/sec) in addition to frame bytes ---*/

   *br = 0;
/*-- assume fail --*/
   framebytes = head_info(buf, n, h);

   if (framebytes == 0)
      return 0; 

   if (h->option == 1)
   {				/* layer III */
      if (h->br_index > 0)
	 *br = 1000 * mp_br_tableL3[h->id][h->br_index];
      else
      {
	 if (h->id)		// mpeg1

	    *br = 1000 * framebytes * mp_sr20_table[h->id][h->sr_index] / (144 * 20);
	 else
	 {			// mpeg2

	    if ((h->sync & 1) == 0)	//  flags mpeg25

	       *br = 500 * framebytes * mp_sr20_table[h->id][h->sr_index] / (72 * 20);
	    else
	       *br = 1000 * framebytes * mp_sr20_table[h->id][h->sr_index] / (72 * 20);
	 }
      }
   }
   if (h->option == 2)
   {				/* layer II */
      if (h->br_index > 0)
	 *br = 1000 * mp_br_table[h->id][h->br_index];
      else
	 *br = 1000 * framebytes * mp_sr20_table[h->id][h->sr_index]
	    / (144 * 20);
   }
   if (h->option == 3)
   {				/* layer I */
      if (h->br_index > 0)
	 *br = 1000 * mp_br_tableL1[h->id][h->br_index];
      else
	 *br = 1000 * framebytes * mp_sr20_table[h->id][h->sr_index]
	    / (48 * 20);
   }


   return framebytes;
}
/*--------------------------------------------------------------*/
static int compare(unsigned char *buf, unsigned char *buf2)
{
   if (buf[0] != buf2[0])
      return 0;
   if (buf[1] != buf2[1])
      return 0;
   return 1;
}
/*----------------------------------------------------------*/
/*-- does not scan for initial sync, initial sync assumed --*/
static int find_sync(unsigned char *buf, int n)
{
   int i0, isync, nmatch, pad;
   int padbytes, option;

/* mod 4/12/95 i0 change from 72, allows as low as 8kbits for mpeg1 */
   i0 = 24;
   padbytes = 1;
   option = (buf[1] & 0x06) >> 1;
   if (option == 3)
   {
      padbytes = 4;
      i0 = 24;			/* for shorter layer I frames */
   }

   pad = (buf[2] & 0x02) >> 1;

   n -= 3;			/*  need 3 bytes of header  */

   while (i0 < 2000)
   {
      isync = sync_scan(buf, n, i0);
      i0 = isync + 1;
      isync -= pad;
      if (isync <= 0)
	 return 0;
      nmatch = sync_test(buf, n, isync, padbytes);
      if (nmatch > 0)
	 return isync;
   }

   return 0;
}
/*------------------------------------------------------*/
/*---- scan for next sync, assume start is valid -------*/
/*---- return number bytes to next sync ----------------*/
static int sync_scan(unsigned char *buf, int n, int i0)
{
   int i;

   for (i = i0; i < n; i++)
      if (compare(buf, buf + i))
	 return i;

   return 0;
}
/*------------------------------------------------------*/
/*- test consecutative syncs, input isync without pad --*/
static int sync_test(unsigned char *buf, int n, int isync, int padbytes)
{
   int i, nmatch, pad;

   nmatch = 0;
   for (i = 0;;)
   {
      pad = padbytes * ((buf[i + 2] & 0x02) >> 1);
      i += (pad + isync);
      if (i > n)
	 break;
      if (!compare(buf, buf + i))
	 return -nmatch;
      nmatch++;
   }
   return nmatch;
}
