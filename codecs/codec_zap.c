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
#include <linux/zaptel.h>

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

#define BUFFER_SAMPLES	8000

static unsigned int global_useplc = 0;
static int cardsmode = 0;

static int totalchannels = 0;
static int complexinuse = 0;
static int simpleinuse = 0;
static int localusecnt = 0;
AST_MUTEX_DEFINE_STATIC(channelcount);

static char *tdesc = "Generic Zaptel Transcoder Codec Translator";

static char show_transcoder_usage[] =
"Usage: show transcoder\n"
"       Displays transcoder utilization.\n";

static int show_transcoder(int fd, int argc, char **argv);

static struct ast_cli_entry transcoder_cli[] = {
	{ { "show", "transcoder", NULL},
	show_transcoder, "Displays transcoder utilization.",
	show_transcoder_usage}
};

struct format_map {
	unsigned int map[32][32];
};

static struct format_map global_format_map = { { { 0 } } };

struct translator {
	struct ast_translator t;
	AST_LIST_ENTRY(translator) entry;
};

static AST_LIST_HEAD_STATIC(zap_translators, translator);

struct ast_translator_pvt {
	int fd;
	int fake;
	int inuse;
	int samples;
	int dstfmt;
	int srcfmt;
#ifdef DEBUG_TRANSCODE
	int totalms;
	int lasttotalms;
#endif
	struct zt_transcode_header *hdr;
	struct ast_frame f;
};


static int show_transcoder(int fd, int argc, char **argv)
{
	ast_mutex_lock(&channelcount);
	if (!totalchannels) { 
		ast_cli(fd, "No transcoder card registered\n");
		ast_mutex_unlock(&channelcount);
		return RESULT_SUCCESS;
	}
	if(!cardsmode)
		ast_cli(fd, "%d/%d encoders/decoders of %d channels (G.729a / G.723.1 5.3 kbps) are in use.\n",complexinuse, simpleinuse, totalchannels);
	else if (cardsmode == 1)
		ast_cli(fd, "%d/%d encoders/decoders of %d channels (G.729a) are in use.\n",complexinuse, simpleinuse, totalchannels);
	else if (cardsmode == 2)
		ast_cli(fd, "%d/%d encoders/decoders of %d channels (G.723.1 5.3 kbps) are in use.\n",complexinuse, simpleinuse, totalchannels);

	ast_mutex_unlock(&channelcount);
	return RESULT_SUCCESS;
}

static int zap_framein(struct ast_translator_pvt *ztp, struct ast_frame *f)
{
	struct zt_transcode_header *hdr = ztp->hdr;
	if (!f->subclass) {
		/* Fake a return frame for calculation purposes */
		ztp->fake = 2;
		ztp->samples = f->samples;
		return 0;
	}
	if(!ztp->inuse) {
		ast_mutex_lock(&channelcount);
		localusecnt++;
		if(ztp->dstfmt == 8 || ztp->dstfmt == 0 ) 
			complexinuse++;
		else 
			simpleinuse++;
		ast_mutex_unlock(&channelcount);
		ztp->inuse = 1;
	}
	if (!hdr->srclen)
		/* Copy at front of buffer */
		hdr->srcoffset = 0;

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
	ztp->samples += f->samples;

	return -1;
}

static struct ast_frame *zap_frameout(struct ast_translator_pvt *ztp)
{
	struct zt_transcode_header *hdr = ztp->hdr;
	unsigned int x;

	if (ztp->fake == 2) {
		ztp->fake = 1;
		ztp->f.frametype = AST_FRAME_VOICE;
		ztp->f.subclass = 0;
		ztp->f.samples = 240;
		ztp->f.data = NULL;
		ztp->f.offset = 0;
		ztp->f.datalen = 0;
		ztp->f.mallocd = 0;
		ztp->samples = 0;
	} else if (ztp->fake == 1) {
		return NULL;
	} else {
		if (hdr->dstlen) {
#ifdef DEBUG_TRANSCODE
			ztp->totalms += hdr->dstsamples;
			if ((ztp->totalms - ztp->lasttotalms) > 8000) {
				ast_verbose("Whee %p, %d (%d to %d)\n", ztp, hdr->dstlen, ztp->lasttotalms, ztp->totalms);
				ztp->lasttotalms = ztp->totalms;
			}
#endif
			ztp->f.frametype = AST_FRAME_VOICE;
			ztp->f.subclass = hdr->dstfmt;
			ztp->f.samples = hdr->dstsamples;
			ztp->f.data = hdr->dstdata + hdr->dstoffset;
			ztp->f.offset = hdr->dstoffset;
			ztp->f.datalen = hdr->dstlen;
			ztp->f.mallocd = 0;
			ztp->samples -= ztp->f.samples;
			hdr->dstlen = 0;
			
		} else {
			if (hdr->srclen) {
				hdr->dstoffset = AST_FRIENDLY_OFFSET;
				x = ZT_TCOP_TRANSCODE;
				if (ioctl(ztp->fd, ZT_TRANSCODE_OP, &x))
					ast_log(LOG_WARNING, "Failed to transcode: %s\n", strerror(errno));
			}
			return NULL;
		}
	}

	return &ztp->f;
}

static void zap_destroy(struct ast_translator_pvt *ztp)
{
	unsigned int x;

	x = ZT_TCOP_RELEASE;
	if (ioctl(ztp->fd, ZT_TRANSCODE_OP, &x))
		ast_log(LOG_WARNING, "Failed to release transcoder channel: %s\n", strerror(errno));
				
	munmap(ztp->hdr, sizeof(*ztp->hdr));
	if(ztp->inuse) {
		ast_mutex_lock(&channelcount);
		localusecnt--;
		if(ztp->dstfmt == 8 || ztp->dstfmt == 0)
			complexinuse--;
		else
			simpleinuse--;
		ast_mutex_unlock(&channelcount);
	}
	close(ztp->fd);
}

static struct ast_translator_pvt *zap_new_alawtog723(void)
{
	/* Request translation through zap if possible */
	int fd;
	unsigned int x = ZT_TCOP_ALLOCATE;
	struct ast_translator_pvt *ztp;
	struct zt_transcode_header *hdr;
	int flags;
	
	ztp = calloc(1,sizeof(struct ast_translator_pvt));
	if(!ztp) {
		ast_log(LOG_ERROR, "Could not allocate ztp structure.\n");
		return NULL;
	}
	ztp->dstfmt = 0;
	ztp->srcfmt = 3;

	if ((fd = open("/dev/zap/transcode", O_RDWR)) < 0)
		return NULL;
	flags = fcntl(fd, F_GETFL);
	if (flags > - 1) {
		if (fcntl(fd, F_SETFL, flags | O_NONBLOCK))
			ast_log(LOG_WARNING, "Could not set non-block mode!\n");
	}
	

	if ((hdr = mmap(NULL, sizeof(*hdr), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
		ast_log(LOG_ERROR, "Memory Map failed for transcoding (%s)\n", strerror(errno));
		close(fd);

		return NULL;
	}

	if (hdr->magic != ZT_TRANSCODE_MAGIC) {
		ast_log(LOG_ERROR, "Transcoder header (%08x) wasn't magic.  Abandoning\n", hdr->magic);
		munmap(hdr, sizeof(*hdr));
		close(fd);

		return NULL;
	}
	
	hdr->srcfmt = (1 << ztp->srcfmt);
	hdr->dstfmt = (1 << ztp->dstfmt);
	if (ioctl(fd, ZT_TRANSCODE_OP, &x)) {
		ast_log(LOG_ERROR, "Unable to attach transcoder: %s\n", strerror(errno));
		munmap(hdr, sizeof(*hdr));
		close(fd);

		return NULL;
	}

	ztp->fd = fd;
	ztp->hdr = hdr;

	return (struct ast_translator_pvt *) ztp;
}

static struct ast_translator_pvt *zap_new_ulawtog723(void)
{
	/* Request translation through zap if possible */
	int fd;
	unsigned int x = ZT_TCOP_ALLOCATE;
	struct ast_translator_pvt *ztp;
	struct zt_transcode_header *hdr;
	int flags;
	
	ztp = calloc(1,sizeof(struct ast_translator_pvt));
	if(!ztp) {
		ast_log(LOG_ERROR, "Could not allocate ztp structure.\n");
		return NULL;
	}
	ztp->dstfmt = 0;
	ztp->srcfmt = 2;

	if ((fd = open("/dev/zap/transcode", O_RDWR)) < 0)
		return NULL;
	flags = fcntl(fd, F_GETFL);
	if (flags > - 1) {
		if (fcntl(fd, F_SETFL, flags | O_NONBLOCK))
			ast_log(LOG_WARNING, "Could not set non-block mode!\n");
	}
	

	if ((hdr = mmap(NULL, sizeof(*hdr), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
		ast_log(LOG_ERROR, "Memory Map failed for transcoding (%s)\n", strerror(errno));
		close(fd);

		return NULL;
	}

	if (hdr->magic != ZT_TRANSCODE_MAGIC) {
		ast_log(LOG_ERROR, "Transcoder header (%08x) wasn't magic.  Abandoning\n", hdr->magic);
		munmap(hdr, sizeof(*hdr));
		close(fd);

		return NULL;
	}
	
	hdr->srcfmt = (1 << ztp->srcfmt);
	hdr->dstfmt = (1 << ztp->dstfmt);
	if (ioctl(fd, ZT_TRANSCODE_OP, &x)) {
		ast_log(LOG_ERROR, "Unable to attach transcoder: %s\n", strerror(errno));
		munmap(hdr, sizeof(*hdr));
		close(fd);

		return NULL;
	}

	ztp->fd = fd;
	ztp->hdr = hdr;

	return (struct ast_translator_pvt *) ztp;
}

static struct ast_translator_pvt *zap_new_g723toalaw(void)
{
	/* Request translation through zap if possible */
	int fd;
	unsigned int x = ZT_TCOP_ALLOCATE;
	struct ast_translator_pvt *ztp;
	struct zt_transcode_header *hdr;
	int flags;
	
	ztp = calloc(1,sizeof(struct ast_translator_pvt));
	if(!ztp) {
		ast_log(LOG_ERROR, "Could not allocate ztp structure.\n");
		return NULL;
	}
	ztp->dstfmt = 3;
	ztp->srcfmt = 0;

	if ((fd = open("/dev/zap/transcode", O_RDWR)) < 0)
		return NULL;
	flags = fcntl(fd, F_GETFL);
	if (flags > - 1) {
		if (fcntl(fd, F_SETFL, flags | O_NONBLOCK))
			ast_log(LOG_WARNING, "Could not set non-block mode!\n");
	}
	

	if ((hdr = mmap(NULL, sizeof(*hdr), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
		ast_log(LOG_ERROR, "Memory Map failed for transcoding (%s)\n", strerror(errno));
		close(fd);

		return NULL;
	}

	if (hdr->magic != ZT_TRANSCODE_MAGIC) {
		ast_log(LOG_ERROR, "Transcoder header (%08x) wasn't magic.  Abandoning\n", hdr->magic);
		munmap(hdr, sizeof(*hdr));
		close(fd);

		return NULL;
	}
	
	hdr->srcfmt = (1 << ztp->srcfmt);
	hdr->dstfmt = (1 << ztp->dstfmt);
	if (ioctl(fd, ZT_TRANSCODE_OP, &x)) {
		ast_log(LOG_ERROR, "Unable to attach transcoder: %s\n", strerror(errno));
		munmap(hdr, sizeof(*hdr));
		close(fd);

		return NULL;
	}

	ztp->fd = fd;
	ztp->hdr = hdr;

	return (struct ast_translator_pvt *) ztp;
}

static struct ast_translator_pvt *zap_new_g723toulaw(void)
{
	/* Request translation through zap if possible */
	int fd;
	unsigned int x = ZT_TCOP_ALLOCATE;
	struct ast_translator_pvt *ztp;
	struct zt_transcode_header *hdr;
	int flags;
	
	ztp = calloc(1,sizeof(struct ast_translator_pvt));
	if(!ztp) {
		ast_log(LOG_ERROR, "Could not allocate ztp structure.\n");
		return NULL;
	}
	ztp->dstfmt = 2;
	ztp->srcfmt = 0;

	if ((fd = open("/dev/zap/transcode", O_RDWR)) < 0)
		return NULL;
	flags = fcntl(fd, F_GETFL);
	if (flags > - 1) {
		if (fcntl(fd, F_SETFL, flags | O_NONBLOCK))
			ast_log(LOG_WARNING, "Could not set non-block mode!\n");
	}
	

	if ((hdr = mmap(NULL, sizeof(*hdr), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
		ast_log(LOG_ERROR, "Memory Map failed for transcoding (%s)\n", strerror(errno));
		close(fd);

		return NULL;
	}

	if (hdr->magic != ZT_TRANSCODE_MAGIC) {
		ast_log(LOG_ERROR, "Transcoder header (%08x) wasn't magic.  Abandoning\n", hdr->magic);
		munmap(hdr, sizeof(*hdr));
		close(fd);

		return NULL;
	}
	
	hdr->srcfmt = (1 << ztp->srcfmt);
	hdr->dstfmt = (1 << ztp->dstfmt);
	if (ioctl(fd, ZT_TRANSCODE_OP, &x)) {
		ast_log(LOG_ERROR, "Unable to attach transcoder: %s\n", strerror(errno));
		munmap(hdr, sizeof(*hdr));
		close(fd);

		return NULL;
	}

	ztp->fd = fd;
	ztp->hdr = hdr;

	return (struct ast_translator_pvt *) ztp;
}

static struct ast_translator_pvt *zap_new_alawtog729(void)
{
	/* Request translation through zap if possible */
	int fd;
	unsigned int x = ZT_TCOP_ALLOCATE;
	struct ast_translator_pvt *ztp;
	struct zt_transcode_header *hdr;
	int flags;
	
	ztp = calloc(1,sizeof(struct ast_translator_pvt));
	if(!ztp) {
		ast_log(LOG_ERROR, "Could not allocate ztp structure.\n");
		return NULL;
	}
	ztp->dstfmt = 8;
	ztp->srcfmt = 3;

	if ((fd = open("/dev/zap/transcode", O_RDWR)) < 0)
		return NULL;
	flags = fcntl(fd, F_GETFL);
	if (flags > - 1) {
		if (fcntl(fd, F_SETFL, flags | O_NONBLOCK))
			ast_log(LOG_WARNING, "Could not set non-block mode!\n");
	}
	

	if ((hdr = mmap(NULL, sizeof(*hdr), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
		ast_log(LOG_ERROR, "Memory Map failed for transcoding (%s)\n", strerror(errno));
		close(fd);

		return NULL;
	}

	if (hdr->magic != ZT_TRANSCODE_MAGIC) {
		ast_log(LOG_ERROR, "Transcoder header (%08x) wasn't magic.  Abandoning\n", hdr->magic);
		munmap(hdr, sizeof(*hdr));
		close(fd);

		return NULL;
	}
	
	hdr->srcfmt = (1 << ztp->srcfmt);
	hdr->dstfmt = (1 << ztp->dstfmt);
	if (ioctl(fd, ZT_TRANSCODE_OP, &x)) {
		ast_log(LOG_ERROR, "Unable to attach transcoder: %s\n", strerror(errno));
		munmap(hdr, sizeof(*hdr));
		close(fd);

		return NULL;
	}

	ztp->fd = fd;
	ztp->hdr = hdr;

	return (struct ast_translator_pvt *) ztp;
}

static struct ast_translator_pvt *zap_new_ulawtog729(void)
{
	/* Request translation through zap if possible */
	int fd;
	unsigned int x = ZT_TCOP_ALLOCATE;
	struct ast_translator_pvt *ztp;
	struct zt_transcode_header *hdr;
	int flags;
	
	ztp = calloc(1,sizeof(struct ast_translator_pvt));
	if(!ztp) {
		ast_log(LOG_ERROR, "Could not allocate ztp structure.\n");
		return NULL;
	}
	ztp->dstfmt = 8;
	ztp->srcfmt = 2;

	if ((fd = open("/dev/zap/transcode", O_RDWR)) < 0)
		return NULL;
	flags = fcntl(fd, F_GETFL);
	if (flags > - 1) {
		if (fcntl(fd, F_SETFL, flags | O_NONBLOCK))
			ast_log(LOG_WARNING, "Could not set non-block mode!\n");
	}
	

	if ((hdr = mmap(NULL, sizeof(*hdr), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
		ast_log(LOG_ERROR, "Memory Map failed for transcoding (%s)\n", strerror(errno));
		close(fd);

		return NULL;
	}

	if (hdr->magic != ZT_TRANSCODE_MAGIC) {
		ast_log(LOG_ERROR, "Transcoder header (%08x) wasn't magic.  Abandoning\n", hdr->magic);
		munmap(hdr, sizeof(*hdr));
		close(fd);

		return NULL;
	}
	
	hdr->srcfmt = (1 << ztp->srcfmt);
	hdr->dstfmt = (1 << ztp->dstfmt);
	if (ioctl(fd, ZT_TRANSCODE_OP, &x)) {
		ast_log(LOG_ERROR, "Unable to attach transcoder: %s\n", strerror(errno));
		munmap(hdr, sizeof(*hdr));
		close(fd);

		return NULL;
	}

	ztp->fd = fd;
	ztp->hdr = hdr;

	return (struct ast_translator_pvt *) ztp;
}

static struct ast_translator_pvt *zap_new_g729toalaw(void)
{
	/* Request translation through zap if possible */
	int fd;
	unsigned int x = ZT_TCOP_ALLOCATE;
	struct ast_translator_pvt *ztp;
	struct zt_transcode_header *hdr;
	int flags;
	
	ztp = calloc(1,sizeof(struct ast_translator_pvt));
	if(!ztp) {
		ast_log(LOG_ERROR, "Could not allocate ztp structure.\n");
		return NULL;
	}
	ztp->dstfmt = 3;
	ztp->srcfmt = 8;

	if ((fd = open("/dev/zap/transcode", O_RDWR)) < 0)
		return NULL;
	flags = fcntl(fd, F_GETFL);
	if (flags > - 1) {
		if (fcntl(fd, F_SETFL, flags | O_NONBLOCK))
			ast_log(LOG_WARNING, "Could not set non-block mode!\n");
	}
	

	if ((hdr = mmap(NULL, sizeof(*hdr), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
		ast_log(LOG_ERROR, "Memory Map failed for transcoding (%s)\n", strerror(errno));
		close(fd);

		return NULL;
	}

	if (hdr->magic != ZT_TRANSCODE_MAGIC) {
		ast_log(LOG_ERROR, "Transcoder header (%08x) wasn't magic.  Abandoning\n", hdr->magic);
		munmap(hdr, sizeof(*hdr));
		close(fd);

		return NULL;
	}
	
	hdr->srcfmt = (1 << ztp->srcfmt);
	hdr->dstfmt = (1 << ztp->dstfmt);
	if (ioctl(fd, ZT_TRANSCODE_OP, &x)) {
		ast_log(LOG_ERROR, "Unable to attach transcoder: %s\n", strerror(errno));
		munmap(hdr, sizeof(*hdr));
		close(fd);

		return NULL;
	}

	ztp->fd = fd;
	ztp->hdr = hdr;

	return (struct ast_translator_pvt *) ztp;
}

static struct ast_translator_pvt *zap_new_g729toulaw(void)
{
	/* Request translation through zap if possible */
	int fd;
	unsigned int x = ZT_TCOP_ALLOCATE;
	struct ast_translator_pvt *ztp;
	struct zt_transcode_header *hdr;
	int flags;
	
	ztp = calloc(1,sizeof(struct ast_translator_pvt));
	if(!ztp) {
		ast_log(LOG_ERROR, "Could not allocate ztp structure.\n");
		return NULL;
	}
	ztp->dstfmt = 2;
	ztp->srcfmt = 8;

	if ((fd = open("/dev/zap/transcode", O_RDWR)) < 0)
		return NULL;
	flags = fcntl(fd, F_GETFL);
	if (flags > - 1) {
		if (fcntl(fd, F_SETFL, flags | O_NONBLOCK))
			ast_log(LOG_WARNING, "Could not set non-block mode!\n");
	}
	

	if ((hdr = mmap(NULL, sizeof(*hdr), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
		ast_log(LOG_ERROR, "Memory Map failed for transcoding (%s)\n", strerror(errno));
		close(fd);

		return NULL;
	}

	if (hdr->magic != ZT_TRANSCODE_MAGIC) {
		ast_log(LOG_ERROR, "Transcoder header (%08x) wasn't magic.  Abandoning\n", hdr->magic);
		munmap(hdr, sizeof(*hdr));
		close(fd);

		return NULL;
	}
	
	hdr->srcfmt = (1 << ztp->srcfmt);
	hdr->dstfmt = (1 << ztp->dstfmt);
	if (ioctl(fd, ZT_TRANSCODE_OP, &x)) {
		ast_log(LOG_ERROR, "Unable to attach transcoder: %s\n", strerror(errno));
		munmap(hdr, sizeof(*hdr));
		close(fd);

		return NULL;
	}

	ztp->fd = fd;
	ztp->hdr = hdr;

	return (struct ast_translator_pvt *) ztp;
}

static struct ast_frame *fakesrc_sample(void)
{
	/* Don't bother really trying to test hardware ones. */
	static struct ast_frame f = {
		.frametype = AST_FRAME_VOICE,
		.samples = 240,
		.src = __PRETTY_FUNCTION__
	};

	return &f;
}

static int register_translator(int dst, int src)
{
	struct translator *zt;
	int res;

	if (!(zt = calloc(1, sizeof(*zt)))) {
		ast_log(LOG_ERROR, "Could not allocate translator\n");
		return -1;
	}
	if (!((cardsmode == 1 && (dst == 8 || src == 8)) || (cardsmode == 2 && (dst == 0 || src == 0)) || (cardsmode == 0)))
		return -1;
	snprintf((char *) (zt->t.name), sizeof(zt->t.name), "zap%sto%s", 
		 ast_getformatname((1 << src)), ast_getformatname((1 << dst)));

	if(src == 8 && dst == 2) /* G.729 to Ulaw */
		zt->t.newpvt = zap_new_g729toulaw;
	else if(src == 8 && dst == 3) /* G.729 to Alaw */
		zt->t.newpvt = zap_new_g729toalaw;
	else if(src == 2 && dst == 8) /* Ulaw to G.729 */
		zt->t.newpvt = zap_new_ulawtog729;
	else if(src == 3 && dst == 8) /* Alaw to G.729 */
		zt->t.newpvt = zap_new_alawtog729;
	else if(src == 0 && dst == 2) /* G.723 to Ulaw */
		zt->t.newpvt = zap_new_g723toulaw;
	else if(src == 0 && dst == 3) /* G.723 to Alaw */
		zt->t.newpvt = zap_new_g723toalaw;
	else if(src == 2 && dst == 0) /* Ulaw to G.723 */
		zt->t.newpvt = zap_new_ulawtog723;
	else if(src == 3 && dst == 0) /* Alaw to G.723 */
		zt->t.newpvt = zap_new_alawtog723;

	zt->t.frameout = zap_frameout;
	zt->t.destroy = zap_destroy;
	zt->t.sample = fakesrc_sample;
	zt->t.framein = zap_framein;
	zt->t.srcfmt = (1 << src);
	zt->t.dstfmt = (1 << dst);
	if ((res = ast_register_translator(&zt->t))) {
		free(zt);
		return -1;
	}

	AST_LIST_LOCK(&zap_translators);
	AST_LIST_INSERT_HEAD(&zap_translators, zt, entry);
	AST_LIST_UNLOCK(&zap_translators);

	global_format_map.map[dst][src] = 1;

	return res;
}

static void drop_translator(int dst, int src)
{
	struct translator *cur;

	AST_LIST_LOCK(&zap_translators);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&zap_translators, cur, entry) {
		if (cur->t.srcfmt != src)
			continue;

		if (cur->t.dstfmt != dst)
			continue;

		AST_LIST_REMOVE_CURRENT(&zap_translators, entry);
		ast_unregister_translator(&cur->t);
		free(cur);
		global_format_map.map[dst][src] = 0;
		break;
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&zap_translators);
}

static void unregister_translators(void)
{
	struct translator *cur;

	AST_LIST_LOCK(&zap_translators);
	while ((cur = AST_LIST_REMOVE_HEAD(&zap_translators, entry))) {
		ast_unregister_translator(&cur->t);
		free(cur);
	}
	AST_LIST_UNLOCK(&zap_translators);
}

static void parse_config(void)
{
	struct ast_variable *var;
	struct ast_config *cfg = ast_config_load("codecs.conf");
	cardsmode = 0;

	if (!cfg)
		return;

	for (var = ast_variable_browse(cfg, "plc"); var; var = var->next) {
		if (!strcasecmp(var->name, "genericplc")) {
			global_useplc = ast_true(var->value);
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "codec_zap: %susing generic PLC\n", global_useplc ? "" : "not ");
		}
	}
	for (var = ast_variable_browse(cfg, "transcoder_card"); var; var = var->next) {
		if (!strcasecmp(var->name, "mode")) {
			if(strstr(var->value, "g729"))
				cardsmode = 1;
			else if(strstr(var->value, "g723"))
				cardsmode = 2;
			else if(strstr(var->value, "mixed"))
				cardsmode = 0;
		}
	}

	ast_config_destroy(cfg);
}

static int build_translators(struct format_map *map, unsigned int dstfmts, unsigned int srcfmts)
{
	unsigned int src, dst;

	for (src = 0; src < 32; src++) {
		for (dst = 0; dst < 32; dst++) {
			if (!(srcfmts & (1<< src)))
				continue;

			if (!(dstfmts & (1 << dst)))
				continue;

			if (global_format_map.map[dst][src])
				continue;

			if (!register_translator(dst, src))
				map->map[dst][src] = 1;
			else
				return 0;
		}
	}
	return 1;
}

static int find_transcoders(void)
{
	struct zt_transcode_info info = { 0, };
	struct format_map map = { { { 0 } } };
	int fd, res;
	unsigned int x, y;

	info.op = ZT_TCOP_GETINFO;
	if ((fd = open("/dev/zap/transcode", O_RDWR)) < 0) {
		ast_log(LOG_NOTICE, "No Zaptel transcoder support!\n");
		return 0;
	}
	for (info.tcnum = 0; !(res = ioctl(fd, ZT_TRANSCODE_OP, &info)); info.tcnum++) {
		if (option_verbose > 1)
			ast_verbose(VERBOSE_PREFIX_2 "Found transcoder '%s'.\n", info.name);

		if(build_translators(&map, info.dstfmts, info.srcfmts)) {
			ast_mutex_lock(&channelcount);
			totalchannels += info.numchannels;
			ast_mutex_unlock(&channelcount);
		}
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
	ast_mutex_lock(&channelcount);
	totalchannels = totalchannels/2;
	ast_mutex_unlock(&channelcount);
	return 0;
}

int reload(void)
{
	parse_config();
	find_transcoders();

	return 0;
}

int unload_module(void)
{
	if(!localusecnt)
		return -1;
	unregister_translators();
	ast_cli_unregister_multiple(transcoder_cli, sizeof(transcoder_cli) / sizeof(struct ast_cli_entry));
	return 0;
}

int load_module(void)
{
	ast_mutex_init(&channelcount);
	parse_config();

	find_transcoders();
	ast_cli_register_multiple(transcoder_cli, sizeof(transcoder_cli) / sizeof(struct ast_cli_entry));
	return 0;
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
