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
 * \brief Work with WAV in the proprietary Microsoft format.
 * Microsoft WAV format (8000hz Signed Linear)
 * \arg File name extension: wav (lower case)
 * \ingroup formats
 */
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/mod_format.h"
#include "asterisk/module.h"
#include "asterisk/endian.h"

/* Some Ideas for this code came from makewave.c by Jeffrey Chilton */

/* Portions of the conversion code are by guido@sienanet.it */

#define	WAV_BUF_SIZE	320

struct wav_desc {	/* format-specific parameters */
	int hz;
	int bytes;
	int lasttimeout;
	int maxlen;
	struct timeval last;
};

#define BLOCKSIZE 160

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


static int check_header_fmt(FILE *f, int hsize, int hz)
{
	short format, chans, bysam, bisam;
	int bysec;
	int freq;
	if (hsize < 16) {
		ast_log(LOG_WARNING, "Unexpected header size %d\n", ltohl(hsize));
		return -1;
	}
	if (fread(&format, 1, 2, f) != 2) {
		ast_log(LOG_WARNING, "Read failed (format)\n");
		return -1;
	}
	if (ltohs(format) != 1) {
		ast_log(LOG_WARNING, "Not a wav file %d\n", ltohs(format));
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
	if (((ltohl(freq) != 8000) && (ltohl(freq) != 16000)) ||
	    ((ltohl(freq) == 8000) && (hz != 8000)) ||
	    ((ltohl(freq) == 16000) && (hz != 16000))) {
		ast_log(LOG_WARNING, "Unexpected frequency mismatch %d (expecting %d)\n", ltohl(freq),hz);
		return -1;
	}
	/* Ignore the byte frequency */
	if (fread(&bysec, 1, 4, f) != 4) {
		ast_log(LOG_WARNING, "Read failed (BYTES_PER_SECOND)\n");
		return -1;
	}
	/* Check bytes per sample */
	if (fread(&bysam, 1, 2, f) != 2) {
		ast_log(LOG_WARNING, "Read failed (BYTES_PER_SAMPLE)\n");
		return -1;
	}
	if (ltohs(bysam) != 2) {
		ast_log(LOG_WARNING, "Can only handle 16bits per sample: %d\n", ltohs(bysam));
		return -1;
	}
	if (fread(&bisam, 1, 2, f) != 2) {
		ast_log(LOG_WARNING, "Read failed (Bits Per Sample): %d\n", ltohs(bisam));
		return -1;
	}
	/* Skip any additional header */
	if (fseek(f,ltohl(hsize)-16,SEEK_CUR) == -1 ) {
		ast_log(LOG_WARNING, "Failed to skip remaining header bytes: %d\n", ltohl(hsize)-16 );
		return -1;
	}
	return 0;
}

static int check_header(FILE *f, int hz)
{
	int type, size, formtype;
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
	/* Skip any facts and get the first data block */
	for(;;)
	{ 
		char buf[4];
	    
	    /* Begin data chunk */
	    if (fread(&buf, 1, 4, f) != 4) {
			ast_log(LOG_WARNING, "Read failed (block header format)\n");
			return -1;
	    }
	    /* Data has the actual length of data in it */
	    if (fread(&data, 1, 4, f) != 4) {
			ast_log(LOG_WARNING, "Read failed (block '%.4s' header length)\n", buf);
			return -1;
	    }
	    data = ltohl(data);
		if (memcmp(&buf, "fmt ", 4) == 0) {
			if (check_header_fmt(f, data, hz))
				return -1;
			continue;
		}
	    if(memcmp(buf, "data", 4) == 0 ) 
			break;
		ast_log(LOG_DEBUG, "Skipping unknown block '%.4s'\n", buf);
	    if (fseek(f,data,SEEK_CUR) == -1 ) {
			ast_log(LOG_WARNING, "Failed to skip '%.4s' block: %d\n", buf, data);
			return -1;
	    }
	}
#if 0
	curpos = lseek(fd, 0, SEEK_CUR);
	truelength = lseek(fd, 0, SEEK_END);
	lseek(fd, curpos, SEEK_SET);
	truelength -= curpos;
#endif	
	return data;
}

static int update_header(FILE *f)
{
	off_t cur,end;
	int datalen,filelen,bytes;
	
	cur = ftello(f);
	fseek(f, 0, SEEK_END);
	end = ftello(f);
	/* data starts 44 bytes in */
	bytes = end - 44;
	datalen = htoll(bytes);
	/* chunk size is bytes of data plus 36 bytes of header */
	filelen = htoll(36 + bytes);
	
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
	if (fseek(f, 40, SEEK_SET)) {
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

static int write_header(FILE *f, int writehz)
{
	unsigned int hz;
	unsigned int bhz;
	unsigned int hs = htoll(16);
	unsigned short fmt = htols(1);
	unsigned short chans = htols(1);
	unsigned short bysam = htols(2);
	unsigned short bisam = htols(16);
	unsigned int size = htoll(0);

	if (writehz == 16000) {
		hz = htoll(16000);
		bhz = htoll(32000);
	} else {
		hz = htoll(8000);
		bhz = htoll(16000);
	}
	/* Write a wav header, ignoring sizes which will be filled in later */
	fseek(f,0,SEEK_SET);
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
	if (fwrite(&bysam, 1, 2, f) != 2) {
		ast_log(LOG_WARNING, "Unable to write header\n");
		return -1;
	}
	if (fwrite(&bisam, 1, 2, f) != 2) {
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
	struct wav_desc *tmp = (struct wav_desc *)s->_private;
	if ((tmp->maxlen = check_header(s->f, (s->fmt->format.id == AST_FORMAT_SLINEAR16 ? 16000 : 8000))) < 0)
		return -1;
	return 0;
}

static int wav_rewrite(struct ast_filestream *s, const char *comment)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */

	struct wav_desc *tmp = (struct wav_desc *)s->_private;
	tmp->hz = (s->fmt->format.id == AST_FORMAT_SLINEAR16 ? 16000 : 8000);
	if (write_header(s->f,tmp->hz))
		return -1;
	return 0;
}

static void wav_close(struct ast_filestream *s)
{
	char zero = 0;
	struct wav_desc *fs = (struct wav_desc *)s->_private;

	if (s->filename) {
		update_header(s->f);
	}

	/* Pad to even length */
	if (fs->bytes & 0x1) {
		if (!fwrite(&zero, 1, 1, s->f)) {
			ast_log(LOG_WARNING, "fwrite() failed: %s\n", strerror(errno));
		}
	}
}

static struct ast_frame *wav_read(struct ast_filestream *s, int *whennext)
{
	int res;
	int samples;	/* actual samples read */
#if __BYTE_ORDER == __BIG_ENDIAN
	int x;
#endif
	int bytes;
	off_t here;
	/* Send a frame from the file to the appropriate channel */
	struct wav_desc *fs = (struct wav_desc *)s->_private;

	bytes = (fs->hz == 16000 ? (WAV_BUF_SIZE * 2) : WAV_BUF_SIZE);

	here = ftello(s->f);
	if (fs->maxlen - here < bytes)		/* truncate if necessary */
		bytes = fs->maxlen - here;
	if (bytes < 0)
		bytes = 0;
/* 	ast_debug(1, "here: %d, maxlen: %d, bytes: %d\n", here, s->maxlen, bytes); */
	s->fr.frametype = AST_FRAME_VOICE;
	ast_format_set(&s->fr.subclass.format, (fs->hz == 16000 ? AST_FORMAT_SLINEAR16 : AST_FORMAT_SLINEAR), 0);
	s->fr.mallocd = 0;
	AST_FRAME_SET_BUFFER(&s->fr, s->buf, AST_FRIENDLY_OFFSET, bytes);
	
	if ( (res = fread(s->fr.data.ptr, 1, s->fr.datalen, s->f)) <= 0 ) {
		if (res)
			ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		return NULL;
	}
	s->fr.datalen = res;
	s->fr.samples = samples = res / 2;

#if __BYTE_ORDER == __BIG_ENDIAN
	tmp = (short *)(s->fr.data.ptr);
	/* file format is little endian so we need to swap */
	for( x = 0; x < samples; x++)
		tmp[x] = (tmp[x] << 8) | ((tmp[x] & 0xff00) >> 8);
#endif

	*whennext = samples;
	return &s->fr;
}

static int wav_write(struct ast_filestream *fs, struct ast_frame *f)
{
#if __BYTE_ORDER == __BIG_ENDIAN
	int x;
	short tmp[16000], *tmpi;
#endif
	struct wav_desc *s = (struct wav_desc *)fs->_private;
	int res;

	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if ((f->subclass.format.id != AST_FORMAT_SLINEAR) && (f->subclass.format.id != AST_FORMAT_SLINEAR16)) {
		ast_log(LOG_WARNING, "Asked to write non-SLINEAR%s frame (%s)!\n", s->hz == 16000 ? "16" : "", ast_getformatname(&f->subclass.format));
		return -1;
	}
	if (ast_format_cmp(&f->subclass.format, &fs->fmt->format) == AST_FORMAT_CMP_NOT_EQUAL) {
		ast_log(LOG_WARNING, "Can't change SLINEAR frequency during write\n");
		return -1;
	}
	if (!f->datalen)
		return -1;

#if __BYTE_ORDER == __BIG_ENDIAN
	/* swap and write */
	if (f->datalen > sizeof(tmp)) {
		ast_log(LOG_WARNING, "Data length is too long\n");
		return -1;
	}
	tmpi = f->data.ptr;
	for (x=0; x < f->datalen/2; x++) 
		tmp[x] = (tmpi[x] << 8) | ((tmpi[x] & 0xff00) >> 8);

	if ((res = fwrite(tmp, 1, f->datalen, fs->f)) != f->datalen ) {
#else
	/* just write */
	if ((res = fwrite(f->data.ptr, 1, f->datalen, fs->f)) != f->datalen ) {
#endif
		ast_log(LOG_WARNING, "Bad write (%d): %s\n", res, strerror(errno));
		return -1;
	}

	s->bytes += f->datalen;
		
	return 0;

}

static int wav_seek(struct ast_filestream *fs, off_t sample_offset, int whence)
{
	off_t min, max, cur, offset = 0, samples;

	samples = sample_offset * 2; /* SLINEAR is 16 bits mono, so sample_offset * 2 = bytes */
	min = 44; /* wav header is 44 bytes */
	cur = ftello(fs->f);
	fseeko(fs->f, 0, SEEK_END);
	max = ftello(fs->f);
	if (whence == SEEK_SET)
		offset = samples + min;
	else if (whence == SEEK_CUR || whence == SEEK_FORCECUR)
		offset = samples + cur;
	else if (whence == SEEK_END)
		offset = max - samples;
        if (whence != SEEK_FORCECUR) {
		offset = (offset > max)?max:offset;
	}
	/* always protect the header space. */
	offset = (offset < min)?min:offset;
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
	/* subtract header size to get samples, then divide by 2 for 16 bit samples */
	return (offset - 44)/2;
}

static struct ast_format_def wav16_f = {
	.name = "wav16",
	.exts = "wav16",
	.open =	wav_open,
	.rewrite = wav_rewrite,
	.write = wav_write,
	.seek = wav_seek,
	.trunc = wav_trunc,
	.tell =	wav_tell,
	.read = wav_read,
	.close = wav_close,
	.buf_size = (WAV_BUF_SIZE * 2) + AST_FRIENDLY_OFFSET,
	.desc_size = sizeof(struct wav_desc),
};

static struct ast_format_def wav_f = {
	.name = "wav",
	.exts = "wav",
	.open =	wav_open,
	.rewrite = wav_rewrite,
	.write = wav_write,
	.seek = wav_seek,
	.trunc = wav_trunc,
	.tell =	wav_tell,
	.read = wav_read,
	.close = wav_close,
	.buf_size = WAV_BUF_SIZE + AST_FRIENDLY_OFFSET,
	.desc_size = sizeof(struct wav_desc),
};

static int load_module(void)
{
	ast_format_set(&wav_f.format, AST_FORMAT_SLINEAR, 0);
	ast_format_set(&wav16_f.format, AST_FORMAT_SLINEAR16, 0);
	if (ast_format_def_register(&wav_f)
		|| ast_format_def_register(&wav16_f))
		return AST_MODULE_LOAD_FAILURE;
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return ast_format_def_unregister(wav_f.name)
		|| ast_format_def_unregister(wav16_f.name);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Microsoft WAV/WAV16 format (8kHz/16kHz Signed Linear)",
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND
);
