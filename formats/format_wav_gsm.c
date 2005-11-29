/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief Save GSM in the proprietary Microsoft format.
 * 
 */
 
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/sched.h"
#include "asterisk/module.h"
#include "asterisk/endian.h"

#include "msgsm.h"

/* Some Ideas for this code came from makewave.c by Jeffrey Chilton */

/* Portions of the conversion code are by guido@sienanet.it */

/* begin binary data: */
char msgsm_silence[] = /* 65 */
{0x48,0x17,0xD6,0x84,0x02,0x80,0x24,0x49,0x92,0x24,0x89,0x02,0x80,0x24,0x49
,0x92,0x24,0x89,0x02,0x80,0x24,0x49,0x92,0x24,0x89,0x02,0x80,0x24,0x49,0x92
,0x24,0x09,0x82,0x74,0x61,0x4D,0x28,0x00,0x48,0x92,0x24,0x49,0x92,0x28,0x00
,0x48,0x92,0x24,0x49,0x92,0x28,0x00,0x48,0x92,0x24,0x49,0x92,0x28,0x00,0x48
,0x92,0x24,0x49,0x92,0x00};
/* end binary data. size = 65 bytes */

struct ast_filestream {
	void *reserved[AST_RESERVED_POINTERS];
	/* Believe it or not, we must decode/recode to account for the
	   weird MS format */
	/* This is what a filestream means to us */
	FILE *f; /* Descriptor */
	struct ast_frame fr;				/* Frame information */
	char waste[AST_FRIENDLY_OFFSET];	/* Buffer for sending frames, etc */
	char empty;							/* Empty character */
	unsigned char gsm[66];				/* Two Real GSM Frames */
	int foffset;
	int secondhalf;						/* Are we on the second half */
	struct timeval last;
};


AST_MUTEX_DEFINE_STATIC(wav_lock);
static int glistcnt = 0;

static char *name = "wav49";
static char *desc = "Microsoft WAV format (Proprietary GSM)";
static char *exts = "WAV|wav49";

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define htoll(b) (b)
#define htols(b) (b)
#define ltohl(b) (b)
#define ltohs(b) (b)
#else
#if __BYTE_ORDER == __BIG_ENDIAN
#define htoll(b)  \
          (((((b)      ) & 0xFF) << 24) | \
	       ((((b) >>  8) & 0xFF) << 16) | \
		   ((((b) >> 16) & 0xFF) <<  8) | \
		   ((((b) >> 24) & 0xFF)      ))
#define htols(b) \
          (((((b)      ) & 0xFF) << 8) | \
		   ((((b) >> 8) & 0xFF)      ))
#define ltohl(b) htoll(b)
#define ltohs(b) htols(b)
#else
#error "Endianess not defined"
#endif
#endif


static int check_header(FILE *f)
{
	int type, size, formtype;
	int fmt, hsize, fact;
	short format, chans;
	int freq;
	int data;
	if (fread(&type, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Read failed (type)\n");
		return -1;
	}
	if (fread(&size, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Read failed (size)\n");
		return -1;
	}
	size = ltohl(size);
	if (fread(&formtype, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Read failed (formtype)\n");
		return -1;
	}
	if (memcmp(&type, "RIFF", 4)) {
		ast_log(LOG_WARNING, "Does not begin with RIFF\n");
		return -1;
	}
	if (memcmp(&formtype, "WAVE", 4)) {
		ast_log(LOG_WARNING, "Does not contain WAVE\n");
		return -1;
	}
	if (fread(&fmt, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Read failed (fmt)\n");
		return -1;
	}
	if (memcmp(&fmt, "fmt ", 4)) {
		ast_log(LOG_WARNING, "Does not say fmt\n");
		return -1;
	}
	if (fread(&hsize, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Read failed (formtype)\n");
		return -1;
	}
	if (ltohl(hsize) != 20) {
		ast_log(LOG_WARNING, "Unexpected header size %d\n", ltohl(hsize));
		return -1;
	}
	if (fread(&format, 1, 2, f) != 2) {
		ast_log(LOG_WARNING, "Read failed (format)\n");
		return -1;
	}
	if (ltohs(format) != 49) {
		ast_log(LOG_WARNING, "Not a GSM file %d\n", ltohs(format));
		return -1;
	}
	if (fread(&chans, 1, 2, f) != 2) {
		ast_log(LOG_WARNING, "Read failed (format)\n");
		return -1;
	}
	if (ltohs(chans) != 1) {
		ast_log(LOG_WARNING, "Not in mono %d\n", ltohs(chans));
		return -1;
	}
	if (fread(&freq, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Read failed (freq)\n");
		return -1;
	}
	if (ltohl(freq) != 8000) {
		ast_log(LOG_WARNING, "Unexpected freqency %d\n", ltohl(freq));
		return -1;
	}
	/* Ignore the byte frequency */
	if (fread(&freq, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Read failed (X_1)\n");
		return -1;
	}
	/* Ignore the two weird fields */
	if (fread(&freq, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Read failed (X_2/X_3)\n");
		return -1;
	}
	/* Ignore the byte frequency */
	if (fread(&freq, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Read failed (Y_1)\n");
		return -1;
	}
	/* Check for the word fact */
	if (fread(&fact, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Read failed (fact)\n");
		return -1;
	}
	if (memcmp(&fact, "fact", 4)) {
		ast_log(LOG_WARNING, "Does not say fact\n");
		return -1;
	}
	/* Ignore the "fact value" */
	if (fread(&fact, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Read failed (fact header)\n");
		return -1;
	}
	if (fread(&fact, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Read failed (fact value)\n");
		return -1;
	}
	/* Check for the word data */
	if (fread(&data, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Read failed (data)\n");
		return -1;
	}
	if (memcmp(&data, "data", 4)) {
		ast_log(LOG_WARNING, "Does not say data\n");
		return -1;
	}
	/* Ignore the data length */
	if (fread(&data, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Read failed (data)\n");
		return -1;
	}
	return 0;
}

static int update_header(FILE *f)
{
	off_t cur,end,bytes;
	int datalen,filelen;
	
	cur = ftell(f);
	fseek(f, 0, SEEK_END);
	end = ftell(f);
	/* in a gsm WAV, data starts 60 bytes in */
	bytes = end - 60;
	datalen = htoll((bytes + 1) & ~0x1);
	filelen = htoll(52 + ((bytes + 1) & ~0x1));
	if (cur < 0) {
		ast_log(LOG_WARNING, "Unable to find our position\n");
		return -1;
	}
	if (fseek(f, 4, SEEK_SET)) {
		ast_log(LOG_WARNING, "Unable to set our position\n");
		return -1;
	}
	if (fwrite(&filelen, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to set write file size\n");
		return -1;
	}
	if (fseek(f, 56, SEEK_SET)) {
		ast_log(LOG_WARNING, "Unable to set our position\n");
		return -1;
	}
	if (fwrite(&datalen, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to set write datalen\n");
		return -1;
	}
	if (fseek(f, cur, SEEK_SET)) {
		ast_log(LOG_WARNING, "Unable to return to position\n");
		return -1;
	}
	return 0;
}

static int write_header(FILE *f)
{
	unsigned int hz=htoll(8000);
	unsigned int bhz = htoll(1625);
	unsigned int hs = htoll(20);
	unsigned short fmt = htols(49);
	unsigned short chans = htols(1);
	unsigned int fhs = htoll(4);
	unsigned int x_1 = htoll(65);
	unsigned short x_2 = htols(2);
	unsigned short x_3 = htols(320);
	unsigned int y_1 = htoll(20160);
	unsigned int size = htoll(0);
	/* Write a GSM header, ignoring sizes which will be filled in later */
	if (fwrite("RIFF", 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (fwrite(&size, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (fwrite("WAVEfmt ", 1, 8, f) != 8) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (fwrite(&hs, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (fwrite(&fmt, 1, 2, f) != 2) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (fwrite(&chans, 1, 2, f) != 2) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (fwrite(&hz, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (fwrite(&bhz, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (fwrite(&x_1, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (fwrite(&x_2, 1, 2, f) != 2) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (fwrite(&x_3, 1, 2, f) != 2) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (fwrite("fact", 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (fwrite(&fhs, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (fwrite(&y_1, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (fwrite("data", 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (fwrite(&size, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	return 0;
}

static struct ast_filestream *wav_open(FILE *f)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		memset(tmp, 0, sizeof(struct ast_filestream));
		if (check_header(f)) {
			free(tmp);
			return NULL;
		}
		if (ast_mutex_lock(&wav_lock)) {
			ast_log(LOG_WARNING, "Unable to lock wav list\n");
			free(tmp);
			return NULL;
		}
		tmp->f = f;
		tmp->fr.data = tmp->gsm;
		tmp->fr.frametype = AST_FRAME_VOICE;
		tmp->fr.subclass = AST_FORMAT_GSM;
		/* datalen will vary for each frame */
		tmp->fr.src = name;
		tmp->fr.mallocd = 0;
		tmp->secondhalf = 0;
		glistcnt++;
		ast_mutex_unlock(&wav_lock);
		ast_update_use_count();
	}
	return tmp;
}

static struct ast_filestream *wav_rewrite(FILE *f, const char *comment)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		memset(tmp, 0, sizeof(struct ast_filestream));
		if (write_header(f)) {
			free(tmp);
			return NULL;
		}
		if (ast_mutex_lock(&wav_lock)) {
			ast_log(LOG_WARNING, "Unable to lock wav list\n");
			free(tmp);
			return NULL;
		}
		tmp->f = f;
		glistcnt++;
		ast_mutex_unlock(&wav_lock);
		ast_update_use_count();
	} else
		ast_log(LOG_WARNING, "Out of memory\n");
	return tmp;
}

static void wav_close(struct ast_filestream *s)
{
	char zero = 0;
	if (ast_mutex_lock(&wav_lock)) {
		ast_log(LOG_WARNING, "Unable to lock wav list\n");
		return;
	}
	glistcnt--;
	ast_mutex_unlock(&wav_lock);
	ast_update_use_count();
	/* Pad to even length */
	fseek(s->f, 0, SEEK_END);
	if (ftell(s->f) & 0x1)
		fwrite(&zero, 1, 1, s->f);
	fclose(s->f);
	free(s);
	s = NULL;
}

static struct ast_frame *wav_read(struct ast_filestream *s, int *whennext)
{
	int res;
	char msdata[66];
	/* Send a frame from the file to the appropriate channel */

	s->fr.frametype = AST_FRAME_VOICE;
	s->fr.subclass = AST_FORMAT_GSM;
	s->fr.offset = AST_FRIENDLY_OFFSET;
	s->fr.samples = 160;
	s->fr.datalen = 33;
	s->fr.mallocd = 0;
	if (s->secondhalf) {
		/* Just return a frame based on the second GSM frame */
		s->fr.data = s->gsm + 33;
	} else {
		if ((res = fread(msdata, 1, 65, s->f)) != 65) {
			if (res && (res != 1))
				ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
			return NULL;
		}
		/* Convert from MS format to two real GSM frames */
		conv65(msdata, s->gsm);
		s->fr.data = s->gsm;
	}
	s->secondhalf = !s->secondhalf;
	*whennext = 160;
	return &s->fr;
}

static int wav_write(struct ast_filestream *fs, struct ast_frame *f)
{
	int res;
	char msdata[66];
	int len =0;
	int alreadyms=0;
	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass != AST_FORMAT_GSM) {
		ast_log(LOG_WARNING, "Asked to write non-GSM frame (%d)!\n", f->subclass);
		return -1;
	}
	if (!(f->datalen % 65)) 
		alreadyms = 1;
	while(len < f->datalen) {
		if (alreadyms) {
			fs->secondhalf = 0;
			if ((res = fwrite(f->data + len, 1, 65, fs->f)) != 65) {
				ast_log(LOG_WARNING, "Bad write (%d/65): %s\n", res, strerror(errno));
				return -1;
			}
			update_header(fs->f);
			len += 65;
		} else {
			if (fs->secondhalf) {
				memcpy(fs->gsm + 33, f->data + len, 33);
				conv66(fs->gsm, msdata);
				if ((res = fwrite(msdata, 1, 65, fs->f)) != 65) {
					ast_log(LOG_WARNING, "Bad write (%d/65): %s\n", res, strerror(errno));
					return -1;
				}
				update_header(fs->f);
			} else {
				/* Copy the data and do nothing */
				memcpy(fs->gsm, f->data + len, 33);
			}
			fs->secondhalf = !fs->secondhalf;
			len += 33;
		}
	}
	return 0;
}

static int wav_seek(struct ast_filestream *fs, long sample_offset, int whence)
{
	off_t offset=0,distance,cur,min,max;
	min = 60;
	cur = ftell(fs->f);
	fseek(fs->f, 0, SEEK_END);
	max = ftell(fs->f);
	/* I'm getting sloppy here, I'm only going to go to even splits of the 2
	 * frames, if you want tighter cuts use format_gsm, format_pcm, or format_wav */
	distance = (sample_offset/320) * 65;
	if(whence == SEEK_SET)
		offset = distance + min;
	else if(whence == SEEK_CUR || whence == SEEK_FORCECUR)
		offset = distance + cur;
	else if(whence == SEEK_END)
		offset = max - distance;
	/* always protect against seeking past end of header */
	offset = (offset < min)?min:offset;
	if (whence != SEEK_FORCECUR) {
		offset = (offset > max)?max:offset;
	} else if (offset > max) {
		int i;
		fseek(fs->f, 0, SEEK_END);
		for (i=0; i< (offset - max) / 65; i++) {
			fwrite(msgsm_silence, 1, 65, fs->f);
		}
	}
	fs->secondhalf = 0;
	return fseek(fs->f, offset, SEEK_SET);
}

static int wav_trunc(struct ast_filestream *fs)
{
	if (ftruncate(fileno(fs->f), ftell(fs->f)))
		return -1;
	return update_header(fs->f);
}

static long wav_tell(struct ast_filestream *fs)
{
	off_t offset;
	offset = ftell(fs->f);
	/* since this will most likely be used later in play or record, lets stick
	 * to that level of resolution, just even frames boundaries */
	return (offset - 52)/65*320;
}

static char *wav_getcomment(struct ast_filestream *s)
{
	return NULL;
}

int load_module()
{
	return ast_format_register(name, exts, AST_FORMAT_GSM,
								wav_open,
								wav_rewrite,
								wav_write,
								wav_seek,
								wav_trunc,
								wav_tell,
								wav_read,
								wav_close,
								wav_getcomment);
								
								
}

int unload_module()
{
	return ast_format_unregister(name);
}	

int usecount()
{
	return glistcnt;
}

char *description()
{
	return desc;
}


char *key()
{
	return ASTERISK_GPL_KEY;
}
