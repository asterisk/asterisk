/*
 * BSD Telephony Of Mexico "Tormenta" Tone Zone Support 2/22/01
 * 
 * Working with the "Tormenta ISA" Card 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under thet erms of the GNU Lesser General Public License as published by
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
 * This information from ITU E.180 Supplement 2.
 * UK information from BT SIN 350 Issue 1.1
 */
#include <asterisk/zonedata.h>

struct tone_zone builtin_zones[]  =
{
	{ 0, "us", "United States / North America", { 2000, 4000 }, 
	{
		{ ZT_TONE_DIALTONE, "350+440" },
		{ ZT_TONE_BUSY, "480+620/500,0/500" },
		{ ZT_TONE_RINGTONE, "440+480/2000,0/4000" },
		{ ZT_TONE_CONGESTION, "480+620/250,0/250" },
		{ ZT_TONE_CALLWAIT, "440/300,0/10000" },
		{ ZT_TONE_DIALRECALL, "!350+440/100,!0/100,!350+440/100,!0/100,!350+440/100,!0/100,350+440" },
		{ ZT_TONE_RECORDTONE, "1400/500,0/15000" },
		{ ZT_TONE_INFO, "!950/330,!1400/330,!1800/330,0" } }
	},
	{ 1, "au", "Australia", {  400, 200, 400, 2000 },
	{
		/* XXX Dialtone: Should be modulated, not added XXX */
		{ ZT_TONE_DIALTONE, "425+25" },		
		{ ZT_TONE_BUSY, "400/375,0/375" },
		{ ZT_TONE_RINGTONE, "400+17/400,0/200,400+17/400,0/2000" },
		/* XXX Congestion: Should reduce by 10 db every other cadence XXX */
		{ ZT_TONE_CONGESTION, "400/375,0/375" }, 
		{ ZT_TONE_CALLWAIT, "425/100,0/100,525/100,0/4700" },
		{ ZT_TONE_DIALRECALL, "!425+25/100!0/100,!425+25/100,!0/100,!425+25/100,!0/100,425+25" },
		{ ZT_TONE_RECORDTONE, "1400/425,0/14525" },
		{ ZT_TONE_INFO, "400/2500,0/500" } }
	},
	{ 2, "fr", "France", { 1500, 3500 },
	{
		/* Dialtone can also be 440+330 */
		{ ZT_TONE_DIALTONE, "440" },
		{ ZT_TONE_BUSY, "440/500,0/500" },
		{ ZT_TONE_RINGTONE, "440/1500,0/3500" },
		/* XXX I'm making up the congestion tone XXX */
		{ ZT_TONE_CONGESTION, "440/250,0/250" },
		/* XXX I'm making up the call wait tone too XXX */
		{ ZT_TONE_CALLWAIT, "440/300,0/10000" },
		/* XXX I'm making up dial recall XXX */
		{ ZT_TONE_DIALRECALL, "!350+440/100,!0/100,!350+440/100,!0/100,!350+440/100,!0/100,350+440" },
		/* XXX I'm making up the record tone XXX */
		{ ZT_TONE_RECORDTONE, "1400/500,0/15000" },
		{ ZT_TONE_INFO, "!950/330,!1400/330,!1800/330,0" } }
	},
	{ 3, "nl", "Netherlands", { 1000, 4000 },
	{
		/* Most of these 425's can also be 450's */
		{ ZT_TONE_DIALTONE, "425" },
		{ ZT_TONE_BUSY, "425/500,0/500" },
		{ ZT_TONE_RINGTONE, "425/1000,0/4000" },
		{ ZT_TONE_CONGESTION, "425/250,0/250" },
		/* XXX I'm making up the call wait tone XXX */
		{ ZT_TONE_CALLWAIT, "440/300,0/10000" },
		/* XXX Assuming this is "Special Dial Tone" XXX */
		{ ZT_TONE_DIALRECALL, "425/500,0/50" },
		/* XXX I'm making up the record tone XXX */
		{ ZT_TONE_RECORDTONE, "1400/500,0/15000" },
		{ ZT_TONE_INFO, "!950/330,!1400/330,!1800/330,0" } }
	},
	{ 4, "uk", "United Kingdom", { 400, 200, 400, 2000 },
	{
		{ ZT_TONE_DIALTONE, "350+440" },
		{ ZT_TONE_BUSY, "400/375,0/375" },
		{ ZT_TONE_RINGTONE, "400+450/400,0/200,400+450/400,0/2000" },
		{ ZT_TONE_CONGESTION, "400/400,0/350,400/225,0/525" },
		{ ZT_TONE_CALLWAIT, "440/100,0/4000" },
		{ ZT_TONE_DIALRECALL, "350+440" },
		/* Not sure about the RECORDTONE */
		{ ZT_TONE_RECORDTONE, "1400/500,0/10000" },
		{ ZT_TONE_INFO, "950/330,1400/330,1800/330,0" } }
	},
		
	{ -1 }		
};
