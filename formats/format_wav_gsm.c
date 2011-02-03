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
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/mod_format.h"
#include "asterisk/module.h"
#include "asterisk/endian.h"

#include "msgsm.h"

/* Some Ideas for this code came from makewave.c by Jeffrey Chilton */

/* Portions of the conversion code are by guido@sienanet.it */

#define	GSM_FRAME_SIZE	33
#define	MSGSM_FRAME_SIZE	65
#define	MSGSM_DATA_OFFSET		60	/* offset of data bytes */
#define	GSM_SAMPLES		160	/* samples in a GSM block */
#define	MSGSM_SAMPLES		(2*GSM_SAMPLES)	/* samples in an MSGSM block */

/* begin binary data: */
static char msgsm_silence[] = /* 65 */
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
		ast_log(LOG_WARNING, "Unexpected frequency %d\n", ltohl(freq));
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
	int datalen, filelen, samples;

	cur = ftello(f);
	fseek(f, 0, SEEK_END);
	end = ftello(f);
	/* in a gsm WAV, data starts 60 bytes in */
	bytes = end - MSGSM_DATA_OFFSET;
	samples = htoll(bytes / MSGSM_FRAME_SIZE * MSGSM_SAMPLES);
	datalen = htoll(bytes);
	filelen = htoll(MSGSM_DATA_OFFSET - 8 + bytes);
	if (cur < 0) {
		ast_log(LOG_WARNING, "Unable to find our position\n");
		return -1;
	}
	if (fseek(f, 4, SEEK_SET)) {
		ast_log(LOG_WARNING, "Unable to set our position\n");
		return -1;
	}
	if (fwrite(&filelen, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to write file size\n");
		return -1;
	}
	if (fseek(f, 48, SEEK_SET)) {
		ast_log(LOG_WARNING, "Unable to set our position\n");
		return -1;
	}
	if (fwrite(&samples, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to write samples\n");
		return -1;
	}
	if (fseek(f, 56, SEEK_SET)) {
		ast_log(LOG_WARNING, "Unable to set our position\n");
		return -1;
	}
	if (fwrite(&datalen, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to write datalen\n");
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
	/* Samples per second (always 8000 for this format). */
	unsigned int sample_rate = htoll(8000);
	/* Bytes per second (always 1625 for this format). */
	unsigned int byte_sample_rate = htoll(1625);
	/* This is the size of the "fmt " subchunk */
	unsigned int fmtsize = htoll(20);
	/* WAV #49 */
	unsigned short fmt = htols(49);
	/* Mono = 1 channel */
	unsigned short chans = htols(1);
	/* Each block of data is exactly 65 bytes in size. */
	unsigned int block_align = htoll(MSGSM_FRAME_SIZE);
	/* Not actually 2, but rounded up to the nearest bit */
	unsigned short bits_per_sample = htols(2);
	/* Needed for compressed formats */
	unsigned short extra_format = htols(MSGSM_SAMPLES);
	/* This is the size of the "fact" subchunk */
	unsigned int factsize = htoll(4);
	/* Number of samples in the data chunk */
	unsigned int num_samples = htoll(0);
	/* Number of bytes in the data chunk */
	unsigned int size = htoll(0);
	/* Write a GSM header, ignoring sizes which will be filled in later */

	/*  0: Chunk ID */
	if (fwrite("RIFF", 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	/*  4: Chunk Size */
	if (fwrite(&size, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	/*  8: Chunk Format */
	if (fwrite("WAVE", 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	/* 12: Subchunk 1: ID */
	if (fwrite("fmt ", 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	/* 16: Subchunk 1: Size (minus 8) */
	if (fwrite(&fmtsize, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	/* 20: Subchunk 1: Audio format (49) */
	if (fwrite(&fmt, 1, 2, f) != 2) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	/* 22: Subchunk 1: Number of channels */
	if (fwrite(&chans, 1, 2, f) != 2) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	/* 24: Subchunk 1: Sample rate */
	if (fwrite(&sample_rate, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	/* 28: Subchunk 1: Byte rate */
	if (fwrite(&byte_sample_rate, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	/* 32: Subchunk 1: Block align */
	if (fwrite(&block_align, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	/* 36: Subchunk 1: Bits per sample */
	if (fwrite(&bits_per_sample, 1, 2, f) != 2) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	/* 38: Subchunk 1: Extra format bytes */
	if (fwrite(&extra_format, 1, 2, f) != 2) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	/* 40: Subchunk 2: ID */
	if (fwrite("fact", 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	/* 44: Subchunk 2: Size (minus 8) */
	if (fwrite(&factsize, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	/* 48: Subchunk 2: Number of samples */
	if (fwrite(&num_samples, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	/* 52: Subchunk 3: ID */
	if (fwrite("data", 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	/* 56: Subchunk 3: Size */
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
	struct wavg_desc *fs = (struct wavg_desc *)s->_private;

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

static struct ast_frame *wav_read(struct ast_filestream *s, int *whennext)
{
	/* Send a frame from the file to the appropriate channel */
	struct wavg_desc *fs = (struct wavg_desc *)s->_private;

	s->fr.frametype = AST_FRAME_VOICE;
	ast_format_set(&s->fr.subclass.format, AST_FORMAT_GSM, 0);
	s->fr.offset = AST_FRIENDLY_OFFSET;
	s->fr.samples = GSM_SAMPLES;
	s->fr.mallocd = 0;
	AST_FRAME_SET_BUFFER(&s->fr, s->buf, AST_FRIENDLY_OFFSET, GSM_FRAME_SIZE);
	if (fs->secondhalf) {
		/* Just return a frame based on the second GSM frame */
		s->fr.data.ptr = (char *)s->fr.data.ptr + GSM_FRAME_SIZE;
		s->fr.offset += GSM_FRAME_SIZE;
	} else {
		/* read and convert */
		unsigned char msdata[MSGSM_FRAME_SIZE];
		int res;
		
		if ((res = fread(msdata, 1, MSGSM_FRAME_SIZE, s->f)) != MSGSM_FRAME_SIZE) {
			if (res && (res != 1))
				ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
			return NULL;
		}
		/* Convert from MS format to two real GSM frames */
		conv65(msdata, s->fr.data.ptr);
	}
	fs->secondhalf = !fs->secondhalf;
	*whennext = GSM_SAMPLES;
	return &s->fr;
}

static int wav_write(struct ast_filestream *s, struct ast_frame *f)
{
	int len;
	int size;
	struct wavg_desc *fs = (struct wavg_desc *)s->_private;

	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass.format.id != AST_FORMAT_GSM) {
		ast_log(LOG_WARNING, "Asked to write non-GSM frame (%s)!\n", ast_getformatname(&f->subclass.format));
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
		unsigned char *src, msdata[MSGSM_FRAME_SIZE];
		if (fs->secondhalf) {	/* second half of raw gsm to be converted */
			memcpy(s->buf + GSM_FRAME_SIZE, f->data.ptr + len, GSM_FRAME_SIZE);
			conv66((unsigned char *) s->buf, msdata);
			src = msdata;
			fs->secondhalf = 0;
		} else if (size == GSM_FRAME_SIZE) {	/* first half of raw gsm */
			memcpy(s->buf, f->data.ptr + len, GSM_FRAME_SIZE);
			src = NULL;	/* nothing to write */
			fs->secondhalf = 1;
		} else {	/* raw msgsm data */
			src = f->data.ptr + len;
		}
		if (src && (res = fwrite(src, 1, MSGSM_FRAME_SIZE, s->f)) != MSGSM_FRAME_SIZE) {
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
	struct wavg_desc *s = (struct wavg_desc *)fs->_private;

	off_t min = MSGSM_DATA_OFFSET;
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
			if (!fwrite(msgsm_silence, 1, MSGSM_FRAME_SIZE, fs->f)) {
				ast_log(LOG_WARNING, "fwrite() failed: %s\n", strerror(errno));
			}
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
	return (offset - MSGSM_DATA_OFFSET)/MSGSM_FRAME_SIZE*MSGSM_SAMPLES;
}

static struct ast_format_def wav49_f = {
	.name = "wav49",
	.exts = "WAV|wav49",
	.open =	wav_open,
	.rewrite = wav_rewrite,
	.write = wav_write,
	.seek = wav_seek,
	.trunc = wav_trunc,
	.tell = wav_tell,
	.read = wav_read,
	.buf_size = 2*GSM_FRAME_SIZE + AST_FRIENDLY_OFFSET,
	.desc_size = sizeof(struct wavg_desc),
};

static int load_module(void)
{
	ast_format_set(&wav49_f.format, AST_FORMAT_GSM, 0);
	if (ast_format_def_register(&wav49_f))
		return AST_MODULE_LOAD_FAILURE;
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return ast_format_def_unregister(wav49_f.name);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Microsoft WAV format (Proprietary GSM)",
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND
);
