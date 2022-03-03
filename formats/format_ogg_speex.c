/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2011-2016, Timo Teräs
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
 * \brief OGG/Speex streams.
 * \arg File name extension: spx
 * \ingroup formats
 */

/*** MODULEINFO
	<depend>speex</depend>
	<depend>ogg</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/mod_format.h"
#include "asterisk/module.h"
#include "asterisk/format_cache.h"

#include <speex/speex_header.h>
#include <ogg/ogg.h>

#define BLOCK_SIZE	4096		/* buffer size for feeding OGG routines */
#define	BUF_SIZE	200

struct speex_desc {	/* format specific parameters */
	/* structures for handling the Ogg container */
	ogg_sync_state oy;
	ogg_stream_state os;
	ogg_page og;
	ogg_packet op;

	int serialno;

	/*! \brief Indicates whether an End of Stream condition has been detected. */
	int eos;
};

static int read_packet(struct ast_filestream *fs)
{
	struct speex_desc *s = (struct speex_desc *)fs->_private;
	char *buffer;
	int result;
	size_t bytes;

	while (1) {
		/* Get one packet */
		result = ogg_stream_packetout(&s->os, &s->op);
		if (result > 0) {
			if (s->op.bytes >= 5 && !memcmp(s->op.packet, "Speex", 5)) {
				s->serialno = s->os.serialno;
			}
			if (s->serialno == -1 || s->os.serialno != s->serialno) {
				continue;
			}
			return 0;
		}

		if (result < 0) {
			ast_log(LOG_WARNING,
				"Corrupt or missing data at this page position; continuing...\n");
		}

		/* No more packets left in the current page... */
		if (s->eos) {
			/* No more pages left in the stream */
			return -1;
		}

		while (!s->eos) {
			/* See if OGG has any pages in it's internal buffers */
			result = ogg_sync_pageout(&s->oy, &s->og);
			if (result > 0) {
				/* Read all streams. */
				if (ogg_page_serialno(&s->og) != s->os.serialno) {
					ogg_stream_reset_serialno(&s->os, ogg_page_serialno(&s->og));
				}
				/* Yes, OGG has more pages in it's internal buffers,
				   add the page to the stream state */
				result = ogg_stream_pagein(&s->os, &s->og);
				if (result == 0) {
					/* Yes, got a new, valid page */
					if (ogg_page_eos(&s->og) &&
					    ogg_page_serialno(&s->og) == s->serialno)
						s->eos = 1;
					break;
				}
				ast_log(LOG_WARNING,
					"Invalid page in the bitstream; continuing...\n");
			}

			if (result < 0) {
				ast_log(LOG_WARNING,
					"Corrupt or missing data in bitstream; continuing...\n");
			}

			/* No, we need to read more data from the file descrptor */
			/* get a buffer from OGG to read the data into */
			buffer = ogg_sync_buffer(&s->oy, BLOCK_SIZE);
			bytes = fread(buffer, 1, BLOCK_SIZE, fs->f);
			ogg_sync_wrote(&s->oy, bytes);
			if (bytes == 0) {
				s->eos = 1;
			}
		}
	}
}

/*!
 * \brief Create a new OGG/Speex filestream and set it up for reading.
 * \param fs File that points to on disk storage of the OGG/Speex data.
 * \return The new filestream.
 */
static int ogg_speex_open(struct ast_filestream *fs)
{
	char *buffer;
	size_t bytes;
	struct speex_desc *s = (struct speex_desc *)fs->_private;
	SpeexHeader *hdr = NULL;
	int i, result, expected_rate;

	expected_rate = ast_format_get_sample_rate(fs->fmt->format);
	s->serialno = -1;
	ogg_sync_init(&s->oy);

	buffer = ogg_sync_buffer(&s->oy, BLOCK_SIZE);
	bytes = fread(buffer, 1, BLOCK_SIZE, fs->f);
	ogg_sync_wrote(&s->oy, bytes);

	result = ogg_sync_pageout(&s->oy, &s->og);
	if (result != 1) {
		if(bytes < BLOCK_SIZE) {
			ast_log(LOG_ERROR, "Run out of data...\n");
		} else {
			ast_log(LOG_ERROR, "Input does not appear to be an Ogg bitstream.\n");
		}
		ogg_sync_clear(&s->oy);
		return -1;
	}

	ogg_stream_init(&s->os, ogg_page_serialno(&s->og));
	if (ogg_stream_pagein(&s->os, &s->og) < 0) {
		ast_log(LOG_ERROR, "Error reading first page of Ogg bitstream data.\n");
		goto error;
	}

	if (read_packet(fs) < 0) {
		ast_log(LOG_ERROR, "Error reading initial header packet.\n");
		goto error;
	}

	hdr = speex_packet_to_header((char*)s->op.packet, s->op.bytes);
	if (memcmp(hdr->speex_string, "Speex   ", 8)) {
		ast_log(LOG_ERROR, "OGG container does not contain Speex audio!\n");
		goto error;
	}
	if (hdr->frames_per_packet != 1) {
		ast_log(LOG_ERROR, "Only one frame-per-packet OGG/Speex files are currently supported!\n");
		goto error;
	}
	if (hdr->nb_channels != 1) {
		ast_log(LOG_ERROR, "Only monophonic OGG/Speex files are currently supported!\n");
		goto error;
	}
	if (hdr->rate != expected_rate) {
		ast_log(LOG_ERROR, "Unexpected sampling rate (%d != %d)!\n",
			hdr->rate, expected_rate);
		goto error;
	}

	/* this packet is the comment */
	if (read_packet(fs) < 0) {
		ast_log(LOG_ERROR, "Error reading comment packet.\n");
		goto error;
	}
	for (i = 0; i < hdr->extra_headers; i++) {
		if (read_packet(fs) < 0) {
			ast_log(LOG_ERROR, "Error reading extra header packet %d.\n", i+1);
			goto error;
		}
	}
	speex_header_free(hdr);

	return 0;
error:
	if (hdr) {
		speex_header_free(hdr);
	}
	ogg_stream_clear(&s->os);
	ogg_sync_clear(&s->oy);
	return -1;
}

/*!
 * \brief Close a OGG/Speex filestream.
 * \param fs A OGG/Speex filestream.
 */
static void ogg_speex_close(struct ast_filestream *fs)
{
	struct speex_desc *s = (struct speex_desc *)fs->_private;

	ogg_stream_clear(&s->os);
	ogg_sync_clear(&s->oy);
}

/*!
 * \brief Read a frame full of audio data from the filestream.
 * \param fs The filestream.
 * \param whennext Number of sample times to schedule the next call.
 * \return A pointer to a frame containing audio data or NULL ifthere is no more audio data.
 */
static struct ast_frame *ogg_speex_read(struct ast_filestream *fs,
					 int *whennext)
{
	struct speex_desc *s = (struct speex_desc *)fs->_private;

	if (read_packet(fs) < 0) {
		return NULL;
	}

	AST_FRAME_SET_BUFFER(&fs->fr, fs->buf, AST_FRIENDLY_OFFSET, BUF_SIZE);
	memcpy(fs->fr.data.ptr, s->op.packet, s->op.bytes);
	fs->fr.datalen = s->op.bytes;
	fs->fr.samples = *whennext = ast_codec_samples_count(&fs->fr);

	return &fs->fr;
}

/*!
 * \brief Truncate an OGG/Speex filestream.
 * \param s The filestream to truncate.
 * \return 0 on success, -1 on failure.
 */

static int ogg_speex_trunc(struct ast_filestream *s)
{
	ast_log(LOG_WARNING, "Truncation is not supported on OGG/Speex streams!\n");
	return -1;
}

static int ogg_speex_write(struct ast_filestream *s, struct ast_frame *f)
{
	ast_log(LOG_WARNING, "Writing is not supported on OGG/Speex streams!\n");
	return -1;
}

/*!
 * \brief Seek to a specific position in an OGG/Speex filestream.
 * \param s The filestream to truncate.
 * \param sample_offset New position for the filestream, measured in 8KHz samples.
 * \param whence Location to measure
 * \return 0 on success, -1 on failure.
 */
static int ogg_speex_seek(struct ast_filestream *s, off_t sample_offset, int whence)
{
	ast_log(LOG_WARNING, "Seeking is not supported on OGG/Speex streams!\n");
	return -1;
}

static off_t ogg_speex_tell(struct ast_filestream *s)
{
	ast_log(LOG_WARNING, "Telling is not supported on OGG/Speex streams!\n");
	return -1;
}

static struct ast_format_def speex_f = {
	.name = "ogg_speex",
	.exts = "spx",
	.open = ogg_speex_open,
	.write = ogg_speex_write,
	.seek = ogg_speex_seek,
	.trunc = ogg_speex_trunc,
	.tell = ogg_speex_tell,
	.read = ogg_speex_read,
	.close = ogg_speex_close,
	.buf_size = BUF_SIZE + AST_FRIENDLY_OFFSET,
	.desc_size = sizeof(struct speex_desc),
};

static struct ast_format_def speex16_f = {
	.name = "ogg_speex16",
	.exts = "spx16",
	.open = ogg_speex_open,
	.write = ogg_speex_write,
	.seek = ogg_speex_seek,
	.trunc = ogg_speex_trunc,
	.tell = ogg_speex_tell,
	.read = ogg_speex_read,
	.close = ogg_speex_close,
	.buf_size = BUF_SIZE + AST_FRIENDLY_OFFSET,
	.desc_size = sizeof(struct speex_desc),
};

static struct ast_format_def speex32_f = {
	.name = "ogg_speex32",
	.exts = "spx32",
	.open = ogg_speex_open,
	.write = ogg_speex_write,
	.seek = ogg_speex_seek,
	.trunc = ogg_speex_trunc,
	.tell = ogg_speex_tell,
	.read = ogg_speex_read,
	.close = ogg_speex_close,
	.buf_size = BUF_SIZE + AST_FRIENDLY_OFFSET,
	.desc_size = sizeof(struct speex_desc),
};

static int unload_module(void)
{
	int res = 0;
	res |= ast_format_def_unregister(speex_f.name);
	res |= ast_format_def_unregister(speex16_f.name);
	res |= ast_format_def_unregister(speex32_f.name);
	return res;
}

static int load_module(void)
{
	speex_f.format = ast_format_speex;
	speex16_f.format = ast_format_speex16;
	speex32_f.format = ast_format_speex32;

	if (ast_format_def_register(&speex_f) ||
	    ast_format_def_register(&speex16_f) ||
	    ast_format_def_register(&speex32_f)) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "OGG/Speex audio",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND
);
