/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2016, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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

/*** MODULEINFO
	<depend>opusfile</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <opus/opus.h>
#include <opus/opusfile.h>
#include "asterisk/mod_format.h"
#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/format_cache.h"

/* 120ms of 48KHz audio */
#define SAMPLES_MAX 5760
#define BUF_SIZE (2 * SAMPLES_MAX)

struct ogg_opus_desc {
	OggOpusFile *of;
};

static int fread_wrapper(void *_stream, unsigned char *_ptr, int _nbytes)
{
	FILE *stream = _stream;
	size_t bytes_read;

	if (!stream || _nbytes < 0) {
		return -1;
	}

	bytes_read = fread(_ptr, 1, _nbytes, stream);

	return bytes_read > 0 || feof(stream) ? (int) bytes_read : OP_EREAD;
}

static int fseek_wrapper(void *_stream, opus_int64 _offset, int _whence)
{
	FILE *stream = _stream;

	return fseeko(stream, (off_t) _offset, _whence);
}

static opus_int64 ftell_wrapper(void *_stream)
{
	FILE *stream = _stream;

	return ftello(stream);
}

static int ogg_opus_open(struct ast_filestream *s)
{
	struct ogg_opus_desc *desc = (struct ogg_opus_desc *) s->_private;
	OpusFileCallbacks cb = {
		.read = fread_wrapper,
		.seek = fseek_wrapper,
		.tell = ftell_wrapper,
		.close = NULL,
	};

	memset(desc, 0, sizeof(*desc));
	desc->of = op_open_callbacks(s->f, &cb, NULL, 0, NULL);
	if (!desc->of) {
		return -1;
	}

	return 0;
}

static int ogg_opus_rewrite(struct ast_filestream *s, const char *comment)
{
	/* XXX Unimplemented. We currently only can read from OGG/Opus streams */
	ast_log(LOG_ERROR, "Cannot write OGG/Opus streams. Sorry :(\n");
	return -1;
}

static int ogg_opus_write(struct ast_filestream *fs, struct ast_frame *f)
{
	/* XXX Unimplemented. We currently only can read from OGG/Opus streams */
	ast_log(LOG_ERROR, "Cannot write OGG/Opus streams. Sorry :(\n");
	return -1;
}

static int ogg_opus_seek(struct ast_filestream *fs, off_t sample_offset, int whence)
{
	int seek_result = -1;
	off_t relative_pcm_pos;
	struct ogg_opus_desc *desc = fs->_private;

	switch (whence) {
	case SEEK_SET:
		seek_result = op_pcm_seek(desc->of, sample_offset);
		break;
	case SEEK_CUR:
		if ((relative_pcm_pos = op_pcm_tell(desc->of)) < 0) {
			seek_result = -1;
			break;
		}
		seek_result = op_pcm_seek(desc->of, relative_pcm_pos + sample_offset);
		break;
	case SEEK_END:
		if ((relative_pcm_pos = op_pcm_total(desc->of, -1)) < 0) {
			seek_result = -1;
			break;
		}
		seek_result = op_pcm_seek(desc->of, relative_pcm_pos - sample_offset);
		break;
	default:
		ast_log(LOG_WARNING, "Unknown *whence* to seek on OGG/Opus streams!\n");
		break;
	}

	/* normalize error value to -1,0 */
	return (seek_result == 0) ? 0 : -1;
}

static int ogg_opus_trunc(struct ast_filestream *fs)
{
	/* XXX Unimplemented. This is only used when recording, and we don't support that right now. */
	ast_log(LOG_ERROR, "Truncation is not supported on OGG/Opus streams!\n");
	return -1;
}

static off_t ogg_opus_tell(struct ast_filestream *fs)
{
	struct ogg_opus_desc *desc = fs->_private;
	off_t pos;

	pos = (off_t) op_pcm_tell(desc->of);
	if (pos < 0) {
		return -1;
	}
	return pos;
}

static struct ast_frame *ogg_opus_read(struct ast_filestream *fs, int *whennext)
{
	struct ogg_opus_desc *desc = fs->_private;
	int hole = 1;
	int samples_read;
	opus_int16 *out_buf;

	AST_FRAME_SET_BUFFER(&fs->fr, fs->buf, AST_FRIENDLY_OFFSET, BUF_SIZE);

	out_buf = (opus_int16 *) fs->fr.data.ptr;

	while (hole) {
		samples_read = op_read(
			desc->of,
			out_buf,
			SAMPLES_MAX,
			NULL);

		if (samples_read != OP_HOLE) {
			hole = 0;
		}
	}

	if (samples_read <= 0) {
		return NULL;
	}

	fs->fr.datalen = samples_read * 2;
	fs->fr.samples = samples_read;
	*whennext = fs->fr.samples;

	return &fs->fr;
}

static void ogg_opus_close(struct ast_filestream *fs)
{
	struct ogg_opus_desc *desc = fs->_private;

	op_free(desc->of);
}

static struct ast_format_def opus_f = {
	.name = "ogg_opus",
	.exts = "opus",
	.open = ogg_opus_open,
	.rewrite = ogg_opus_rewrite,
	.write = ogg_opus_write,
	.seek = ogg_opus_seek,
	.trunc = ogg_opus_trunc,
	.tell = ogg_opus_tell,
	.read = ogg_opus_read,
	.close = ogg_opus_close,
	.buf_size = BUF_SIZE + AST_FRIENDLY_OFFSET,
	.desc_size = sizeof(struct ogg_opus_desc),
};

static int load_module(void)
{
	opus_f.format = ast_format_slin48;
	if (ast_format_def_register(&opus_f)) {
		return AST_MODULE_LOAD_FAILURE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return ast_format_def_unregister(opus_f.name);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "OGG/Opus audio",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND
);
