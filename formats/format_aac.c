/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2024, Sperl Viktor
 * 
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
 * \brief AAC Format Handler
 * \author Sperl Viktor <viktike32@gmail.com>
 * \ingroup formats
 */

/*** MODULEINFO
	<depend>faac</depend>
	<depend>faad</depend>
	<defaultenabled>no</defaultenabled>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

/* The AAC encoder (libfaac) */
#include <faac.h>
/* The AAC decoder (libfaad) */
#include <neaacdec.h>

#include "asterisk/module.h"
#include "asterisk/mod_format.h"
#include "asterisk/logger.h"
#include "asterisk/format_cache.h"

#define CHANNELS 1
#define DECODER_BLOCKSIZE 1024
#define SLIN_SAMPLE_SIZE 320
#define BITS 2

struct aac_private {
	/* encoder buffer */
	char * ebuffer;
	int esamples, edatalen;

	/* receives the total number of samples that should be fed to each encoder call */
	unsigned long inputSamples;
	/* receives the maximum number of bytes that can be in the output buffer after an encoder call */
	unsigned long maxOutputBytes;

	/* decoder buffer */
	int16_t safe_buffer[DECODER_BLOCKSIZE * 48];
	unsigned char dinput[DECODER_BLOCKSIZE];
	unsigned long dbytes;
	unsigned int dinoff;
	unsigned char channels;
	long dsamples, doffset, dconsumed;

	/* handles */
	NeAACDecHandle decoder;
	faacEncHandle encoder;

	/* counters */
	unsigned int decoder_counter, encoder_counter;
};

static void aac_encoder_flush(struct ast_filestream *s, unsigned int samples)
{
	struct aac_private *p = s->_private;
	int encodedBufferBytes = 0;
	unsigned char encodedBuffer[p->maxOutputBytes];
	encodedBufferBytes = faacEncEncode(p->encoder, (int *)p->ebuffer, samples, encodedBuffer, (unsigned int)p->maxOutputBytes);
	if (encodedBufferBytes > 0) {
		fwrite(encodedBuffer, 1, encodedBufferBytes, s->f);
		ast_debug(3, "Encoder wrote: %d bytes (%d samples)", encodedBufferBytes, samples);
	} else if (encodedBufferBytes == 0) {
		ast_debug(3, "Filling encoder buffer...");
	} else {
		ast_debug(3, "Encoder error %d", encodedBufferBytes);
	}
}

static unsigned int aac_encoder_init(struct aac_private *p, unsigned long sample_rate)
{
	faacEncConfigurationPtr encoder_config;

	if (p->encoder_counter == 0) {
		p->encoder = faacEncOpen(sample_rate, CHANNELS, &p->inputSamples, &p->maxOutputBytes);
		encoder_config = faacEncGetCurrentConfiguration(p->encoder);
		encoder_config->inputFormat = FAAC_INPUT_16BIT;
		encoder_config->outputFormat = 1;
		encoder_config->bitRate = sample_rate * 2;
		encoder_config->bandWidth = 4000;
		encoder_config->aacObjectType = LOW;
		encoder_config->mpegVersion = MPEG4;
		encoder_config->useTns = 1;
		encoder_config->useLfe = 0;
		encoder_config->jointmode = 1;
		encoder_config->quantqual = 50;
		encoder_config->pnslevel = 4;
		faacEncSetConfiguration(p->encoder, encoder_config);

		ast_debug(3, "Encoder expects %ld input samples per encode() call with %d channels, max output buffer size %ld bytes",
			p->inputSamples,
			CHANNELS,
			p->maxOutputBytes
		);
	}
	return p->encoder_counter;
}

static void aac_decoder_init(struct aac_private *p, unsigned long sample_rate)
{
	NeAACDecConfigurationPtr decoder_config;

	p->decoder = NeAACDecOpen();
	decoder_config = NeAACDecGetCurrentConfiguration(p->decoder);
	/* Low-Complexity profile */
	decoder_config->defObjectType = LC;
	decoder_config->defSampleRate = sample_rate;
	/* Only does 5.1 -> stereo downmixing, ... */
	decoder_config->downMatrix = 1;
	/* ... therefor we need this for proper downmixing instead of FAAD_FMT_16BIT */
	decoder_config->outputFormat = FAAD_FMT_DOUBLE;	
	decoder_config->dontUpSampleImplicitSBR = 1;
	NeAACDecSetConfiguration(p->decoder, decoder_config);

	return;
}

static void aac_decoder_deinit(struct ast_filestream *s)
{
	struct aac_private *p = s->_private;

	if (p->decoder_counter != 0) {
		NeAACDecClose(p->decoder);
	}

	return;
}

static int aac_encoder_deinit(struct ast_filestream *s)
{
	int encoderLastOutputBufferBytes = 0;
	struct aac_private *p = s->_private;

	if (p->encoder_counter != 0) {
		if (p->edatalen > 0) {
			aac_encoder_flush(s, p->esamples);
		}
		ast_free(p->ebuffer);
		unsigned char encoderLastOutputBuffer[p->maxOutputBytes];
		do {
			encoderLastOutputBufferBytes = faacEncEncode(p->encoder, NULL, 0, encoderLastOutputBuffer, p->maxOutputBytes);
			if (encoderLastOutputBufferBytes > 0) {
				fwrite(encoderLastOutputBuffer, 1, encoderLastOutputBufferBytes, s->f);
				ast_debug(3, "Encoder last wrote: %d bytes", encoderLastOutputBufferBytes);
			}
		} while (encoderLastOutputBufferBytes > 0);
		faacEncClose(p->encoder);
	}

	return encoderLastOutputBufferBytes;
}

static int aac_open(struct ast_filestream *s)
{
	struct aac_private *p = s->_private;

	/* decoder */
	p->decoder_counter = 0;

	/* encoder */
	p->encoder_counter = 0;

	return 0;
}


static void aac_close(struct ast_filestream *s)
{
	/* decoder */
	aac_decoder_deinit(s);

	/* encoder */
	aac_encoder_deinit(s);

	return;
}

static void aac_decode(struct aac_private *p, long offset)
{
	NeAACDecFrameInfo decoderInfo;

	double * unsafe_buffer;
	double sample;
	long double sum;
	unsigned int channels;

	unsafe_buffer = NeAACDecDecode(p->decoder, &decoderInfo, p->dinput + offset, p->dconsumed);
	channels = (unsigned int)decoderInfo.channels;
	if (channels == 0) {
		p->decoder_counter++;
		p->dbytes = p->dconsumed;
		p->dsamples = 0;
		ast_log(LOG_NOTICE, "Decoder error[0]: no audio channels found");
		return;
	} else if (channels != CHANNELS) {
		/* downmix to mono */
		double *mono_buffer = (double *)ast_malloc(decoderInfo.samples * sizeof(double) / channels);
		for (int i = 0; i < decoderInfo.samples; i += channels) {
			sum = 0.0d;
			for (int j = 0; j < channels; j++) {
				sum += unsafe_buffer[i + j];
			}
			mono_buffer[i / channels] = sum / channels;
		}
		/* double to 16bit */
		for (int i = 0; i < decoderInfo.samples / channels; ++i) {
			sample = mono_buffer[i];
			if (sample > 1.0d) sample = 1.0d;
			if (sample < -1.0d) sample = -1.0d;
			p->safe_buffer[i] = (int16_t)(sample * INT16_MAX);
		}
		ast_free(mono_buffer);
		p->dsamples = decoderInfo.samples / channels;
	} else {
		/* double to 16bit */
		for (int i = 0; i < decoderInfo.samples; ++i) {
			sample = unsafe_buffer[i];
			if (sample > 1.0d) sample = 1.0d;
			if (sample < -1.0d) sample = -1.0d;
			p->safe_buffer[i] = (int16_t)(sample * INT16_MAX);
		}
		p->dsamples = decoderInfo.samples;
	}
	p->dbytes = decoderInfo.bytesconsumed;
	p->doffset = 0;
	if (decoderInfo.error != 0) {
		ast_log(LOG_NOTICE, "Decoder error[%d]: %s",
			decoderInfo.error,
			NeAACDecGetErrorMessage(decoderInfo.error)
		);
	} else {
		ast_debug(3, "Decode(), got %ld (%ld before downmixing) samples (from %ld bytes)", p->dsamples, decoderInfo.samples, p->dbytes);
	}
	p->decoder_counter++;
}

static struct ast_frame *aac_read(struct ast_filestream *s, int *whennext, unsigned long expected_sample_rate, unsigned int sample_size)
{
	/* decoder init */
	unsigned long sample_rate;
	long init = 0;

	/* positions */
	int remaining_samples, still_needed_samples, copied_samples;

	struct aac_private *p = s->_private;

	if (p->encoder_counter != 0) {
		ast_log(LOG_ERROR, "This filestream is already in encoder mode");
		return NULL;
	}

	if (p->decoder_counter == 0) {
		aac_decoder_init(p, expected_sample_rate);
		p->dconsumed = fread(p->dinput, 1, DECODER_BLOCKSIZE, s->f);
		init = NeAACDecInit(p->decoder, p->dinput, p->dconsumed, &sample_rate, &p->channels);
		ast_debug(3, "Decoder params: %ld sampling rate, %d channels, skip %ld bytes", sample_rate, p->channels, init);

		if (sample_rate != expected_sample_rate) {
			ast_log(LOG_ERROR, "Incompatible sampling rate: %ld, must be %ld", sample_rate, expected_sample_rate);
			return NULL;
		}

		p->dconsumed -= init;
		aac_decode(p, init);
		p->dinoff = 0;
	}

	AST_FRAME_SET_BUFFER(&s->fr, s->buf, AST_FRIENDLY_OFFSET, sample_size);

	remaining_samples = p->dsamples - (sample_size / BITS);
	if (remaining_samples >= 0) {
		/* output a full ast_frame */
		s->fr.datalen = sample_size;
		s->fr.samples = sample_size / BITS;
		*whennext = sample_size / BITS;
		memcpy(s->fr.data.ptr, (char *)p->safe_buffer + p->doffset, sample_size);
		p->doffset += sample_size;
		p->dsamples = remaining_samples;
		return &s->fr;
	} else {
		/* prepare to output a partial ast_frame */
		if (p->dsamples > 0) {
			s->fr.datalen = p->dsamples * BITS;
			s->fr.samples = p->dsamples;
			memcpy(s->fr.data.ptr, (char *)p->safe_buffer + p->doffset, p->dsamples * BITS);
			copied_samples = p->dsamples;
			still_needed_samples = (sample_size / BITS) - p->dsamples;
		} else {
			copied_samples = 0;
			still_needed_samples = sample_size / BITS;
		}
reread:
		ast_debug(3, "Buffer underrun: needed %d samples (%d samples already done)",
			still_needed_samples,
			copied_samples
		);
		p->dinoff += p->dbytes;
		p->dconsumed -= p->dbytes;
		if (p->dconsumed > 0 && p->dconsumed >= (DECODER_BLOCKSIZE / 2)) {
			/* no need yet to read more data from file */
			aac_decode(p, p->dinoff);
		}
		if (p->dconsumed < (DECODER_BLOCKSIZE / 2) || p->dsamples == 0) {
			/* it's time to read more data from file */
			if (p->dconsumed > 0) {
				fseek(s->f, p->dconsumed * -1, SEEK_CUR);
				ast_debug(3,"Rewind %ld", p->dconsumed);
			}
			p->dconsumed = fread(p->dinput, 1, DECODER_BLOCKSIZE, s->f);
			p->dinoff = 0;
			ast_debug(3, "Read %ld bytes from file.", p->dconsumed);
			if (p->dconsumed == 0) {
				/* no more samples from decoder */
				if (p->dsamples > 0) {
					/* output the partial ast_frame */
					*whennext = 0;
					p->dsamples = 0;
					p->doffset = 0;
					return &s->fr;
				} else {
					ast_debug(3, "File EOF");
					return NULL;
				}
			} else {
				aac_decode(p, 0);
			}
		}
		if (p->dsamples >= still_needed_samples) {
			/* finish and output the ast_frame */
			s->fr.datalen = sample_size;
			s->fr.samples = sample_size / BITS;
			*whennext = sample_size / BITS;
			memcpy(s->fr.data.ptr + (copied_samples  * BITS), (char *)p->safe_buffer + p->doffset, sample_size - (still_needed_samples * BITS));
			p->doffset = sample_size - (still_needed_samples * BITS);
			p->dsamples -= still_needed_samples;
			return &s->fr;
		} else {
			/* still not enough samples */
			s->fr.datalen += p->dsamples * BITS;
			s->fr.samples += p->dsamples;
			memcpy(s->fr.data.ptr + (copied_samples  * BITS), (char *)p->safe_buffer + p->doffset, p->dsamples * BITS);
			copied_samples += p->dsamples;
			still_needed_samples -= p->dsamples;
			p->doffset = 0;
			p->dsamples = 0;
			goto reread;
		}
	}
}

static int aac_write(struct ast_filestream *fs, struct ast_frame *f, unsigned long sample_rate)
{
	int i;
	int samples_fit, to_copy_bytes, channel_shift, remaining_bytes;

	struct aac_private *p = fs->_private;
	int this_samples = f->samples * CHANNELS;
	int this_datalen = f->datalen * CHANNELS;
	int len_per_sample = f->datalen / f->samples;

	if (p->decoder_counter != 0) {
		ast_log(LOG_ERROR, "This filestream is already in decoder mode");
		return -1;
	}

	if (aac_encoder_init(p, sample_rate) == 0) {
		p->encoder_counter++;
		p->edatalen = 0;
		p->esamples = 0;
		p->ebuffer = ast_malloc(p->inputSamples * len_per_sample);
	}

	if (p->esamples + this_samples >= p->inputSamples) {
		samples_fit = p->inputSamples - p->esamples;
		to_copy_bytes = len_per_sample * (samples_fit / CHANNELS);
		for (i = 0; i < CHANNELS; i++) {
			channel_shift = i * to_copy_bytes;
			memcpy(p->ebuffer + p->edatalen + channel_shift, f->data.ptr, to_copy_bytes);
		}
		aac_encoder_flush(fs, p->inputSamples);
		p->encoder_counter++;
		p->esamples = this_samples - samples_fit;
		p->edatalen = this_datalen - (to_copy_bytes * CHANNELS);
		if(p->edatalen > 0){
			remaining_bytes = f->datalen - to_copy_bytes;
			for(i = 0; i < CHANNELS; i++){
				channel_shift = i * remaining_bytes;
				memcpy(p->ebuffer + channel_shift, f->data.ptr + to_copy_bytes, remaining_bytes);
			}
		}
	} else {
		for (i = 0; i < CHANNELS; i++) {
			channel_shift = i * f->datalen;
			memcpy(p->ebuffer + p->edatalen + channel_shift, f->data.ptr, f->datalen);
		}
		p->edatalen += this_datalen;
		p->esamples += this_samples;
	}
	return 0;
}

static int aac_seek(struct ast_filestream *s, off_t sample_offset, int whence)
{
	if (sample_offset != 0) {
		ast_debug(3, "Cannot seek to %ld in a Variable Bit Rate / Avarage Bit Rate file. Seeking only possible in Constant Bit Rate files.", sample_offset);
	}
	return -1;
}

static int aac_trunc(struct ast_filestream *s)
{
	int fd;
	off_t current;

	if ((fd = fileno(s->f)) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to determine file descriptor for aac filestream %p: %s\n", s, strerror(errno));
		return -1;
	}
	if ((current = ftello(s->f)) < 0) {
		ast_log(AST_LOG_WARNING, "Unable to determine current position in aac filestream %p: %s\n", s, strerror(errno));
		return -1;
	}
	return ftruncate(fd, current);
}

static off_t aac_tell(struct ast_filestream *s)
{
	return ftello(s->f) / BITS;
}

static char *aac_getcomment(struct ast_filestream *s)
{
	char * comment = "Asterisk AAC decoder\0";
	return comment;
}

/* Sampling rate: 8khz */
static int aac8_write(struct ast_filestream *fs, struct ast_frame *f)
{
	return aac_write(fs, f, 8000);
}
static struct ast_frame *aac8_read(struct ast_filestream *s, int *whennext)
{
	return aac_read(s, whennext, 8000, SLIN_SAMPLE_SIZE);
}
static struct ast_format_def aac8_f = {
	.name = "aac",
	.exts = "aac8|aac|m4a|mp4",
	.mime_types = "audio/aac",
	.open = aac_open,
	.write = aac8_write,
	.seek =	aac_seek,
	.trunc = aac_trunc,
	.tell =	aac_tell,
	.read =	aac8_read,
	.close = aac_close,
	.getcomment = aac_getcomment,
	.buf_size = SLIN_SAMPLE_SIZE + AST_FRIENDLY_OFFSET,
	.desc_size = sizeof(struct aac_private),
};

/* Sampling rate: 16khz */
static int aac16_write(struct ast_filestream *fs, struct ast_frame *f)
{
	return aac_write(fs, f, 16000);
}
static struct ast_frame *aac16_read(struct ast_filestream *s, int *whennext)
{
	return aac_read(s, whennext, 16000, SLIN_SAMPLE_SIZE * 2);
}
static struct ast_format_def aac16_f = {
	.name = "aac16",
	.exts = "aac16",
	.mime_types = "audio/aac",
	.open = aac_open,
	.write = aac16_write,
	.seek =	aac_seek,
	.trunc = aac_trunc,
	.tell =	aac_tell,
	.read =	aac16_read,
	.close = aac_close,
	.getcomment = aac_getcomment,
	.buf_size = (SLIN_SAMPLE_SIZE * 2) + AST_FRIENDLY_OFFSET,
	.desc_size = sizeof(struct aac_private),
};

/* Sampling rate: 32khz */
static int aac32_write(struct ast_filestream *fs, struct ast_frame *f)
{
	return aac_write(fs, f, 32000);
}
static struct ast_frame *aac32_read(struct ast_filestream *s, int *whennext)
{
	return aac_read(s, whennext, 32000, SLIN_SAMPLE_SIZE * 4);
}
static struct ast_format_def aac32_f = {
	.name = "aac32",
	.exts = "aac32",
	.mime_types = "audio/aac",
	.open = aac_open,
	.write = aac32_write,
	.seek =	aac_seek,
	.trunc = aac_trunc,
	.tell =	aac_tell,
	.read =	aac32_read,
	.close = aac_close,
	.getcomment = aac_getcomment,
	.buf_size = (SLIN_SAMPLE_SIZE * 4) + AST_FRIENDLY_OFFSET,
	.desc_size = sizeof(struct aac_private),
};

/* Sampling rate: 48khz */
static int aac48_write(struct ast_filestream *fs, struct ast_frame *f)
{
	return aac_write(fs, f, 48000);
}
static struct ast_frame *aac48_read(struct ast_filestream *s, int *whennext)
{
	return aac_read(s, whennext, 48000, SLIN_SAMPLE_SIZE * 6);
}
static struct ast_format_def aac48_f = {
	.name = "aac48",
	.exts = "aac48",
	.mime_types = "audio/aac",
	.open = aac_open,
	.write = aac48_write,
	.seek =	aac_seek,
	.trunc = aac_trunc,
	.tell =	aac_tell,
	.read =	aac48_read,
	.close = aac_close,
	.getcomment = aac_getcomment,
	.buf_size = (SLIN_SAMPLE_SIZE * 6) + AST_FRIENDLY_OFFSET,
	.desc_size = sizeof(struct aac_private),
};

static struct ast_format_def *aac_list[] = {
	&aac8_f,
	&aac16_f,
	&aac32_f,
	&aac48_f,
};

static int unload_module(void)
{
	int res = 0;
	int i = 0;

	for (i = 0; i < ARRAY_LEN(aac_list); i++) {
		if (ast_format_def_unregister(aac_list[i]->name)) {
			res = -1;
		}
	}
	return res;
}

static int load_module(void)
{
	char * ver;
	char * desc;

	int i;

	/* encoder */
	faacEncGetVersion(&ver, &desc);
	ast_log(LOG_NOTICE, "Encoder v%d:\n%sVersion: %s", FAAC_CFG_VERSION, desc, ver);

	/* decoder */
	NeAACDecGetVersion(&ver, &desc);
	ast_log(LOG_NOTICE, "Decoder v%s:\n%sVersion: %s", FAAD2_VERSION, desc, ver);
	ast_log(LOG_NOTICE, "Decoder has capabilities: %ld", NeAACDecGetCapabilities());

	aac8_f.format = ast_format_slin;
	aac16_f.format = ast_format_slin16;
	aac32_f.format = ast_format_slin32;
	aac48_f.format = ast_format_slin48;

	for (i = 0; i < ARRAY_LEN(aac_list); i++) {
		if (ast_format_def_register(aac_list[i])) {
			unload_module();
			return AST_MODULE_LOAD_DECLINE;
		}
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "AAC - Advanced Audio Coder format");
