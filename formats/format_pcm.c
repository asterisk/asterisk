/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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
 * \brief Flat, binary, ulaw PCM file format.
 * \arg File name extension: alaw, al, alw, pcm, ulaw, ul, mu, ulw, g722, au
 * 
 * \ingroup formats
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/
 
#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/mod_format.h"
#include "asterisk/module.h"
#include "asterisk/endian.h"
#include "asterisk/ulaw.h"
#include "asterisk/alaw.h"
#include "asterisk/format_cache.h"

#define BUF_SIZE 160		/* 160 bytes, and same number of samples */

static char ulaw_silence[BUF_SIZE];
static char alaw_silence[BUF_SIZE];

/* #define REALTIME_WRITE */	/* XXX does it work at all ? */

#ifdef REALTIME_WRITE
struct pcm_desc {
	unsigned long start_time;
};

/* Returns time in msec since system boot. */
static unsigned long get_time(void)
{
	struct tms buf;
	clock_t cur;

	cur = times( &buf );
	if( cur < 0 ) {
		ast_log( LOG_WARNING, "Cannot get current time\n" );
		return 0;
	}
	return cur * 1000 / sysconf( _SC_CLK_TCK );
}

static int pcma_open(struct ast_filestream *s)
{
	if (ast_format_cmp(s->fmt->format, ast_format_alaw) == AST_FORMAT_CMP_EQUAL)
		pd->starttime = get_time();
	return 0;
}

static int pcma_rewrite(struct ast_filestream *s, const char *comment)
{
	return pcma_open(s);
}
#endif

static struct ast_frame *pcm_read(struct ast_filestream *s, int *whennext)
{
	int res;
	
	/* Send a frame from the file to the appropriate channel */

	AST_FRAME_SET_BUFFER(&s->fr, s->buf, AST_FRIENDLY_OFFSET, BUF_SIZE);
	if ((res = fread(s->fr.data.ptr, 1, s->fr.datalen, s->f)) < 1) {
		if (res)
			ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		return NULL;
	}
	s->fr.datalen = res;
	if (ast_format_cmp(s->fmt->format, ast_format_g722) == AST_FORMAT_CMP_EQUAL)
		*whennext = s->fr.samples = res * 2;
	else
		*whennext = s->fr.samples = res;
	return &s->fr;
}

static int pcm_seek(struct ast_filestream *fs, off_t sample_offset, int whence)
{
	off_t cur, max, offset = 0;
 	int ret = -1;	/* assume error */

	if ((cur = ftello(fs->f)) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to determine current position in pcm filestream %p: %s\n", fs, strerror(errno));
		return -1;
	}

	if (fseeko(fs->f, 0, SEEK_END) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to seek to end of pcm filestream %p: %s\n", fs, strerror(errno));
		return -1;
	}

	if ((max = ftello(fs->f)) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to determine max position in pcm filestream %p: %s\n", fs, strerror(errno));
		return -1;
	}

	switch (whence) {
	case SEEK_SET:
		offset = sample_offset;
		break;
	case SEEK_END:
		offset = max - sample_offset;
		break;
	case SEEK_CUR:
	case SEEK_FORCECUR:
		offset = cur + sample_offset;
		break;
	default:
		ast_log(LOG_WARNING, "invalid whence %d, assuming SEEK_SET\n", whence);
		offset = sample_offset;
	}
	if (offset < 0) {
		ast_log(LOG_WARNING, "negative offset %ld, resetting to 0\n", (long) offset);
		offset = 0;
	}
	if (whence == SEEK_FORCECUR && offset > max) { /* extend the file */
		size_t left = offset - max;
		const char *src = (ast_format_cmp(fs->fmt->format, ast_format_alaw) == AST_FORMAT_CMP_EQUAL) ? alaw_silence : ulaw_silence;

		while (left) {
			size_t written = fwrite(src, 1, (left > BUF_SIZE) ? BUF_SIZE : left, fs->f);
			if (written == -1)
				break;	/* error */
			left -= written;
		}
		ret = 0; /* successful */
	} else {
		if (offset > max) {
			ast_log(LOG_WARNING, "offset too large %ld, truncating to %ld\n", (long) offset, (long) max);
			offset = max;
		}
		ret = fseeko(fs->f, offset, SEEK_SET);
	}
	return ret;
}

static int pcm_trunc(struct ast_filestream *fs)
{
	int cur, fd;

	if ((fd = fileno(fs->f)) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to determine file descriptor for pcm filestream %p: %s\n", fs, strerror(errno));
		return -1;
	}
	if ((cur = ftello(fs->f)) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to determine current position in pcm filestream %p: %s\n", fs, strerror(errno));
		return -1;
	}
	/* Truncate file to current length */
	return ftruncate(fd, cur);
}

static off_t pcm_tell(struct ast_filestream *fs)
{
	return ftello(fs->f);
}

static int pcm_write(struct ast_filestream *fs, struct ast_frame *f)
{
	int res;

#ifdef REALTIME_WRITE
	if (ast_format_cmp(s->fmt->format, ast_format_alaw) == AST_FORMAT_CMP_EQUAL) {
		struct pcm_desc *pd = (struct pcm_desc *)fs->_private;
		struct stat stat_buf;
		unsigned long cur_time = get_time();
		unsigned long fpos = ( cur_time - pd->start_time ) * 8;	/* 8 bytes per msec */
		/* Check if we have written to this position yet. If we have, then increment pos by one frame
		*  for some degree of protection against receiving packets in the same clock tick.
		*/
		
		fstat(fileno(fs->f), &stat_buf );
		if (stat_buf.st_size > fpos )
			fpos += f->datalen;	/* Incrementing with the size of this current frame */

		if (stat_buf.st_size < fpos) {
			/* fill the gap with 0x55 rather than 0. */
			char buf[1024];
			unsigned long cur, to_write;

			cur = stat_buf.st_size;
			if (fseek(fs->f, cur, SEEK_SET) < 0) {
				ast_log( LOG_WARNING, "Cannot seek in file: %s\n", strerror(errno) );
				return -1;
			}
			memset(buf, 0x55, 512);
			while (cur < fpos) {
				to_write = fpos - cur;
				if (to_write > sizeof(buf))
					to_write = sizeof(buf);
				fwrite(buf, 1, to_write, fs->f);
				cur += to_write;
			}
		}

		if (fseek(s->f, fpos, SEEK_SET) < 0) {
			ast_log( LOG_WARNING, "Cannot seek in file: %s\n", strerror(errno) );
			return -1;
		}
	}
#endif	/* REALTIME_WRITE */
	
	if ((res = fwrite(f->data.ptr, 1, f->datalen, fs->f)) != f->datalen) {
		ast_log(LOG_WARNING, "Bad write (%d/%d): %s\n", res, f->datalen, strerror(errno));
		return -1;
	}
	return 0;
}

/* SUN .au support routines */

#define AU_HEADER_SIZE		24
#define AU_HEADER(var)		uint32_t var[6]

#define AU_HDR_MAGIC_OFF	0
#define AU_HDR_HDR_SIZE_OFF	1
#define AU_HDR_DATA_SIZE_OFF	2
#define AU_HDR_ENCODING_OFF	3
#define AU_HDR_SAMPLE_RATE_OFF	4
#define AU_HDR_CHANNELS_OFF	5

#define AU_ENC_8BIT_ULAW	1

#define AU_MAGIC 0x2e736e64
#if __BYTE_ORDER == __BIG_ENDIAN
#define htoll(b) (b)
#define htols(b) (b)
#define ltohl(b) (b)
#define ltohs(b) (b)
#else
#if __BYTE_ORDER == __LITTLE_ENDIAN
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
	AU_HEADER(header);
	uint32_t magic;
	uint32_t hdr_size;
	uint32_t data_size;
	uint32_t encoding;
	uint32_t sample_rate;
	uint32_t channels;

	if (fread(header, 1, AU_HEADER_SIZE, f) != AU_HEADER_SIZE) {
		ast_log(LOG_WARNING, "Read failed (header)\n");
		return -1;
	}
	magic = ltohl(header[AU_HDR_MAGIC_OFF]);
	if (magic != (uint32_t) AU_MAGIC) {
		ast_log(LOG_WARNING, "Bad magic: 0x%x\n", magic);
	}
	hdr_size = ltohl(header[AU_HDR_HDR_SIZE_OFF]);
	if (hdr_size < AU_HEADER_SIZE) {
		hdr_size = AU_HEADER_SIZE;
	}
/*	data_size = ltohl(header[AU_HDR_DATA_SIZE_OFF]); */
	encoding = ltohl(header[AU_HDR_ENCODING_OFF]);
	if (encoding != AU_ENC_8BIT_ULAW) {
		ast_log(LOG_WARNING, "Unexpected format: %u. Only 8bit ULAW allowed (%d)\n", encoding, AU_ENC_8BIT_ULAW);
		return -1;
	}
	sample_rate = ltohl(header[AU_HDR_SAMPLE_RATE_OFF]);
	if (sample_rate != DEFAULT_SAMPLE_RATE) {
		ast_log(LOG_WARNING, "Sample rate can only be 8000 not %u\n", sample_rate);
		return -1;
	}
	channels = ltohl(header[AU_HDR_CHANNELS_OFF]);
	if (channels != 1) {
		ast_log(LOG_WARNING, "Not in mono: channels=%u\n", channels);
		return -1;
	}
	/* Skip to data */
	fseek(f, 0, SEEK_END);
	data_size = ftell(f) - hdr_size;
	if (fseek(f, hdr_size, SEEK_SET) == -1 ) {
		ast_log(LOG_WARNING, "Failed to skip to data: %u\n", hdr_size);
		return -1;
	}
	return data_size;
}

static int update_header(FILE *f)
{
	off_t cur, end;
	uint32_t datalen;
	int bytes;

	cur = ftell(f);
	fseek(f, 0, SEEK_END);
	end = ftell(f);
	/* data starts 24 bytes in */
	bytes = end - AU_HEADER_SIZE;
	datalen = htoll(bytes);

	if (cur < 0) {
		ast_log(LOG_WARNING, "Unable to find our position\n");
		return -1;
	}
	if (fseek(f, AU_HDR_DATA_SIZE_OFF * sizeof(uint32_t), SEEK_SET)) {
		ast_log(LOG_WARNING, "Unable to set our position\n");
		return -1;
	}
	if (fwrite(&datalen, 1, sizeof(datalen), f) != sizeof(datalen)) {
		ast_log(LOG_WARNING, "Unable to set write file size\n");
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
	AU_HEADER(header);

	header[AU_HDR_MAGIC_OFF] = htoll((uint32_t) AU_MAGIC);
	header[AU_HDR_HDR_SIZE_OFF] = htoll(AU_HEADER_SIZE);
	header[AU_HDR_DATA_SIZE_OFF] = 0;
	header[AU_HDR_ENCODING_OFF] = htoll(AU_ENC_8BIT_ULAW);
	header[AU_HDR_SAMPLE_RATE_OFF] = htoll(DEFAULT_SAMPLE_RATE);
	header[AU_HDR_CHANNELS_OFF] = htoll(1);

	/* Write an au header, ignoring sizes which will be filled in later */
	fseek(f, 0, SEEK_SET);
	if (fwrite(header, 1, AU_HEADER_SIZE, f) != AU_HEADER_SIZE) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	return 0;
}

static int au_open(struct ast_filestream *s)
{
	if (check_header(s->f) < 0)
		return -1;
	return 0;
}

static int au_rewrite(struct ast_filestream *s, const char *comment)
{
	if (write_header(s->f))
		return -1;
	return 0;
}

/* XXX check this, probably incorrect */
static int au_seek(struct ast_filestream *fs, off_t sample_offset, int whence)
{
	off_t min = AU_HEADER_SIZE, max, cur;
	long offset = 0, bytes;

	if (ast_format_cmp(fs->fmt->format, ast_format_g722) == AST_FORMAT_CMP_EQUAL)
		bytes = sample_offset / 2;
	else
		bytes = sample_offset;

	if ((cur = ftello(fs->f)) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to determine current position in au filestream %p: %s\n", fs, strerror(errno));
		return -1;
	}

	if (fseeko(fs->f, 0, SEEK_END) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to seek to end of au filestream %p: %s\n", fs, strerror(errno));
		return -1;
	}

	if ((max = ftello(fs->f)) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to determine max position in au filestream %p: %s\n", fs, strerror(errno));
		return -1;
	}

	if (whence == SEEK_SET)
		offset = bytes + min;
	else if (whence == SEEK_CUR || whence == SEEK_FORCECUR)
		offset = bytes + cur;
	else if (whence == SEEK_END)
		offset = max - bytes;

	if (whence != SEEK_FORCECUR) {
		offset = (offset > max) ? max : offset;
	}

	/* always protect the header space. */
	offset = (offset < min) ? min : offset;

	return fseeko(fs->f, offset, SEEK_SET);
}

static int au_trunc(struct ast_filestream *fs)
{
	int fd;
	off_t cur;

	if ((fd = fileno(fs->f)) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to determine file descriptor for au filestream %p: %s\n", fs, strerror(errno));
		return -1;
	}
	if ((cur = ftello(fs->f)) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to determine current position in au filestream %p: %s\n", fs, strerror(errno));
		return -1;
	}
	/* Truncate file to current length */
	if (ftruncate(fd, cur)) {
		return -1;
	}
	return update_header(fs->f);
}

static off_t au_tell(struct ast_filestream *fs)
{
	off_t offset = ftello(fs->f);
	return offset - AU_HEADER_SIZE;
}

static struct ast_format_def alaw_f = {
	.name = "alaw",
	.exts = "alaw|al|alw",
	.write = pcm_write,
	.seek = pcm_seek,
	.trunc = pcm_trunc,
	.tell = pcm_tell,
	.read = pcm_read,
	.buf_size = BUF_SIZE + AST_FRIENDLY_OFFSET,
#ifdef REALTIME_WRITE
	.open = pcma_open,
	.rewrite = pcma_rewrite,
	.desc_size = sizeof(struct pcm_desc),
#endif
};

static struct ast_format_def pcm_f = {
	.name = "pcm",
	.exts = "pcm|ulaw|ul|mu|ulw",
	.write = pcm_write,
	.seek = pcm_seek,
	.trunc = pcm_trunc,
	.tell = pcm_tell,
	.read = pcm_read,
	.buf_size = BUF_SIZE + AST_FRIENDLY_OFFSET,
};

static struct ast_format_def g722_f = {
	.name = "g722",
	.exts = "g722",
	.write = pcm_write,
	.seek = pcm_seek,
	.trunc = pcm_trunc,
	.tell = pcm_tell,
	.read = pcm_read,
	.buf_size = (BUF_SIZE * 2) + AST_FRIENDLY_OFFSET,
};

static struct ast_format_def au_f = {
	.name = "au",
	.exts = "au",
	.open = au_open,
	.rewrite = au_rewrite,
	.write = pcm_write,
	.seek = au_seek,
	.trunc = au_trunc,
	.tell = au_tell,
	.read = pcm_read,
	.buf_size = BUF_SIZE + AST_FRIENDLY_OFFSET,	/* this many shorts */
};

static int load_module(void)
{
	int i;

	/* XXX better init ? */
	for (i = 0; i < ARRAY_LEN(ulaw_silence); i++)
		ulaw_silence[i] = AST_LIN2MU(0);
	for (i = 0; i < ARRAY_LEN(alaw_silence); i++)
		alaw_silence[i] = AST_LIN2A(0);

	pcm_f.format = ast_format_ulaw;
	alaw_f.format = ast_format_alaw;
	au_f.format = ast_format_ulaw;
	g722_f.format = ast_format_g722;
	if ( ast_format_def_register(&pcm_f)
		|| ast_format_def_register(&alaw_f)
		|| ast_format_def_register(&au_f)
		|| ast_format_def_register(&g722_f) )
		return AST_MODULE_LOAD_FAILURE;
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return ast_format_def_unregister(pcm_f.name)
		|| ast_format_def_unregister(alaw_f.name)
		|| ast_format_def_unregister(au_f.name)
		|| ast_format_def_unregister(g722_f.name);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Raw/Sun uLaw/ALaw 8KHz (PCM,PCMA,AU), G.722 16Khz",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND
);
