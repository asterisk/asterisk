/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Convenient Application Routines
 * 
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <regex.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/file.h>
#include <asterisk/app.h>
#include <asterisk/dsp.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <asterisk/utils.h>
#include <asterisk/lock.h>
#include <asterisk/indications.h>
#include "asterisk.h"
#include "astconf.h"

#define MAX_OTHER_FORMATS 10


int ast_app_dtget(struct ast_channel *chan, const char *context, char *collect, size_t size, int maxlen, int timeout) 
{
	struct tone_zone_sound *ts;
	int res=0, x=0;

	if(!timeout && chan->pbx)
		timeout = chan->pbx->dtimeout;
	else if(!timeout)
		timeout = 5;
	
	ts = ast_get_indication_tone(chan->zone,"dial");
    if (ts && ts->data[0]) {
        res = ast_playtones_start(chan, 0, ts->data, 0);
	} else 
		ast_log(LOG_NOTICE,"Huh....? no dial for indications?\n");
	
	memset(collect, 0, size);
	for (x=0; strlen(collect) < size; ) {
		res = ast_waitfordigit(chan, timeout);
		if (!ast_ignore_pattern(context, collect))
			ast_playtones_stop(chan);
		if (res < 1)
			break;
		collect[x++] = res;
		if (!ast_matchmore_extension(chan, context, collect, 1, chan->cid.cid_num)) {
			if (collect[x-1] == '#') {
				/* Not a valid extension, ending in #, assume the # was to finish dialing */
				collect[x-1] = '\0';
			}
			break;
		}
	}
	if (res >= 0) {
		if (ast_exists_extension(chan, context, collect, 1, chan->cid.cid_num))
			res = 1;
		else
			res = 0;
	}
	return res;
}



/* set timeout to 0 for "standard" timeouts. Set timeout to -1 for 
   "ludicrous time" (essentially never times out) */
int ast_app_getdata(struct ast_channel *c, char *prompt, char *s, int maxlen, int timeout)
{
	int res,to,fto;
	/* XXX Merge with full version? XXX */
	if (maxlen)
		s[0] = '\0';
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

static int (*ast_has_voicemail_func)(const char *mailbox, const char *folder) = NULL;
static int (*ast_messagecount_func)(const char *mailbox, int *newmsgs, int *oldmsgs) = NULL;

void ast_install_vm_functions(int (*has_voicemail_func)(const char *mailbox, const char *folder),
			      int (*messagecount_func)(const char *mailbox, int *newmsgs, int *oldmsgs))
{
	ast_has_voicemail_func = has_voicemail_func;
	ast_messagecount_func = messagecount_func;
}

void ast_uninstall_vm_functions(void)
{
	ast_has_voicemail_func = NULL;
	ast_messagecount_func = NULL;
}

int ast_app_has_voicemail(const char *mailbox, const char *folder)
{
	static int warned = 0;
	if (ast_has_voicemail_func)
		return ast_has_voicemail_func(mailbox, folder);

	if ((option_verbose > 2) && !warned) {
		ast_verbose(VERBOSE_PREFIX_3 "Message check requested for mailbox %s/folder %s but voicemail not loaded.", mailbox, folder ? folder : "INBOX");
		warned++;
	}
	return 0;
}


int ast_app_messagecount(const char *mailbox, int *newmsgs, int *oldmsgs)
{
	static int warned = 0;
	if (newmsgs)
		*newmsgs = 0;
	if (oldmsgs)
		*oldmsgs = 0;
	if (ast_messagecount_func)
		return ast_messagecount_func(mailbox, newmsgs, oldmsgs);

	if (!warned && (option_verbose > 2)) {
		warned++;
		ast_verbose(VERBOSE_PREFIX_3 "Message count requested for mailbox %s but voicemail not loaded.", mailbox);
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

struct linear_state {
	int fd;
	int autoclose;
	int allowoverride;
	int origwfmt;
};

static void linear_release(struct ast_channel *chan, void *params)
{
	struct linear_state *ls = params;
	if (ls->origwfmt && ast_set_write_format(chan, ls->origwfmt)) {
		ast_log(LOG_WARNING, "Unable to restore channel '%s' to format '%d'\n", chan->name, ls->origwfmt);
	}
	if (ls->autoclose)
		close(ls->fd);
	free(params);
}

static int linear_generator(struct ast_channel *chan, void *data, int len, int samples)
{
	struct ast_frame f;
	short buf[2048 + AST_FRIENDLY_OFFSET / 2];
	struct linear_state *ls = data;
	int res;
	len = samples * 2;
	if (len > sizeof(buf) - AST_FRIENDLY_OFFSET) {
		ast_log(LOG_WARNING, "Can't generate %d bytes of data!\n" ,len);
		len = sizeof(buf) - AST_FRIENDLY_OFFSET;
	}
	memset(&f, 0, sizeof(f));
	res = read(ls->fd, buf + AST_FRIENDLY_OFFSET/2, len);
	if (res > 0) {
		f.frametype = AST_FRAME_VOICE;
		f.subclass = AST_FORMAT_SLINEAR;
		f.data = buf + AST_FRIENDLY_OFFSET/2;
		f.datalen = res;
		f.samples = res / 2;
		f.offset = AST_FRIENDLY_OFFSET;
		ast_write(chan, &f);
		if (res == len)
			return 0;
	}
	return -1;
}

static void *linear_alloc(struct ast_channel *chan, void *params)
{
	struct linear_state *ls;
	/* In this case, params is already malloc'd */
	if (params) {
		ls = params;
		if (ls->allowoverride)
			ast_set_flag(chan, AST_FLAG_WRITE_INT);
		else
			ast_clear_flag(chan, AST_FLAG_WRITE_INT);
		ls->origwfmt = chan->writeformat;
		if (ast_set_write_format(chan, AST_FORMAT_SLINEAR)) {
			ast_log(LOG_WARNING, "Unable to set '%s' to linear format (write)\n", chan->name);
			free(ls);
			ls = params = NULL;
		}
	}
	return params;
}

static struct ast_generator linearstream = 
{
	alloc: linear_alloc,
	release: linear_release,
	generate: linear_generator,
};

int ast_linear_stream(struct ast_channel *chan, const char *filename, int fd, int allowoverride)
{
	struct linear_state *lin;
	char tmpf[256] = "";
	int res = -1;
	int autoclose = 0;
	if (fd < 0) {
		if (!filename || ast_strlen_zero(filename))
			return -1;
		autoclose = 1;
		if (filename[0] == '/') 
			strncpy(tmpf, filename, sizeof(tmpf) - 1);
		else
			snprintf(tmpf, sizeof(tmpf), "%s/%s/%s", (char *)ast_config_AST_VAR_DIR, "sounds", filename);
		fd = open(tmpf, O_RDONLY);
		if (fd < 0){
			ast_log(LOG_WARNING, "Unable to open file '%s': %s\n", tmpf, strerror(errno));
			return -1;
		}
	}
	lin = malloc(sizeof(struct linear_state));
	if (lin) {
		memset(lin, 0, sizeof(lin));
		lin->fd = fd;
		lin->allowoverride = allowoverride;
		lin->autoclose = autoclose;
		res = ast_activate_generator(chan, &linearstream, lin);
	}
	return res;
}

int ast_control_streamfile(struct ast_channel *chan, const char *file, const char *fwd, const char *rev, const char *stop, const char *pause, int skipms) 
{
	struct timeval started, ended;
	long elapsed = 0,last_elapsed =0;
	char *breaks=NULL;
	char *end=NULL;
	int blen=2;
	int res=0;

	if (stop)
		blen += strlen(stop);
	if (pause)
		blen += strlen(pause);

	if (blen > 2) {
		breaks = alloca(blen + 1);
		breaks[0] = '\0';
		strcat(breaks, stop);
		strcat(breaks, pause);
	}
	if (chan->_state != AST_STATE_UP)
		res = ast_answer(chan);

	if (chan)
		ast_stopstream(chan);


	if (file) {
		if ((end = strchr(file,':'))) {
			if (!strcasecmp(end, ":end")) {
				*end = '\0';
				end++;
			}
		}
	}

	for (;;) {
		gettimeofday(&started,NULL);

		if (chan)
			ast_stopstream(chan);
		res = ast_streamfile(chan, file, chan->language);
		if (!res) {
			if (end) {
				ast_seekstream(chan->stream, 0, SEEK_END);
				end=NULL;
			}
			res = 1;
			if (elapsed) {
				ast_stream_fastforward(chan->stream, elapsed);
				last_elapsed = elapsed - 200;
			}
			if (res)
				res = ast_waitstream_fr(chan, breaks, fwd, rev, skipms);
			else
				break;
		}

		if (res < 1)
			break;

		if (pause != NULL && strchr(pause, res)) {
			gettimeofday(&ended, NULL);
			elapsed = (((ended.tv_sec * 1000) + ended.tv_usec / 1000) - ((started.tv_sec * 1000) + started.tv_usec / 1000) + last_elapsed);
			for(;;) {
				if (chan)
					ast_stopstream(chan);
				res = ast_waitfordigit(chan, 1000);
				if (res == 0)
					continue;
				else if (res == -1 || strchr(pause, res) || (stop && strchr(stop, res)))
					break;
			}
			if (res == *pause) {
				res = 0;
				continue;
			}
		}
		if (res == -1)
			break;

		/* if we get one of our stop chars, return it to the calling function */
		if (stop && strchr(stop, res)) {
			/* res = 0; */
			break;
		}
	}
	if (chan)
		ast_stopstream(chan);

	return res;
}

int ast_play_and_wait(struct ast_channel *chan, const char *fn)
{
	int d;
	d = ast_streamfile(chan, fn, chan->language);
	if (d)
		return d;
	d = ast_waitstream(chan, AST_DIGIT_ANY);
	ast_stopstream(chan);
	return d;
}

static int global_silence_threshold = 128;
static int global_maxsilence = 0;

int ast_play_and_record(struct ast_channel *chan, const char *playfile, const char *recordfile, int maxtime, const char *fmt, int *duration, int silencethreshold, int maxsilence, const char *path)
{
	char d, *fmts;
	char comment[256];
	int x, fmtcnt=1, res=-1,outmsg=0;
	struct ast_frame *f;
	struct ast_filestream *others[MAX_OTHER_FORMATS];
	char *sfmt[MAX_OTHER_FORMATS];
	char *stringp=NULL;
	time_t start, end;
	struct ast_dsp *sildet=NULL;   	/* silence detector dsp */
	int totalsilence = 0;
	int dspsilence = 0;
	int gotsilence = 0;		/* did we timeout for silence? */
	int rfmt=0;

	if (silencethreshold < 0)
		silencethreshold = global_silence_threshold;

	if (maxsilence < 0)
		maxsilence = global_maxsilence;

	/* barf if no pointer passed to store duration in */
	if (duration == NULL) {
		ast_log(LOG_WARNING, "Error play_and_record called without duration pointer\n");
		return -1;
	}

	ast_log(LOG_DEBUG,"play_and_record: %s, %s, '%s'\n", playfile ? playfile : "<None>", recordfile, fmt);
	snprintf(comment,sizeof(comment),"Playing %s, Recording to: %s on %s\n", playfile ? playfile : "<None>", recordfile, chan->name);

	if (playfile) {
		d = ast_play_and_wait(chan, playfile);
		if (d > -1)
			d = ast_streamfile(chan, "beep",chan->language);
		if (!d)
			d = ast_waitstream(chan,"");
		if (d < 0)
			return -1;
	}

	fmts = ast_strdupa(fmt);

	stringp=fmts;
	strsep(&stringp, "|");
	ast_log(LOG_DEBUG,"Recording Formats: sfmts=%s\n", fmts);
	sfmt[0] = ast_strdupa(fmts);

	while((fmt = strsep(&stringp, "|"))) {
		if (fmtcnt > MAX_OTHER_FORMATS - 1) {
			ast_log(LOG_WARNING, "Please increase MAX_OTHER_FORMATS in app_voicemail.c\n");
			break;
		}
		sfmt[fmtcnt++] = ast_strdupa(fmt);
	}

	time(&start);
	end=start;  /* pre-initialize end to be same as start in case we never get into loop */
	for (x=0;x<fmtcnt;x++) {
		others[x] = ast_writefile(recordfile, sfmt[x], comment, O_TRUNC, 0, 0700);
		ast_verbose( VERBOSE_PREFIX_3 "x=%i, open writing:  %s format: %s, %p\n", x, recordfile, sfmt[x], others[x]);

		if (!others[x]) {
			break;
		}
	}

	if (path)
		ast_unlock_path(path);


	
	if (maxsilence > 0) {
		sildet = ast_dsp_new(); /* Create the silence detector */
		if (!sildet) {
			ast_log(LOG_WARNING, "Unable to create silence detector :(\n");
			return -1;
		}
		ast_dsp_set_threshold(sildet, silencethreshold);
		rfmt = chan->readformat;
		res = ast_set_read_format(chan, AST_FORMAT_SLINEAR);
		if (res < 0) {
			ast_log(LOG_WARNING, "Unable to set to linear mode, giving up\n");
			ast_dsp_free(sildet);
			return -1;
		}
	}

	if (x == fmtcnt) {
	/* Loop forever, writing the packets we read to the writer(s), until
	   we read a # or get a hangup */
		f = NULL;
		for(;;) {
		 	res = ast_waitfor(chan, 2000);
			if (!res) {
				ast_log(LOG_DEBUG, "One waitfor failed, trying another\n");
				/* Try one more time in case of masq */
			 	res = ast_waitfor(chan, 2000);
				if (!res) {
					ast_log(LOG_WARNING, "No audio available on %s??\n", chan->name);
					res = -1;
				}
			}

			if (res < 0) {
				f = NULL;
				break;
			}
			f = ast_read(chan);
			if (!f)
				break;
			if (f->frametype == AST_FRAME_VOICE) {
				/* write each format */
				for (x=0;x<fmtcnt;x++) {
					res = ast_writestream(others[x], f);
				}

				/* Silence Detection */
				if (maxsilence > 0) {
					dspsilence = 0;
					ast_dsp_silence(sildet, f, &dspsilence);
					if (dspsilence)
						totalsilence = dspsilence;
					else
						totalsilence = 0;

					if (totalsilence > maxsilence) {
						/* Ended happily with silence */
						if (option_verbose > 2)
							ast_verbose( VERBOSE_PREFIX_3 "Recording automatically stopped after a silence of %d seconds\n", totalsilence/1000);
						ast_frfree(f);
						gotsilence = 1;
						outmsg=2;
						break;
					}
				}
				/* Exit on any error */
				if (res) {
					ast_log(LOG_WARNING, "Error writing frame\n");
					ast_frfree(f);
					break;
				}
			} else if (f->frametype == AST_FRAME_VIDEO) {
				/* Write only once */
				ast_writestream(others[0], f);
			} else if (f->frametype == AST_FRAME_DTMF) {
				if (f->subclass == '#') {
					if (option_verbose > 2)
						ast_verbose( VERBOSE_PREFIX_3 "User ended message by pressing %c\n", f->subclass);
					res = '#';
					outmsg = 2;
					ast_frfree(f);
					break;
				}
			}
				if (f->subclass == '0') {
				/* Check for a '0' during message recording also, in case caller wants operator */
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "User cancelled by pressing %c\n", f->subclass);
					res = '0';
					outmsg = 0;
					ast_frfree(f);
					break;
				}
			if (maxtime) {
				time(&end);
				if (maxtime < (end - start)) {
					if (option_verbose > 2)
						ast_verbose( VERBOSE_PREFIX_3 "Took too long, cutting it short...\n");
					outmsg = 2;
					res = 't';
					ast_frfree(f);
					break;
				}
			}
			ast_frfree(f);
		}
		if (end == start) time(&end);
		if (!f) {
			if (option_verbose > 2)
				ast_verbose( VERBOSE_PREFIX_3 "User hung up\n");
			res = -1;
			outmsg=1;
		}
	} else {
		ast_log(LOG_WARNING, "Error creating writestream '%s', format '%s'\n", recordfile, sfmt[x]);
	}

	*duration = end - start;

	for (x=0;x<fmtcnt;x++) {
		if (!others[x])
			break;
		if (res > 0) {
			if (totalsilence)
				ast_stream_rewind(others[x], totalsilence-200);
			else
				ast_stream_rewind(others[x], 200);
		}
		ast_truncstream(others[x]);
		ast_closestream(others[x]);
	}
	if (rfmt) {
		if (ast_set_read_format(chan, rfmt)) {
			ast_log(LOG_WARNING, "Unable to restore format %s to channel '%s'\n", ast_getformatname(rfmt), chan->name);
		}
	}
	if (outmsg > 1) {
		/* Let them know recording is stopped */
		if(!ast_streamfile(chan, "auth-thankyou", chan->language))
			ast_waitstream(chan, "");
	}
	if (sildet)
		ast_dsp_free(sildet);
	return res;
}

int ast_play_and_prepend(struct ast_channel *chan, char *playfile, char *recordfile, int maxtime, char *fmt, int *duration, int beep, int silencethreshold, int maxsilence)
{
	char d = 0, *fmts;
	char comment[256];
	int x, fmtcnt=1, res=-1,outmsg=0;
	struct ast_frame *f;
	struct ast_filestream *others[MAX_OTHER_FORMATS];
	struct ast_filestream *realfiles[MAX_OTHER_FORMATS];
	char *sfmt[MAX_OTHER_FORMATS];
	char *stringp=NULL;
	time_t start, end;
	struct ast_dsp *sildet;   	/* silence detector dsp */
	int totalsilence = 0;
	int dspsilence = 0;
	int gotsilence = 0;		/* did we timeout for silence? */
	int rfmt=0;	
	char prependfile[80];
	
	if (silencethreshold < 0)
		silencethreshold = global_silence_threshold;

	if (maxsilence < 0)
		maxsilence = global_maxsilence;

	/* barf if no pointer passed to store duration in */
	if (duration == NULL) {
		ast_log(LOG_WARNING, "Error play_and_prepend called without duration pointer\n");
		return -1;
	}

	ast_log(LOG_DEBUG,"play_and_prepend: %s, %s, '%s'\n", playfile ? playfile : "<None>", recordfile, fmt);
	snprintf(comment,sizeof(comment),"Playing %s, Recording to: %s on %s\n", playfile ? playfile : "<None>", recordfile, chan->name);

	if (playfile || beep) {	
		if (!beep)
			d = ast_play_and_wait(chan, playfile);
		if (d > -1)
			d = ast_streamfile(chan, "beep",chan->language);
		if (!d)
			d = ast_waitstream(chan,"");
		if (d < 0)
			return -1;
	}
	strncpy(prependfile, recordfile, sizeof(prependfile) -1);	
	strncat(prependfile, "-prepend", sizeof(prependfile) - strlen(prependfile) - 1);
			
	fmts = ast_strdupa(fmt);
	
	stringp=fmts;
	strsep(&stringp, "|");
	ast_log(LOG_DEBUG,"Recording Formats: sfmts=%s\n", fmts);	
	sfmt[0] = ast_strdupa(fmts);
	
	while((fmt = strsep(&stringp, "|"))) {
		if (fmtcnt > MAX_OTHER_FORMATS - 1) {
			ast_log(LOG_WARNING, "Please increase MAX_OTHER_FORMATS in app_voicemail.c\n");
			break;
		}
		sfmt[fmtcnt++] = ast_strdupa(fmt);
	}

	time(&start);
	end=start;  /* pre-initialize end to be same as start in case we never get into loop */
	for (x=0;x<fmtcnt;x++) {
		others[x] = ast_writefile(prependfile, sfmt[x], comment, O_TRUNC, 0, 0700);
		ast_verbose( VERBOSE_PREFIX_3 "x=%i, open writing:  %s format: %s, %p\n", x, prependfile, sfmt[x], others[x]);
		if (!others[x]) {
			break;
		}
	}
	
	sildet = ast_dsp_new(); /* Create the silence detector */
	if (!sildet) {
		ast_log(LOG_WARNING, "Unable to create silence detector :(\n");
		return -1;
	}
	ast_dsp_set_threshold(sildet, silencethreshold);

	if (maxsilence > 0) {
		rfmt = chan->readformat;
		res = ast_set_read_format(chan, AST_FORMAT_SLINEAR);
		if (res < 0) {
			ast_log(LOG_WARNING, "Unable to set to linear mode, giving up\n");
			return -1;
		}
	}
						
	if (x == fmtcnt) {
	/* Loop forever, writing the packets we read to the writer(s), until
	   we read a # or get a hangup */
		f = NULL;
		for(;;) {
		 	res = ast_waitfor(chan, 2000);
			if (!res) {
				ast_log(LOG_DEBUG, "One waitfor failed, trying another\n");
				/* Try one more time in case of masq */
			 	res = ast_waitfor(chan, 2000);
				if (!res) {
					ast_log(LOG_WARNING, "No audio available on %s??\n", chan->name);
					res = -1;
				}
			}
			
			if (res < 0) {
				f = NULL;
				break;
			}
			f = ast_read(chan);
			if (!f)
				break;
			if (f->frametype == AST_FRAME_VOICE) {
				/* write each format */
				for (x=0;x<fmtcnt;x++) {
					if (!others[x])
						break;
					res = ast_writestream(others[x], f);
				}
				
				/* Silence Detection */
				if (maxsilence > 0) {
					dspsilence = 0;
					ast_dsp_silence(sildet, f, &dspsilence);
					if (dspsilence)
						totalsilence = dspsilence;
					else
						totalsilence = 0;
					
					if (totalsilence > maxsilence) {
					/* Ended happily with silence */
					if (option_verbose > 2) 
						ast_verbose( VERBOSE_PREFIX_3 "Recording automatically stopped after a silence of %d seconds\n", totalsilence/1000);
					ast_frfree(f);
					gotsilence = 1;
					outmsg=2;
					break;
					}
				}
				/* Exit on any error */
				if (res) {
					ast_log(LOG_WARNING, "Error writing frame\n");
					ast_frfree(f);
					break;
				}
			} else if (f->frametype == AST_FRAME_VIDEO) {
				/* Write only once */
				ast_writestream(others[0], f);
			} else if (f->frametype == AST_FRAME_DTMF) {
				/* stop recording with any digit */
				if (option_verbose > 2) 
					ast_verbose( VERBOSE_PREFIX_3 "User ended message by pressing %c\n", f->subclass);
				res = 't';
				outmsg = 2;
				ast_frfree(f);
				break;
			}
			if (maxtime) {
				time(&end);
				if (maxtime < (end - start)) {
					if (option_verbose > 2)
						ast_verbose( VERBOSE_PREFIX_3 "Took too long, cutting it short...\n");
					res = 't';
					outmsg=2;
					ast_frfree(f);
					break;
				}
			}
			ast_frfree(f);
		}
		if (end == start) time(&end);
		if (!f) {
			if (option_verbose > 2) 
				ast_verbose( VERBOSE_PREFIX_3 "User hung up\n");
			res = -1;
			outmsg=1;
#if 0
			/* delete all the prepend files */
			for (x=0;x<fmtcnt;x++) {
				if (!others[x])
					break;
				ast_closestream(others[x]);
				ast_filedelete(prependfile, sfmt[x]);
			}
#endif
		}
	} else {
		ast_log(LOG_WARNING, "Error creating writestream '%s', format '%s'\n", prependfile, sfmt[x]); 
	}
	*duration = end - start;
#if 0
	if (outmsg > 1) {
#else
	if (outmsg) {
#endif
		struct ast_frame *fr;
		for (x=0;x<fmtcnt;x++) {
			snprintf(comment, sizeof(comment), "Opening the real file %s.%s\n", recordfile, sfmt[x]);
			realfiles[x] = ast_readfile(recordfile, sfmt[x], comment, O_RDONLY, 0, 0);
			if (!others[x] || !realfiles[x])
				break;
			if (totalsilence)
				ast_stream_rewind(others[x], totalsilence-200);
			else
				ast_stream_rewind(others[x], 200);
			ast_truncstream(others[x]);
			/* add the original file too */
			while ((fr = ast_readframe(realfiles[x]))) {
				ast_writestream(others[x],fr);
			}
			ast_closestream(others[x]);
			ast_closestream(realfiles[x]);
			ast_filerename(prependfile, recordfile, sfmt[x]);
#if 0
			ast_verbose("Recording Format: sfmts=%s, prependfile %s, recordfile %s\n", sfmt[x],prependfile,recordfile);
#endif
			ast_filedelete(prependfile, sfmt[x]);
		}
	}
	if (rfmt) {
		if (ast_set_read_format(chan, rfmt)) {
			ast_log(LOG_WARNING, "Unable to restore format %s to channel '%s'\n", ast_getformatname(rfmt), chan->name);
		}
	}
	if (outmsg) {
		if (outmsg > 1) {
			/* Let them know it worked */
			ast_streamfile(chan, "auth-thankyou", chan->language);
			ast_waitstream(chan, "");
		}
	}	
	return res;
}

/* Channel group core functions */

int ast_app_group_split_group(char *data, char *group, int group_max, char *category, int category_max)
{
	int res=0;
	char tmp[256] = "";
	char *grp=NULL, *cat=NULL;

	if (data && !ast_strlen_zero(data)) {
		strncpy(tmp, data, sizeof(tmp) - 1);
		grp = tmp;
		cat = strchr(tmp, '@');
		if (cat) {
			*cat = '\0';
			cat++;
		}
	}

	if (grp && !ast_strlen_zero(grp))
		strncpy(group, grp, group_max -1);
	else
		res = -1;

	if (cat)
		snprintf(category, category_max, "%s_%s", GROUP_CATEGORY_PREFIX, cat);
	else
		strncpy(category, GROUP_CATEGORY_PREFIX, category_max - 1);

	return res;
}

int ast_app_group_set_channel(struct ast_channel *chan, char *data)
{
	int res=0;
	char group[80] = "";
	char category[80] = "";

	if (!ast_app_group_split_group(data, group, sizeof(group), category, sizeof(category))) {
		pbx_builtin_setvar_helper(chan, category, group);
	} else
		res = -1;

	return res;
}

int ast_app_group_get_count(char *group, char *category)
{
	struct ast_channel *chan;
	int count = 0;
	char *test;
	char cat[80] = "";

	if (category && !ast_strlen_zero(category)) {
		strncpy(cat, category, sizeof(cat) - 1);
	} else {
		strncpy(cat, GROUP_CATEGORY_PREFIX, sizeof(cat) - 1);
	}

	if (group && !ast_strlen_zero(group)) {
		chan = ast_channel_walk_locked(NULL);
		while(chan) {
			test = pbx_builtin_getvar_helper(chan, cat);
			if (test && !strcasecmp(test, group))
				count++;
			ast_mutex_unlock(&chan->lock);
			chan = ast_channel_walk_locked(chan);
		}
	}

	return count;
}

int ast_app_group_match_get_count(char *groupmatch, char *category)
{
	regex_t regexbuf;
	struct ast_channel *chan;
	int count = 0;
	char *test;
	char cat[80] = "";

	if (!groupmatch || ast_strlen_zero(groupmatch))
		return 0;

	/* if regex compilation fails, return zero matches */
	if (regcomp(&regexbuf, groupmatch, REG_EXTENDED | REG_NOSUB))
		return 0;

	if (category && !ast_strlen_zero(category)) {
		strncpy(cat, category, sizeof(cat) - 1);
	} else {
		strncpy(cat, GROUP_CATEGORY_PREFIX, sizeof(cat) - 1);
	}

	chan = ast_channel_walk_locked(NULL);
	while(chan) {
		test = pbx_builtin_getvar_helper(chan, cat);
		if (test && !regexec(&regexbuf, test, 0, NULL, 0))
			count++;
		ast_mutex_unlock(&chan->lock);
		chan = ast_channel_walk_locked(chan);
	}

	regfree(&regexbuf);

	return count;
}

int ast_seperate_app_args(char *buf, char delim, char **array, int arraylen)
{
	int x = 0;
	memset(array, 0, arraylen * sizeof(char *));
	if (!buf)
		return 0;
	for (array[x] = buf ; x < arraylen && array[x]; x++) {
		if ((array[x+1] = strchr(array[x], delim))) {
			*array[x+1] = '\0';
			array[x+1]++;
		}
	}
	return x;
}

int ast_lock_path(const char *path)
{
	char *s;
	char *fs;
	int res;
	int fd;
	time_t start;
	s = alloca(strlen(path) + 10);
	fs = alloca(strlen(path) + 20);
	if (!fs || !s) {
		ast_log(LOG_WARNING, "Out of memory!\n");
		return -1;
	}
	snprintf(fs, strlen(path) + 19, "%s/%s-%08x", path, ".lock", rand());
	fd = open(fs, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (fd < 0) {
		fprintf(stderr, "Unable to create lock file: %s\n", strerror(errno));
		return -1;
	}
	close(fd);
	snprintf(s, strlen(path) + 9, "%s/%s", path, ".lock");
	time(&start);
	while (((res = link(fs, s)) < 0) && (errno == EEXIST) && (time(NULL) - start < 5))
		usleep(1);
	if (res < 0) {
		ast_log(LOG_WARNING, "Failed to lock path '%s': %s\n", path, strerror(errno));
	}
	unlink(fs);
	ast_log(LOG_DEBUG, "Locked path '%s'\n", path);
	return res;
}

int ast_unlock_path(const char *path)
{
	char *s;
	s = alloca(strlen(path) + 10);
	if (!s)
		return -1;
	snprintf(s, strlen(path) + 9, "%s/%s", path, ".lock");
	ast_log(LOG_DEBUG, "Unlocked path '%s'\n", path);
	return unlink(s);
}

int ast_record_review(struct ast_channel *chan, const char *playfile, const char *recordfile, int maxtime, const char *fmt, int *duration, const char *path) 
{
	int silencethreshold = 128; 
	int maxsilence=0;
	int res = 0;
	int cmd = 0;
	int max_attempts = 3;
	int attempts = 0;
	int recorded = 0;
	int message_exists = 0;
	/* Note that urgent and private are for flagging messages as such in the future */

	/* barf if no pointer passed to store duration in */
	if (duration == NULL) {
		ast_log(LOG_WARNING, "Error ast_record_review called without duration pointer\n");
		return -1;
	}

	cmd = '3';	 /* Want to start by recording */

	while ((cmd >= 0) && (cmd != 't')) {
		switch (cmd) {
		case '1':
			if (!message_exists) {
				/* In this case, 1 is to record a message */
				cmd = '3';
				break;
			} else {
				ast_streamfile(chan, "vm-msgsaved", chan->language);
				ast_waitstream(chan, "");
				cmd = 't';
				return res;
			}
		case '2':
			/* Review */
			ast_verbose(VERBOSE_PREFIX_3 "Reviewing the recording\n");
			ast_streamfile(chan, recordfile, chan->language);
			cmd = ast_waitstream(chan, AST_DIGIT_ANY);
			break;
		case '3':
			message_exists = 0;
			/* Record */
			if (recorded == 1)
				ast_verbose(VERBOSE_PREFIX_3 "Re-recording\n");
			else	
				ast_verbose(VERBOSE_PREFIX_3 "Recording\n");
			recorded = 1;
			cmd = ast_play_and_record(chan, playfile, recordfile, maxtime, fmt, duration, silencethreshold, maxsilence, path);
			if (cmd == -1) {
			/* User has hung up, no options to give */
				return cmd;
			}
			if (cmd == '0') {
				break;
			} else if (cmd == '*') {
				break;
			} 
			else {
				/* If all is well, a message exists */
				message_exists = 1;
				cmd = 0;
			}
			break;
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
		case '*':
		case '#':
			cmd = ast_play_and_wait(chan, "vm-sorry");
			break;
		default:
			if (message_exists) {
				cmd = ast_play_and_wait(chan, "vm-review");
			}
			else {
				cmd = ast_play_and_wait(chan, "vm-torerecord");
				if (!cmd)
					cmd = ast_waitfordigit(chan, 600);
			}
			
			if (!cmd)
				cmd = ast_waitfordigit(chan, 6000);
			if (!cmd) {
				attempts++;
			}
			if (attempts > max_attempts) {
				cmd = 't';
			}
		}
	}
	if (cmd == 't')
		cmd = 0;
	return cmd;
}
