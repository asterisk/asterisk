/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Channel Management
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/file.h>
#include <asterisk/app.h>
#include <asterisk/dsp.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <asterisk/utils.h>
#include "asterisk.h"
#include "astconf.h"

/* set timeout to 0 for "standard" timeouts. Set timeout to -1 for 
   "ludicrous time" (essentially never times out) */
int ast_app_getdata(struct ast_channel *c, char *prompt, char *s, int maxlen, int timeout)
{
	int res,to,fto;
	/* XXX Merge with full version? XXX */
	if (prompt) {
		res = ast_streamfile(c, prompt, c->language);
		if (res < 0)
			return res;
	}
	fto = c->pbx ? c->pbx->rtimeout * 1000 : 6000;
	to = c->pbx ? c->pbx->dtimeout * 1000 : 2000;

	if (timeout > 0) fto = to = timeout;
	if (timeout < 0) fto = to = 1000000000;
	res = ast_readstring(c, s, maxlen, to, fto, "#");
	return res;
}


int ast_app_getdata_full(struct ast_channel *c, char *prompt, char *s, int maxlen, int timeout, int audiofd, int ctrlfd)
{
	int res,to,fto;
	if (prompt) {
		res = ast_streamfile(c, prompt, c->language);
		if (res < 0)
			return res;
	}
	fto = 6000;
	to = 2000;
	if (timeout > 0) fto = to = timeout;
	if (timeout < 0) fto = to = 1000000000;
	res = ast_readstring_full(c, s, maxlen, to, fto, "#", audiofd, ctrlfd);
	return res;
}

int ast_app_getvoice(struct ast_channel *c, char *dest, char *dstfmt, char *prompt, int silence, int maxsec)
{
	int res;
	struct ast_filestream *writer;
	int rfmt;
	int totalms=0, total;
	
	struct ast_frame *f;
	struct ast_dsp *sildet;
	/* Play prompt if requested */
	if (prompt) {
		res = ast_streamfile(c, prompt, c->language);
		if (res < 0)
			return res;
		res = ast_waitstream(c,"");
		if (res < 0)
			return res;
	}
	rfmt = c->readformat;
	res = ast_set_read_format(c, AST_FORMAT_SLINEAR);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set to linear mode, giving up\n");
		return -1;
	}
	sildet = ast_dsp_new();
	if (!sildet) {
		ast_log(LOG_WARNING, "Unable to create silence detector :(\n");
		return -1;
	}
	writer = ast_writefile(dest, dstfmt, "Voice file", 0, 0, 0666);
	if (!writer) {
		ast_log(LOG_WARNING, "Unable to open file '%s' in format '%s' for writing\n", dest, dstfmt);
		ast_dsp_free(sildet);
		return -1;
	}
	for(;;) {
		if ((res = ast_waitfor(c, 2000)) < 0) {
			ast_log(LOG_NOTICE, "Waitfor failed while recording file '%s' format '%s'\n", dest, dstfmt);
			break;
		}
		if (res) {
			f = ast_read(c);
			if (!f) {
				ast_log(LOG_NOTICE, "Hungup while recording file '%s' format '%s'\n", dest, dstfmt);
				break;
			}
			if ((f->frametype == AST_FRAME_DTMF) && (f->subclass == '#')) {
				/* Ended happily with DTMF */
				ast_frfree(f);
				break;
			} else if (f->frametype == AST_FRAME_VOICE) {
				ast_dsp_silence(sildet, f, &total); 
				if (total > silence) {
					/* Ended happily with silence */
					ast_frfree(f);
					break;
				}
				totalms += f->samples / 8;
				if (totalms > maxsec * 1000) {
					/* Ended happily with too much stuff */
					ast_log(LOG_NOTICE, "Constraining voice on '%s' to %d seconds\n", c->name, maxsec);
					ast_frfree(f);
					break;
				}
			}
			ast_frfree(f);
		}
	}
	res = ast_set_read_format(c, rfmt);
	if (res)
		ast_log(LOG_WARNING, "Unable to restore read format on '%s'\n", c->name);
	ast_dsp_free(sildet);
	ast_closestream(writer);
	return 0;
}

int ast_app_has_voicemail(const char *mailbox)
{
	DIR *dir;
	struct dirent *de;
	char fn[256];
	char tmp[256]="";
	char *mb, *cur;
	char *context;
	int ret;
	/* If no mailbox, return immediately */
	if (ast_strlen_zero(mailbox))
		return 0;
	if (strchr(mailbox, ',')) {
		strncpy(tmp, mailbox, sizeof(tmp));
		mb = tmp;
		ret = 0;
		while((cur = strsep(&mb, ","))) {
			if (!ast_strlen_zero(cur)) {
				if (ast_app_has_voicemail(cur))
					return 1; 
			}
		}
		return 0;
	}
	strncpy(tmp, mailbox, sizeof(tmp) - 1);
	context = strchr(tmp, '@');
	if (context) {
		*context = '\0';
		context++;
	} else
		context = "default";
	snprintf(fn, sizeof(fn), "%s/voicemail/%s/%s/INBOX", (char *)ast_config_AST_SPOOL_DIR, context, tmp);
	dir = opendir(fn);
	if (!dir)
		return 0;
	while ((de = readdir(dir))) {
		if (!strncasecmp(de->d_name, "msg", 3))
			break;
	}
	closedir(dir);
	if (de)
		return 1;
	return 0;
}

int ast_app_messagecount(const char *mailbox, int *newmsgs, int *oldmsgs)
{
	DIR *dir;
	struct dirent *de;
	char fn[256];
	char tmp[256]="";
	char *mb, *cur;
	char *context;
	int ret;
	if (newmsgs)
		*newmsgs = 0;
	if (oldmsgs)
		*oldmsgs = 0;
	/* If no mailbox, return immediately */
	if (ast_strlen_zero(mailbox))
		return 0;
	if (strchr(mailbox, ',')) {
		int tmpnew, tmpold;
		strncpy(tmp, mailbox, sizeof(tmp));
		mb = tmp;
		ret = 0;
		while((cur = strsep(&mb, ", "))) {
			if (!ast_strlen_zero(cur)) {
				if (ast_app_messagecount(cur, newmsgs ? &tmpnew : NULL, oldmsgs ? &tmpold : NULL))
					return -1;
				else {
					if (newmsgs)
						*newmsgs += tmpnew; 
					if (oldmsgs)
						*oldmsgs += tmpold;
				}
			}
		}
		return 0;
	}
	strncpy(tmp, mailbox, sizeof(tmp) - 1);
	context = strchr(tmp, '@');
	if (context) {
		*context = '\0';
		context++;
	} else
		context = "default";
	if (newmsgs) {
		snprintf(fn, sizeof(fn), "%s/voicemail/%s/%s/INBOX", (char *)ast_config_AST_SPOOL_DIR, context, tmp);
		dir = opendir(fn);
		if (dir) {
			while ((de = readdir(dir))) {
				if ((strlen(de->d_name) > 3) && !strncasecmp(de->d_name, "msg", 3) &&
					!strcasecmp(de->d_name + strlen(de->d_name) - 3, "txt"))
						(*newmsgs)++;
					
			}
			closedir(dir);
		}
	}
	if (oldmsgs) {
		snprintf(fn, sizeof(fn), "%s/voicemail/%s/%s/Old", (char *)ast_config_AST_SPOOL_DIR, context, tmp);
		dir = opendir(fn);
		if (dir) {
			while ((de = readdir(dir))) {
				if ((strlen(de->d_name) > 3) && !strncasecmp(de->d_name, "msg", 3) &&
					!strcasecmp(de->d_name + strlen(de->d_name) - 3, "txt"))
						(*oldmsgs)++;
					
			}
			closedir(dir);
		}
	}
	return 0;
}

int ast_dtmf_stream(struct ast_channel *chan,struct ast_channel *peer,char *digits,int between) 
{
	char *ptr=NULL;
	int res=0;
	struct ast_frame f;
	if (!between)
		between = 100;

	if (peer)
		res = ast_autoservice_start(peer);

	if (!res) {
		res = ast_waitfor(chan,100);
		if (res > -1) {
			for (ptr=digits;*ptr;*ptr++) {
				if (*ptr == 'w') {
					res = ast_safe_sleep(chan, 500);
					if (res) 
						break;
					continue;
				}
				memset(&f, 0, sizeof(f));
				f.frametype = AST_FRAME_DTMF;
				f.subclass = *ptr;
				f.src = "ast_dtmf_stream";
				if (strchr("0123456789*#abcdABCD",*ptr)==NULL) {
					ast_log(LOG_WARNING, "Illegal DTMF character '%c' in string. (0-9*#aAbBcCdD allowed)\n",*ptr);
				} else {
					res = ast_write(chan, &f);
					if (res) 
						break;
					/* pause between digits */
					res = ast_safe_sleep(chan,between);
					if (res) 
						break;
				}
			}
		}
		if (peer)
			res = ast_autoservice_stop(peer);
	}
	return res;
}
