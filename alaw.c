/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * u-Law to Signed linear conversion
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION("$Revision$")

#include "asterisk/alaw.h"

#define AMI_MASK 0x55

static inline unsigned char linear2alaw (short int linear)
{
    int mask;
    int seg;
    int pcm_val;
    static int seg_end[8] =
    {
         0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF, 0x3FFF, 0x7FFF
    };
    
    pcm_val = linear;
    if (pcm_val >= 0)
    {
        /* Sign (7th) bit = 1 */
        mask = AMI_MASK | 0x80;
    }
    else
    {
        /* Sign bit = 0 */
        mask = AMI_MASK;
        pcm_val = -pcm_val;
    }

    /* Convert the scaled magnitude to segment number. */
    for (seg = 0;  seg < 8;  seg++)
    {
        if (pcm_val <= seg_end[seg])
	    break;
    }
    /* Combine the sign, segment, and quantization bits. */
    return  ((seg << 4) | ((pcm_val >> ((seg)  ?  (seg + 3)  :  4)) & 0x0F)) ^ mask;
}
/*- End of function --------------------------------------------------------*/

static inline short int alaw2linear (unsigned char alaw)
{
    int i;
    int seg;

    alaw ^= AMI_MASK;
    i = ((alaw & 0x0F) << 4);
    seg = (((int) alaw & 0x70) >> 4);
    if (seg)
        i = (i + 0x100) << (seg - 1);
    return (short int) ((alaw & 0x80)  ?  i  :  -i);
}

unsigned char __ast_lin2a[8192];
short __ast_alaw[256];

void ast_alaw_init(void)
{
	int i;
	/* 
	 *  Set up mu-law conversion table
	 */
	for(i = 0;i < 256;i++)
	   {
	        __ast_alaw[i] = alaw2linear(i);
	   }
	  /* set up the reverse (mu-law) conversion table */
	for(i = -32768; i < 32768; i++)
	   {
		__ast_lin2a[((unsigned short)i) >> 3] = linear2alaw(i);
	   }

}

