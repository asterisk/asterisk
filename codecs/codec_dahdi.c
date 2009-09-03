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
	<depend>dahdi_transcode</depend>
	<depend>dahdi</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/poll.h>

#include "asterisk/lock.h"
#include "asterisk/translate.h"
#include "asterisk/config.h"
#include "asterisk/options.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/utils.h"
#include "asterisk/linkedlists.h"
#include "asterisk/dahdi_compat.h"
#include "asterisk/frame.h"
#include "asterisk/ulaw.h"

#define BUFFER_SIZE 8000

#define G723_SAMPLES 240
#define G729_SAMPLES 160

static unsigned int global_useplc = 0;

static struct channel_usage {
	int total;
	int encoders;
	int decoders;
} channels;

static char show_transcoder_usage[] =
"Usage: show transcoder\n"
"       Displays channel utilization of DAHDI transcoder(s).\n";

static char transcoder_show_usage[] =
"Usage: transcoder show\n"
"       Displays channel utilization of DAHDI transcoder(s).\n";

static int transcoder_show(int fd, int argc, char **argv);

static struct ast_cli_entry cli_deprecated[] = {
	{ { "show", "transcoder", NULL },
	  transcoder_show,
	  "Display DAHDI transcoder utilization.",
	  show_transcoder_usage}
};

static struct ast_cli_entry cli[] = {
	{ { "transcoder", "show", NULL },
	  transcoder_show,
	  "Display DAHDI transcoder utilization.",
	  transcoder_show_usage, NULL,
	  &cli_deprecated[0]}
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
static int ulawtolin(struct ast_trans_pvt *pvt)
{
	struct codec_dahdi_pvt *ztp = pvt->pvt;
	int i = ztp->required_samples;
	uint8_t *src = &ztp->ulaw_buffer[0];
	int16_t *dst = (int16_t *)pvt->outbuf + pvt->datalen;

	/* convert and copy in outbuf */
	while (i--) {
		*dst++ = AST_MULAW(*src++);
	}

	return 0;
}

/* Only used by an encoder. */
static int lintoulaw(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct codec_dahdi_pvt *ztp = pvt->pvt;
	int i = f->samples;
	uint8_t *dst = &ztp->ulaw_buffer[ztp->samples_in_buffer];
	int16_t *src = (int16_t*)f->data;

	if (ztp->samples_in_buffer + i > sizeof(ztp->ulaw_buffer)) {
		ast_log(LOG_ERROR, "Out of buffer space!\n");
		return -i;
	}

	while (i--) {
		*dst++ = AST_LIN2MU(*src++);
	}

	ztp->samples_in_buffer += f->samples;
	return 0;
}

static int transcoder_show(int fd, int argc, char **argv)
{
	struct channel_usage copy;

	copy = channels;

	if (copy.total == 0)
		ast_cli(fd, "No DAHDI transcoders found.\n");
	else
		ast_cli(fd, "%d/%d encoders/decoders of %d channels are in use.\n", copy.encoders, copy.decoders, copy.total);

	return RESULT_SUCCESS;
}

static void dahdi_write_frame(struct codec_dahdi_pvt *ztp, const uint8_t *buffer, const ssize_t count)
{
	int res;
	struct pollfd p = {0};
	if (!count) return;
	res = write(ztp->fd, buffer, count); 
	if (option_verbose > 10) {
		if (-1 == res) {
			ast_log(LOG_ERROR, "Failed to write to transcoder: %s\n", strerror(errno));
		} 
		if (count != res) {
			ast_log(LOG_ERROR, "Requested write of %zd bytes, but only wrote %d bytes.\n", count, res);
		}
	}
	p.fd = ztp->fd;
	p.events = POLLOUT;
	res = poll(&p, 1, 50);
}

static int dahdi_encoder_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct codec_dahdi_pvt *ztp = pvt->pvt;

	if (!f->subclass) {
		/* We're just faking a return for calculation purposes. */
		ztp->fake = 2;
		pvt->samples = f->samples;
		return 0;
	}

	/* Buffer up the packets and send them to the hardware if we
	 * have enough samples set up. */
	if (ztp->softslin) {
		if (lintoulaw(pvt, f)) {
			 return -1;
		}
	} else {
		/* NOTE:  If softslin support is not needed, and the sample
		 * size is equal to the required sample size, we wouldn't
		 * need this copy operation.  But at the time this was
		 * written, only softslin is supported. */
		if (ztp->samples_in_buffer + f->samples > sizeof(ztp->ulaw_buffer)) {
			ast_log(LOG_ERROR, "Out of buffer space.\n");
			return -1;
		}
		memcpy(&ztp->ulaw_buffer[ztp->samples_in_buffer], f->data, f->samples);
		ztp->samples_in_buffer += f->samples;
	}

	while (ztp->samples_in_buffer > ztp->required_samples) {
		dahdi_write_frame(ztp, ztp->ulaw_buffer, ztp->required_samples);
		ztp->samples_in_buffer -= ztp->required_samples;
		if (ztp->samples_in_buffer) {
			/* Shift any remaining bytes down. */
			memmove(ztp->ulaw_buffer, &ztp->ulaw_buffer[ztp->required_samples],
				ztp->samples_in_buffer);
		}
	}
	pvt->samples += f->samples;
	pvt->datalen = 0;
	return -1;
}

static struct ast_frame *dahdi_encoder_frameout(struct ast_trans_pvt *pvt)
{
	struct codec_dahdi_pvt *ztp = pvt->pvt;
	int res;

	if (2 == ztp->fake) {
		ztp->fake = 1;
		pvt->f.frametype = AST_FRAME_VOICE;
		pvt->f.subclass = 0;
		pvt->f.samples = ztp->required_samples;
		pvt->f.data = NULL;
		pvt->f.offset = 0;
		pvt->f.datalen = 0;
		pvt->f.mallocd = 0;
		ast_set_flag(&pvt->f, AST_FRFLAG_FROM_TRANSLATOR);
		pvt->samples = 0;

		return &pvt->f;

	} else if (1 == ztp->fake) {
		ztp->fake = 0;
		return NULL;
	}

	res = read(ztp->fd, pvt->outbuf + pvt->datalen, pvt->t->buf_size - pvt->datalen);
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
		pvt->f.samples = ztp->required_samples;
		pvt->f.frametype = AST_FRAME_VOICE;
		pvt->f.subclass = 1 <<  (pvt->t->dstfmt);
		pvt->f.mallocd = 0;
		pvt->f.offset = AST_FRIENDLY_OFFSET;
		pvt->f.src = pvt->t->name;
		pvt->f.data = pvt->outbuf;
		ast_set_flag(&pvt->f, AST_FRFLAG_FROM_TRANSLATOR);

		pvt->samples = 0;
		pvt->datalen = 0;
		return &pvt->f;
	}

	/* Shouldn't get here... */
	return NULL;
}

static int dahdi_decoder_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct codec_dahdi_pvt *ztp = pvt->pvt;

	if (!f->subclass) {
		/* We're just faking a return for calculation purposes. */
		ztp->fake = 2;
		pvt->samples = f->samples;
		return 0;
	}

	if (!f->datalen) {
		if (f->samples != ztp->required_samples) {
			ast_log(LOG_ERROR, "%d != %d %d\n", f->samples, ztp->required_samples, f->datalen);
		}
	}
	dahdi_write_frame(ztp, f->data, f->datalen);
	pvt->samples += f->samples;
	pvt->datalen = 0;
	return -1;
}

static struct ast_frame *dahdi_decoder_frameout(struct ast_trans_pvt *pvt)
{
	int res;
	struct codec_dahdi_pvt *ztp = pvt->pvt;

	if (2 == ztp->fake) {
		ztp->fake = 1;
		pvt->f.frametype = AST_FRAME_VOICE;
		pvt->f.subclass = 0;
		pvt->f.samples = ztp->required_samples;
		pvt->f.data = NULL;
		pvt->f.offset = 0;
		pvt->f.datalen = 0;
		pvt->f.mallocd = 0;
		ast_set_flag(&pvt->f, AST_FRFLAG_FROM_TRANSLATOR);
		pvt->samples = 0;
		return &pvt->f;
	} else if (1 == ztp->fake) {
		pvt->samples = 0;
		ztp->fake = 0;
		return NULL;
	}

	/* Let's check to see if there is a new frame for us.... */
	if (ztp->softslin) {
		res = read(ztp->fd, ztp->ulaw_buffer, sizeof(ztp->ulaw_buffer));
	} else {
		res = read(ztp->fd, pvt->outbuf + pvt->datalen, pvt->t->buf_size - pvt->datalen);
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
		if (ztp->softslin) {
			ulawtolin(pvt);
			pvt->f.datalen = res * 2;
		} else {
			pvt->f.datalen = res;
		}
		pvt->datalen = 0;
		pvt->f.frametype = AST_FRAME_VOICE;
		pvt->f.subclass = 1 <<  (pvt->t->dstfmt);
		pvt->f.mallocd = 0;
		pvt->f.offset = AST_FRIENDLY_OFFSET;
		pvt->f.src = pvt->t->name;
		pvt->f.data = pvt->outbuf;
		pvt->f.samples = ztp->required_samples;
		ast_set_flag(&pvt->f, AST_FRFLAG_FROM_TRANSLATOR);
		pvt->samples = 0;

		return &pvt->f;
	}

	/* Shouldn't get here... */
	return NULL;
}


static void dahdi_destroy(struct ast_trans_pvt *pvt)
{
	struct codec_dahdi_pvt *ztp = pvt->pvt;

	switch (ztp->fmts.dstfmt) {
	case AST_FORMAT_G729A:
	case AST_FORMAT_G723_1:
		ast_atomic_fetchadd_int(&channels.encoders, -1);
		break;
	default:
		ast_atomic_fetchadd_int(&channels.decoders, -1);
		break;
	}

	close(ztp->fd);
}

static int dahdi_translate(struct ast_trans_pvt *pvt, int dest, int source)
{
	/* Request translation through zap if possible */
	int fd;
	struct codec_dahdi_pvt *ztp = pvt->pvt;
	int flags;
	int tried_once = 0;
#ifdef HAVE_ZAPTEL
	const char *dev_filename = "/dev/zap/transcode";
#else
	const char *dev_filename = "/dev/dahdi/transcode";
#endif
	
	if ((fd = open(dev_filename, O_RDWR)) < 0) {
		ast_log(LOG_ERROR, "Failed to open %s: %s\n", dev_filename, strerror(errno));
		return -1;
	}
	
	ztp->fmts.srcfmt = (1 << source);
	ztp->fmts.dstfmt = (1 << dest);

	if (option_debug) {
		ast_log(LOG_DEBUG, "Opening transcoder channel from %d to %d.\n", source, dest);
	}

retry:
	if (ioctl(fd, DAHDI_TC_ALLOCATE, &ztp->fmts)) {
		if ((ENODEV == errno) && !tried_once) {
			/* We requested to translate to/from an unsupported
			 * format.  Most likely this is because signed linear
			 * was not supported by any hardware devices even
			 * though this module always registers signed linear
			 * support. In this case we'll retry, requesting
			 * support for ULAW instead of signed linear and then
			 * we'll just convert from ulaw to signed linear in
			 * software. */
			if (AST_FORMAT_SLINEAR == ztp->fmts.srcfmt) {
				if (option_debug) {
					ast_log(LOG_DEBUG, "Using soft_slin support on source\n");
				}
				ztp->softslin = 1;
				ztp->fmts.srcfmt = AST_FORMAT_ULAW;
			} else if (AST_FORMAT_SLINEAR == ztp->fmts.dstfmt) {
				if (option_debug) {
					ast_log(LOG_DEBUG, "Using soft_slin support on destination\n");
				}
				ztp->softslin = 1;
				ztp->fmts.dstfmt = AST_FORMAT_ULAW;
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

	ztp->fd = fd;

	ztp->required_samples = ((ztp->fmts.dstfmt|ztp->fmts.srcfmt)&AST_FORMAT_G723_1) ? G723_SAMPLES : G729_SAMPLES;

	switch (ztp->fmts.dstfmt) {
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
	return dahdi_translate(pvt, pvt->t->dstfmt, pvt->t->srcfmt);
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
	if (zt->t.srcfmt&(AST_FORMAT_ULAW|AST_FORMAT_ALAW|AST_FORMAT_SLINEAR)) {
		return 1;
	} else {
		return 0;
	}
}

static int register_translator(int dst, int src)
{
	struct translator *zt;
	int res;

	if (!(zt = ast_calloc(1, sizeof(*zt)))) {
		return -1;
	}

	snprintf((char *) (zt->t.name), sizeof(zt->t.name), "zap%sto%s", 
		 ast_getformatname((1 << src)), ast_getformatname((1 << dst)));
	zt->t.srcfmt = (1 << src);
	zt->t.dstfmt = (1 << dst);
	zt->t.buf_size = BUFFER_SIZE;
	if (is_encoder(zt)) {
		zt->t.framein = dahdi_encoder_framein;
		zt->t.frameout = dahdi_encoder_frameout;
#if 0
		zt->t.buffer_samples = 0;
#endif
	} else {
		zt->t.framein = dahdi_decoder_framein;
		zt->t.frameout = dahdi_decoder_frameout;
#if 0
		if (AST_FORMAT_G723_1 == zt->t.srcfmt) {
			zt->t.plc_samples = G723_SAMPLES;
		} else {
			zt->t.plc_samples = G729_SAMPLES;
		}
		zt->t.buffer_samples = zt->t.plc_samples * 8;
#endif
	}
	zt->t.destroy = dahdi_destroy;
	zt->t.buffer_samples = 0;
	zt->t.newpvt = dahdi_new;
	zt->t.sample = fakesrc_sample;
#if 0
	zt->t.useplc = global_useplc;
#endif
	zt->t.useplc = 0;
	zt->t.native_plc = 0;

	zt->t.desc_size = sizeof(struct codec_dahdi_pvt);
	if ((res = ast_register_translator(&zt->t))) {
		free(zt);
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
		if (cur->t.srcfmt != src)
			continue;

		if (cur->t.dstfmt != dst)
			continue;

		AST_LIST_REMOVE_CURRENT(&translators, entry);
		ast_unregister_translator(&cur->t);
		free(cur);
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
		free(cur);
	}
	AST_LIST_UNLOCK(&translators);
}

static void parse_config(void)
{
	struct ast_variable *var;
	struct ast_config *cfg = ast_config_load("codecs.conf");

	if (!cfg)
		return;

	for (var = ast_variable_browse(cfg, "plc"); var; var = var->next) {
	       if (!strcasecmp(var->name, "genericplc")) {
		       global_useplc = ast_true(var->value);
		       if (option_verbose > 2)
			       ast_verbose(VERBOSE_PREFIX_3 "codec_zap: %susing generic PLC\n",
					   global_useplc ? "" : "not ");
	       }
	}

	ast_config_destroy(cfg);
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

	if ((fd = open(DAHDI_FILE_TRANSCODE, O_RDWR)) < 0) {
		ast_log(LOG_ERROR, "Failed to open " DAHDI_FILE_TRANSCODE ": %s\n", strerror(errno));
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
		if (info.dstfmts & (AST_FORMAT_ULAW | AST_FORMAT_ALAW)) {
			info.dstfmts |= AST_FORMAT_SLINEAR;
			info.dstfmts &= ~(AST_FORMAT_ULAW | AST_FORMAT_ALAW);
		}
		if (info.srcfmts & (AST_FORMAT_ULAW | AST_FORMAT_ALAW)) {
			info.srcfmts |= AST_FORMAT_SLINEAR;
			info.srcfmts &= ~(AST_FORMAT_ULAW | AST_FORMAT_ALAW);
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
	struct translator *cur;

	parse_config();

	AST_LIST_LOCK(&translators);
	AST_LIST_TRAVERSE(&translators, cur, entry)
		cur->t.useplc = global_useplc;
	AST_LIST_UNLOCK(&translators);

	return 0;
}

static int unload_module(void)
{
	ast_cli_unregister_multiple(cli, sizeof(cli) / sizeof(cli[0]));
	unregister_translators();

	return 0;
}

static int load_module(void)
{
	ast_ulaw_init();
	parse_config();
	find_transcoders();
	ast_cli_register_multiple(cli, sizeof(cli) / sizeof(cli[0]));

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Generic DAHDI Transcoder Codec Translator",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
