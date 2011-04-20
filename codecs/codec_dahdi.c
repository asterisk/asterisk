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
	<depend>dahdi</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

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

#define BUFFER_SIZE 8000

#define G723_SAMPLES 240
#define G729_SAMPLES 160

#ifndef DAHDI_FORMAT_MAX_AUDIO
#define DAHDI_FORMAT_G723_1    (1 << 0)
#define DAHDI_FORMAT_GSM       (1 << 1)
#define DAHDI_FORMAT_ULAW      (1 << 2)
#define DAHDI_FORMAT_ALAW      (1 << 3)
#define DAHDI_FORMAT_G726      (1 << 4)
#define DAHDI_FORMAT_ADPCM     (1 << 5)
#define DAHDI_FORMAT_SLINEAR   (1 << 6)
#define DAHDI_FORMAT_LPC10     (1 << 7)
#define DAHDI_FORMAT_G729A     (1 << 8)
#define DAHDI_FORMAT_SPEEX     (1 << 9)
#define DAHDI_FORMAT_ILBC      (1 << 10)
#endif

static struct channel_usage {
	int total;
	int encoders;
	int decoders;
} channels;

static char *handle_cli_transcoder_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);

static struct ast_cli_entry cli[] = {
	AST_CLI_DEFINE(handle_cli_transcoder_show, "Display DAHDI transcoder utilization.")
};

struct format_map {
	unsigned int map[32][32];
};

static struct format_map global_format_map = { { { 0 } } };

struct translator {
	struct ast_translator t;
	AST_LIST_ENTRY(translator) entry;
};

static AST_LIST_HEAD_STATIC(translators, translator);

struct codec_dahdi_pvt {
	int fd;
	struct dahdi_transcoder_formats fmts;
	unsigned int softslin:1;
	unsigned int fake:2;
	uint16_t required_samples;
	uint16_t samples_in_buffer;
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
	struct pollfd p = {0};
	if (!count) return;
	res = write(dahdip->fd, buffer, count);
	if (option_verbose > 10) {
		if (-1 == res) {
			ast_log(LOG_ERROR, "Failed to write to transcoder: %s\n", strerror(errno));
		}
		if (count != res) {
			ast_log(LOG_ERROR, "Requested write of %zd bytes, but only wrote %d bytes.\n", count, res);
		}
	}
	p.fd = dahdip->fd;
	p.events = POLLOUT;
	res = poll(&p, 1, 50);
}

static int dahdi_encoder_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct codec_dahdi_pvt *dahdip = pvt->pvt;

	if (!f->subclass.format.id) {
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

	while (dahdip->samples_in_buffer > dahdip->required_samples) {
		dahdi_write_frame(dahdip, dahdip->ulaw_buffer, dahdip->required_samples);
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

static struct ast_frame *dahdi_encoder_frameout(struct ast_trans_pvt *pvt)
{
	struct codec_dahdi_pvt *dahdip = pvt->pvt;
	int res;

	if (2 == dahdip->fake) {
		dahdip->fake = 1;
		pvt->f.frametype = AST_FRAME_VOICE;
		ast_format_clear(&pvt->f.subclass.format);
		pvt->f.samples = dahdip->required_samples;
		pvt->f.data.ptr = NULL;
		pvt->f.offset = 0;
		pvt->f.datalen = 0;
		pvt->f.mallocd = 0;
		pvt->samples = 0;

		return ast_frisolate(&pvt->f);

	} else if (1 == dahdip->fake) {
		dahdip->fake = 0;
		return NULL;
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
		pvt->f.samples = dahdip->required_samples;
		pvt->f.frametype = AST_FRAME_VOICE;
		ast_format_copy(&pvt->f.subclass.format, &pvt->t->dst_format);
		pvt->f.mallocd = 0;
		pvt->f.offset = AST_FRIENDLY_OFFSET;
		pvt->f.src = pvt->t->name;
		pvt->f.data.ptr = pvt->outbuf.c;

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

	if (!f->subclass.format.id) {
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
	pvt->samples += f->samples;
	pvt->datalen = 0;
	return -1;
}

static struct ast_frame *dahdi_decoder_frameout(struct ast_trans_pvt *pvt)
{
	int res;
	struct codec_dahdi_pvt *dahdip = pvt->pvt;

	if (2 == dahdip->fake) {
		dahdip->fake = 1;
		pvt->f.frametype = AST_FRAME_VOICE;
		ast_format_clear(&pvt->f.subclass.format);
		pvt->f.samples = dahdip->required_samples;
		pvt->f.data.ptr = NULL;
		pvt->f.offset = 0;
		pvt->f.datalen = 0;
		pvt->f.mallocd = 0;
		pvt->samples = 0;
		return ast_frisolate(&pvt->f);
	} else if (1 == dahdip->fake) {
		pvt->samples = 0;
		dahdip->fake = 0;
		return NULL;
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
		pvt->f.frametype = AST_FRAME_VOICE;
		ast_format_copy(&pvt->f.subclass.format, &pvt->t->dst_format);
		pvt->f.mallocd = 0;
		pvt->f.offset = AST_FRIENDLY_OFFSET;
		pvt->f.src = pvt->t->name;
		pvt->f.data.ptr = pvt->outbuf.c;
		pvt->f.samples = res;
		pvt->samples = 0;

		return ast_frisolate(&pvt->f);
	}

	/* Shouldn't get here... */
	return NULL;
}


static void dahdi_destroy(struct ast_trans_pvt *pvt)
{
	struct codec_dahdi_pvt *dahdip = pvt->pvt;

	switch (dahdip->fmts.dstfmt) {
	case AST_FORMAT_G729A:
	case AST_FORMAT_G723_1:
		ast_atomic_fetchadd_int(&channels.encoders, -1);
		break;
	default:
		ast_atomic_fetchadd_int(&channels.decoders, -1);
		break;
	}

	close(dahdip->fd);
}

static int dahdi_translate(struct ast_trans_pvt *pvt, struct ast_format *dst_format, struct ast_format *src_format)
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

	dahdip->fmts.srcfmt = ast_format_to_old_bitfield(src_format);
	dahdip->fmts.dstfmt = ast_format_to_old_bitfield(dst_format);

	ast_debug(1, "Opening transcoder channel from %s to %s.\n", ast_getformatname(src_format), ast_getformatname(dst_format));

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
			if (AST_FORMAT_SLINEAR == ast_format_id_from_old_bitfield(dahdip->fmts.srcfmt)) {
				ast_debug(1, "Using soft_slin support on source\n");
				dahdip->softslin = 1;
				dahdip->fmts.srcfmt = ast_format_id_to_old_bitfield(AST_FORMAT_ULAW);
			} else if (AST_FORMAT_SLINEAR == ast_format_id_from_old_bitfield(dahdip->fmts.dstfmt)) {
				ast_debug(1, "Using soft_slin support on destination\n");
				dahdip->softslin = 1;
				dahdip->fmts.dstfmt = ast_format_id_to_old_bitfield(AST_FORMAT_ULAW);
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

	dahdip->required_samples = ((dahdip->fmts.dstfmt|dahdip->fmts.srcfmt) & (ast_format_id_to_old_bitfield(AST_FORMAT_G723_1))) ? G723_SAMPLES : G729_SAMPLES;

	switch (ast_format_id_from_old_bitfield(dahdip->fmts.dstfmt)) {
	case AST_FORMAT_G729A:
		ast_atomic_fetchadd_int(&channels.encoders, +1);
		break;
	case AST_FORMAT_G723_1:
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
	return dahdi_translate(pvt,
		&pvt->t->dst_format,
		&pvt->t->src_format);
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

static int is_encoder(struct translator *zt)
{
	if ((zt->t.src_format.id == AST_FORMAT_ULAW) ||
		(zt->t.src_format.id == AST_FORMAT_ALAW) ||
		(zt->t.src_format.id == AST_FORMAT_SLINEAR)) {
		return 1;
	} else {
		return 0;
	}
}

static int register_translator(int dst, int src)
{
	struct translator *zt;
	int res;
	struct ast_format dst_format;
	struct ast_format src_format;

	ast_format_from_old_bitfield(&dst_format, (1 << dst));
	ast_format_from_old_bitfield(&src_format, (1 << src));

	if (!(zt = ast_calloc(1, sizeof(*zt)))) {
		return -1;
	}

	snprintf((char *) (zt->t.name), sizeof(zt->t.name), "zap%sto%s",
		 ast_getformatname(&src_format), ast_getformatname(&dst_format));
	ast_format_copy(&zt->t.src_format, &src_format);
	ast_format_copy(&zt->t.dst_format, &dst_format);
	zt->t.buf_size = BUFFER_SIZE;
	if (is_encoder(zt)) {
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

	AST_LIST_LOCK(&translators);
	AST_LIST_INSERT_HEAD(&translators, zt, entry);
	AST_LIST_UNLOCK(&translators);

	global_format_map.map[dst][src] = 1;

	return res;
}

static void drop_translator(int dst, int src)
{
	struct translator *cur;

	AST_LIST_LOCK(&translators);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&translators, cur, entry) {
		if (cur->t.src_format.id != ast_format_id_from_old_bitfield((1 << src)))
			continue;

		if (cur->t.dst_format.id != ast_format_id_from_old_bitfield((1 << dst)))
			continue;

		AST_LIST_REMOVE_CURRENT(entry);
		ast_unregister_translator(&cur->t);
		ast_free(cur);
		global_format_map.map[dst][src] = 0;
		break;
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&translators);
}

static void unregister_translators(void)
{
	struct translator *cur;

	AST_LIST_LOCK(&translators);
	while ((cur = AST_LIST_REMOVE_HEAD(&translators, entry))) {
		ast_unregister_translator(&cur->t);
		ast_free(cur);
	}
	AST_LIST_UNLOCK(&translators);
}

static void build_translators(struct format_map *map, unsigned int dstfmts, unsigned int srcfmts)
{
	unsigned int src, dst;

	for (src = 0; src < 32; src++) {
		for (dst = 0; dst < 32; dst++) {
			if (!(srcfmts & (1 << src)))
				continue;

			if (!(dstfmts & (1 << dst)))
				continue;

			if (global_format_map.map[dst][src])
				continue;

			if (!register_translator(dst, src))
				map->map[dst][src] = 1;
		}
	}
}

static int find_transcoders(void)
{
	struct dahdi_transcoder_info info = { 0, };
	struct format_map map = { { { 0 } } };
	int fd, res;
	unsigned int x, y;

	if ((fd = open("/dev/dahdi/transcode", O_RDWR)) < 0) {
		ast_log(LOG_ERROR, "Failed to open /dev/dahdi/transcode: %s\n", strerror(errno));
		return 0;
	}

	for (info.tcnum = 0; !(res = ioctl(fd, DAHDI_TC_GETINFO, &info)); info.tcnum++) {
		if (option_verbose > 1)
			ast_verbose(VERBOSE_PREFIX_2 "Found transcoder '%s'.\n", info.name);

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

		build_translators(&map, info.dstfmts, info.srcfmts);
		ast_atomic_fetchadd_int(&channels.total, info.numchannels / 2);

	}

	close(fd);

	if (!info.tcnum && (option_verbose > 1))
		ast_verbose(VERBOSE_PREFIX_2 "No hardware transcoders found.\n");

	for (x = 0; x < 32; x++) {
		for (y = 0; y < 32; y++) {
			if (!map.map[x][y] && global_format_map.map[x][y])
				drop_translator(x, y);
		}
	}

	return 0;
}

static int reload(void)
{
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_cli_unregister_multiple(cli, ARRAY_LEN(cli));
	unregister_translators();

	return 0;
}

static int load_module(void)
{
	ast_ulaw_init();
	find_transcoders();
	ast_cli_register_multiple(cli, ARRAY_LEN(cli));
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Generic DAHDI Transcoder Codec Translator",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
