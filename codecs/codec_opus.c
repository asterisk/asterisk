/*
 *  * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Lorenzo Miniero
 * Copyright (C) 2016, Frank Haase
 *
 * Lorenzo Miniero <lorenzo@meetecho.com>
 * Frank Haase <fra.haase@gmail.com>
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
 * \brief Translate between signed linear and Opus (Open Codec), with stereo and mono support
 *
 * \author Lorenzo Miniero <lorenzo@meetecho.com>, Frank Haase <fra.haase@gmail.com>
 *
 *
 * \ingroup codecs
 *
 * \extref The Opus library - http://opus-codec.org
 *
 */

/*** MODULEINFO
  <depend>opus</depend>
  <support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <opus/opus.h>
#include <asterisk/opus.h>
#include "asterisk/format.h"

#include "asterisk/translate.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/config.h"
#include "asterisk/utils.h"
#include "asterisk/linkedlists.h"

#define	BUFFER_SAMPLES	16000
#define USE_FEC		0

/* Sample frame data */
#include "asterisk/slin.h"
#include "ex_opus.h"

static struct codec_usage {
	int encoder_id;
	int decoder_id;
	int encoders;
	int decoders;
} usage;

/* Private structures */
struct opus_coder_pvt {
	void *opus;	
	unsigned int sample_rate; 
	unsigned int stereo;
	unsigned int multiplier;
	unsigned int fec;
	int id;
	int16_t buf[BUFFER_SAMPLES];	
	int16_t out_buf[OPUS_FRAME_SIZE * 2];
	unsigned int framesize;
};

static struct ast_frame *opus_sample(void)
{
	static struct ast_frame f = {
		.frametype = AST_FRAME_VOICE,
		.datalen = sizeof(ex_opus),
		.samples = OPUS_FRAME_SIZE, 
		.mallocd = 0,
		.offset = 0,
		.src = __PRETTY_FUNCTION__,
		.data.ptr = ex_opus,
	};

	f.subclass.format = ast_format_opus;

	return &f;
}

static int valid_sample_rate(int rate)
{
	return rate == 8000
		|| rate == 12000
		|| rate == 16000
		|| rate == 24000
		|| rate == 48000;
}

static void set_bandwidth_fec_options(void *opus, unsigned int sample_rate, unsigned int fec)  
{
	switch (sample_rate) {
		case 8000:
			opus_encoder_ctl(opus, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_NARROWBAND));
			break;
		case 12000:
			opus_encoder_ctl(opus, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_MEDIUMBAND));
			break;
		case 16000:
			opus_encoder_ctl(opus, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_WIDEBAND));
			break;
		case 24000:
			opus_encoder_ctl(opus, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_SUPERWIDEBAND));
			break;
		case 48000:
			opus_encoder_ctl(opus, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND));
			break;
	}
	opus_encoder_ctl(opus, OPUS_SET_INBAND_FEC(fec));
}

/* Translator callbacks */
static int lintoopus_new(struct ast_trans_pvt *pvt)
{
	struct opus_coder_pvt *opvt = pvt->pvt;
	int error;
	if (!valid_sample_rate(pvt->t->src_codec.sample_rate)) {
		ast_log(LOG_ERROR, "Invalid sampling rate. Valid sampling rates for opus are:\
				8000, 12000, 16000, 24000, 48000 hz.\n");
		return -1;
	}

	opvt->sample_rate = pvt->t->src_codec.sample_rate;
	opvt->multiplier = 48000 / pvt->t->src_codec.sample_rate; 
	opvt->fec = USE_FEC;

	/* We will set opus to use stereo by default. If this is non 
	 * stereo opus it will be changed at the first incoming frame.*/
	opvt->stereo = 1;
	error = 0;
	opvt->opus = opus_encoder_create(pvt->t->src_codec.sample_rate, 2, OPUS_APPLICATION_VOIP, &error);
	if (error != OPUS_OK) {
		ast_log(LOG_ERROR, "Error creating the Opus encoder: %s\n", opus_strerror(error));
		return -1;
	}

	opus_encoder_ctl(opvt->opus, OPUS_SET_FORCE_CHANNELS(2));
	set_bandwidth_fec_options(opvt->opus, pvt->t->src_codec.sample_rate, opvt->fec);
	opvt->framesize = pvt->t->src_codec.sample_rate / 50;
	opvt->id = ast_atomic_fetchadd_int(&usage.encoder_id, 1) + 1;
	ast_atomic_fetchadd_int(&usage.encoders, + 1);
	ast_debug(3, "Created encoder #%d (%d -> opus)\n", opvt->id, pvt->t->src_codec.sample_rate);
	return 0;
}

static int opustolin_new(struct ast_trans_pvt *pvt)
{
	struct opus_coder_pvt *opvt = pvt->pvt;
	int error;
	if (!valid_sample_rate(pvt->t->dst_codec.sample_rate)) {
		return -1;
	}

	opvt->sample_rate = pvt->t->dst_codec.sample_rate;
	opvt->multiplier = 48000 / pvt->t->dst_codec.sample_rate;
	opvt->fec = USE_FEC;
	opvt->stereo = 1;
	error = 0;
	opvt->opus = opus_decoder_create(pvt->t->dst_codec.sample_rate, 2, &error);
	if (error != OPUS_OK) {
		ast_log(LOG_ERROR, "Error creating the Opus decoder: %s\n", opus_strerror(error));
		return -1;
	}

	opvt->id = ast_atomic_fetchadd_int(&usage.decoder_id, 1) + 1;
	ast_atomic_fetchadd_int(&usage.decoders, +1);
	ast_debug(3, "Created decoder #%d (opus -> %d)\n", opvt->id, pvt->t->dst_codec.sample_rate);
	return 0;
}

static int lintoopus_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct opus_coder_pvt *opvt = pvt->pvt;
	struct ast_format *format = pvt->f.subclass.format;	
	struct opus_attr *attr = ast_format_get_attribute_data(format);
	int error;

	if (attr != NULL) {
		/* Check if we have to change form stereo to mono or vice versa. */
		if (attr->stereo != opvt->stereo) {
			if (attr->stereo == 0) {
				opus_encoder_destroy(opvt->opus);
				opvt->stereo = 0;
				error = 0;
				opvt->opus = opus_encoder_create(opvt->sample_rate, 1, OPUS_APPLICATION_VOIP, &error);
				ast_debug(3, "Changing Opus encoder from stereo to mono.\n");
			} else if (attr->stereo == 1) {
				opus_encoder_destroy(opvt->opus);
				opvt->stereo = 1;
				opvt->opus = opus_encoder_create(opvt->sample_rate, 2, OPUS_APPLICATION_VOIP, &error);
				opus_encoder_ctl(opvt->opus, OPUS_SET_FORCE_CHANNELS(2));
				ast_debug(3, "Changing Opus encoder from mono to stereo.\n");
			}
			set_bandwidth_fec_options(opvt->opus, opvt->sample_rate, opvt->fec);
		}
	} 
	memcpy(opvt->buf + pvt->samples, f->data.ptr, f->datalen);
	pvt->samples += f->samples;
	return 0;
}

static struct ast_frame *lintoopus_frameout(struct ast_trans_pvt *pvt)
{
	struct opus_coder_pvt *opvt = pvt->pvt;
	struct ast_frame *result = NULL;
	struct ast_frame *last = NULL;
	int out_samples = 0; 
	int k;
	int i;
	int interleaved;
	interleaved = pvt->interleaved_stereo ? 2 : 1; 

	while (pvt->samples >= opvt->framesize * interleaved) {
		int status = 0;
		if (opvt->stereo == 1 && pvt->interleaved_stereo == 0) { 
			/* No stereo samples, but stereo output (put the same audio on both channels). */
			opus_int16 stereobuf[opvt->framesize * 2];
			i = 0;
			k = 0;
			for (i = 0; i < opvt->framesize * 2; i+=2) { 
				stereobuf[i] = opvt->buf[out_samples + k];
				stereobuf[i + 1] = opvt->buf[out_samples + k];
				k++;
			}
			status = opus_encode(opvt->opus, stereobuf, opvt->framesize, pvt->outbuf.uc, BUFFER_SAMPLES);
		} else if ((opvt->stereo == 1 && pvt->interleaved_stereo == 1) || opvt->stereo == 0) { 
			/* Stereo source (interleaved format) and stereo output or everything mono. */
			status = opus_encode(opvt->opus, opvt->buf, opvt->framesize, pvt->outbuf.uc, BUFFER_SAMPLES);
		}
		out_samples += opvt->framesize * interleaved;
		pvt->samples -= opvt->framesize * interleaved;
		if (status < 0) {
			ast_log(LOG_ERROR, "Error encoding the Opus frame: %s\n", opus_strerror(status));
		} else {
			struct ast_frame *current;
			current = ast_trans_frameout(pvt, status, opvt->multiplier * opvt->framesize);
			if (!current) {
				continue;
			} else if (last) {
				AST_LIST_NEXT(last, frame_list) = current;
			} else {
				result = current;
			}
			last = current;
		}
	}
	/* Move the data at the end of the buffer to the front. */
	if (out_samples) {
		if (pvt->interleaved_stereo == 0)
			memmove(opvt->buf, opvt->buf + out_samples, pvt->samples * 2);
		else 
			memmove(opvt->buf, opvt->buf + out_samples, pvt->samples * 4);

	}
	return result;
}

static int opustolin_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct opus_coder_pvt *opvt = pvt->pvt;
	struct ast_format *format = f->subclass.format;	
	struct opus_attr *attr = ast_format_get_attribute_data(format);
	int error = 0;
	int samples = 0;
	int i;
	int k = 0;
	if (attr != NULL) {
		if (attr->stereo != opvt->stereo) {
			opus_decoder_destroy(opvt->opus);
			if (attr->stereo == 0) {
				opvt->stereo = 0;
				opvt->opus = opus_decoder_create(opvt->sample_rate, 1, &error);
				ast_debug(3, "Changing Opus decoder from stereo to mono.\n");
			} else  {
				opvt->stereo = 1;
				opvt->opus = opus_decoder_create(opvt->sample_rate, 2, &error);
				ast_debug(3, "Changing Opus decoder from mono to stereo.\n");
			}
			set_bandwidth_fec_options(opvt->opus, opvt->sample_rate, opvt->fec);
		}
	}
	if (opvt->stereo == 0) {
		if ((samples = opus_decode(opvt->opus, f->data.ptr, f->datalen, pvt->outbuf.i16, BUFFER_SAMPLES, opvt->fec)) < 0) {
			ast_log(LOG_ERROR, "Error decoding the Opus frame: %s\n", opus_strerror(samples));
			return -1;
		}
	} else {
		/* If we have incoming stereo signals we will only copy one channel to lin. */
		samples = opus_decode(opvt->opus, f->data.ptr, f->datalen, opvt->out_buf, BUFFER_SAMPLES, opvt->fec);
		if (samples < 0)
			ast_log(LOG_ERROR, "Error decoding the Opus stereo frame: %s\n", opus_strerror(samples));	
		for (i = 0; i < samples * 2; i += 2) {
			pvt->outbuf.i16[k] = opvt->out_buf[i];
			k++;
		}
	}
	pvt->samples += samples;
	pvt->datalen += samples * 2;
	return 0;
}

static void lintoopus_destroy(struct ast_trans_pvt *arg)
{
	struct opus_coder_pvt *opvt = arg->pvt;

	if (!opvt || !opvt->opus) {
		return;
	}

	opus_encoder_destroy(opvt->opus);
	opvt->opus = NULL;

	ast_atomic_fetchadd_int(&usage.encoders, -1);

	ast_debug(3, "Destroyed encoder #%d (%d->opus)\n", opvt->id, opvt->sample_rate);
}

static void opustolin_destroy(struct ast_trans_pvt *arg)
{
	struct opus_coder_pvt *opvt = arg->pvt;

	if (!opvt || !opvt->opus) {
		return;
	}

	opus_decoder_destroy(opvt->opus);
	opvt->opus = NULL;

	ast_atomic_fetchadd_int(&usage.decoders, -1);

	ast_debug(3, "Destroyed decoder #%d (opus->%d)\n", opvt->id, opvt->sample_rate);
}

static char *handle_cli_opus_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct codec_usage copy;

	switch (cmd) {
		case CLI_INIT:
			e->command = "opus show";
			e->usage =
				"Usage: opus show\n"
				"       Displays Opus encoder/decoder utilization.\n";
			return NULL;
		case CLI_GENERATE:
			return NULL;
	}

	if (a->argc != 2) {
		return CLI_SHOWUSAGE;
	}

	copy = usage;

	ast_cli(a->fd, "%d/%d encoders/decoders are in use.\n", copy.encoders, copy.decoders);

	return CLI_SUCCESS;
}

/* Translators */
static struct ast_translator opustolin = {
	.name = "opustolin",
	.src_codec = {
		.name = "opus",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 48000,
	},
	.dst_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.format = "slin",
	.newpvt = opustolin_new,
	.framein = opustolin_framein,
	.destroy = opustolin_destroy,
	.sample = opus_sample,
	.desc_size = sizeof(struct opus_coder_pvt),
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_translator lintoopus = {
	.name = "lintoopus",
	.src_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	},
	.dst_codec = {
		.name = "opus",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 48000,
	},
	.format = "opus",
	.newpvt = lintoopus_new,
	.framein = lintoopus_framein,
	.frameout = lintoopus_frameout,
	.destroy = lintoopus_destroy,
	.sample = slin8_sample,
	.desc_size = sizeof(struct opus_coder_pvt),
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_translator opustolin12 = {
	.name = "opustolin12",
	.src_codec = {
		.name = "opus",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 48000,
	},
	.dst_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 12000,
	},
	.format = "slin12",
	.newpvt = opustolin_new,
	.framein = opustolin_framein,
	.destroy = opustolin_destroy,
	.sample = opus_sample,
	.desc_size = sizeof(struct opus_coder_pvt),
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_translator lin12toopus = {
	.name = "lin12toopus",
	.src_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 12000,
	},
	.dst_codec = {
		.name = "opus",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 48000,
	},
	.format = "opus",
	.newpvt = lintoopus_new,
	.framein = lintoopus_framein,
	.frameout = lintoopus_frameout,
	.destroy = lintoopus_destroy,
	.desc_size = sizeof(struct opus_coder_pvt),
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_translator opustolin16 = {
	.name = "opustolin16",
	.src_codec = {
		.name = "opus",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 48000,
	},
	.dst_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 16000,
	},
	.format = "slin16",
	.newpvt = opustolin_new,
	.framein = opustolin_framein,
	.destroy = opustolin_destroy,
	.sample = opus_sample,
	.desc_size = sizeof(struct opus_coder_pvt),
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_translator lin16toopus = {
	.name = "lin16toopus",
	.src_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 16000,
	},
	.dst_codec = {
		.name = "opus",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 48000,
	},
	.format = "opus",
	.newpvt = lintoopus_new,
	.framein = lintoopus_framein,
	.frameout = lintoopus_frameout,
	.destroy = lintoopus_destroy,
	.sample = slin16_sample,
	.desc_size = sizeof(struct opus_coder_pvt),
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_translator opustolin24 = {
	.name = "opustolin24",
	.src_codec = {
		.name = "opus",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 48000,
	},
	.dst_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 24000,
	},
	.format = "slin24",
	.newpvt = opustolin_new,
	.framein = opustolin_framein,
	.destroy = opustolin_destroy,
	.sample = opus_sample,
	.desc_size = sizeof(struct opus_coder_pvt),
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_translator lin24toopus = {
	.name = "lin24toopus",
	.src_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 24000,
	},
	.dst_codec = {
		.name = "opus",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 48000,
	},
	.format = "opus",
	.newpvt = lintoopus_new,
	.framein = lintoopus_framein,
	.frameout = lintoopus_frameout,
	.destroy = lintoopus_destroy,
	.desc_size = sizeof(struct opus_coder_pvt),
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_translator opustolin48 = {
	.name = "opustolin48",
	.src_codec = {
		.name = "opus",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 48000,
	},
	.dst_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 48000,
	},
	.format = "slin48",
	.newpvt = opustolin_new,
	.framein = opustolin_framein,
	.destroy = opustolin_destroy,
	.sample = opus_sample,
	.desc_size = sizeof(struct opus_coder_pvt),
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_translator lin48toopus = {
	.name = "lin48toopus",
	.src_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 48000,
	},
	.dst_codec = {
		.name = "opus",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 48000,
	},
	.format = "opus",
	.newpvt = lintoopus_new,
	.framein = lintoopus_framein,
	.frameout = lintoopus_frameout,
	.destroy = lintoopus_destroy,
	.desc_size = sizeof(struct opus_coder_pvt),
	.buffer_samples = BUFFER_SAMPLES,
	.buf_size = BUFFER_SAMPLES * 2,
};

static struct ast_cli_entry cli[] = {
	AST_CLI_DEFINE(handle_cli_opus_show, "Display Opus codec utilization.")
};

static int reload(void)
{
	/* Reload does nothing */
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_translator(&opustolin);
	res |= ast_unregister_translator(&lintoopus);
	res |= ast_unregister_translator(&opustolin12);
	res |= ast_unregister_translator(&lin12toopus);
	res |= ast_unregister_translator(&opustolin16);
	res |= ast_unregister_translator(&lin16toopus);
	res |= ast_unregister_translator(&opustolin24);
	res |= ast_unregister_translator(&lin24toopus);
	res |= ast_unregister_translator(&opustolin48);
	res |= ast_unregister_translator(&lin48toopus);

	ast_cli_unregister_multiple(cli, ARRAY_LEN(cli));

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_register_translator(&opustolin);
	res |= ast_register_translator(&lintoopus);
	res |= ast_register_translator(&opustolin12);
	res |= ast_register_translator(&lin12toopus);
	res |= ast_register_translator(&opustolin16);
	res |= ast_register_translator(&lin16toopus);
	res |= ast_register_translator(&opustolin24);
	res |= ast_register_translator(&lin24toopus);
	res |= ast_register_translator(&opustolin48);
	res |= ast_register_translator(&lin48toopus);

	ast_cli_register_multiple(cli, ARRAY_LEN(cli));

	return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Opus Coder/Decoder",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
		);

