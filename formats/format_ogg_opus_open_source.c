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
	<use type="external">opusenc</use>
	<conflict>format_ogg_opus</conflict>
	<defaultenabled>yes</defaultenabled>
 ***/

#include "asterisk.h"

#if defined(ASTERISK_REGISTER_FILE)
ASTERISK_REGISTER_FILE()
#elif defined(ASTERISK_FILE_VERSION)
ASTERISK_FILE_VERSION(__FILE__, "$Revision: $")
#endif

#include <fcntl.h>                  /* for SEEK_CUR, SEEK_END, SEEK_SET */

#include "asterisk/format_cache.h"  /* for ast_format_slin48 */
#include "asterisk/frame.h"         /* for ast_frame, AST_FRIENDLY_OFFSET */
#include "asterisk/logger.h"        /* for ast_log, LOG_ERROR, AST_LOG_ERROR */
#include "asterisk/mod_format.h"    /* for ast_filestream, ast_format_def */
#include "asterisk/module.h"
#if defined(HAVE_OPUSENC)
#include "asterisk/config.h"        /* for ast_variable, ast_config_destroy */
#include "asterisk/format.h"        /* for ast_format_get_... */
#include "asterisk/utils.h"         /* for ast_flags */
#endif

#include <opus/opus.h>
#include <opus/opusfile.h>
#if defined(HAVE_OPUSENC)
#include <opus/opusenc.h>
#endif

#include "asterisk/opus.h"

/* 120ms of 48KHz audio */
#define SAMPLES_MAX 5760
#define BUF_SIZE (2 * SAMPLES_MAX)

#if defined(HAVE_OPUSENC)
/* Variables that can be set in formats.conf */
static int complexity = 10; /* OPUS default */
static int maxbitrate = CODEC_OPUS_DEFAULT_BITRATE;
#endif

struct ogg_opus_desc {
	OggOpusFile *of;

#if defined(HAVE_OPUSENC)
	OggOpusEnc *enc;
	OggOpusComments *comments;
#endif

	size_t writing;
	off_t writing_pcm_pos;
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

#if defined(HAVE_OPUSENC)
static int fwrite_wrapper(void *user_data, const unsigned char *ptr, opus_int32 len)
{
	FILE *stream = user_data;

	return fwrite(ptr, 1, len, stream) != (size_t) len;
}

static int fclose_wrapper(void *user_data)
{
	return 0;
}

static const OpusEncCallbacks enc_callbacks = {
	.write = fwrite_wrapper,
	.close = fclose_wrapper,
};

static int ogg_opus_rewrite(struct ast_filestream *fs, const char *comment)
{
	struct ogg_opus_desc *desc = fs->_private;
	int err, rate, channels, family;

	desc->writing = 1;
	desc->writing_pcm_pos = 0;

	desc->comments = ope_comments_create();
	ope_comments_add(desc->comments, "ENCODER", "Asterisk PBX");
	if (comment)
		ope_comments_add(desc->comments, "COMMENT", comment);

	rate = ast_format_get_sample_rate(fs->fmt->format);
#if defined(ASTERISK_VERSION_NUM) && (ASTERISK_VERSION_NUM < 150000)
	channels = 1;
#else
	channels = ast_format_get_channel_count(fs->fmt->format);
#endif
	if (channels < 3) {
		family = 0;
	} else {
		family = 1;
	}

	desc->enc = ope_encoder_create_callbacks(&enc_callbacks, fs->f, desc->comments, rate, channels, family, &err);

	if (!desc->enc) {
		ast_log(AST_LOG_ERROR, "Error creating the OGG/Opus encoder: %s\n", ope_strerror(err));
		return -1;
	}

	ope_encoder_ctl(desc->enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
	ope_encoder_ctl(desc->enc, OPUS_SET_COMPLEXITY(complexity));
	ope_encoder_ctl(desc->enc, OPUS_SET_BITRATE(maxbitrate));

	return 0;
}

static int ogg_opus_write(struct ast_filestream *fs, struct ast_frame *f)
{
	struct ogg_opus_desc *desc = fs->_private;
	int err;

	if (!desc->writing) {
		ast_log(LOG_ERROR, "This OGG/Opus stream is not set up for writing!\n");
		return -1;
	}

	if (!f->datalen) {
		return -1;
	}

	err = ope_encoder_write(desc->enc, f->data.ptr, f->samples);
	if (err) {
		ast_log(AST_LOG_ERROR, "Error encoding OGG/Opus frame: %s\n", ope_strerror(err));
		return -1;
	}

	desc->writing_pcm_pos += f->samples;
	return 0;
}
#else
static int ogg_opus_rewrite(struct ast_filestream *fs, const char *comment)
{
	ast_log(LOG_ERROR, "Writing OGG/Opus streams is not built-in\n");
	return -1;
}

static int ogg_opus_write(struct ast_filestream *fs, struct ast_frame *f)
{
	ast_log(LOG_ERROR, "Writing OGG/Opus streams is not built-in\n");
	return -1;
}
#endif

static int ogg_opus_seek(struct ast_filestream *fs, off_t sample_offset, int whence)
{
	int seek_result = -1;
	off_t relative_pcm_pos;
	struct ogg_opus_desc *desc = fs->_private;

	if (desc->writing) {
		return -1;
	}

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
	return -1;
}

static off_t ogg_opus_tell(struct ast_filestream *fs)
{
	struct ogg_opus_desc *desc = fs->_private;
	off_t pos;

	if (desc->writing) {
		return desc->writing_pcm_pos / CODEC_OPUS_DEFAULT_SAMPLE_RATE * DEFAULT_SAMPLE_RATE;
	}

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

	if (desc->writing) {
		ast_log(LOG_WARNING, "Reading is not supported on OGG/Opus in writing mode.\n");
		return NULL;
	}

	AST_FRAME_SET_BUFFER(&fs->fr, fs->buf, AST_FRIENDLY_OFFSET, BUF_SIZE)

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

	if (desc->writing) {
#if defined(HAVE_OPUSENC)
		ope_encoder_drain(desc->enc);
		ope_encoder_destroy(desc->enc);
		ope_comments_destroy(desc->comments);
#endif
	} else {
		op_free(desc->of);
	}
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

static int parse_config(int reload)
{
#if defined(HAVE_OPUSENC)
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	struct ast_config *cfg = ast_config_load("formats.conf", config_flags);
	struct ast_variable *var;
	int i, res = 0;

	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEUNCHANGED || cfg == CONFIG_STATUS_FILEINVALID) {
		return res;
	}

	for (var = ast_variable_browse(cfg, "opus"); var; var = var->next) {
		if (!strcasecmp(var->name, "complexity")) {
			i = atoi(var->value);
			if (i < 0 || i > 10) {
				res = 1;
				ast_log(LOG_ERROR, "Complexity must be in 0-10\n");
				break;
			}

			complexity = i;
		} else if (!strcasecmp(var->name, CODEC_OPUS_ATTR_MAX_AVERAGE_BITRATE)) {
			i = atoi(var->value);
			if (i < 500 || i > 512000) {
				res = 1;
				ast_log(LOG_ERROR, CODEC_OPUS_ATTR_MAX_AVERAGE_BITRATE " must be in 500-512000\n");
				break;
			}

			maxbitrate = i;
		}
	}
	ast_config_destroy(cfg);

	return res;
#else
	return 0;
#endif
}

static int load_module(void)
{
	if (parse_config(0)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	opus_f.format = ast_format_slin48;
	if (ast_format_def_register(&opus_f)) {
		return AST_MODULE_LOAD_FAILURE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload_module(void)
{
	if (parse_config(1)) {
		return AST_MODULE_LOAD_DECLINE;
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
	.reload = reload_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND
);
