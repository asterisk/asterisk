/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * MP3 Header Analysis Routines.  Thanks to Robert Kaye for the logic!
 *
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

static int bitrates1[] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 };
static int bitrates2[] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160 };

static int samplerates1[] = { 44100, 48000, 32000 };
static int samplerates2[] = { 22050, 24000, 16000 };

static int outputsamples[] = { 576, 1152 };

static int mp3_samples(unsigned char *header)
{
	int ver = (header[1] & 0x8) >> 3;
	return outputsamples[ver];
}

static int mp3_bitrate(unsigned char *header)
{
	int ver = (header[1] & 0x8) >> 3;
	int br = (header[2] >> 4);

	if (ver > 14) {
		ast_log(LOG_WARNING, "Invalid bit rate\n");
		return -1;
	}
	if (ver)
		return bitrates1[br];
	else {
		return bitrates2[br];
	}
}

static int mp3_samplerate(unsigned char *header)
{
	int ver = (header[1] & 0x8) >> 3;
	int sr = (header[2] >> 2) & 0x3;
	
	if (ver > 2) {
		ast_log(LOG_WARNING, "Invalid sample rate\n");
		return -1;
	}

	if (ver)
		return samplerates1[sr];
	else
		return samplerates2[sr];
}

static int mp3_padding(unsigned char *header)
{
	return (header[2] >> 1) & 0x1;	
}

static int mp3_badheader(unsigned char *header)
{
	if ((header[0] != 0xFF) || ((header[1] & 0xF0) != 0xF0))
		return -1;
	return 0;
}

static int mp3_framelen(unsigned char *header)
{
	int br = mp3_bitrate(header);
	int sr = mp3_samplerate(header);
	int size;
	
	if ((br < 0) || (sr < 0))
		return -1;
	size = 144000 * br / sr + mp3_padding(header);
	return size;
}

