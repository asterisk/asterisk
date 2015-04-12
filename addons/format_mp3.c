/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Anthony Minessale <anthmct@yahoo.com>
 *
 * Derived from other asterisk sound formats by
 * Mark Spencer <markster@linux-support.net>
 *
 * Thanks to mpglib from http://www.mpg123.org/
 * and Chris Stenton [jacs@gnome.co.uk]
 * for coding the ability to play stereo and non-8khz files

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

/*!
 * \file
 * \brief MP3 Format Handler
 * \ingroup formats
 */

/*** MODULEINFO
	<defaultenabled>no</defaultenabled>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "mp3/mpg123.h"
#include "mp3/mpglib.h"

#include "asterisk/module.h"
#include "asterisk/mod_format.h"
#include "asterisk/logger.h"
#include "asterisk/format_cache.h"

#define MP3_BUFLEN 320
#define MP3_SCACHE 16384
#define MP3_DCACHE 8192

struct mp3_private {
	/*! state for the mp3 decoder */
	struct mpstr mp;
	/*! buffer to hold mp3 data after read from disk */
	char sbuf[MP3_SCACHE];
	/*! buffer for slinear audio after being decoded out of sbuf */
	char dbuf[MP3_DCACHE];
	/*! how much data has been written to the output buffer in the ast_filestream */
	int buflen;
	/*! how much data has been written to sbuf */
	int sbuflen;
	/*! how much data is left to be read out of dbuf, starting at dbufoffset */
	int dbuflen;
	/*! current offset for reading data out of dbuf */
	int dbufoffset;
	int offset;
	long seek;
};

static const char name[] = "mp3";

#define BLOCKSIZE 160
#define OUTSCALE 4096

#define GAIN -4		/* 2^GAIN is the multiple to increase the volume by */

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


static int mp3_open(struct ast_filestream *s)
{
	struct mp3_private *p = s->_private;
	InitMP3(&p->mp, OUTSCALE);
	return 0;
}


static void mp3_close(struct ast_filestream *s)
{
	struct mp3_private *p = s->_private;

	ExitMP3(&p->mp);
	return;
}

static int mp3_squeue(struct ast_filestream *s)
{
	struct mp3_private *p = s->_private;
	int res=0;

	res = ftell(s->f);
	p->sbuflen = fread(p->sbuf, 1, MP3_SCACHE, s->f);
	if(p->sbuflen < 0) {
		ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", p->sbuflen, strerror(errno));
		return -1;
	}
	res = decodeMP3(&p->mp,p->sbuf,p->sbuflen,p->dbuf,MP3_DCACHE,&p->dbuflen);
	if(res != MP3_OK)
		return -1;
	p->sbuflen -= p->dbuflen;
	p->dbufoffset = 0;
	return 0;
}

static int mp3_dqueue(struct ast_filestream *s)
{
	struct mp3_private *p = s->_private;
	int res=0;

	if((res = decodeMP3(&p->mp,NULL,0,p->dbuf,MP3_DCACHE,&p->dbuflen)) == MP3_OK) {
		p->sbuflen -= p->dbuflen;
		p->dbufoffset = 0;
	}
	return res;
}

static int mp3_queue(struct ast_filestream *s)
{
	struct mp3_private *p = s->_private;
	int res = 0, bytes = 0;

	if(p->seek) {
		ExitMP3(&p->mp);
		InitMP3(&p->mp, OUTSCALE);
		fseek(s->f, 0, SEEK_SET);
		p->sbuflen = p->dbuflen = p->offset = 0;
		while(p->offset < p->seek) {
			if(mp3_squeue(s))
				return -1;
			while(p->offset < p->seek && ((res = mp3_dqueue(s))) == MP3_OK) {
				for(bytes = 0 ; bytes < p->dbuflen ; bytes++) {
					p->dbufoffset++;
					p->offset++;
					if(p->offset >= p->seek)
						break;
				}
			}
			if(res == MP3_ERR)
				return -1;
		}

		p->seek = 0;
		return 0;
	}
	if(p->dbuflen == 0) {
		if(p->sbuflen) {
			res = mp3_dqueue(s);
			if(res == MP3_ERR)
				return -1;
		}
		if(! p->sbuflen || res != MP3_OK) {
			if(mp3_squeue(s))
				return -1;
		}

	}

	return 0;
}

static struct ast_frame *mp3_read(struct ast_filestream *s, int *whennext)
{

	struct mp3_private *p = s->_private;
	int delay =0;
	int save=0;

	/* Pre-populate the buffer that holds audio to be returned (dbuf) */
	if (mp3_queue(s)) {
		return NULL;
	}

	if (p->dbuflen) {
		/* Read out what's waiting in dbuf */
		for (p->buflen = 0; p->buflen < MP3_BUFLEN && p->buflen < p->dbuflen; p->buflen++) {
			s->buf[p->buflen + AST_FRIENDLY_OFFSET] = p->dbuf[p->buflen + p->dbufoffset];
		}
		p->dbufoffset += p->buflen;
		p->dbuflen -= p->buflen;
	}

	if (p->buflen < MP3_BUFLEN) {
		/* dbuf didn't have enough, so reset dbuf, fill it back up and continue */
		p->dbuflen = p->dbufoffset = 0;

		if (mp3_queue(s)) {
			return NULL;
		}

		/* Make sure dbuf has enough to complete this read attempt */
		if (p->dbuflen >= (MP3_BUFLEN - p->buflen)) {
			for (save = p->buflen; p->buflen < MP3_BUFLEN; p->buflen++) {
				s->buf[p->buflen + AST_FRIENDLY_OFFSET] = p->dbuf[(p->buflen - save) + p->dbufoffset];
			}
			p->dbufoffset += (MP3_BUFLEN - save);
			p->dbuflen -= (MP3_BUFLEN - save);
		}

	}

	p->offset += p->buflen;
	delay = p->buflen / 2;
	AST_FRAME_SET_BUFFER(&s->fr, s->buf, AST_FRIENDLY_OFFSET, p->buflen);
	s->fr.samples = delay;
	*whennext = delay;
	return &s->fr;
}


static int mp3_write(struct ast_filestream *fs, struct ast_frame *f)
{
	ast_log(LOG_ERROR,"I Can't write MP3 only read them.\n");
	return -1;

}


static int mp3_seek(struct ast_filestream *s, off_t sample_offset, int whence)
{
	struct mp3_private *p = s->_private;
	off_t min,max,cur;
	long offset=0,samples;
	samples = sample_offset * 2;

	min = 0;
	fseek(s->f, 0, SEEK_END);
	max = ftell(s->f) * 100;
	cur = p->offset;

	if (whence == SEEK_SET)
		offset = samples + min;
	else if (whence == SEEK_CUR || whence == SEEK_FORCECUR)
		offset = samples + cur;
	else if (whence == SEEK_END)
		offset = max - samples;
	if (whence != SEEK_FORCECUR) {
		offset = (offset > max)?max:offset;
	}

	p->seek = offset;
	return fseek(s->f, offset, SEEK_SET);

}

static int mp3_rewrite(struct ast_filestream *s, const char *comment)
{
	ast_log(LOG_ERROR,"I Can't write MP3 only read them.\n");
	return -1;
}

static int mp3_trunc(struct ast_filestream *s)
{

	ast_log(LOG_ERROR,"I Can't write MP3 only read them.\n");
	return -1;
}

static off_t mp3_tell(struct ast_filestream *s)
{
	struct mp3_private *p = s->_private;

	return p->offset/2;
}

static char *mp3_getcomment(struct ast_filestream *s)
{
	return NULL;
}

static struct ast_format_def mp3_f = {
	.name = "mp3",
	.exts = "mp3",
	.open = mp3_open,
	.write = mp3_write,
	.rewrite = mp3_rewrite,
	.seek =	mp3_seek,
	.trunc = mp3_trunc,
	.tell =	mp3_tell,
	.read =	mp3_read,
	.close = mp3_close,
	.getcomment = mp3_getcomment,
	.buf_size = MP3_BUFLEN + AST_FRIENDLY_OFFSET,
	.desc_size = sizeof(struct mp3_private),
};


static int load_module(void)
{
	mp3_f.format = ast_format_slin;
	InitMP3Constants();
	return ast_format_def_register(&mp3_f);
}

static int unload_module(void)
{
	return ast_format_def_unregister(name);
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "MP3 format [Any rate but 8000hz mono is optimal]");

