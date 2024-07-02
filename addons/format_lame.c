/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2024, Naveen Albert, sponsored by Marfox Ltd.
 * Copyright (C) 2024, Sperl Viktor
 *
 * Naveen Albert <asterisk@phreaknet.org>
 * Sperl Viktor <viktike32@gmail.com>
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

/*!
 * \file
 * \brief MP3 Format including encoder and decoder
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 * \author Sperl Viktor <viktike32@gmail.com>
 *
 * \ingroup formats
 */

/*** MODULEINFO
	<depend>lame</depend>
	<conflict>format_mp3</conflict>
	<defaultenabled>no</defaultenabled>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <lame/lame.h>

#include "asterisk/module.h"
#include "asterisk/mod_format.h"
#include "asterisk/logger.h"
#include "asterisk/format_cache.h"

#define SLIN_BUFLEN 320
#define DECORDER_OUTLEN 32768
#define BITS 2

struct mp3lame_private {
	lame_global_flags * lgfp;
	hip_global_flags * hgfp;

	int sample_rate;

	/* decoder buffer */
	short int doutput[DECORDER_OUTLEN *  1024];
	long doffset;
	int dsamples;

	/* decoder positions */
	int total_bytes_decoded;
	int total_bytes_compressed;

	/* counters */
	unsigned int decoder_counter;
	unsigned int encoder_counter;
};

static int mp3lame_encoder_init(struct mp3lame_private *p)
{
	p->lgfp = lame_init();
	if (!p->lgfp) {
		return -1;
	}
	/* Mono */
	lame_set_num_channels(p->lgfp, 1);
	/* Sample rate */
	lame_set_in_samplerate(p->lgfp, p->sample_rate);
	/* Bit rate, e.g.:
	* 16kbps for 8000Hz
	* 32kbps for 16000Hz
	* 64kbps for 32000Hz
	* 96kbsp for 48000Hz
	*/
	lame_set_brate(p->lgfp, p->sample_rate / 500);
	/* Mono */
	lame_set_mode(p->lgfp, 3);
	/* Medium quality */
	lame_set_quality(p->lgfp, 5);
	return lame_init_params(p->lgfp);
}

static int mp3lame_encoder_deinit(struct ast_filestream *fs)
{
	int res;
	int wres;
	struct mp3lame_private *p = fs->_private;
	unsigned char last_encoder_buffer[p->sample_rate];

	res = lame_encode_flush(p->lgfp, last_encoder_buffer, sizeof(last_encoder_buffer));
	if (res < 0) {
		ast_log(LOG_WARNING, "LAME encode last returned error %d", res);
		lame_close(p->lgfp);
		return -1;
	} else if (res > 0) {
		wres = fwrite(last_encoder_buffer, 1, res, fs->f);
		if (wres != res) {
			ast_log(LOG_WARNING, "Bad write (%d/%d): %s", wres, res, strerror(errno));
			lame_close(p->lgfp);
			return -1;
		} else {
			ast_debug(3, "LAME encode flushed %d bytes", wres);
		}
	}

	return lame_close(p->lgfp);
}

static int mp3lame_decoder_init(struct ast_filestream *fs)
{
	int size;
	struct mp3lame_private *p = fs->_private;

	p->hgfp = hip_decode_init();
	if (!p->hgfp) {
		return -1;
	}
	/* Get filesize in bytes */
	fseek(fs->f, 0, SEEK_END);
	size = ftell(fs->f);
	fseek(fs->f, 0, SEEK_SET);
	return size;
}

static int mp3lame_decoder_deinit(struct mp3lame_private *p)
{
	return hip_decode_exit(p->hgfp);
}

static int mp3lame_hip_decode(hip_global_flags * handle, int input_size, unsigned char * input, short int * output, int sample_rate)
{
	int samples;
	mp3data_struct headers;

	samples = hip_decode_headers(handle, input, input_size, output, NULL, &headers);

	if (headers.header_parsed == 1) {
		ast_debug(3, "LAME decoder found MP3 headers:\nchannels=%d, samplerate=%d, bitrate=%d, framesize=%d, mode=[%d:%d]",
			headers.stereo,
			headers.samplerate,
			headers.bitrate,
			headers.framesize,
			headers.mode,
			headers.mode_ext
		);
		if (headers.stereo < 1) {
			ast_debug(3, "LAME decoder no audio channels");
			return -1;
		} else if (headers.stereo > 1) {
			ast_log(LOG_ERROR, "LAME decoder invalid number of channels: %d, only mono is acceptable", headers.stereo);
			return -2;
		}
		if (headers.samplerate != sample_rate) {
			ast_log(LOG_ERROR, "LAME decoder invalid sampling rate: %d, expected %d", headers.samplerate, sample_rate);
			return -3;
		}
	}

	return samples;
}

static int mp3lame_file_read(struct ast_filestream *s, int sample_rate)
{
	int rres;
	struct mp3lame_private *p = s->_private;
	unsigned char dinput[p->total_bytes_compressed];

	rres = fread(dinput, 1, p->total_bytes_compressed, s->f);
	if (rres != p->total_bytes_compressed) {
		ast_log(LOG_ERROR, "Short read (%d/%d)", rres, p->total_bytes_compressed);
		return -1;
	}

	ast_debug(3, "LAME decoder input: %d bytes", rres);
	p->dsamples = mp3lame_hip_decode(p->hgfp, rres, dinput, p->doutput, sample_rate);
	while (p->dsamples == 0) {
		p->dsamples = mp3lame_hip_decode(p->hgfp, 0, NULL, p->doutput, sample_rate);
	}
	ast_debug(3, "LAME decoder got %d output samples", p->dsamples);

	return p->dsamples * BITS;
}

static int mp3lame_open(struct ast_filestream *s)
{
	struct mp3lame_private *p = s->_private;

	p->encoder_counter = 0;
	p->decoder_counter = 0;

	return 0;
}

static void mp3lame_close(struct ast_filestream *s)
{
	struct mp3lame_private *p = s->_private;

	if (p->encoder_counter != 0) {
		mp3lame_encoder_deinit(s);
	}

	return;
}

static struct ast_frame *mp3lame_read(struct ast_filestream *s, int *whennext, int frame_size, int sample_rate)
{
	int remaining_samples;
	struct mp3lame_private *p = s->_private;

	if (p->encoder_counter != 0) {
		ast_log(LOG_ERROR, "MP3 filestream %p is already in encoder mode (lame)", s);
		return NULL;
	}

	if (p->decoder_counter == 0) {
		p->total_bytes_compressed = mp3lame_decoder_init(s);
		if (p->total_bytes_compressed < 0) {
			ast_log(LOG_ERROR, "HIP decoder initialization failed %d", p->total_bytes_compressed / BITS);
			return NULL;
		}
		p->total_bytes_decoded = mp3lame_file_read(s, sample_rate);
		if (p->total_bytes_decoded < 0) {
			return NULL;
		}
		p->doffset = mp3lame_decoder_deinit(p);
	}
	p->decoder_counter++;

	AST_FRAME_SET_BUFFER(&s->fr, s->buf, AST_FRIENDLY_OFFSET, frame_size);

	remaining_samples = p->dsamples - (frame_size / BITS);
	if (remaining_samples >= 0) {
		/* Output a full ast_frame */
		s->fr.datalen = frame_size;
		s->fr.samples = frame_size / BITS;
		*whennext = frame_size / BITS;
		memcpy(s->fr.data.ptr, (char *)p->doutput + p->doffset, frame_size);
		p->doffset += frame_size;
		p->dsamples = remaining_samples;
		return &s->fr;
	} else {
		/* Output a partial ast_frame */
		if (p->dsamples > 0) {
			s->fr.datalen = p->dsamples * BITS;
			s->fr.samples = p->dsamples;
			memcpy(s->fr.data.ptr, (char *)p->doutput + p->doffset, p->dsamples * BITS);
			*whennext = 0;
			p->dsamples = 0;
			return &s->fr;
		} else {
			p->dsamples = 0;
			return NULL;
		}
	}
}

static int mp3lame_write(struct ast_filestream *fs, struct ast_frame *f, int sample_rate)
{
	int res;
	int wres;
	int ret;
	unsigned char encoder_buffer[sample_rate];
	struct mp3lame_private *p = fs->_private;

	if (p->decoder_counter != 0) {
		ast_log(LOG_ERROR, "MP3 filestream %p is already in decoder mode (hip)", fs);
		return -1;
	}

	if (p->encoder_counter == 0) {
		p->sample_rate = sample_rate;
		ret = mp3lame_encoder_init(p);
		if (ret < 0) {
			ast_log(LOG_ERROR, "LAME encoder initialization failed %d", ret);
			return -1;
		}
	}
	p->encoder_counter++;

	/*
	* Reinitialize lame encoder before every five minutes of audio data.
	* Unfortunatly lame stops encoding after this time.
	* Probably an internal timer in lame.
	* 14999 calculated as 300 seconds multipled by 50 frames (1000ms as 1 second / 20ms packetization time) - 1 to be sure
	*/
	if (p->encoder_counter % 14999 == 0) {
		ast_debug(3, "Reinitializing LAME encoder after 5 mins");
		mp3lame_encoder_deinit(fs);
		ret = mp3lame_encoder_init(p);
		if (ret < 0) {
			ast_log(LOG_ERROR, "LAME encoder initialization failed: %d", ret);
			return -1;
		}
	}

	res = lame_encode_buffer(p->lgfp, f->data.ptr, f->data.ptr, f->samples, encoder_buffer, sizeof(encoder_buffer));
	if (res < 0) {
		ast_log(LOG_WARNING, "LAME encoder returned error: %d", res);
		return -1;
	} else if (res > 0) {
		wres = fwrite(encoder_buffer, 1, res, fs->f);
		if (wres != res) {
			ast_log(LOG_WARNING, "Bad write (%d/%d): %s", wres, res, strerror(errno));
			return -1;
		} else {
			ast_debug(3, "LAME encoder wrote %d bytes", wres);
		}
	}

	return 0;
}

static off_t mp3lame_tell(struct ast_filestream *s)
{
	struct mp3lame_private *p = s->_private;

	if (p->decoder_counter != 0) {
		return p->doffset;
	} else {
		return ftello(s->f) / BITS;
	}
}

static int mp3lame_seek(struct ast_filestream *s, off_t sample_offset, int whence)
{
	struct mp3lame_private *p = s->_private;

	off_t offset = 0;
	off_t min = 0;
	off_t max;
	off_t current;

	sample_offset <<= 1;

	if (p->decoder_counter != 0) {
		max = p->total_bytes_decoded;
		current = p->doffset;
	} else {
		if ((current = ftello(s->f)) < 0) {
			ast_log(AST_LOG_WARNING, "Unable to determine current position in mp3 filestream %p: %s", s, strerror(errno));
			return -1;
		}

		if (fseeko(s->f, 0, SEEK_END) < 0) {
			ast_log(AST_LOG_WARNING, "Unable to seek to end of mp3 filestream %p: %s", s, strerror(errno));
			return -1;
		}

		if ((max = ftello(s->f)) < 0) {
			ast_log(AST_LOG_WARNING, "Unable to determine max position in mp3 filestream %p: %s", s, strerror(errno));
			return -1;
		}
	}

	if (whence == SEEK_SET)
		offset = sample_offset;
	else if (whence == SEEK_CUR || whence == SEEK_FORCECUR)
		offset = sample_offset + current;
	else if (whence == SEEK_END)
		offset = max - sample_offset;
	if (whence != SEEK_FORCECUR) {
		offset = (offset > max)?max:offset;
	}
	/* always protect against seeking past begining. */
	offset = (offset < min)?min:offset;

	if (p->decoder_counter != 0) {
		p->doffset = offset;
		p->dsamples = (p->total_bytes_decoded - offset) / BITS;

		return 0;
	}

	return fseeko(s->f, offset, SEEK_SET);
}

static int mp3lame_trunc(struct ast_filestream *s)
{
	int fd;
	struct mp3lame_private *p = s->_private;
	off_t current = mp3lame_tell(s);

	if (p->decoder_counter != 0) {
		p->dsamples = 0;
		p->total_bytes_decoded = p->doffset;
		return 0;
	} else {
		if ((fd = fileno(s->f)) < 0) {
			ast_log(AST_LOG_WARNING, "Unable to determine file descriptor for mp3 filestream %p: %s", s, strerror(errno));
			return -1;
		}
		if ((current = ftello(s->f)) < 0) {
			ast_log(AST_LOG_WARNING, "Unable to determine current position in mp3 filestream %p: %s", s, strerror(errno));
			return -1;
		}
		return ftruncate(fd, current);
	}
}

static char *mp3lame_getcomment(struct ast_filestream *s)
{
	char * comment = "Asterisk MP3 lame\0";
	return comment;
}

/* Sampling rate: 8khz */
static int mp3lame_write8(struct ast_filestream *fs, struct ast_frame *f)
{
	return mp3lame_write(fs, f, 8000);
}
static struct ast_frame *mp3lame_read8(struct ast_filestream *s, int *whennext)
{
	return mp3lame_read(s, whennext, SLIN_BUFLEN, 8000);
}
static struct ast_format_def lame8_f = {
	.name = "lame8",
	.exts = "8mp3|mp3",
	.mime_types = "audio/mp3",
	.open = mp3lame_open,
	.write = mp3lame_write8,
	.seek =	mp3lame_seek,
	.trunc = mp3lame_trunc,
	.tell =	mp3lame_tell,
	.read =	mp3lame_read8,
	.close = mp3lame_close,
	.getcomment = mp3lame_getcomment,
	.buf_size = SLIN_BUFLEN + AST_FRIENDLY_OFFSET,
	.desc_size = sizeof(struct mp3lame_private),
};

/* Sampling rate: 16khz */
static int mp3lame_write16(struct ast_filestream *fs, struct ast_frame *f)
{
	return mp3lame_write(fs, f, 16000);
}
static struct ast_frame *mp3lame_read16(struct ast_filestream *s, int *whennext)
{
	return mp3lame_read(s, whennext, (SLIN_BUFLEN * 2), 16000);
}
static struct ast_format_def lame16_f = {
	.name = "lame16",
	.exts = "16mp3",
	.mime_types = "audio/mp3",
	.open = mp3lame_open,
	.write = mp3lame_write16,
	.seek =	mp3lame_seek,
	.trunc = mp3lame_trunc,
	.tell =	mp3lame_tell,
	.read =	mp3lame_read16,
	.close = mp3lame_close,
	.getcomment = mp3lame_getcomment,
	.buf_size = (SLIN_BUFLEN * 2) + AST_FRIENDLY_OFFSET,
	.desc_size = sizeof(struct mp3lame_private),
};

/* Sampling rate: 32khz */
static int mp3lame_write32(struct ast_filestream *fs, struct ast_frame *f)
{
	return mp3lame_write(fs, f, 32000);
}
static struct ast_frame *mp3lame_read32(struct ast_filestream *s, int *whennext)
{
	return mp3lame_read(s, whennext, (SLIN_BUFLEN * 4), 32000);
}
static struct ast_format_def lame32_f = {
	.name = "lame32",
	.exts = "32mp3",
	.mime_types = "audio/mp3",
	.open = mp3lame_open,
	.write = mp3lame_write32,
	.seek =	mp3lame_seek,
	.trunc = mp3lame_trunc,
	.tell =	mp3lame_tell,
	.read =	mp3lame_read32,
	.close = mp3lame_close,
	.getcomment = mp3lame_getcomment,
	.buf_size = (SLIN_BUFLEN * 4) + AST_FRIENDLY_OFFSET,
	.desc_size = sizeof(struct mp3lame_private),
};

/* Sampling rate: 48khz */
static int mp3lame_write48(struct ast_filestream *fs, struct ast_frame *f)
{
	return mp3lame_write(fs, f, 48000);
}
static struct ast_frame *mp3lame_read48(struct ast_filestream *s, int *whennext)
{
	return mp3lame_read(s, whennext, (SLIN_BUFLEN * 6), 48000);
}
static struct ast_format_def lame48_f = {
	.name = "lame48",
	.exts = "48mp3",
	.mime_types = "audio/mp3",
	.open = mp3lame_open,
	.write = mp3lame_write48,
	.seek =	mp3lame_seek,
	.trunc = mp3lame_trunc,
	.tell =	mp3lame_tell,
	.read =	mp3lame_read48,
	.close = mp3lame_close,
	.getcomment = mp3lame_getcomment,
	.buf_size = (SLIN_BUFLEN * 6) + AST_FRIENDLY_OFFSET,
	.desc_size = sizeof(struct mp3lame_private),
};

static struct ast_format_def *lame_list[] = {
	&lame8_f,
	&lame16_f,
	&lame32_f,
	&lame48_f,
};

static int unload_module(void)
{
	int res = 0;
	int i = 0;

	for (i = 0; i < ARRAY_LEN(lame_list); i++) {
		if (ast_format_def_unregister(lame_list[i]->name)) {
			res = -1;
		}
	}
	return res;
}

static int load_module(void)
{
	int i;

	ast_log(LOG_NOTICE, "LAME version: %s\n", get_lame_version());

	lame8_f.format = ast_format_slin;
	lame16_f.format = ast_format_slin16;
	lame32_f.format = ast_format_slin32;
	lame48_f.format = ast_format_slin48;

	for (i = 0; i < ARRAY_LEN(lame_list); i++) {
		if (ast_format_def_register(lame_list[i])) {
			unload_module();
			return AST_MODULE_LOAD_DECLINE;
		}
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "MP3 format using LAME");
