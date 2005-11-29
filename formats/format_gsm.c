/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Save to raw, headerless GSM data.
 * \arg File name extension: gsm
 * \ingroup formats
 */
 
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/sched.h"
#include "asterisk/module.h"
#include "asterisk/endian.h"

#include "msgsm.h"

/* Some Ideas for this code came from makegsme.c by Jeffrey Chilton */

/* Portions of the conversion code are by guido@sienanet.it */

/* silent gsm frame */
/* begin binary data: */
char gsm_silence[] = /* 33 */
{0xD8,0x20,0xA2,0xE1,0x5A,0x50,0x00,0x49,0x24,0x92,0x49,0x24,0x50,0x00,0x49
,0x24,0x92,0x49,0x24,0x50,0x00,0x49,0x24,0x92,0x49,0x24,0x50,0x00,0x49,0x24
,0x92,0x49,0x24};
/* end binary data. size = 33 bytes */

struct ast_filestream {
	void *reserved[AST_RESERVED_POINTERS];
	/* Believe it or not, we must decode/recode to account for the
	   weird MS format */
	/* This is what a filestream means to us */
	FILE *f; /* Descriptor */
	struct ast_frame fr;				/* Frame information */
	char waste[AST_FRIENDLY_OFFSET];	/* Buffer for sending frames, etc */
	char empty;							/* Empty character */
	unsigned char gsm[66];				/* Two Real GSM Frames */
};


AST_MUTEX_DEFINE_STATIC(gsm_lock);
static int glistcnt = 0;

static char *name = "gsm";
static char *desc = "Raw GSM data";
static char *exts = "gsm";

static struct ast_filestream *gsm_open(FILE *f)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		memset(tmp, 0, sizeof(struct ast_filestream));
		if (ast_mutex_lock(&gsm_lock)) {
			ast_log(LOG_WARNING, "Unable to lock gsm list\n");
			free(tmp);
			return NULL;
		}
		tmp->f = f;
		tmp->fr.data = tmp->gsm;
		tmp->fr.frametype = AST_FRAME_VOICE;
		tmp->fr.subclass = AST_FORMAT_GSM;
		/* datalen will vary for each frame */
		tmp->fr.src = name;
		tmp->fr.mallocd = 0;
		glistcnt++;
		ast_mutex_unlock(&gsm_lock);
		ast_update_use_count();
	}
	return tmp;
}

static struct ast_filestream *gsm_rewrite(FILE *f, const char *comment)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		memset(tmp, 0, sizeof(struct ast_filestream));
		if (ast_mutex_lock(&gsm_lock)) {
			ast_log(LOG_WARNING, "Unable to lock gsm list\n");
			free(tmp);
			return NULL;
		}
		tmp->f = f;
		glistcnt++;
		ast_mutex_unlock(&gsm_lock);
		ast_update_use_count();
	} else
		ast_log(LOG_WARNING, "Out of memory\n");
	return tmp;
}

static void gsm_close(struct ast_filestream *s)
{
	if (ast_mutex_lock(&gsm_lock)) {
		ast_log(LOG_WARNING, "Unable to lock gsm list\n");
		return;
	}
	glistcnt--;
	ast_mutex_unlock(&gsm_lock);
	ast_update_use_count();
	fclose(s->f);
	free(s);
}

static struct ast_frame *gsm_read(struct ast_filestream *s, int *whennext)
{
	int res;
	s->fr.frametype = AST_FRAME_VOICE;
	s->fr.subclass = AST_FORMAT_GSM;
	s->fr.offset = AST_FRIENDLY_OFFSET;
	s->fr.samples = 160;
	s->fr.datalen = 33;
	s->fr.mallocd = 0;
	s->fr.data = s->gsm;
	if ((res = fread(s->gsm, 1, 33, s->f)) != 33) {
		if (res)
			ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		return NULL;
	}
	*whennext = 160;
	return &s->fr;
}

static int gsm_write(struct ast_filestream *fs, struct ast_frame *f)
{
	int res;
	unsigned char gsm[66];
	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass != AST_FORMAT_GSM) {
		ast_log(LOG_WARNING, "Asked to write non-GSM frame (%d)!\n", f->subclass);
		return -1;
	}
	if (!(f->datalen % 65)) {
		/* This is in MSGSM format, need to be converted */
		int len=0;
		while(len < f->datalen) {
			conv65(f->data + len, gsm);
			if ((res = fwrite(gsm, 1, 66, fs->f)) != 66) {
				ast_log(LOG_WARNING, "Bad write (%d/66): %s\n", res, strerror(errno));
				return -1;
			}
			len += 65;
		}
	} else {
		if (f->datalen % 33) {
			ast_log(LOG_WARNING, "Invalid data length, %d, should be multiple of 33\n", f->datalen);
			return -1;
		}
		if ((res = fwrite(f->data, 1, f->datalen, fs->f)) != f->datalen) {
				ast_log(LOG_WARNING, "Bad write (%d/33): %s\n", res, strerror(errno));
				return -1;
		}
	}
	return 0;
}

static int gsm_seek(struct ast_filestream *fs, long sample_offset, int whence)
{
	off_t offset=0,min,cur,max,distance;
	
	min = 0;
	cur = ftell(fs->f);
	fseek(fs->f, 0, SEEK_END);
	max = ftell(fs->f);
	/* have to fudge to frame here, so not fully to sample */
	distance = (sample_offset/160) * 33;
	if(whence == SEEK_SET)
		offset = distance;
	else if(whence == SEEK_CUR || whence == SEEK_FORCECUR)
		offset = distance + cur;
	else if(whence == SEEK_END)
		offset = max - distance;
	/* Always protect against seeking past the begining. */
	offset = (offset < min)?min:offset;
	if (whence != SEEK_FORCECUR) {
		offset = (offset > max)?max:offset;
	} else if (offset > max) {
		int i;
		fseek(fs->f, 0, SEEK_END);
		for (i=0; i< (offset - max) / 33; i++) {
			fwrite(gsm_silence, 1, 33, fs->f);
		}
	}
	return fseek(fs->f, offset, SEEK_SET);
}

static int gsm_trunc(struct ast_filestream *fs)
{
	return ftruncate(fileno(fs->f), ftell(fs->f));
}

static long gsm_tell(struct ast_filestream *fs)
{
	off_t offset;
	offset = ftell(fs->f);
	return (offset/33)*160;
}

static char *gsm_getcomment(struct ast_filestream *s)
{
	return NULL;
}

int load_module()
{
	return ast_format_register(name, exts, AST_FORMAT_GSM,
								gsm_open,
								gsm_rewrite,
								gsm_write,
								gsm_seek,
								gsm_trunc,
								gsm_tell,
								gsm_read,
								gsm_close,
								gsm_getcomment);
								
								
}

int unload_module()
{
	return ast_format_unregister(name);
}	

int usecount()
{
	return glistcnt;
}

char *description()
{
	return desc;
}


char *key()
{
	return ASTERISK_GPL_KEY;
}
