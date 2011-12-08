/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Jeff Ollie
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
 * \brief OGG/Vorbis streams.
 * \arg File name extension: ogg
 * \ingroup formats
 */

/* the order of these dependencies is important... it also specifies
   the link order of the libraries during linking
*/

/*** MODULEINFO
	<depend>vorbis</depend>
	<depend>ogg</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>

#ifdef _WIN32
#include <io.h>
#endif

#include "asterisk/mod_format.h"
#include "asterisk/module.h"

/*
 * this is the number of samples we deal with. Samples are converted
 * to SLINEAR so each one uses 2 bytes in the buffer.
 */
#define SAMPLES_MAX 160
#define	BUF_SIZE	(2*SAMPLES_MAX)

#define BLOCK_SIZE 4096		/* used internally in the vorbis routines */

struct vorbis_desc {	/* format specific parameters */
	/* structures for handling the Ogg container */
	ogg_sync_state oy;
	ogg_stream_state os;
	ogg_page og;
	ogg_packet op;
	
	/* structures for handling Vorbis audio data */
	vorbis_info vi;
	vorbis_comment vc;
	vorbis_dsp_state vd;
	vorbis_block vb;
	
	/*! \brief Indicates whether this filestream is set up for reading or writing. */
	int writing;
	
	/*! \brief Indicates whether an End of Stream condition has been detected. */
	int eos;
};

/*!
 * \brief Create a new OGG/Vorbis filestream and set it up for reading.
 * \param s File that points to on disk storage of the OGG/Vorbis data.
 * \return The new filestream.
 */
static int ogg_vorbis_open(struct ast_filestream *s)
{
	int i;
	int bytes;
	int result;
	char **ptr;
	char *buffer;
	struct vorbis_desc *tmp = (struct vorbis_desc *)s->_private;

	tmp->writing = 0;

	ogg_sync_init(&tmp->oy);

	buffer = ogg_sync_buffer(&tmp->oy, BLOCK_SIZE);
	bytes = fread(buffer, 1, BLOCK_SIZE, s->f);
	ogg_sync_wrote(&tmp->oy, bytes);

	result = ogg_sync_pageout(&tmp->oy, &tmp->og);
	if (result != 1) {
		if(bytes < BLOCK_SIZE) {
			ast_log(LOG_ERROR, "Run out of data...\n");
		} else {
			ast_log(LOG_ERROR, "Input does not appear to be an Ogg bitstream.\n");
		}
		ogg_sync_clear(&tmp->oy);
		return -1;
	}
	
	ogg_stream_init(&tmp->os, ogg_page_serialno(&tmp->og));
	vorbis_info_init(&tmp->vi);
	vorbis_comment_init(&tmp->vc);

	if (ogg_stream_pagein(&tmp->os, &tmp->og) < 0) { 
		ast_log(LOG_ERROR, "Error reading first page of Ogg bitstream data.\n");
error:
		ogg_stream_clear(&tmp->os);
		vorbis_comment_clear(&tmp->vc);
		vorbis_info_clear(&tmp->vi);
		ogg_sync_clear(&tmp->oy);
		return -1;
	}
	
	if (ogg_stream_packetout(&tmp->os, &tmp->op) != 1) { 
		ast_log(LOG_ERROR, "Error reading initial header packet.\n");
		goto error;
	}
	
	if (vorbis_synthesis_headerin(&tmp->vi, &tmp->vc, &tmp->op) < 0) { 
		ast_log(LOG_ERROR, "This Ogg bitstream does not contain Vorbis audio data.\n");
		goto error;
	}
	
	for (i = 0; i < 2 ; ) {
		while (i < 2) {
			result = ogg_sync_pageout(&tmp->oy, &tmp->og);
			if (result == 0)
				break;
			if (result == 1) {
				ogg_stream_pagein(&tmp->os, &tmp->og);
				while(i < 2) {
					result = ogg_stream_packetout(&tmp->os,&tmp->op);
					if(result == 0)
						break;
					if(result < 0) {
						ast_log(LOG_ERROR, "Corrupt secondary header.  Exiting.\n");
						goto error;
					}
					vorbis_synthesis_headerin(&tmp->vi, &tmp->vc, &tmp->op);
					i++;
				}
			}
		}

		buffer = ogg_sync_buffer(&tmp->oy, BLOCK_SIZE);
		bytes = fread(buffer, 1, BLOCK_SIZE, s->f);
		if (bytes == 0 && i < 2) {
			ast_log(LOG_ERROR, "End of file before finding all Vorbis headers!\n");
			goto error;
		}
		ogg_sync_wrote(&tmp->oy, bytes);
	}
	
	for (ptr = tmp->vc.user_comments; *ptr; ptr++)
		ast_debug(1, "OGG/Vorbis comment: %s\n", *ptr);
		ast_debug(1, "OGG/Vorbis bitstream is %d channel, %ldHz\n", tmp->vi.channels, tmp->vi.rate);
		ast_debug(1, "OGG/Vorbis file encoded by: %s\n", tmp->vc.vendor);

	if (tmp->vi.channels != 1) {
		ast_log(LOG_ERROR, "Only monophonic OGG/Vorbis files are currently supported!\n");
		goto error;
	}
	
	if (tmp->vi.rate != DEFAULT_SAMPLE_RATE) {
		ast_log(LOG_ERROR, "Only 8000Hz OGG/Vorbis files are currently supported!\n");
		vorbis_block_clear(&tmp->vb);
		vorbis_dsp_clear(&tmp->vd);
		goto error;
	}
	
	vorbis_synthesis_init(&tmp->vd, &tmp->vi);
	vorbis_block_init(&tmp->vd, &tmp->vb);

	return 0;
}

/*!
 * \brief Create a new OGG/Vorbis filestream and set it up for writing.
 * \param s File pointer that points to on-disk storage.
 * \param comment Comment that should be embedded in the OGG/Vorbis file.
 * \return A new filestream.
 */
static int ogg_vorbis_rewrite(struct ast_filestream *s,
						 const char *comment)
{
	ogg_packet header;
	ogg_packet header_comm;
	ogg_packet header_code;
	struct vorbis_desc *tmp = (struct vorbis_desc *)s->_private;

	tmp->writing = 1;

	vorbis_info_init(&tmp->vi);

	if (vorbis_encode_init_vbr(&tmp->vi, 1, DEFAULT_SAMPLE_RATE, 0.4)) {
		ast_log(LOG_ERROR, "Unable to initialize Vorbis encoder!\n");
		return -1;
	}

	vorbis_comment_init(&tmp->vc);
	vorbis_comment_add_tag(&tmp->vc, "ENCODER", "Asterisk PBX");
	if (comment)
		vorbis_comment_add_tag(&tmp->vc, "COMMENT", (char *) comment);

	vorbis_analysis_init(&tmp->vd, &tmp->vi);
	vorbis_block_init(&tmp->vd, &tmp->vb);

	ogg_stream_init(&tmp->os, ast_random());

	vorbis_analysis_headerout(&tmp->vd, &tmp->vc, &header, &header_comm,
				  &header_code);
	ogg_stream_packetin(&tmp->os, &header);
	ogg_stream_packetin(&tmp->os, &header_comm);
	ogg_stream_packetin(&tmp->os, &header_code);

	while (!tmp->eos) {
		if (ogg_stream_flush(&tmp->os, &tmp->og) == 0)
			break;
		if (!fwrite(tmp->og.header, 1, tmp->og.header_len, s->f)) {
			ast_log(LOG_WARNING, "fwrite() failed: %s\n", strerror(errno));
		}
		if (!fwrite(tmp->og.body, 1, tmp->og.body_len, s->f)) {
			ast_log(LOG_WARNING, "fwrite() failed: %s\n", strerror(errno));
		}
		if (ogg_page_eos(&tmp->og))
			tmp->eos = 1;
	}

	return 0;
}

/*!
 * \brief Write out any pending encoded data.
 * \param s An OGG/Vorbis filestream.
 * \param f The file to write to.
 */
static void write_stream(struct vorbis_desc *s, FILE *f)
{
	while (vorbis_analysis_blockout(&s->vd, &s->vb) == 1) {
		vorbis_analysis(&s->vb, NULL);
		vorbis_bitrate_addblock(&s->vb);

		while (vorbis_bitrate_flushpacket(&s->vd, &s->op)) {
			ogg_stream_packetin(&s->os, &s->op);
			while (!s->eos) {
				if (ogg_stream_pageout(&s->os, &s->og) == 0) {
					break;
				}
				if (!fwrite(s->og.header, 1, s->og.header_len, f)) {
				ast_log(LOG_WARNING, "fwrite() failed: %s\n", strerror(errno));
				}
				if (!fwrite(s->og.body, 1, s->og.body_len, f)) {
					ast_log(LOG_WARNING, "fwrite() failed: %s\n", strerror(errno));
				}
				if (ogg_page_eos(&s->og)) {
					s->eos = 1;
				}
			}
		}
	}
}

/*!
 * \brief Write audio data from a frame to an OGG/Vorbis filestream.
 * \param fs An OGG/Vorbis filestream.
 * \param f A frame containing audio to be written to the filestream.
 * \return -1 if there was an error, 0 on success.
 */
static int ogg_vorbis_write(struct ast_filestream *fs, struct ast_frame *f)
{
	int i;
	float **buffer;
	short *data;
	struct vorbis_desc *s = (struct vorbis_desc *)fs->_private;

	if (!s->writing) {
		ast_log(LOG_ERROR, "This stream is not set up for writing!\n");
		return -1;
	}

	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass.format.id != AST_FORMAT_SLINEAR) {
		ast_log(LOG_WARNING, "Asked to write non-SLINEAR frame (%s)!\n",
			ast_getformatname(&f->subclass.format));
		return -1;
	}
	if (!f->datalen)
		return -1;

	data = (short *) f->data.ptr;

	buffer = vorbis_analysis_buffer(&s->vd, f->samples);

	for (i = 0; i < f->samples; i++)
		buffer[0][i] = (double)data[i] / 32768.0;

	vorbis_analysis_wrote(&s->vd, f->samples);

	write_stream(s, fs->f);

	return 0;
}

/*!
 * \brief Close a OGG/Vorbis filestream.
 * \param fs A OGG/Vorbis filestream.
 */
static void ogg_vorbis_close(struct ast_filestream *fs)
{
	struct vorbis_desc *s = (struct vorbis_desc *)fs->_private;

	if (s->writing) {
		/* Tell the Vorbis encoder that the stream is finished
		 * and write out the rest of the data */
		vorbis_analysis_wrote(&s->vd, 0);
		write_stream(s, fs->f);
	}

	ogg_stream_clear(&s->os);
	vorbis_block_clear(&s->vb);
	vorbis_dsp_clear(&s->vd);
	vorbis_comment_clear(&s->vc);
	vorbis_info_clear(&s->vi);

	if (s->writing) {
		ogg_sync_clear(&s->oy);
	}
}

/*!
 * \brief Get audio data.
 * \param fs An OGG/Vorbis filestream.
 * \param pcm Pointer to a buffere to store audio data in.
 */

static int read_samples(struct ast_filestream *fs, float ***pcm)
{
	int samples_in;
	int result;
	char *buffer;
	int bytes;
	struct vorbis_desc *s = (struct vorbis_desc *)fs->_private;

	while (1) {
		samples_in = vorbis_synthesis_pcmout(&s->vd, pcm);
		if (samples_in > 0) {
			return samples_in;
		}

		/* The Vorbis decoder needs more data... */
		/* See ifOGG has any packets in the current page for the Vorbis decoder. */
		result = ogg_stream_packetout(&s->os, &s->op);
		if (result > 0) {
			/* Yes OGG had some more packets for the Vorbis decoder. */
			if (vorbis_synthesis(&s->vb, &s->op) == 0) {
				vorbis_synthesis_blockin(&s->vd, &s->vb);
			}

			continue;
		}

		if (result < 0)
			ast_log(LOG_WARNING,
					"Corrupt or missing data at this page position; continuing...\n");

		/* No more packets left in the current page... */

		if (s->eos) {
			/* No more pages left in the stream */
			return -1;
		}

		while (!s->eos) {
			/* See ifOGG has any pages in it's internal buffers */
			result = ogg_sync_pageout(&s->oy, &s->og);
			if (result > 0) {
				/* Yes, OGG has more pages in it's internal buffers,
				   add the page to the stream state */
				result = ogg_stream_pagein(&s->os, &s->og);
				if (result == 0) {
					/* Yes, got a new,valid page */
					if (ogg_page_eos(&s->og)) {
						s->eos = 1;
					}
					break;
				}
				ast_log(LOG_WARNING,
						"Invalid page in the bitstream; continuing...\n");
			}

			if (result < 0)
				ast_log(LOG_WARNING,
						"Corrupt or missing data in bitstream; continuing...\n");

			/* No, we need to read more data from the file descrptor */
			/* get a buffer from OGG to read the data into */
			buffer = ogg_sync_buffer(&s->oy, BLOCK_SIZE);
			/* read more data from the file descriptor */
			bytes = fread(buffer, 1, BLOCK_SIZE, fs->f);
			/* Tell OGG how many bytes we actually read into the buffer */
			ogg_sync_wrote(&s->oy, bytes);
			if (bytes == 0) {
				s->eos = 1;
			}
		}
	}
}

/*!
 * \brief Read a frame full of audio data from the filestream.
 * \param fs The filestream.
 * \param whennext Number of sample times to schedule the next call.
 * \return A pointer to a frame containing audio data or NULL ifthere is no more audio data.
 */
static struct ast_frame *ogg_vorbis_read(struct ast_filestream *fs,
					 int *whennext)
{
	int clipflag = 0;
	int i;
	int j;
	double accumulator[SAMPLES_MAX];
	int val;
	int samples_in;
	int samples_out = 0;
	struct vorbis_desc *s = (struct vorbis_desc *)fs->_private;
	short *buf;	/* SLIN data buffer */

	fs->fr.frametype = AST_FRAME_VOICE;
	ast_format_set(&fs->fr.subclass.format, AST_FORMAT_SLINEAR, 0);
	fs->fr.mallocd = 0;
	AST_FRAME_SET_BUFFER(&fs->fr, fs->buf, AST_FRIENDLY_OFFSET, BUF_SIZE);
	buf = (short *)(fs->fr.data.ptr);	/* SLIN data buffer */

	while (samples_out != SAMPLES_MAX) {
		float **pcm;
		int len = SAMPLES_MAX - samples_out;

		/* See ifVorbis decoder has some audio data for us ... */
		samples_in = read_samples(fs, &pcm);
		if (samples_in <= 0)
			break;

		/* Got some audio data from Vorbis... */
		/* Convert the float audio data to 16-bit signed linear */

		clipflag = 0;
		if (samples_in > len)
			samples_in = len;
		for (j = 0; j < samples_in; j++)
			accumulator[j] = 0.0;

		for (i = 0; i < s->vi.channels; i++) {
			float *mono = pcm[i];
			for (j = 0; j < samples_in; j++)
				accumulator[j] += mono[j];
		}

		for (j = 0; j < samples_in; j++) {
			val = accumulator[j] * 32767.0 / s->vi.channels;
			if (val > 32767) {
				val = 32767;
				clipflag = 1;
			} else if (val < -32768) {
				val = -32768;
				clipflag = 1;
			}
			buf[samples_out + j] = val;
		}

		if (clipflag)
			ast_log(LOG_WARNING, "Clipping in frame %ld\n", (long) (s->vd.sequence));
		/* Tell the Vorbis decoder how many samples we actually used. */
		vorbis_synthesis_read(&s->vd, samples_in);
		samples_out += samples_in;
	}

	if (samples_out > 0) {
		fs->fr.datalen = samples_out * 2;
		fs->fr.samples = samples_out;
		*whennext = samples_out;

		return &fs->fr;
	} else {
		return NULL;
	}
}

/*!
 * \brief Trucate an OGG/Vorbis filestream.
 * \param s The filestream to truncate.
 * \return 0 on success, -1 on failure.
 */

static int ogg_vorbis_trunc(struct ast_filestream *s)
{
	ast_log(LOG_WARNING, "Truncation is not supported on OGG/Vorbis streams!\n");
	return -1;
}

/*!
 * \brief Seek to a specific position in an OGG/Vorbis filestream.
 * \param s The filestream to truncate.
 * \param sample_offset New position for the filestream, measured in 8KHz samples.
 * \param whence Location to measure 
 * \return 0 on success, -1 on failure.
 */
static int ogg_vorbis_seek(struct ast_filestream *s, off_t sample_offset, int whence)
{
	ast_log(LOG_WARNING, "Seeking is not supported on OGG/Vorbis streams!\n");
	return -1;
}

static off_t ogg_vorbis_tell(struct ast_filestream *s)
{
	ast_log(LOG_WARNING, "Telling is not supported on OGG/Vorbis streams!\n");
	return -1;
}

static struct ast_format_def vorbis_f = {
	.name = "ogg_vorbis",
	.exts = "ogg",
	.open = ogg_vorbis_open,
	.rewrite = ogg_vorbis_rewrite,
	.write = ogg_vorbis_write,
	.seek =	ogg_vorbis_seek,
	.trunc = ogg_vorbis_trunc,
	.tell = ogg_vorbis_tell,
	.read = ogg_vorbis_read,
	.close = ogg_vorbis_close,
	.buf_size = BUF_SIZE + AST_FRIENDLY_OFFSET,
	.desc_size = sizeof(struct vorbis_desc),
};

static int load_module(void)
{
	ast_format_set(&vorbis_f.format, AST_FORMAT_SLINEAR, 0);
	if (ast_format_def_register(&vorbis_f))
		return AST_MODULE_LOAD_FAILURE;
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return ast_format_def_unregister(vorbis_f.name);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "OGG/Vorbis audio",
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND
);
