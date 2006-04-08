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
 * Microsoft WAV format (Proprietary GSM)
 * \arg File name extension: WAV,wav49  (Upper case WAV, lower case is another format)
 * This format can be played on Windows systems, used for
 * e-mail attachments mainly.
 * \ingroup formats
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

#define	GSM_FRAME_SIZE	33
#define	MSGSM_FRAME_SIZE	65
#define	MSGSM_DATA_OFS		60	/* offset of data bytes */
#define	GSM_SAMPLES		160	/* samples in a GSM block */
#define	MSGSM_SAMPLES		(2*GSM_SAMPLES)	/* samples in an MSGSM block */

/* begin binary data: */
char msgsm_silence[] = /* 65 */
{0x48,0x17,0xD6,0x84,0x02,0x80,0x24,0x49,0x92,0x24,0x89,0x02,0x80,0x24,0x49
,0x92,0x24,0x89,0x02,0x80,0x24,0x49,0x92,0x24,0x89,0x02,0x80,0x24,0x49,0x92
,0x24,0x09,0x82,0x74,0x61,0x4D,0x28,0x00,0x48,0x92,0x24,0x49,0x92,0x28,0x00
,0x48,0x92,0x24,0x49,0x92,0x28,0x00,0x48,0x92,0x24,0x49,0x92,0x28,0x00,0x48
,0x92,0x24,0x49,0x92,0x00};
/* end binary data. size = 65 bytes */

struct wavg_desc {
	/* Believe it or not, we must decode/recode to account for the
	   weird MS format */
	int secondhalf;						/* Are we on the second half */
};

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
	if (ltohl(freq) != DEFAULT_SAMPLE_RATE) {
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
	
	cur = ftello(f);
	fseek(f, 0, SEEK_END);
	end = ftello(f);
	/* in a gsm WAV, data starts 60 bytes in */
	bytes = end - MSGSM_DATA_OFS;
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
	if (fseeko(f, cur, SEEK_SET)) {
		ast_log(LOG_WARNING, "Unable to return to position\n");
		return -1;
	}
	return 0;
}

static int write_header(FILE *f)
{
	unsigned int hz=htoll(DEFAULT_SAMPLE_RATE);	/* XXX the following are relate to DEFAULT_SAMPLE_RATE ? */
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

static int wav_open(struct ast_filestream *s)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct wavg_desc *fs = (struct wavg_desc *)s->private;

	if (check_header(s->f))
		return -1;
	fs->secondhalf = 0;	/* not strictly necessary */
	return 0;
}

static int wav_rewrite(struct ast_filestream *s, const char *comment)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */

	if (write_header(s->f))
		return -1;
	return 0;
}

static void wav_close(struct ast_filestream *s)
{
	char zero = 0;
	/* Pad to even length */
	fseek(s->f, 0, SEEK_END);
	if (ftello(s->f) & 0x1)
		fwrite(&zero, 1, 1, s->f);
}

static struct ast_frame *wav_read(struct ast_filestream *s, int *whennext)
{
	/* Send a frame from the file to the appropriate channel */
	struct wavg_desc *fs = (struct wavg_desc *)s->private;

	s->fr.frametype = AST_FRAME_VOICE;
	s->fr.subclass = AST_FORMAT_GSM;
	s->fr.offset = AST_FRIENDLY_OFFSET;
	s->fr.samples = GSM_SAMPLES;
	s->fr.mallocd = 0;
	FR_SET_BUF(&s->fr, s->buf, AST_FRIENDLY_OFFSET, GSM_FRAME_SIZE);
	if (fs->secondhalf) {
		/* Just return a frame based on the second GSM frame */
		s->fr.data = (char *)s->fr.data + GSM_FRAME_SIZE;
		s->fr.offset += GSM_FRAME_SIZE;
	} else {
		/* read and convert */
		char msdata[MSGSM_FRAME_SIZE];
		int res;
		
		if ((res = fread(msdata, 1, MSGSM_FRAME_SIZE, s->f)) != MSGSM_FRAME_SIZE) {
			if (res && (res != 1))
				ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
			return NULL;
		}
		/* Convert from MS format to two real GSM frames */
		conv65(msdata, s->fr.data);
	}
	fs->secondhalf = !fs->secondhalf;
	*whennext = GSM_SAMPLES;
	return &s->fr;
}

static int wav_write(struct ast_filestream *s, struct ast_frame *f)
{
	int len;
	int size;
	struct wavg_desc *fs = (struct wavg_desc *)s->private;

	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass != AST_FORMAT_GSM) {
		ast_log(LOG_WARNING, "Asked to write non-GSM frame (%d)!\n", f->subclass);
		return -1;
	}
	/* XXX this might fail... if the input is a multiple of MSGSM_FRAME_SIZE
	 * we assume it is already in the correct format.
	 */
	if (!(f->datalen % MSGSM_FRAME_SIZE)) {
		size = MSGSM_FRAME_SIZE;
		fs->secondhalf = 0;
	} else {
		size = GSM_FRAME_SIZE;
	}
	for (len = 0; len < f->datalen ; len += size) {
		int res;
		char *src, msdata[MSGSM_FRAME_SIZE];
		if (fs->secondhalf) {	/* second half of raw gsm to be converted */
			memcpy(s->buf + GSM_FRAME_SIZE, f->data + len, GSM_FRAME_SIZE);
			conv66(s->buf, msdata);
			src = msdata;
			fs->secondhalf = 0;
		} else if (size == GSM_FRAME_SIZE) {	/* first half of raw gsm */
			memcpy(s->buf, f->data + len, GSM_FRAME_SIZE);
			src = NULL;	/* nothing to write */
			fs->secondhalf = 1;
		} else {	/* raw msgsm data */
			src = f->data + len;
		}
		if (src && (res = fwrite(src, 1, size, s->f)) != size) {
			ast_log(LOG_WARNING, "Bad write (%d/65): %s\n", res, strerror(errno));
			return -1;
		}
		update_header(s->f); /* XXX inefficient! */
	}
	return 0;
}

static int wav_seek(struct ast_filestream *fs, off_t sample_offset, int whence)
{
	off_t offset=0, distance, max;
	struct wavg_desc *s = (struct wavg_desc *)fs->private;

	off_t min = MSGSM_DATA_OFS;
	off_t cur = ftello(fs->f);
	fseek(fs->f, 0, SEEK_END);
	max = ftello(fs->f);	/* XXX ideally, should round correctly */
	/* Compute the distance in bytes, rounded to the block size */
	distance = (sample_offset/MSGSM_SAMPLES) * MSGSM_FRAME_SIZE;
	if (whence == SEEK_SET)
		offset = distance + min;
	else if (whence == SEEK_CUR || whence == SEEK_FORCECUR)
		offset = distance + cur;
	else if (whence == SEEK_END)
		offset = max - distance;
	/* always protect against seeking past end of header */
	if (offset < min)
		offset = min;
	if (whence != SEEK_FORCECUR) {
		if (offset > max)
			offset = max;
	} else if (offset > max) {
		int i;
		fseek(fs->f, 0, SEEK_END);
		for (i=0; i< (offset - max) / MSGSM_FRAME_SIZE; i++) {
			fwrite(msgsm_silence, 1, MSGSM_FRAME_SIZE, fs->f);
		}
	}
	s->secondhalf = 0;
	return fseeko(fs->f, offset, SEEK_SET);
}

static int wav_trunc(struct ast_filestream *fs)
{
	if (ftruncate(fileno(fs->f), ftello(fs->f)))
		return -1;
	return update_header(fs->f);
}

static off_t wav_tell(struct ast_filestream *fs)
{
	off_t offset;
	offset = ftello(fs->f);
	/* since this will most likely be used later in play or record, lets stick
	 * to that level of resolution, just even frames boundaries */
	/* XXX why 52 ? */
	return (offset - 52)/MSGSM_FRAME_SIZE*MSGSM_SAMPLES;
}

static struct ast_format_lock me = { .usecnt = -1 };

static const struct ast_format wav49_f = {
	.name = "wav49",
	.exts = "WAV|wav49",
	.format = AST_FORMAT_GSM,
	.open =	wav_open,
	.rewrite = wav_rewrite,
	.write = wav_write,
	.seek = wav_seek,
	.trunc = wav_trunc,
	.tell = wav_tell,
	.read = wav_read,
	.close = wav_close,
	.buf_size = 2*GSM_FRAME_SIZE + AST_FRIENDLY_OFFSET,
	.desc_size = sizeof(struct wavg_desc),
	.lockp = &me,
};

int load_module()
{
	return ast_format_register(&wav49_f);
}

int unload_module()
{
	return ast_format_unregister(wav49_f.name);
}	

int usecount()
{
	return me.usecnt;
}

const char *description()
{
	return "Microsoft WAV format (Proprietary GSM)";
}

const char *key()
{
	return ASTERISK_GPL_KEY;
}
