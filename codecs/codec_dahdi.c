/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Zaptel native transcoding support
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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
 * \brief Translate between various formats natively through Zaptel transcoding
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

#define BUFFER_SAMPLES	8000

static unsigned int global_useplc = 0;

static struct channel_usage {
	int total;
	int encoders;
	int decoders;
} channels;

static char show_transcoder_usage[] =
"Usage: show transcoder\n"
"       Displays channel utilization of Zaptel transcoder(s).\n";

static char transcoder_show_usage[] =
"Usage: transcoder show\n"
"       Displays channel utilization of Zaptel transcoder(s).\n";

static int transcoder_show(int fd, int argc, char **argv);

static struct ast_cli_entry cli_deprecated[] = {
	{ { "show", "transcoder", NULL },
	  transcoder_show,
	  "Display Zaptel transcoder utilization.",
	  show_transcoder_usage}
};

static struct ast_cli_entry cli[] = {
	{ { "transcoder", "show", NULL },
	  transcoder_show,
	  "Display Zaptel transcoder utilization.",
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

struct pvt {
	int fd;
	int fake;
	unsigned int g729b_warning:1;
#ifdef DEBUG_TRANSCODE
	int totalms;
	int lasttotalms;
#endif
	DAHDI_TRANSCODE_HEADER *hdr;
};

static int transcoder_show(int fd, int argc, char **argv)
{
	struct channel_usage copy;

	copy = channels;

	if (copy.total == 0)
		ast_cli(fd, "No Zaptel transcoders found.\n");
	else
		ast_cli(fd, "%d/%d encoders/decoders of %d channels are in use.\n", copy.encoders, copy.decoders, copy.total);

	return RESULT_SUCCESS;
}

static int zap_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct pvt *ztp = pvt->pvt;
	DAHDI_TRANSCODE_HEADER *hdr = ztp->hdr;

	if (!f->subclass) {
		/* Fake a return frame for calculation purposes */
		ztp->fake = 2;
		pvt->samples = f->samples;
		return 0;
	}

	if (!hdr->srclen)
		/* Copy at front of buffer */
		hdr->srcoffset = 0;

	/* if we get handed a G.729 frame that is not a multiple of
	   10 bytes (10 milliseconds), then it has a CNG frame and
	   we need to avoid sending that to the transcoder
	*/
	if ((f->subclass == AST_FORMAT_G729A) && ((f->datalen % 10) != 0)) {
		if (!ztp->g729b_warning) {
			ast_log(LOG_WARNING, "G.729B CNG frame received but is not supported; dropping.\n");
			ztp->g729b_warning = 1;
		}
		f->datalen -= f->datalen % 10;
		f->samples = f->datalen * 8;
	}

	if (hdr->srclen + f->datalen > sizeof(hdr->srcdata)) {
		ast_log(LOG_WARNING, "Out of space for codec translation!\n");
		return -1;
	}

	if (hdr->srclen + f->datalen + hdr->srcoffset > sizeof(hdr->srcdata)) {
		/* Very unlikely */
		memmove(hdr->srcdata, hdr->srcdata + hdr->srcoffset, hdr->srclen);
		hdr->srcoffset = 0;
	}

	memcpy(hdr->srcdata + hdr->srcoffset + hdr->srclen, f->data, f->datalen);
	hdr->srclen += f->datalen;
	pvt->samples += f->samples;

	return -1;
}

static struct ast_frame *zap_frameout(struct ast_trans_pvt *pvt)
{
	struct pvt *ztp = pvt->pvt;
	DAHDI_TRANSCODE_HEADER *hdr = ztp->hdr;
	unsigned int x;

	if (ztp->fake == 2) {
		ztp->fake = 1;
		pvt->f.frametype = AST_FRAME_VOICE;
		pvt->f.subclass = 0;
		pvt->f.samples = 160;
		pvt->f.data = NULL;
		pvt->f.offset = 0;
		pvt->f.datalen = 0;
		pvt->f.mallocd = 0;
		ast_set_flag(&pvt->f, AST_FRFLAG_FROM_TRANSLATOR);
		pvt->samples = 0;
	} else if (ztp->fake == 1) {
		return NULL;
	} else {
		if (hdr->dstlen) {
#ifdef DEBUG_TRANSCODE
			ztp->totalms += hdr->dstsamples;
			if ((ztp->totalms - ztp->lasttotalms) > 8000) {
				printf("Whee %p, %d (%d to %d)\n", ztp, hdr->dstlen, ztp->lasttotalms, ztp->totalms);
				ztp->lasttotalms = ztp->totalms;
			}
#endif
			pvt->f.frametype = AST_FRAME_VOICE;
			pvt->f.subclass = hdr->dstfmt;
			pvt->f.samples = hdr->dstsamples;
			pvt->f.data = hdr->dstdata + hdr->dstoffset;
			pvt->f.offset = hdr->dstoffset;
			pvt->f.datalen = hdr->dstlen;
			pvt->f.mallocd = 0;
			ast_set_flag(&pvt->f, AST_FRFLAG_FROM_TRANSLATOR);
			pvt->samples -= pvt->f.samples;
			hdr->dstlen = 0;
			
		} else {
			if (hdr->srclen) {
				hdr->dstoffset = AST_FRIENDLY_OFFSET;
				x = DAHDI_TCOP_TRANSCODE;
				if (ioctl(ztp->fd, DAHDI_TRANSCODE_OP, &x))
					ast_log(LOG_WARNING, "Failed to transcode: %s\n", strerror(errno));
			}
			return NULL;
		}
	}

	return &pvt->f;
}

static void zap_destroy(struct ast_trans_pvt *pvt)
{
	struct pvt *ztp = pvt->pvt;
	unsigned int x;

	x = DAHDI_TCOP_RELEASE;
	if (ioctl(ztp->fd, DAHDI_TRANSCODE_OP, &x))
		ast_log(LOG_WARNING, "Failed to release transcoder channel: %s\n", strerror(errno));

	switch (ztp->hdr->dstfmt) {
	case AST_FORMAT_G729A:
	case AST_FORMAT_G723_1:
		ast_atomic_fetchadd_int(&channels.encoders, -1);
		break;
	default:
		ast_atomic_fetchadd_int(&channels.decoders, -1);
		break;
	}

	munmap(ztp->hdr, sizeof(*ztp->hdr));
	close(ztp->fd);
}

static int zap_translate(struct ast_trans_pvt *pvt, int dest, int source)
{
	/* Request translation through zap if possible */
	int fd;
	unsigned int x = DAHDI_TCOP_ALLOCATE;
	struct pvt *ztp = pvt->pvt;
	DAHDI_TRANSCODE_HEADER *hdr;
	int flags;
	
	if ((fd = open("/dev/zap/transcode", O_RDWR)) < 0)
		return -1;
	flags = fcntl(fd, F_GETFL);
	if (flags > - 1) {
		if (fcntl(fd, F_SETFL, flags | O_NONBLOCK))
			ast_log(LOG_WARNING, "Could not set non-block mode!\n");
	}
	

	if ((hdr = mmap(NULL, sizeof(*hdr), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
		ast_log(LOG_ERROR, "Memory Map failed for transcoding (%s)\n", strerror(errno));
		close(fd);

		return -1;
	}

	if (hdr->magic != DAHDI_TRANSCODE_MAGIC) {
		ast_log(LOG_ERROR, "Transcoder header (%08x) wasn't magic.  Abandoning\n", hdr->magic);
		munmap(hdr, sizeof(*hdr));
		close(fd);

		return -1;
	}
	
	hdr->srcfmt = (1 << source);
	hdr->dstfmt = (1 << dest);
	if (ioctl(fd, DAHDI_TRANSCODE_OP, &x)) {
		ast_log(LOG_ERROR, "Unable to attach transcoder: %s\n", strerror(errno));
		munmap(hdr, sizeof(*hdr));
		close(fd);

		return -1;
	}

	ztp = pvt->pvt;
	ztp->fd = fd;
	ztp->hdr = hdr;

	switch (hdr->dstfmt) {
	case AST_FORMAT_G729A:
	case AST_FORMAT_G723_1:
		ast_atomic_fetchadd_int(&channels.encoders, +1);
		break;
	default:
		ast_atomic_fetchadd_int(&channels.decoders, +1);
		break;
	}

	return 0;
}

static int zap_new(struct ast_trans_pvt *pvt)
{
	return zap_translate(pvt, pvt->t->dstfmt, pvt->t->srcfmt);
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

static int register_translator(int dst, int src)
{
	struct translator *zt;
	int res;

	if (!(zt = ast_calloc(1, sizeof(*zt))))
		return -1;

	snprintf((char *) (zt->t.name), sizeof(zt->t.name), "zap%sto%s", 
		 ast_getformatname((1 << src)), ast_getformatname((1 << dst)));
	zt->t.srcfmt = (1 << src);
	zt->t.dstfmt = (1 << dst);
	zt->t.newpvt = zap_new;
	zt->t.framein = zap_framein;
	zt->t.frameout = zap_frameout;
	zt->t.destroy = zap_destroy;
	zt->t.sample = fakesrc_sample;
	zt->t.useplc = global_useplc;
	zt->t.buf_size = BUFFER_SAMPLES * 2;
	zt->t.desc_size = sizeof(struct pvt);
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
	DAHDI_TRANSCODE_INFO info = { 0, };
	struct format_map map = { { { 0 } } };
	int fd, res;
	unsigned int x, y;

	if ((fd = open("/dev/zap/transcode", O_RDWR)) < 0) {
		ast_verbose(VERBOSE_PREFIX_2 "No hardware transcoders found.\n");
		return 0;
	}

	info.op = DAHDI_TCOP_GETINFO;
	for (info.tcnum = 0; !(res = ioctl(fd, DAHDI_TRANSCODE_OP, &info)); info.tcnum++) {
		if (option_verbose > 1)
			ast_verbose(VERBOSE_PREFIX_2 "Found transcoder '%s'.\n", info.name);
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
	parse_config();
	find_transcoders();
	ast_cli_register_multiple(cli, sizeof(cli) / sizeof(cli[0]));

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Generic Zaptel Transcoder Codec Translator",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
