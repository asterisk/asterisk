/*
 * BSD Telephony Of Mexico "Tormenta" Tone Zone Support 2/22/01
 * 
 * Working with the "Tormenta ISA" Card 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 * Primary Author: Mark Spencer <markster@linux-support.net>
 *
 */

#ifndef _ASTERISK_ZONEDATA_H
#define _ASTERISK_ZONEDATA_H

#define	ZT_MAX_CADENCE	16
#define	ZT_TONE_MAX	16

struct tone_zone_sound {
	int toneid;
	char data[256];				/* Actual zone description */
	/* Description is a series of tones of the format:
	   [!]freq1[+freq2][/time] separated by commas.  There
	   are no spaces.  The sequence is repeated back to the 
	   first tone description not preceeded by !.  time is
	   specified in milliseconds */
};

struct tone_zone {
	int zone;					/* Zone number */
	char country[10];				/* Country code */
	char description[40];				/* Description */
	int ringcadence[ZT_MAX_CADENCE];		/* Ring cadence */
	struct tone_zone_sound tones[ZT_TONE_MAX];
};

extern struct tone_zone builtin_zones[];

#define ZT_TONE_DIALTONE        0
#define ZT_TONE_BUSY            1
#define ZT_TONE_RINGTONE        2
#define ZT_TONE_CONGESTION      3
#define ZT_TONE_CALLWAIT        4
#define ZT_TONE_DIALRECALL      5
#define ZT_TONE_RECORDTONE      6
#define ZT_TONE_INFO            7
#define ZT_TONE_CUST1           8
#define ZT_TONE_CUST2           9

/* Retrieve a raw tone zone structure */
extern struct tone_zone *tone_zone_find(char *country);

#endif
