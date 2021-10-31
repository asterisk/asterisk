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

#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>
#include <vorbis/vorbisfile.h>

#ifdef _WIN32
#include <io.h>
#endif

#include "asterisk/mod_format.h"
#include "asterisk/module.h"
#include "asterisk/format_cache.h"

/*
 * this is the number of samples we deal with. Samples are converted
 * to SLINEAR so each one uses 2 bytes in the buffer.
 */
#define SAMPLES_MAX 512
#define	BUF_SIZE	(2*SAMPLES_MAX)

#define BLOCK_SIZE 4096		/* used internally in the vorbis routines */

struct ogg_vorbis_desc {	/* format specific parameters */
	/* OggVorbis_File structure for libvorbisfile interface */
	OggVorbis_File ov_f;

	/* structures for handling the Ogg container */
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

	/*! \brief Stores the current pcm position to support tell() on writing mode. */
	off_t writing_pcm_pos;

	/*! \brief Indicates whether an End of Stream condition has been detected. */
	int eos;
};

#if !defined(HAVE_VORBIS_OPEN_CALLBACKS)
/*
 * Declared for backward compatibility with vorbisfile v1.1.2.
 * Code taken from vorbisfile.h v1.2.0.
 */
static int _ov_header_fseek_wrap(FILE *f, ogg_int64_t off, int whence)
{
	if (f == NULL) {
		return -1;
	}
	return fseek(f, off, whence);
}

static ov_callbacks OV_CALLBACKS_NOCLOSE = {
  (size_t (*)(void *, size_t, size_t, void *))  fread,
  (int (*)(void *, ogg_int64_t, int))           _ov_header_fseek_wrap,
  (int (*)(void *))                             NULL,
  (long (*)(void *))                            ftell
};
#endif	/* !defined(HAVE_VORBIS_OPEN_CALLBACKS) */

/*!
 * \brief Create a new OGG/Vorbis filestream and set it up for reading.
 * \param s File that points to on disk storage of the OGG/Vorbis data.
 * \return The new filestream.
 */
static int ogg_vorbis_open(struct ast_filestream *s)
{
	int result;
	struct ogg_vorbis_desc *desc = (struct ogg_vorbis_desc *) s->_private;

	/* initialize private description block */
	memset(desc, 0, sizeof(struct ogg_vorbis_desc));
	desc->writing = 0;

	/* actually open file */
	result = ov_open_callbacks(s->f, &desc->ov_f, NULL, 0, OV_CALLBACKS_NOCLOSE);
	if (result != 0) {
		ast_log(LOG_ERROR, "Error opening Ogg/Vorbis file stream.\n");
		return -1;
	}

	/* check stream(s) type */
	if (desc->ov_f.vi->channels != 1) {
		ast_log(LOG_ERROR, "Only monophonic OGG/Vorbis files are currently supported!\n");
		ov_clear(&desc->ov_f);
		return -1;
	}

	if (desc->ov_f.vi->rate != DEFAULT_SAMPLE_RATE) {
		ast_log(LOG_ERROR, "Only 8000Hz OGG/Vorbis files are currently supported!\n");
		ov_clear(&desc->ov_f);
		return -1;
	}

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
	struct ogg_vorbis_desc *tmp = (struct ogg_vorbis_desc *) s->_private;

	tmp->writing = 1;
	tmp->writing_pcm_pos = 0;

	vorbis_info_init(&tmp->vi);

	if (vorbis_encode_init_vbr(&tmp->vi, 1, DEFAULT_SAMPLE_RATE, 0.4)) {
		ast_log(LOG_ERROR, "Unable to initialize Vorbis encoder!\n");
		vorbis_info_clear(&tmp->vi);
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
		if (fwrite(tmp->og.header, 1, tmp->og.header_len, s->f) != tmp->og.header_len) {
			ast_log(LOG_WARNING, "fwrite() failed: %s\n", strerror(errno));
		}
		if (fwrite(tmp->og.body, 1, tmp->og.body_len, s->f) != tmp->og.body_len) {
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
static void write_stream(struct ogg_vorbis_desc *s, FILE *f)
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
				if (fwrite(s->og.header, 1, s->og.header_len, f) != s->og.header_len) {
					ast_log(LOG_WARNING, "fwrite() failed: %s\n", strerror(errno));
				}
				if (fwrite(s->og.body, 1, s->og.body_len, f) != s->og.body_len) {
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
	struct ogg_vorbis_desc *s = (struct ogg_vorbis_desc *) fs->_private;

	if (!s->writing) {
		ast_log(LOG_ERROR, "This stream is not set up for writing!\n");
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

	s->writing_pcm_pos +=  f->samples;

	return 0;
}

/*!
 * \brief Close a OGG/Vorbis filestream.
 * \param fs A OGG/Vorbis filestream.
 */
static void ogg_vorbis_close(struct ast_filestream *fs)
{
	struct ogg_vorbis_desc *s = (struct ogg_vorbis_desc *) fs->_private;

	if (s->writing) {
		/* Tell the Vorbis encoder that the stream is finished
		 * and write out the rest of the data */
		vorbis_analysis_wrote(&s->vd, 0);
		write_stream(s, fs->f);

		/* Cleanup */
		ogg_stream_clear(&s->os);
		vorbis_block_clear(&s->vb);
		vorbis_dsp_clear(&s->vd);
		vorbis_comment_clear(&s->vc);
		vorbis_info_clear(&s->vi);
	} else {
		/* clear OggVorbis_File handle */
		ov_clear(&s->ov_f);
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
	struct ogg_vorbis_desc *desc = (struct ogg_vorbis_desc *) fs->_private;
	int current_bitstream = -10;
	char *out_buf;
	long bytes_read;

	if (desc->writing) {
		ast_log(LOG_WARNING, "Reading is not supported on OGG/Vorbis on write files.\n");
		return NULL;
	}

	/* initialize frame */
	AST_FRAME_SET_BUFFER(&fs->fr, fs->buf, AST_FRIENDLY_OFFSET, BUF_SIZE);
	out_buf = (char *) (fs->fr.data.ptr);	/* SLIN data buffer */

	/* read samples from OV interface */
	bytes_read = ov_read(
		&desc->ov_f,
		out_buf,						/* Buffer to write data */
		BUF_SIZE,						/* Size of buffer */
		(__BYTE_ORDER == __BIG_ENDIAN),	/* Endianes (0 for little) */
		2,								/* 1 = 8bit, 2 = 16bit */
		1,								/* 0 = unsigned, 1 = signed */
		&current_bitstream				/* Returns the current bitstream section */
	);

	/* check returned data */
	if (bytes_read <= 0) {
		/* End of stream */
		return NULL;
	}

	/* Return decoded bytes */
	fs->fr.datalen = bytes_read;
	fs->fr.samples = bytes_read / 2;
	*whennext = fs->fr.samples;
	return &fs->fr;
}

/*!
 * \brief Truncate an OGG/Vorbis filestream.
 * \param fs The filestream to truncate.
 * \return 0 on success, -1 on failure.
 */

static int ogg_vorbis_trunc(struct ast_filestream *fs)
{
	ast_log(LOG_WARNING, "Truncation is not supported on OGG/Vorbis streams!\n");
	return -1;
}

/*!
 * \brief Tell the current position in OGG/Vorbis filestream measured in pcms.
 * \param fs The filestream to take action on.
 * \return 0 or greater with the position measured in samples, or -1 for false.
 */
static off_t ogg_vorbis_tell(struct ast_filestream *fs)
{
	off_t pos;
	struct ogg_vorbis_desc *desc = (struct ogg_vorbis_desc *) fs->_private;

	if (desc->writing) {
		return desc->writing_pcm_pos;
	}

	if ((pos = ov_pcm_tell(&desc->ov_f)) < 0) {
		return -1;
	}
	return pos;
}

/*!
 * \brief Seek to a specific position in an OGG/Vorbis filestream.
 * \param fs The filestream to take action on.
 * \param sample_offset New position for the filestream, measured in 8KHz samples.
 * \param whence Location to measure
 * \return 0 on success, -1 on failure.
 */
static int ogg_vorbis_seek(struct ast_filestream *fs, off_t sample_offset, int whence)
{
	int seek_result = -1;
	off_t relative_pcm_pos;
	struct ogg_vorbis_desc *desc = (struct ogg_vorbis_desc *) fs->_private;

	if (desc->writing) {
		ast_log(LOG_WARNING, "Seeking is not supported on OGG/Vorbis streams in writing mode!\n");
		return -1;
	}

	/* ov_pcm_seek support seeking only from begining (SEEK_SET), the rest must be emulated */
	switch (whence) {
	case SEEK_SET:
		seek_result = ov_pcm_seek(&desc->ov_f, sample_offset);
		break;
	case SEEK_CUR:
		if ((relative_pcm_pos = ogg_vorbis_tell(fs)) < 0) {
			seek_result = -1;
			break;
		}
		seek_result = ov_pcm_seek(&desc->ov_f, relative_pcm_pos + sample_offset);
		break;
	case SEEK_END:
		if ((relative_pcm_pos = ov_pcm_total(&desc->ov_f, -1)) < 0) {
			seek_result = -1;
			break;
		}
		seek_result = ov_pcm_seek(&desc->ov_f, relative_pcm_pos - sample_offset);
		break;
	default:
		ast_log(LOG_WARNING, "Unknown *whence* to seek on OGG/Vorbis streams!\n");
		break;
	}

	/* normalize error value to -1,0 */
	return (seek_result == 0) ? 0 : -1;
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
	.desc_size = sizeof(struct ogg_vorbis_desc),
};

static int load_module(void)
{
	vorbis_f.format = ast_format_slin;
	if (ast_format_def_register(&vorbis_f))
		return AST_MODULE_LOAD_DECLINE;
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return ast_format_def_unregister(vorbis_f.name);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "OGG/Vorbis audio",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND
);
