/*
 * Asterisk -- An open source telephony toolkit.
 *
 * DAHDI native transcoding support
 *
 * Copyright (C) 1999 - 2008, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * Kevin P. Fleming <kpfleming@digium.com>
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
 * \brief Translate between various formats natively through DAHDI transcoding
 *
 * \ingroup codecs
 */

/*** MODULEINFO
	<support_level>core</support_level>
	<depend>dahdi</depend>
 ***/

#include "asterisk.h"
#include <stdbool.h>

ASTERISK_REGISTER_FILE()

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <dahdi/user.h>

#include "asterisk/lock.h"
#include "asterisk/translate.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/channel.h"
#include "asterisk/utils.h"
#include "asterisk/linkedlists.h"
#include "asterisk/ulaw.h"
#include "asterisk/format_compatibility.h"

#define BUFFER_SIZE 8000

#define G723_SAMPLES 240
#define G729_SAMPLES 160
#define ULAW_SAMPLES 160

/* Defines from DAHDI. */
#ifndef DAHDI_FORMAT_MAX_AUDIO
/*! G.723.1 compression */
#define DAHDI_FORMAT_G723_1    (1 << 0)
/*! GSM compression */
#define DAHDI_FORMAT_GSM       (1 << 1)
/*! Raw mu-law data (G.711) */
#define DAHDI_FORMAT_ULAW      (1 << 2)
/*! Raw A-law data (G.711) */
#define DAHDI_FORMAT_ALAW      (1 << 3)
/*! ADPCM (G.726, 32kbps) */
#define DAHDI_FORMAT_G726      (1 << 4)
/*! ADPCM (IMA) */
#define DAHDI_FORMAT_ADPCM     (1 << 5)
/*! Raw 16-bit Signed Linear (8000 Hz) PCM */
#define DAHDI_FORMAT_SLINEAR   (1 << 6)
/*! LPC10, 180 samples/frame */
#define DAHDI_FORMAT_LPC10     (1 << 7)
/*! G.729A audio */
#define DAHDI_FORMAT_G729A     (1 << 8)
/*! SpeeX Free Compression */
#define DAHDI_FORMAT_SPEEX     (1 << 9)
/*! iLBC Free Compression */
#define DAHDI_FORMAT_ILBC      (1 << 10)
#endif

static struct channel_usage {
	int total;
	int encoders;
	int decoders;
} channels;

#if defined(NOT_NEEDED)
/*!
 * \internal
 * \brief Convert DAHDI format bitfield to old Asterisk format bitfield.
 * \since 13.0.0
 *
 * \param dahdi Bitfield from DAHDI to convert.
 *
 * \note They should be the same values but they don't have to be.
 *
 * \return Old Asterisk bitfield equivalent.
 */
static uint64_t bitfield_dahdi2ast(unsigned dahdi)
{
	uint64_t ast;

	switch (dahdi) {
	case DAHDI_FORMAT_G723_1:
		ast = AST_FORMAT_G723;
		break;
	case DAHDI_FORMAT_GSM:
		ast = AST_FORMAT_GSM;
		break;
	case DAHDI_FORMAT_ULAW:
		ast = AST_FORMAT_ULAW;
		break;
	case DAHDI_FORMAT_ALAW:
		ast = AST_FORMAT_ALAW;
		break;
	case DAHDI_FORMAT_G726:
		ast = AST_FORMAT_G726_AAL2;
		break;
	case DAHDI_FORMAT_ADPCM:
		ast = AST_FORMAT_ADPCM;
		break;
	case DAHDI_FORMAT_SLINEAR:
		ast = AST_FORMAT_SLIN;
		break;
	case DAHDI_FORMAT_LPC10:
		ast = AST_FORMAT_LPC10;
		break;
	case DAHDI_FORMAT_G729A:
		ast = AST_FORMAT_G729;
		break;
	case DAHDI_FORMAT_SPEEX:
		ast = AST_FORMAT_SPEEX;
		break;
	case DAHDI_FORMAT_ILBC:
		ast = AST_FORMAT_ILBC;
		break;
	default:
		ast = 0;
		break;
	}

	return ast;
}
#endif	/* defined(NOT_NEEDED) */

/*!
 * \internal
 * \brief Get the ast_codec by DAHDI format.
 * \since 13.0.0
 *
 * \param dahdi_fmt DAHDI specific codec identifier.
 *
 * \return Specified codec if exists otherwise NULL.
 */
static const struct ast_codec *get_dahdi_codec(uint32_t dahdi_fmt)
{
	const struct ast_codec *codec;

	static const struct ast_codec dahdi_g723_1 = {
		.name = "g723",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	};
	static const struct ast_codec dahdi_gsm = {
		.name = "gsm",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	};
	static const struct ast_codec dahdi_ulaw = {
		.name = "ulaw",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	};
	static const struct ast_codec dahdi_alaw = {
		.name = "alaw",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	};
	static const struct ast_codec dahdi_g726 = {
		.name = "g726",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	};
	static const struct ast_codec dahdi_adpcm = {
		.name = "adpcm",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	};
	static const struct ast_codec dahdi_slinear = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	};
	static const struct ast_codec dahdi_lpc10 = {
		.name = "lpc10",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	};
	static const struct ast_codec dahdi_g729a = {
		.name = "g729",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	};
	static const struct ast_codec dahdi_speex = {
		.name = "speex",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	};
	static const struct ast_codec dahdi_ilbc = {
		.name = "ilbc",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = 8000,
	};

	switch (dahdi_fmt) {
	case DAHDI_FORMAT_G723_1:
		codec = &dahdi_g723_1;
		break;
	case DAHDI_FORMAT_GSM:
		codec = &dahdi_gsm;
		break;
	case DAHDI_FORMAT_ULAW:
		codec = &dahdi_ulaw;
		break;
	case DAHDI_FORMAT_ALAW:
		codec = &dahdi_alaw;
		break;
	case DAHDI_FORMAT_G726:
		codec = &dahdi_g726;
		break;
	case DAHDI_FORMAT_ADPCM:
		codec = &dahdi_adpcm;
		break;
	case DAHDI_FORMAT_SLINEAR:
		codec = &dahdi_slinear;
		break;
	case DAHDI_FORMAT_LPC10:
		codec = &dahdi_lpc10;
		break;
	case DAHDI_FORMAT_G729A:
		codec = &dahdi_g729a;
		break;
	case DAHDI_FORMAT_SPEEX:
		codec = &dahdi_speex;
		break;
	case DAHDI_FORMAT_ILBC:
		codec = &dahdi_ilbc;
		break;
	default:
		codec = NULL;
		break;
	}

	return codec;
}

static char *handle_cli_transcoder_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);

static struct ast_cli_entry cli[] = {
	AST_CLI_DEFINE(handle_cli_transcoder_show, "Display DAHDI transcoder utilization.")
};

struct translator {
	struct ast_translator t;
	uint32_t src_dahdi_fmt;
	uint32_t dst_dahdi_fmt;
	AST_LIST_ENTRY(translator) entry;
};

#ifndef container_of
#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))
#endif

static AST_LIST_HEAD_STATIC(translators, translator);

struct codec_dahdi_pvt {
	int fd;
	struct dahdi_transcoder_formats fmts;
	unsigned int softslin:1;
	unsigned int fake:2;
	uint16_t required_samples;
	uint16_t samples_in_buffer;
	uint16_t samples_written_to_hardware;
	uint8_t ulaw_buffer[1024];
};

/* Only used by a decoder */
static int ulawtolin(struct ast_trans_pvt *pvt, int samples)
{
	struct codec_dahdi_pvt *dahdip = pvt->pvt;
	int i = samples;
	uint8_t *src = &dahdip->ulaw_buffer[0];
	int16_t *dst = pvt->outbuf.i16 + pvt->datalen;

	/* convert and copy in outbuf */
	while (i--) {
		*dst++ = AST_MULAW(*src++);
	}

	return 0;
}

/* Only used by an encoder. */
static int lintoulaw(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct codec_dahdi_pvt *dahdip = pvt->pvt;
	int i = f->samples;
	uint8_t *dst = &dahdip->ulaw_buffer[dahdip->samples_in_buffer];
	int16_t *src = f->data.ptr;

	if (dahdip->samples_in_buffer + i > sizeof(dahdip->ulaw_buffer)) {
		ast_log(LOG_ERROR, "Out of buffer space!\n");
		return -i;
	}

	while (i--) {
		*dst++ = AST_LIN2MU(*src++);
	}

	dahdip->samples_in_buffer += f->samples;
	return 0;
}

static char *handle_cli_transcoder_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct channel_usage copy;

	switch (cmd) {
	case CLI_INIT:
		e->command = "transcoder show";
		e->usage =
			"Usage: transcoder show\n"
			"       Displays channel utilization of DAHDI transcoder(s).\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 2)
		return CLI_SHOWUSAGE;

	copy = channels;

	if (copy.total == 0)
		ast_cli(a->fd, "No DAHDI transcoders found.\n");
	else
		ast_cli(a->fd, "%d/%d encoders/decoders of %d channels are in use.\n", copy.encoders, copy.decoders, copy.total);

	return CLI_SUCCESS;
}

static void dahdi_write_frame(struct codec_dahdi_pvt *dahdip, const uint8_t *buffer, const ssize_t count)
{
	int res;
	if (!count) return;
	res = write(dahdip->fd, buffer, count);
	if (-1 == res) {
		ast_log(LOG_ERROR, "Failed to write to transcoder: %s\n", strerror(errno));
	}
	if (count != res) {
		ast_log(LOG_ERROR, "Requested write of %zd bytes, but only wrote %d bytes.\n", count, res);
	}
}

static int dahdi_encoder_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct codec_dahdi_pvt *dahdip = pvt->pvt;

	if (!f->subclass.format) {
		/* We're just faking a return for calculation purposes. */
		dahdip->fake = 2;
		pvt->samples = f->samples;
		return 0;
	}

	/* Buffer up the packets and send them to the hardware if we
	 * have enough samples set up. */
	if (dahdip->softslin) {
		if (lintoulaw(pvt, f)) {
			 return -1;
		}
	} else {
		/* NOTE:  If softslin support is not needed, and the sample
		 * size is equal to the required sample size, we wouldn't
		 * need this copy operation.  But at the time this was
		 * written, only softslin is supported. */
		if (dahdip->samples_in_buffer + f->samples > sizeof(dahdip->ulaw_buffer)) {
			ast_log(LOG_ERROR, "Out of buffer space.\n");
			return -1;
		}
		memcpy(&dahdip->ulaw_buffer[dahdip->samples_in_buffer], f->data.ptr, f->samples);
		dahdip->samples_in_buffer += f->samples;
	}

	while (dahdip->samples_in_buffer >= dahdip->required_samples) {
		dahdi_write_frame(dahdip, dahdip->ulaw_buffer, dahdip->required_samples);
		dahdip->samples_written_to_hardware += dahdip->required_samples;
		dahdip->samples_in_buffer -= dahdip->required_samples;
		if (dahdip->samples_in_buffer) {
			/* Shift any remaining bytes down. */
			memmove(dahdip->ulaw_buffer, &dahdip->ulaw_buffer[dahdip->required_samples],
				dahdip->samples_in_buffer);
		}
	}
	pvt->samples += f->samples;
	pvt->datalen = 0;
	return -1;
}

static void dahdi_wait_for_packet(int fd)
{
	struct pollfd p = {0};
	p.fd = fd;
	p.events = POLLIN;
	poll(&p, 1, 10);
}

static struct ast_frame *dahdi_encoder_frameout(struct ast_trans_pvt *pvt)
{
	struct codec_dahdi_pvt *dahdip = pvt->pvt;
	int res;

	if (2 == dahdip->fake) {
		struct ast_frame frm = {
			.frametype = AST_FRAME_VOICE,
			.samples = dahdip->required_samples,
			.src = pvt->t->name,
		};

		dahdip->fake = 1;
		pvt->samples = 0;

		return ast_frisolate(&frm);
	} else if (1 == dahdip->fake) {
		dahdip->fake = 0;
		return NULL;
	}

	if (dahdip->samples_written_to_hardware >= dahdip->required_samples) {
		dahdi_wait_for_packet(dahdip->fd);
	}

	res = read(dahdip->fd, pvt->outbuf.c + pvt->datalen, pvt->t->buf_size - pvt->datalen);
	if (-1 == res) {
		if (EWOULDBLOCK == errno) {
			/* Nothing waiting... */
			return NULL;
		} else {
			ast_log(LOG_ERROR, "Failed to read from transcoder: %s\n", strerror(errno));
			return NULL;
		}
	} else {
		pvt->f.datalen = res;
		pvt->f.samples = ast_codec_samples_count(&pvt->f);

		dahdip->samples_written_to_hardware =
		  (dahdip->samples_written_to_hardware >= pvt->f.samples) ?
		     dahdip->samples_written_to_hardware - pvt->f.samples : 0;

		pvt->samples = 0;
		pvt->datalen = 0;
		return ast_frisolate(&pvt->f);
	}

	/* Shouldn't get here... */
	return NULL;
}

static int dahdi_decoder_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct codec_dahdi_pvt *dahdip = pvt->pvt;

	if (!f->subclass.format) {
		/* We're just faking a return for calculation purposes. */
		dahdip->fake = 2;
		pvt->samples = f->samples;
		return 0;
	}

	if (!f->datalen) {
		if (f->samples != dahdip->required_samples) {
			ast_log(LOG_ERROR, "%d != %d %d\n", f->samples, dahdip->required_samples, f->datalen);
		}
	}
	dahdi_write_frame(dahdip, f->data.ptr, f->datalen);
	dahdip->samples_written_to_hardware += f->samples;
	pvt->samples += f->samples;
	pvt->datalen = 0;
	return -1;
}

static struct ast_frame *dahdi_decoder_frameout(struct ast_trans_pvt *pvt)
{
	int res;
	struct codec_dahdi_pvt *dahdip = pvt->pvt;

	if (2 == dahdip->fake) {
		struct ast_frame frm = {
			.frametype = AST_FRAME_VOICE,
			.samples = dahdip->required_samples,
			.src = pvt->t->name,
		};

		dahdip->fake = 1;
		pvt->samples = 0;

		return ast_frisolate(&frm);
	} else if (1 == dahdip->fake) {
		pvt->samples = 0;
		dahdip->fake = 0;
		return NULL;
	}

	if (dahdip->samples_written_to_hardware >= ULAW_SAMPLES) {
		dahdi_wait_for_packet(dahdip->fd);
	}

	/* Let's check to see if there is a new frame for us.... */
	if (dahdip->softslin) {
		res = read(dahdip->fd, dahdip->ulaw_buffer, sizeof(dahdip->ulaw_buffer));
	} else {
		res = read(dahdip->fd, pvt->outbuf.c + pvt->datalen, pvt->t->buf_size - pvt->datalen);
	}

	if (-1 == res) {
		if (EWOULDBLOCK == errno) {
			/* Nothing waiting... */
			return NULL;
		} else {
			ast_log(LOG_ERROR, "Failed to read from transcoder: %s\n", strerror(errno));
			return NULL;
		}
	} else {
		if (dahdip->softslin) {
			ulawtolin(pvt, res);
			pvt->f.datalen = res * 2;
		} else {
			pvt->f.datalen = res;
		}
		pvt->datalen = 0;
		pvt->f.samples = res;
		pvt->samples = 0;
		dahdip->samples_written_to_hardware =
			(dahdip->samples_written_to_hardware >= res) ?
			        dahdip->samples_written_to_hardware - res : 0;

		return ast_frisolate(&pvt->f);
	}

	/* Shouldn't get here... */
	return NULL;
}


static void dahdi_destroy(struct ast_trans_pvt *pvt)
{
	struct codec_dahdi_pvt *dahdip = pvt->pvt;

	switch (dahdip->fmts.dstfmt) {
	case DAHDI_FORMAT_G729A:
	case DAHDI_FORMAT_G723_1:
		ast_atomic_fetchadd_int(&channels.encoders, -1);
		break;
	default:
		ast_atomic_fetchadd_int(&channels.decoders, -1);
		break;
	}

	close(dahdip->fd);
}

static struct ast_format *dahdi_format_to_cached(int format)
{
	switch (format) {
	case DAHDI_FORMAT_G723_1:
		return ast_format_g723;
	case DAHDI_FORMAT_GSM:
		return ast_format_gsm;
	case DAHDI_FORMAT_ULAW:
		return ast_format_ulaw;
	case DAHDI_FORMAT_ALAW:
		return ast_format_alaw;
	case DAHDI_FORMAT_G726:
		return ast_format_g726;
	case DAHDI_FORMAT_ADPCM:
		return ast_format_adpcm;
	case DAHDI_FORMAT_SLINEAR:
		return ast_format_slin;
	case DAHDI_FORMAT_LPC10:
		return ast_format_lpc10;
	case DAHDI_FORMAT_G729A:
		return ast_format_g729;
	case DAHDI_FORMAT_SPEEX:
		return ast_format_speex;
	case DAHDI_FORMAT_ILBC:
		return ast_format_ilbc;
	}

	/* This will never be reached */
	ast_assert(0);
	return NULL;
}

static int dahdi_translate(struct ast_trans_pvt *pvt, uint32_t dst_dahdi_fmt, uint32_t src_dahdi_fmt)
{
	/* Request translation through zap if possible */
	int fd;
	struct codec_dahdi_pvt *dahdip = pvt->pvt;
	int flags;
	int tried_once = 0;
	const char *dev_filename = "/dev/dahdi/transcode";

	if ((fd = open(dev_filename, O_RDWR)) < 0) {
		ast_log(LOG_ERROR, "Failed to open %s: %s\n", dev_filename, strerror(errno));
		return -1;
	}

	dahdip->fmts.srcfmt = src_dahdi_fmt;
	dahdip->fmts.dstfmt = dst_dahdi_fmt;

	ast_assert(pvt->f.subclass.format == NULL);
	pvt->f.subclass.format = ao2_bump(dahdi_format_to_cached(dahdip->fmts.dstfmt));

	ast_debug(1, "Opening transcoder channel from %s to %s.\n", pvt->t->src_codec.name, pvt->t->dst_codec.name);

retry:
	if (ioctl(fd, DAHDI_TC_ALLOCATE, &dahdip->fmts)) {
		if ((ENODEV == errno) && !tried_once) {
			/* We requested to translate to/from an unsupported
			 * format.  Most likely this is because signed linear
			 * was not supported by any hardware devices even
			 * though this module always registers signed linear
			 * support. In this case we'll retry, requesting
			 * support for ULAW instead of signed linear and then
			 * we'll just convert from ulaw to signed linear in
			 * software. */
			if (dahdip->fmts.srcfmt == DAHDI_FORMAT_SLINEAR) {
				ast_debug(1, "Using soft_slin support on source\n");
				dahdip->softslin = 1;
				dahdip->fmts.srcfmt = DAHDI_FORMAT_ULAW;
			} else if (dahdip->fmts.dstfmt == DAHDI_FORMAT_SLINEAR) {
				ast_debug(1, "Using soft_slin support on destination\n");
				dahdip->softslin = 1;
				dahdip->fmts.dstfmt = DAHDI_FORMAT_ULAW;
			}
			tried_once = 1;
			goto retry;
		}
		ast_log(LOG_ERROR, "Unable to attach to transcoder: %s\n", strerror(errno));
		close(fd);

		return -1;
	}

	flags = fcntl(fd, F_GETFL);
	if (flags > - 1) {
		if (fcntl(fd, F_SETFL, flags | O_NONBLOCK))
			ast_log(LOG_WARNING, "Could not set non-block mode!\n");
	}

	dahdip->fd = fd;

	dahdip->required_samples = ((dahdip->fmts.dstfmt|dahdip->fmts.srcfmt) & (DAHDI_FORMAT_G723_1)) ? G723_SAMPLES : G729_SAMPLES;

	switch (dahdip->fmts.dstfmt) {
	case DAHDI_FORMAT_G729A:
		ast_atomic_fetchadd_int(&channels.encoders, +1);
		break;
	case DAHDI_FORMAT_G723_1:
		ast_atomic_fetchadd_int(&channels.encoders, +1);
		break;
	default:
		ast_atomic_fetchadd_int(&channels.decoders, +1);
		break;
	}

	return 0;
}

static int dahdi_new(struct ast_trans_pvt *pvt)
{
	struct translator *zt = container_of(pvt->t, struct translator, t);

	return dahdi_translate(pvt, zt->dst_dahdi_fmt, zt->src_dahdi_fmt);
}

static struct ast_frame *fakesrc_sample(void)
{
	/* Don't bother really trying to test hardware ones. */
	static struct ast_frame f = {
		.frametype = AST_FRAME_VOICE,
		.samples = 160,
		.src = __PRETTY_FUNCTION__
	};

	return &f;
}

static bool is_encoder(uint32_t src_dahdi_fmt)
{
	return ((src_dahdi_fmt & (DAHDI_FORMAT_ULAW | DAHDI_FORMAT_ALAW | DAHDI_FORMAT_SLINEAR)) > 0);
}

/* Must be called with the translators list locked. */
static int register_translator(uint32_t dst_dahdi_fmt, uint32_t src_dahdi_fmt)
{
	const struct ast_codec *dst_codec;
	const struct ast_codec *src_codec;
	struct translator *zt;
	int res;

	dst_codec = get_dahdi_codec(dst_dahdi_fmt);
	src_codec = get_dahdi_codec(src_dahdi_fmt);
	if (!dst_codec || !src_codec) {
		return -1;
	}

	if (!(zt = ast_calloc(1, sizeof(*zt)))) {
		return -1;
	}

	zt->src_dahdi_fmt = src_dahdi_fmt;
	zt->dst_dahdi_fmt = dst_dahdi_fmt;

	snprintf(zt->t.name, sizeof(zt->t.name), "dahdi_%s_to_%s",
		src_codec->name, dst_codec->name);

	memcpy(&zt->t.src_codec, src_codec, sizeof(*src_codec));
	memcpy(&zt->t.dst_codec, dst_codec, sizeof(*dst_codec));
	zt->t.buf_size = BUFFER_SIZE;
	if (is_encoder(src_dahdi_fmt)) {
		zt->t.framein = dahdi_encoder_framein;
		zt->t.frameout = dahdi_encoder_frameout;
	} else {
		zt->t.framein = dahdi_decoder_framein;
		zt->t.frameout = dahdi_decoder_frameout;
	}
	zt->t.destroy = dahdi_destroy;
	zt->t.buffer_samples = 0;
	zt->t.newpvt = dahdi_new;
	zt->t.sample = fakesrc_sample;
	zt->t.native_plc = 0;

	zt->t.desc_size = sizeof(struct codec_dahdi_pvt);
	if ((res = ast_register_translator(&zt->t))) {
		ast_free(zt);
		return -1;
	}

	AST_LIST_INSERT_HEAD(&translators, zt, entry);

	return res;
}

static void unregister_translators(void)
{
	struct translator *cur;

	AST_LIST_LOCK(&translators);
	while ((cur = AST_LIST_REMOVE_HEAD(&translators, entry))) {
		ast_free(cur);
	}
	AST_LIST_UNLOCK(&translators);
}

/* Must be called with the translators list locked. */
static bool is_already_registered(uint32_t dstfmt, uint32_t srcfmt)
{
	bool res = false;
	const struct translator *zt;

	AST_LIST_TRAVERSE(&translators, zt, entry) {
		if ((zt->src_dahdi_fmt == srcfmt) && (zt->dst_dahdi_fmt == dstfmt)) {
			res = true;
			break;
		}
	}

	return res;
}

static void build_translators(uint32_t dstfmts, uint32_t srcfmts)
{
	uint32_t srcfmt;
	uint32_t dstfmt;

	AST_LIST_LOCK(&translators);

	for (srcfmt = 1; srcfmt != 0; srcfmt <<= 1) {
		for (dstfmt = 1; dstfmt != 0; dstfmt <<= 1) {
			if (!(dstfmts & dstfmt) || !(srcfmts & srcfmt)) {
				continue;
			}
			if (is_already_registered(dstfmt, srcfmt)) {
				continue;
			}
			register_translator(dstfmt, srcfmt);
		}
	}

	AST_LIST_UNLOCK(&translators);
}

static int find_transcoders(void)
{
	struct dahdi_transcoder_info info = { 0, };
	int fd;

	if ((fd = open("/dev/dahdi/transcode", O_RDWR)) < 0) {
		ast_log(LOG_ERROR, "Failed to open /dev/dahdi/transcode: %s\n", strerror(errno));
		return 0;
	}

	for (info.tcnum = 0; !ioctl(fd, DAHDI_TC_GETINFO, &info); info.tcnum++) {
		ast_verb(2, "Found transcoder '%s'.\n", info.name);

		/* Complex codecs need to support signed linear.  If the
		 * hardware transcoder does not natively support signed linear
		 * format, we will emulate it in software directly in this
		 * module. Also, do not allow direct ulaw/alaw to complex
		 * codec translation, since that will prevent the generic PLC
		 * functions from working. */
		if (info.dstfmts & (DAHDI_FORMAT_ULAW | DAHDI_FORMAT_ALAW)) {
			info.dstfmts |= DAHDI_FORMAT_SLINEAR;
			info.dstfmts &= ~(DAHDI_FORMAT_ULAW | DAHDI_FORMAT_ALAW);
		}
		if (info.srcfmts & (DAHDI_FORMAT_ULAW | DAHDI_FORMAT_ALAW)) {
			info.srcfmts |= DAHDI_FORMAT_SLINEAR;
			info.srcfmts &= ~(DAHDI_FORMAT_ULAW | DAHDI_FORMAT_ALAW);
		}

		build_translators(info.dstfmts, info.srcfmts);
		ast_atomic_fetchadd_int(&channels.total, info.numchannels / 2);
	}

	close(fd);

	if (!info.tcnum) {
		ast_verb(2, "No hardware transcoders found.\n");
	}

	return 0;
}

static void unload_module(void)
{
	unregister_translators();
}

static int load_module(void)
{
	find_transcoders();
	ast_cli_register_multiple(cli, ARRAY_LEN(cli));
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Generic DAHDI Transcoder Codec Translator");
