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
 * \brief Convenient Application Routines
 *
 * \author Mark Spencer <markster@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#include <regex.h>          /* for regcomp(3) */
#include <sys/file.h>       /* for flock(2) */
#include <signal.h>         /* for pthread_sigmask(3) */
#include <stdlib.h>         /* for closefrom(3) */
#include <sys/types.h>
#include <sys/wait.h>       /* for waitpid(2) */
#ifndef HAVE_CLOSEFROM
#include <dirent.h>         /* for opendir(3)   */
#endif
#ifdef HAVE_CAP
#include <sys/capability.h>
#endif /* HAVE_CAP */

#include "asterisk/paths.h"	/* use ast_config_AST_DATA_DIR */
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/file.h"
#include "asterisk/app.h"
#include "asterisk/dsp.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/indications.h"
#include "asterisk/linkedlists.h"
#include "asterisk/threadstorage.h"
#include "asterisk/test.h"

AST_THREADSTORAGE_PUBLIC(ast_str_thread_global_buf);

static pthread_t shaun_of_the_dead_thread = AST_PTHREADT_NULL;

struct zombie {
	pid_t pid;
	AST_LIST_ENTRY(zombie) list;
};

static AST_LIST_HEAD_STATIC(zombies, zombie);

static void *shaun_of_the_dead(void *data)
{
	struct zombie *cur;
	int status;
	for (;;) {
		if (!AST_LIST_EMPTY(&zombies)) {
			/* Don't allow cancellation while we have a lock. */
			pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
			AST_LIST_LOCK(&zombies);
			AST_LIST_TRAVERSE_SAFE_BEGIN(&zombies, cur, list) {
				if (waitpid(cur->pid, &status, WNOHANG) != 0) {
					AST_LIST_REMOVE_CURRENT(list);
					ast_free(cur);
				}
			}
			AST_LIST_TRAVERSE_SAFE_END
			AST_LIST_UNLOCK(&zombies);
			pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		}
		pthread_testcancel();
		/* Wait for 60 seconds, without engaging in a busy loop. */
		ast_poll(NULL, 0, AST_LIST_FIRST(&zombies) ? 5000 : 60000);
	}
	return NULL;
}


#define AST_MAX_FORMATS 10

static AST_RWLIST_HEAD_STATIC(groups, ast_group_info);

/*!
 * \brief This function presents a dialtone and reads an extension into 'collect'
 * which must be a pointer to a **pre-initialized** array of char having a
 * size of 'size' suitable for writing to.  It will collect no more than the smaller
 * of 'maxlen' or 'size' minus the original strlen() of collect digits.
 * \param chan struct.
 * \param context
 * \param collect
 * \param size
 * \param maxlen
 * \param timeout timeout in seconds
 *
 * \return 0 if extension does not exist, 1 if extension exists
*/
int ast_app_dtget(struct ast_channel *chan, const char *context, char *collect, size_t size, int maxlen, int timeout)
{
	struct ast_tone_zone_sound *ts;
	int res = 0, x = 0;

	if (maxlen > size) {
		maxlen = size;
	}

	if (!timeout && chan->pbx) {
		timeout = chan->pbx->dtimeoutms / 1000.0;
	} else if (!timeout) {
		timeout = 5;
	}

	if ((ts = ast_get_indication_tone(chan->zone, "dial"))) {
		res = ast_playtones_start(chan, 0, ts->data, 0);
		ts = ast_tone_zone_sound_unref(ts);
	} else {
		ast_log(LOG_NOTICE, "Huh....? no dial for indications?\n");
	}

	for (x = strlen(collect); x < maxlen; ) {
		res = ast_waitfordigit(chan, timeout);
		if (!ast_ignore_pattern(context, collect)) {
			ast_playtones_stop(chan);
		}
		if (res < 1) {
			break;
		}
		if (res == '#') {
			break;
		}
		collect[x++] = res;
		if (!ast_matchmore_extension(chan, context, collect, 1,
			S_COR(chan->caller.id.number.valid, chan->caller.id.number.str, NULL))) {
			break;
		}
	}

	if (res >= 0) {
		res = ast_exists_extension(chan, context, collect, 1,
			S_COR(chan->caller.id.number.valid, chan->caller.id.number.str, NULL)) ? 1 : 0;
	}

	return res;
}

/*!
 * \brief ast_app_getdata
 * \param c The channel to read from
 * \param prompt The file to stream to the channel
 * \param s The string to read in to.  Must be at least the size of your length
 * \param maxlen How many digits to read (maximum)
 * \param timeout set timeout to 0 for "standard" timeouts. Set timeout to -1 for 
 *      "ludicrous time" (essentially never times out) */
enum ast_getdata_result ast_app_getdata(struct ast_channel *c, const char *prompt, char *s, int maxlen, int timeout)
{
	int res = 0, to, fto;
	char *front, *filename;

	/* XXX Merge with full version? XXX */

	if (maxlen)
		s[0] = '\0';

	if (!prompt)
		prompt = "";

	filename = ast_strdupa(prompt);
	while ((front = strsep(&filename, "&"))) {
		if (!ast_strlen_zero(front)) {
			res = ast_streamfile(c, front, c->language);
			if (res)
				continue;
		}
		if (ast_strlen_zero(filename)) {
			/* set timeouts for the last prompt */
			fto = c->pbx ? c->pbx->rtimeoutms : 6000;
			to = c->pbx ? c->pbx->dtimeoutms : 2000;

			if (timeout > 0) {
				fto = to = timeout;
			}
			if (timeout < 0) {
				fto = to = 1000000000;
			}
		} else {
			/* there is more than one prompt, so
			 * get rid of the long timeout between
			 * prompts, and make it 50ms */
			fto = 50;
			to = c->pbx ? c->pbx->dtimeoutms : 2000;
		}
		res = ast_readstring(c, s, maxlen, to, fto, "#");
		if (res == AST_GETDATA_EMPTY_END_TERMINATED) {
			return res;
		}
		if (!ast_strlen_zero(s)) {
			return res;
		}
	}

	return res;
}

/* The lock type used by ast_lock_path() / ast_unlock_path() */
static enum AST_LOCK_TYPE ast_lock_type = AST_LOCK_TYPE_LOCKFILE;

int ast_app_getdata_full(struct ast_channel *c, const char *prompt, char *s, int maxlen, int timeout, int audiofd, int ctrlfd)
{
	int res, to = 2000, fto = 6000;

	if (!ast_strlen_zero(prompt)) {
		res = ast_streamfile(c, prompt, c->language);
		if (res < 0) {
			return res;
		}
	}

	if (timeout > 0) {
		fto = to = timeout;
	}
	if (timeout < 0) {
		fto = to = 1000000000;
	}

	res = ast_readstring_full(c, s, maxlen, to, fto, "#", audiofd, ctrlfd);

	return res;
}

int ast_app_run_macro(struct ast_channel *autoservice_chan, struct ast_channel *macro_chan, const char * const macro_name, const char * const macro_args)
{
	struct ast_app *macro_app;
	int res;
	char buf[1024];

	macro_app = pbx_findapp("Macro");
	if (!macro_app) {
		ast_log(LOG_WARNING, "Cannot run macro '%s' because the 'Macro' application in not available\n", macro_name);
		return -1;
	}
	snprintf(buf, sizeof(buf), "%s%s%s", macro_name, ast_strlen_zero(macro_args) ? "" : ",", S_OR(macro_args, ""));
	if (autoservice_chan) {
		ast_autoservice_start(autoservice_chan);
	}
	res = pbx_exec(macro_chan, macro_app, buf);
	if (autoservice_chan) {
		ast_autoservice_stop(autoservice_chan);
	}
	return res;
}

static int (*ast_has_voicemail_func)(const char *mailbox, const char *folder) = NULL;
static int (*ast_inboxcount_func)(const char *mailbox, int *newmsgs, int *oldmsgs) = NULL;
static int (*ast_inboxcount2_func)(const char *mailbox, int *urgentmsgs, int *newmsgs, int *oldmsgs) = NULL;
static int (*ast_sayname_func)(struct ast_channel *chan, const char *mailbox, const char *context) = NULL;
static int (*ast_messagecount_func)(const char *context, const char *mailbox, const char *folder) = NULL;

void ast_install_vm_functions(int (*has_voicemail_func)(const char *mailbox, const char *folder),
			      int (*inboxcount_func)(const char *mailbox, int *newmsgs, int *oldmsgs),
			      int (*inboxcount2_func)(const char *mailbox, int *urgentmsgs, int *newmsgs, int *oldmsgs),
			      int (*messagecount_func)(const char *context, const char *mailbox, const char *folder),
			      int (*sayname_func)(struct ast_channel *chan, const char *mailbox, const char *context))
{
	ast_has_voicemail_func = has_voicemail_func;
	ast_inboxcount_func = inboxcount_func;
	ast_inboxcount2_func = inboxcount2_func;
	ast_messagecount_func = messagecount_func;
	ast_sayname_func = sayname_func;
}

void ast_uninstall_vm_functions(void)
{
	ast_has_voicemail_func = NULL;
	ast_inboxcount_func = NULL;
	ast_inboxcount2_func = NULL;
	ast_messagecount_func = NULL;
	ast_sayname_func = NULL;
}

int ast_app_has_voicemail(const char *mailbox, const char *folder)
{
	static int warned = 0;
	if (ast_has_voicemail_func) {
		return ast_has_voicemail_func(mailbox, folder);
	}

	if (warned++ % 10 == 0) {
		ast_verb(3, "Message check requested for mailbox %s/folder %s but voicemail not loaded.\n", mailbox, folder ? folder : "INBOX");
	}
	return 0;
}


int ast_app_inboxcount(const char *mailbox, int *newmsgs, int *oldmsgs)
{
	static int warned = 0;
	if (newmsgs) {
		*newmsgs = 0;
	}
	if (oldmsgs) {
		*oldmsgs = 0;
	}
	if (ast_inboxcount_func) {
		return ast_inboxcount_func(mailbox, newmsgs, oldmsgs);
	}

	if (warned++ % 10 == 0) {
		ast_verb(3, "Message count requested for mailbox %s but voicemail not loaded.\n", mailbox);
	}

	return 0;
}

int ast_app_inboxcount2(const char *mailbox, int *urgentmsgs, int *newmsgs, int *oldmsgs)
{
	static int warned = 0;
	if (newmsgs) {
		*newmsgs = 0;
	}
	if (oldmsgs) {
		*oldmsgs = 0;
	}
	if (urgentmsgs) {
		*urgentmsgs = 0;
	}
	if (ast_inboxcount_func) {
		return ast_inboxcount2_func(mailbox, urgentmsgs, newmsgs, oldmsgs);
	}

	if (warned++ % 10 == 0) {
		ast_verb(3, "Message count requested for mailbox %s but voicemail not loaded.\n", mailbox);
	}

	return 0;
}

int ast_app_sayname(struct ast_channel *chan, const char *mailbox, const char *context)
{
	if (ast_sayname_func) {
		return ast_sayname_func(chan, mailbox, context);
	}
	return -1;
}

int ast_app_messagecount(const char *context, const char *mailbox, const char *folder)
{
	static int warned = 0;
	if (ast_messagecount_func) {
		return ast_messagecount_func(context, mailbox, folder);
	}

	if (!warned) {
		warned++;
		ast_verb(3, "Message count requested for mailbox %s@%s/%s but voicemail not loaded.\n", mailbox, context, folder);
	}

	return 0;
}

int ast_dtmf_stream(struct ast_channel *chan, struct ast_channel *peer, const char *digits, int between, unsigned int duration)
{
	const char *ptr;
	int res = 0;
	struct ast_silence_generator *silgen = NULL;

	if (!between) {
		between = 100;
	}

	if (peer) {
		res = ast_autoservice_start(peer);
	}

	if (!res) {
		res = ast_waitfor(chan, 100);
	}

	/* ast_waitfor will return the number of remaining ms on success */
	if (res < 0) {
		if (peer) {
			ast_autoservice_stop(peer);
		}
		return res;
	}

	if (ast_opt_transmit_silence) {
		silgen = ast_channel_start_silence_generator(chan);
	}

	for (ptr = digits; *ptr; ptr++) {
		if (*ptr == 'w') {
			/* 'w' -- wait half a second */
			if ((res = ast_safe_sleep(chan, 500))) {
				break;
			}
		} else if (strchr("0123456789*#abcdfABCDF", *ptr)) {
			/* Character represents valid DTMF */
			if (*ptr == 'f' || *ptr == 'F') {
				/* ignore return values if not supported by channel */
				ast_indicate(chan, AST_CONTROL_FLASH);
			} else {
				ast_senddigit(chan, *ptr, duration);
			}
			/* pause between digits */
			if ((res = ast_safe_sleep(chan, between))) {
				break;
			}
		} else {
			ast_log(LOG_WARNING, "Illegal DTMF character '%c' in string. (0-9*#aAbBcCdD allowed)\n", *ptr);
		}
	}

	if (peer) {
		/* Stop autoservice on the peer channel, but don't overwrite any error condition
		   that has occurred previously while acting on the primary channel */
		if (ast_autoservice_stop(peer) && !res) {
			res = -1;
		}
	}

	if (silgen) {
		ast_channel_stop_silence_generator(chan, silgen);
	}

	return res;
}

struct linear_state {
	int fd;
	int autoclose;
	int allowoverride;
	struct ast_format origwfmt;
};

static void linear_release(struct ast_channel *chan, void *params)
{
	struct linear_state *ls = params;

	if (ls->origwfmt.id && ast_set_write_format(chan, &ls->origwfmt)) {
		ast_log(LOG_WARNING, "Unable to restore channel '%s' to format '%d'\n", chan->name, ls->origwfmt.id);
	}

	if (ls->autoclose) {
		close(ls->fd);
	}

	ast_free(params);
}

static int linear_generator(struct ast_channel *chan, void *data, int len, int samples)
{
	short buf[2048 + AST_FRIENDLY_OFFSET / 2];
	struct linear_state *ls = data;
	struct ast_frame f = {
		.frametype = AST_FRAME_VOICE,
		.data.ptr = buf + AST_FRIENDLY_OFFSET / 2,
		.offset = AST_FRIENDLY_OFFSET,
	};
	int res;

	ast_format_set(&f.subclass.format, AST_FORMAT_SLINEAR, 0);

	len = samples * 2;
	if (len > sizeof(buf) - AST_FRIENDLY_OFFSET) {
		ast_log(LOG_WARNING, "Can't generate %d bytes of data!\n" , len);
		len = sizeof(buf) - AST_FRIENDLY_OFFSET;
	}
	res = read(ls->fd, buf + AST_FRIENDLY_OFFSET/2, len);
	if (res > 0) {
		f.datalen = res;
		f.samples = res / 2;
		ast_write(chan, &f);
		if (res == len) {
			return 0;
		}
	}
	return -1;
}

static void *linear_alloc(struct ast_channel *chan, void *params)
{
	struct linear_state *ls = params;

	if (!params) {
		return NULL;
	}

	/* In this case, params is already malloc'd */
	if (ls->allowoverride) {
		ast_set_flag(chan, AST_FLAG_WRITE_INT);
	} else {
		ast_clear_flag(chan, AST_FLAG_WRITE_INT);
	}

	ast_format_copy(&ls->origwfmt, &chan->writeformat);

	if (ast_set_write_format_by_id(chan, AST_FORMAT_SLINEAR)) {
		ast_log(LOG_WARNING, "Unable to set '%s' to linear format (write)\n", chan->name);
		ast_free(ls);
		ls = params = NULL;
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
	char tmpf[256];
	int res = -1;
	int autoclose = 0;
	if (fd < 0) {
		if (ast_strlen_zero(filename)) {
			return -1;
		}
		autoclose = 1;
		if (filename[0] == '/') {
			ast_copy_string(tmpf, filename, sizeof(tmpf));
		} else {
			snprintf(tmpf, sizeof(tmpf), "%s/%s/%s", ast_config_AST_DATA_DIR, "sounds", filename);
		}
		if ((fd = open(tmpf, O_RDONLY)) < 0) {
			ast_log(LOG_WARNING, "Unable to open file '%s': %s\n", tmpf, strerror(errno));
			return -1;
		}
	}
	if ((lin = ast_calloc(1, sizeof(*lin)))) {
		lin->fd = fd;
		lin->allowoverride = allowoverride;
		lin->autoclose = autoclose;
		res = ast_activate_generator(chan, &linearstream, lin);
	}
	return res;
}

int ast_control_streamfile(struct ast_channel *chan, const char *file,
			   const char *fwd, const char *rev,
			   const char *stop, const char *suspend,
			   const char *restart, int skipms, long *offsetms)
{
	char *breaks = NULL;
	char *end = NULL;
	int blen = 2;
	int res;
	long pause_restart_point = 0;
	long offset = 0;

	if (offsetms) {
		offset = *offsetms * 8; /* XXX Assumes 8kHz */
	}

	if (stop) {
		blen += strlen(stop);
	}
	if (suspend) {
		blen += strlen(suspend);
	}
	if (restart) {
		blen += strlen(restart);
	}

	if (blen > 2) {
		breaks = alloca(blen + 1);
		breaks[0] = '\0';
		if (stop) {
			strcat(breaks, stop);
		}
		if (suspend) {
			strcat(breaks, suspend);
		}
		if (restart) {
			strcat(breaks, restart);
		}
	}
	if (chan->_state != AST_STATE_UP) {
		res = ast_answer(chan);
	}

	if (file) {
		if ((end = strchr(file, ':'))) {
			if (!strcasecmp(end, ":end")) {
				*end = '\0';
				end++;
			}
		}
	}

	for (;;) {
		ast_stopstream(chan);
		res = ast_streamfile(chan, file, chan->language);
		if (!res) {
			if (pause_restart_point) {
				ast_seekstream(chan->stream, pause_restart_point, SEEK_SET);
				pause_restart_point = 0;
			}
			else if (end || offset < 0) {
				if (offset == -8) {
					offset = 0;
				}
				ast_verb(3, "ControlPlayback seek to offset %ld from end\n", offset);

				ast_seekstream(chan->stream, offset, SEEK_END);
				end = NULL;
				offset = 0;
			} else if (offset) {
				ast_verb(3, "ControlPlayback seek to offset %ld\n", offset);
				ast_seekstream(chan->stream, offset, SEEK_SET);
				offset = 0;
			}
			res = ast_waitstream_fr(chan, breaks, fwd, rev, skipms);
		}

		if (res < 1) {
			break;
		}

		/* We go at next loop if we got the restart char */
		if (restart && strchr(restart, res)) {
			ast_debug(1, "we'll restart the stream here at next loop\n");
			pause_restart_point = 0;
			continue;
		}

		if (suspend && strchr(suspend, res)) {
			pause_restart_point = ast_tellstream(chan->stream);
			for (;;) {
				ast_stopstream(chan);
				if (!(res = ast_waitfordigit(chan, 1000))) {
					continue;
				} else if (res == -1 || strchr(suspend, res) || (stop && strchr(stop, res))) {
					break;
				}
			}
			if (res == *suspend) {
				res = 0;
				continue;
			}
		}

		if (res == -1) {
			break;
		}

		/* if we get one of our stop chars, return it to the calling function */
		if (stop && strchr(stop, res)) {
			break;
		}
	}

	if (pause_restart_point) {
		offset = pause_restart_point;
	} else {
		if (chan->stream) {
			offset = ast_tellstream(chan->stream);
		} else {
			offset = -8;  /* indicate end of file */
		}
	}

	if (offsetms) {
		*offsetms = offset / 8; /* samples --> ms ... XXX Assumes 8 kHz */
	}

	/* If we are returning a digit cast it as char */
	if (res > 0 || chan->stream) {
		res = (char)res;
	}

	ast_stopstream(chan);

	return res;
}

int ast_play_and_wait(struct ast_channel *chan, const char *fn)
{
	int d = 0;

	ast_test_suite_event_notify("PLAYBACK", "Message: %s", fn);
	if ((d = ast_streamfile(chan, fn, chan->language))) {
		return d;
	}

	d = ast_waitstream(chan, AST_DIGIT_ANY);

	ast_stopstream(chan);

	return d;
}

static int global_silence_threshold = 128;
static int global_maxsilence = 0;

/*! Optionally play a sound file or a beep, then record audio and video from the channel.
 * \param chan Channel to playback to/record from.
 * \param playfile Filename of sound to play before recording begins.
 * \param recordfile Filename to record to.
 * \param maxtime Maximum length of recording (in seconds).
 * \param fmt Format(s) to record message in. Multiple formats may be specified by separating them with a '|'.
 * \param duration Where to store actual length of the recorded message (in milliseconds).
 * \param beep Whether to play a beep before starting to record.
 * \param silencethreshold
 * \param maxsilence Length of silence that will end a recording (in milliseconds).
 * \param path Optional filesystem path to unlock.
 * \param prepend If true, prepend the recorded audio to an existing file and follow prepend mode recording rules
 * \param acceptdtmf DTMF digits that will end the recording.
 * \param canceldtmf DTMF digits that will cancel the recording.
 * \param skip_confirmation_sound If true, don't play auth-thankyou at end. Nice for custom recording prompts in apps.
 *
 * \retval -1 failure or hangup
 * \retval 'S' Recording ended from silence timeout
 * \retval 't' Recording ended from the message exceeding the maximum duration, or via DTMF in prepend mode
 * \retval dtmfchar Recording ended via the return value's DTMF character for either cancel or accept.
 */
static int __ast_play_and_record(struct ast_channel *chan, const char *playfile, const char *recordfile, int maxtime, const char *fmt, int *duration, int beep, int silencethreshold, int maxsilence, const char *path, int prepend, const char *acceptdtmf, const char *canceldtmf, int skip_confirmation_sound)
{
	int d = 0;
	char *fmts;
	char comment[256];
	int x, fmtcnt = 1, res = -1, outmsg = 0;
	struct ast_filestream *others[AST_MAX_FORMATS];
	char *sfmt[AST_MAX_FORMATS];
	char *stringp = NULL;
	time_t start, end;
	struct ast_dsp *sildet = NULL;   /* silence detector dsp */
	int totalsilence = 0;
	int dspsilence = 0;
	int olddspsilence = 0;
	struct ast_format rfmt;
	struct ast_silence_generator *silgen = NULL;
	char prependfile[PATH_MAX];

	ast_format_clear(&rfmt);
	if (silencethreshold < 0) {
		silencethreshold = global_silence_threshold;
	}

	if (maxsilence < 0) {
		maxsilence = global_maxsilence;
	}

	/* barf if no pointer passed to store duration in */
	if (!duration) {
		ast_log(LOG_WARNING, "Error play_and_record called without duration pointer\n");
		return -1;
	}

	ast_debug(1, "play_and_record: %s, %s, '%s'\n", playfile ? playfile : "<None>", recordfile, fmt);
	snprintf(comment, sizeof(comment), "Playing %s, Recording to: %s on %s\n", playfile ? playfile : "<None>", recordfile, chan->name);

	if (playfile || beep) {
		if (!beep) {
			d = ast_play_and_wait(chan, playfile);
		}
		if (d > -1) {
			d = ast_stream_and_wait(chan, "beep", "");
		}
		if (d < 0) {
			return -1;
		}
	}

	if (prepend) {
		ast_copy_string(prependfile, recordfile, sizeof(prependfile));
		strncat(prependfile, "-prepend", sizeof(prependfile) - strlen(prependfile) - 1);
	}

	fmts = ast_strdupa(fmt);

	stringp = fmts;
	strsep(&stringp, "|");
	ast_debug(1, "Recording Formats: sfmts=%s\n", fmts);
	sfmt[0] = ast_strdupa(fmts);

	while ((fmt = strsep(&stringp, "|"))) {
		if (fmtcnt > AST_MAX_FORMATS - 1) {
			ast_log(LOG_WARNING, "Please increase AST_MAX_FORMATS in file.h\n");
			break;
		}
		sfmt[fmtcnt++] = ast_strdupa(fmt);
	}

	end = start = time(NULL);  /* pre-initialize end to be same as start in case we never get into loop */
	for (x = 0; x < fmtcnt; x++) {
		others[x] = ast_writefile(prepend ? prependfile : recordfile, sfmt[x], comment, O_TRUNC, 0, AST_FILE_MODE);
		ast_verb(3, "x=%d, open writing:  %s format: %s, %p\n", x, prepend ? prependfile : recordfile, sfmt[x], others[x]);

		if (!others[x]) {
			break;
		}
	}

	if (path) {
		ast_unlock_path(path);
	}

	if (maxsilence > 0) {
		sildet = ast_dsp_new(); /* Create the silence detector */
		if (!sildet) {
			ast_log(LOG_WARNING, "Unable to create silence detector :(\n");
			return -1;
		}
		ast_dsp_set_threshold(sildet, silencethreshold);
		ast_format_copy(&rfmt, &chan->readformat);
		res = ast_set_read_format_by_id(chan, AST_FORMAT_SLINEAR);
		if (res < 0) {
			ast_log(LOG_WARNING, "Unable to set to linear mode, giving up\n");
			ast_dsp_free(sildet);
			return -1;
		}
	}

	if (!prepend) {
		/* Request a video update */
		ast_indicate(chan, AST_CONTROL_VIDUPDATE);

		if (ast_opt_transmit_silence) {
			silgen = ast_channel_start_silence_generator(chan);
		}
	}

	if (x == fmtcnt) {
		/* Loop forever, writing the packets we read to the writer(s), until
		   we read a digit or get a hangup */
		struct ast_frame *f;
		for (;;) {
			if (!(res = ast_waitfor(chan, 2000))) {
				ast_debug(1, "One waitfor failed, trying another\n");
				/* Try one more time in case of masq */
				if (!(res = ast_waitfor(chan, 2000))) {
					ast_log(LOG_WARNING, "No audio available on %s??\n", chan->name);
					res = -1;
				}
			}

			if (res < 0) {
				f = NULL;
				break;
			}
			if (!(f = ast_read(chan))) {
				break;
			}
			if (f->frametype == AST_FRAME_VOICE) {
				/* write each format */
				for (x = 0; x < fmtcnt; x++) {
					if (prepend && !others[x]) {
						break;
					}
					res = ast_writestream(others[x], f);
				}

				/* Silence Detection */
				if (maxsilence > 0) {
					dspsilence = 0;
					ast_dsp_silence(sildet, f, &dspsilence);
					if (olddspsilence > dspsilence) {
						totalsilence += olddspsilence;
					}
					olddspsilence = dspsilence;

					if (dspsilence > maxsilence) {
						/* Ended happily with silence */
						ast_verb(3, "Recording automatically stopped after a silence of %d seconds\n", dspsilence/1000);
						res = 'S';
						outmsg = 2;
						break;
					}
				}
				/* Exit on any error */
				if (res) {
					ast_log(LOG_WARNING, "Error writing frame\n");
					break;
				}
			} else if (f->frametype == AST_FRAME_VIDEO) {
				/* Write only once */
				ast_writestream(others[0], f);
			} else if (f->frametype == AST_FRAME_DTMF) {
				if (prepend) {
				/* stop recording with any digit */
					ast_verb(3, "User ended message by pressing %c\n", f->subclass.integer);
					res = 't';
					outmsg = 2;
					break;
				}
				if (strchr(acceptdtmf, f->subclass.integer)) {
					ast_verb(3, "User ended message by pressing %c\n", f->subclass.integer);
					res = f->subclass.integer;
					outmsg = 2;
					break;
				}
				if (strchr(canceldtmf, f->subclass.integer)) {
					ast_verb(3, "User cancelled message by pressing %c\n", f->subclass.integer);
					res = f->subclass.integer;
					outmsg = 0;
					break;
				}
			}
			if (maxtime) {
				end = time(NULL);
				if (maxtime < (end - start)) {
					ast_verb(3, "Took too long, cutting it short...\n");
					res = 't';
					outmsg = 2;
					break;
				}
			}
			ast_frfree(f);
		}
		if (!f) {
			ast_verb(3, "User hung up\n");
			res = -1;
			outmsg = 1;
		} else {
			ast_frfree(f);
		}
	} else {
		ast_log(LOG_WARNING, "Error creating writestream '%s', format '%s'\n", recordfile, sfmt[x]);
	}

	if (!prepend) {
		if (silgen) {
			ast_channel_stop_silence_generator(chan, silgen);
		}
	}

	/*!\note
	 * Instead of asking how much time passed (end - start), calculate the number
	 * of seconds of audio which actually went into the file.  This fixes a
	 * problem where audio is stopped up on the network and never gets to us.
	 *
	 * Note that we still want to use the number of seconds passed for the max
	 * message, otherwise we could get a situation where this stream is never
	 * closed (which would create a resource leak).
	 */
	*duration = others[0] ? ast_tellstream(others[0]) / 8000 : 0;

	if (!prepend) {
		/* Reduce duration by a total silence amount */
		if (olddspsilence <= dspsilence) {
			totalsilence += dspsilence;
		}

		if (totalsilence > 0) {
			*duration -= (totalsilence - 200) / 1000;
		}
		if (*duration < 0) {
			*duration = 0;
		}
		for (x = 0; x < fmtcnt; x++) {
			if (!others[x]) {
				break;
			}
			/*!\note
			 * If we ended with silence, trim all but the first 200ms of silence
			 * off the recording.  However, if we ended with '#', we don't want
			 * to trim ANY part of the recording.
			 */
			if (res > 0 && dspsilence) {
				/* rewind only the trailing silence */
				ast_stream_rewind(others[x], dspsilence - 200);
			}
			ast_truncstream(others[x]);
			ast_closestream(others[x]);
		}
	}

	if (prepend && outmsg) {
		struct ast_filestream *realfiles[AST_MAX_FORMATS];
		struct ast_frame *fr;

		for (x = 0; x < fmtcnt; x++) {
			snprintf(comment, sizeof(comment), "Opening the real file %s.%s\n", recordfile, sfmt[x]);
			realfiles[x] = ast_readfile(recordfile, sfmt[x], comment, O_RDONLY, 0, 0);
			if (!others[x] || !realfiles[x]) {
				break;
			}
			/*!\note Same logic as above. */
			if (dspsilence) {
				ast_stream_rewind(others[x], dspsilence - 200);
			}
			ast_truncstream(others[x]);
			/* add the original file too */
			while ((fr = ast_readframe(realfiles[x]))) {
				ast_writestream(others[x], fr);
				ast_frfree(fr);
			}
			ast_closestream(others[x]);
			ast_closestream(realfiles[x]);
			ast_filerename(prependfile, recordfile, sfmt[x]);
			ast_verb(4, "Recording Format: sfmts=%s, prependfile %s, recordfile %s\n", sfmt[x], prependfile, recordfile);
			ast_filedelete(prependfile, sfmt[x]);
		}
	}
	if (rfmt.id && ast_set_read_format(chan, &rfmt)) {
		ast_log(LOG_WARNING, "Unable to restore format %s to channel '%s'\n", ast_getformatname(&rfmt), chan->name);
	}
	if ((outmsg == 2) && (!skip_confirmation_sound)) {
		ast_stream_and_wait(chan, "auth-thankyou", "");
	}
	if (sildet) {
		ast_dsp_free(sildet);
	}
	return res;
}

static const char default_acceptdtmf[] = "#";
static const char default_canceldtmf[] = "";

int ast_play_and_record_full(struct ast_channel *chan, const char *playfile, const char *recordfile, int maxtime, const char *fmt, int *duration, int silencethreshold, int maxsilence, const char *path, const char *acceptdtmf, const char *canceldtmf)
{
	return __ast_play_and_record(chan, playfile, recordfile, maxtime, fmt, duration, 0, silencethreshold, maxsilence, path, 0, S_OR(acceptdtmf, default_acceptdtmf), S_OR(canceldtmf, default_canceldtmf), 0);
}

int ast_play_and_record(struct ast_channel *chan, const char *playfile, const char *recordfile, int maxtime, const char *fmt, int *duration, int silencethreshold, int maxsilence, const char *path)
{
	return __ast_play_and_record(chan, playfile, recordfile, maxtime, fmt, duration, 0, silencethreshold, maxsilence, path, 0, default_acceptdtmf, default_canceldtmf, 0);
}

int ast_play_and_prepend(struct ast_channel *chan, char *playfile, char *recordfile, int maxtime, char *fmt, int *duration, int beep, int silencethreshold, int maxsilence)
{
	return __ast_play_and_record(chan, playfile, recordfile, maxtime, fmt, duration, beep, silencethreshold, maxsilence, NULL, 1, default_acceptdtmf, default_canceldtmf, 1);
}

/* Channel group core functions */

int ast_app_group_split_group(const char *data, char *group, int group_max, char *category, int category_max)
{
	int res = 0;
	char tmp[256];
	char *grp = NULL, *cat = NULL;

	if (!ast_strlen_zero(data)) {
		ast_copy_string(tmp, data, sizeof(tmp));
		grp = tmp;
		if ((cat = strchr(tmp, '@'))) {
			*cat++ = '\0';
		}
	}

	if (!ast_strlen_zero(grp)) {
		ast_copy_string(group, grp, group_max);
	} else {
		*group = '\0';
	}

	if (!ast_strlen_zero(cat)) {
		ast_copy_string(category, cat, category_max);
	}

	return res;
}

int ast_app_group_set_channel(struct ast_channel *chan, const char *data)
{
	int res = 0;
	char group[80] = "", category[80] = "";
	struct ast_group_info *gi = NULL;
	size_t len = 0;

	if (ast_app_group_split_group(data, group, sizeof(group), category, sizeof(category))) {
		return -1;
	}

	/* Calculate memory we will need if this is new */
	len = sizeof(*gi) + strlen(group) + 1;
	if (!ast_strlen_zero(category)) {
		len += strlen(category) + 1;
	}

	AST_RWLIST_WRLOCK(&groups);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&groups, gi, group_list) {
		if ((gi->chan == chan) && ((ast_strlen_zero(category) && ast_strlen_zero(gi->category)) || (!ast_strlen_zero(gi->category) && !strcasecmp(gi->category, category)))) {
			AST_RWLIST_REMOVE_CURRENT(group_list);
			free(gi);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;

	if (ast_strlen_zero(group)) {
		/* Enable unsetting the group */
	} else if ((gi = calloc(1, len))) {
		gi->chan = chan;
		gi->group = (char *) gi + sizeof(*gi);
		strcpy(gi->group, group);
		if (!ast_strlen_zero(category)) {
			gi->category = (char *) gi + sizeof(*gi) + strlen(group) + 1;
			strcpy(gi->category, category);
		}
		AST_RWLIST_INSERT_TAIL(&groups, gi, group_list);
	} else {
		res = -1;
	}

	AST_RWLIST_UNLOCK(&groups);

	return res;
}

int ast_app_group_get_count(const char *group, const char *category)
{
	struct ast_group_info *gi = NULL;
	int count = 0;

	if (ast_strlen_zero(group)) {
		return 0;
	}

	AST_RWLIST_RDLOCK(&groups);
	AST_RWLIST_TRAVERSE(&groups, gi, group_list) {
		if (!strcasecmp(gi->group, group) && (ast_strlen_zero(category) || (!ast_strlen_zero(gi->category) && !strcasecmp(gi->category, category)))) {
			count++;
		}
	}
	AST_RWLIST_UNLOCK(&groups);

	return count;
}

int ast_app_group_match_get_count(const char *groupmatch, const char *category)
{
	struct ast_group_info *gi = NULL;
	regex_t regexbuf_group;
	regex_t regexbuf_category;
	int count = 0;

	if (ast_strlen_zero(groupmatch)) {
		ast_log(LOG_NOTICE, "groupmatch empty\n");
		return 0;
	}

	/* if regex compilation fails, return zero matches */
	if (regcomp(&regexbuf_group, groupmatch, REG_EXTENDED | REG_NOSUB)) {
		ast_log(LOG_ERROR, "Regex compile failed on: %s\n", groupmatch);
		return 0;
	}

	if (!ast_strlen_zero(category) && regcomp(&regexbuf_category, category, REG_EXTENDED | REG_NOSUB)) {
		ast_log(LOG_ERROR, "Regex compile failed on: %s\n", category);
		return 0;
	}

	AST_RWLIST_RDLOCK(&groups);
	AST_RWLIST_TRAVERSE(&groups, gi, group_list) {
		if (!regexec(&regexbuf_group, gi->group, 0, NULL, 0) && (ast_strlen_zero(category) || (!ast_strlen_zero(gi->category) && !regexec(&regexbuf_category, gi->category, 0, NULL, 0)))) {
			count++;
		}
	}
	AST_RWLIST_UNLOCK(&groups);

	regfree(&regexbuf_group);
	if (!ast_strlen_zero(category)) {
		regfree(&regexbuf_category);
	}

	return count;
}

int ast_app_group_update(struct ast_channel *old, struct ast_channel *new)
{
	struct ast_group_info *gi = NULL;

	AST_RWLIST_WRLOCK(&groups);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&groups, gi, group_list) {
		if (gi->chan == old) {
			gi->chan = new;
		} else if (gi->chan == new) {
			AST_RWLIST_REMOVE_CURRENT(group_list);
			ast_free(gi);
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	AST_RWLIST_UNLOCK(&groups);

	return 0;
}

int ast_app_group_discard(struct ast_channel *chan)
{
	struct ast_group_info *gi = NULL;

	AST_RWLIST_WRLOCK(&groups);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&groups, gi, group_list) {
		if (gi->chan == chan) {
			AST_RWLIST_REMOVE_CURRENT(group_list);
			ast_free(gi);
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	AST_RWLIST_UNLOCK(&groups);

	return 0;
}

int ast_app_group_list_wrlock(void)
{
	return AST_RWLIST_WRLOCK(&groups);
}

int ast_app_group_list_rdlock(void)
{
	return AST_RWLIST_RDLOCK(&groups);
}

struct ast_group_info *ast_app_group_list_head(void)
{
	return AST_RWLIST_FIRST(&groups);
}

int ast_app_group_list_unlock(void)
{
	return AST_RWLIST_UNLOCK(&groups);
}

#undef ast_app_separate_args
unsigned int ast_app_separate_args(char *buf, char delim, char **array, int arraylen);

unsigned int __ast_app_separate_args(char *buf, char delim, int remove_chars, char **array, int arraylen)
{
	int argc;
	char *scan, *wasdelim = NULL;
	int paren = 0, quote = 0, bracket = 0;

	if (!array || !arraylen) {
		return 0;
	}

	memset(array, 0, arraylen * sizeof(*array));

	if (!buf) {
		return 0;
	}

	scan = buf;

	for (argc = 0; *scan && (argc < arraylen - 1); argc++) {
		array[argc] = scan;
		for (; *scan; scan++) {
			if (*scan == '(') {
				paren++;
			} else if (*scan == ')') {
				if (paren) {
					paren--;
				}
			} else if (*scan == '[') {
				bracket++;
			} else if (*scan == ']') {
				if (bracket) {
					bracket--;
				}
			} else if (*scan == '"' && delim != '"') {
				quote = quote ? 0 : 1;
				if (remove_chars) {
					/* Remove quote character from argument */
					memmove(scan, scan + 1, strlen(scan));
					scan--;
				}
			} else if (*scan == '\\') {
				if (remove_chars) {
					/* Literal character, don't parse */
					memmove(scan, scan + 1, strlen(scan));
				} else {
					scan++;
				}
			} else if ((*scan == delim) && !paren && !quote && !bracket) {
				wasdelim = scan;
				*scan++ = '\0';
				break;
			}
		}
	}

	/* If the last character in the original string was the delimiter, then
	 * there is one additional argument. */
	if (*scan || (scan > buf && (scan - 1) == wasdelim)) {
		array[argc++] = scan;
	}

	return argc;
}

/* ABI compatible function */
unsigned int ast_app_separate_args(char *buf, char delim, char **array, int arraylen)
{
	return __ast_app_separate_args(buf, delim, 1, array, arraylen);
}

static enum AST_LOCK_RESULT ast_lock_path_lockfile(const char *path)
{
	char *s;
	char *fs;
	int res;
	int fd;
	int lp = strlen(path);
	time_t start;

	s = alloca(lp + 10);
	fs = alloca(lp + 20);

	snprintf(fs, strlen(path) + 19, "%s/.lock-%08lx", path, ast_random());
	fd = open(fs, O_WRONLY | O_CREAT | O_EXCL, AST_FILE_MODE);
	if (fd < 0) {
		ast_log(LOG_ERROR, "Unable to create lock file '%s': %s\n", path, strerror(errno));
		return AST_LOCK_PATH_NOT_FOUND;
	}
	close(fd);

	snprintf(s, strlen(path) + 9, "%s/.lock", path);
	start = time(NULL);
	while (((res = link(fs, s)) < 0) && (errno == EEXIST) && (time(NULL) - start < 5)) {
		sched_yield();
	}

	unlink(fs);

	if (res) {
		ast_log(LOG_WARNING, "Failed to lock path '%s': %s\n", path, strerror(errno));
		return AST_LOCK_TIMEOUT;
	} else {
		ast_debug(1, "Locked path '%s'\n", path);
		return AST_LOCK_SUCCESS;
	}
}

static int ast_unlock_path_lockfile(const char *path)
{
	char *s;
	int res;

	s = alloca(strlen(path) + 10);

	snprintf(s, strlen(path) + 9, "%s/%s", path, ".lock");

	if ((res = unlink(s))) {
		ast_log(LOG_ERROR, "Could not unlock path '%s': %s\n", path, strerror(errno));
	} else {
		ast_debug(1, "Unlocked path '%s'\n", path);
	}

	return res;
}

struct path_lock {
	AST_LIST_ENTRY(path_lock) le;
	int fd;
	char *path;
};

static AST_LIST_HEAD_STATIC(path_lock_list, path_lock);

static void path_lock_destroy(struct path_lock *obj)
{
	if (obj->fd >= 0) {
		close(obj->fd);
	}
	if (obj->path) {
		free(obj->path);
	}
	free(obj);
}

static enum AST_LOCK_RESULT ast_lock_path_flock(const char *path)
{
	char *fs;
	int res;
	int fd;
	time_t start;
	struct path_lock *pl;
	struct stat st, ost;

	fs = alloca(strlen(path) + 20);

	snprintf(fs, strlen(path) + 19, "%s/lock", path);
	if (lstat(fs, &st) == 0) {
		if ((st.st_mode & S_IFMT) == S_IFLNK) {
			ast_log(LOG_WARNING, "Unable to create lock file "
					"'%s': it's already a symbolic link\n",
					fs);
			return AST_LOCK_FAILURE;
		}
		if (st.st_nlink > 1) {
			ast_log(LOG_WARNING, "Unable to create lock file "
					"'%s': %u hard links exist\n",
					fs, (unsigned int) st.st_nlink);
			return AST_LOCK_FAILURE;
		}
	}
	if ((fd = open(fs, O_WRONLY | O_CREAT, 0600)) < 0) {
		ast_log(LOG_WARNING, "Unable to create lock file '%s': %s\n",
				fs, strerror(errno));
		return AST_LOCK_PATH_NOT_FOUND;
	}
	if (!(pl = ast_calloc(1, sizeof(*pl)))) {
		/* We don't unlink the lock file here, on the possibility that
		 * someone else created it - better to leave a little mess
		 * than create a big one by destroying someone else's lock
		 * and causing something to be corrupted.
		 */
		close(fd);
		return AST_LOCK_FAILURE;
	}
	pl->fd = fd;
	pl->path = strdup(path);

	time(&start);
	while (
		#ifdef SOLARIS
		((res = fcntl(pl->fd, F_SETLK, fcntl(pl->fd, F_GETFL) | O_NONBLOCK)) < 0) &&
		#else
		((res = flock(pl->fd, LOCK_EX | LOCK_NB)) < 0) &&
		#endif
			(errno == EWOULDBLOCK) &&
			(time(NULL) - start < 5))
		usleep(1000);
	if (res) {
		ast_log(LOG_WARNING, "Failed to lock path '%s': %s\n",
				path, strerror(errno));
		/* No unlinking of lock done, since we tried and failed to
		 * flock() it.
		 */
		path_lock_destroy(pl);
		return AST_LOCK_TIMEOUT;
	}

	/* Check for the race where the file is recreated or deleted out from
	 * underneath us.
	 */
	if (lstat(fs, &st) != 0 && fstat(pl->fd, &ost) != 0 &&
			st.st_dev != ost.st_dev &&
			st.st_ino != ost.st_ino) {
		ast_log(LOG_WARNING, "Unable to create lock file '%s': "
				"file changed underneath us\n", fs);
		path_lock_destroy(pl);
		return AST_LOCK_FAILURE;
	}

	/* Success: file created, flocked, and is the one we started with */
	AST_LIST_LOCK(&path_lock_list);
	AST_LIST_INSERT_TAIL(&path_lock_list, pl, le);
	AST_LIST_UNLOCK(&path_lock_list);

	ast_debug(1, "Locked path '%s'\n", path);

	return AST_LOCK_SUCCESS;
}

static int ast_unlock_path_flock(const char *path)
{
	char *s;
	struct path_lock *p;

	s = alloca(strlen(path) + 20);

	AST_LIST_LOCK(&path_lock_list);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&path_lock_list, p, le) {
		if (!strcmp(p->path, path)) {
			AST_LIST_REMOVE_CURRENT(le);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&path_lock_list);

	if (p) {
		snprintf(s, strlen(path) + 19, "%s/lock", path);
		unlink(s);
		path_lock_destroy(p);
		ast_debug(1, "Unlocked path '%s'\n", path);
	} else {
		ast_debug(1, "Failed to unlock path '%s': "
				"lock not found\n", path);
	}

	return 0;
}

void ast_set_lock_type(enum AST_LOCK_TYPE type)
{
	ast_lock_type = type;
}

enum AST_LOCK_RESULT ast_lock_path(const char *path)
{
	enum AST_LOCK_RESULT r = AST_LOCK_FAILURE;

	switch (ast_lock_type) {
	case AST_LOCK_TYPE_LOCKFILE:
		r = ast_lock_path_lockfile(path);
		break;
	case AST_LOCK_TYPE_FLOCK:
		r = ast_lock_path_flock(path);
		break;
	}

	return r;
}

int ast_unlock_path(const char *path)
{
	int r = 0;

	switch (ast_lock_type) {
	case AST_LOCK_TYPE_LOCKFILE:
		r = ast_unlock_path_lockfile(path);
		break;
	case AST_LOCK_TYPE_FLOCK:
		r = ast_unlock_path_flock(path);
		break;
	}

	return r;
}

int ast_record_review(struct ast_channel *chan, const char *playfile, const char *recordfile, int maxtime, const char *fmt, int *duration, const char *path) 
{
	int silencethreshold;
	int maxsilence = 0;
	int res = 0;
	int cmd = 0;
	int max_attempts = 3;
	int attempts = 0;
	int recorded = 0;
	int message_exists = 0;
	/* Note that urgent and private are for flagging messages as such in the future */

	/* barf if no pointer passed to store duration in */
	if (!duration) {
		ast_log(LOG_WARNING, "Error ast_record_review called without duration pointer\n");
		return -1;
	}

	cmd = '3';	 /* Want to start by recording */

	silencethreshold = ast_dsp_get_threshold_from_settings(THRESHOLD_SILENCE);

	while ((cmd >= 0) && (cmd != 't')) {
		switch (cmd) {
		case '1':
			if (!message_exists) {
				/* In this case, 1 is to record a message */
				cmd = '3';
				break;
			} else {
				ast_stream_and_wait(chan, "vm-msgsaved", "");
				cmd = 't';
				return res;
			}
		case '2':
			/* Review */
			ast_verb(3, "Reviewing the recording\n");
			cmd = ast_stream_and_wait(chan, recordfile, AST_DIGIT_ANY);
			break;
		case '3':
			message_exists = 0;
			/* Record */
			ast_verb(3, "R%secording\n", recorded == 1 ? "e-r" : "");
			recorded = 1;
			if ((cmd = ast_play_and_record(chan, playfile, recordfile, maxtime, fmt, duration, silencethreshold, maxsilence, path)) == -1) {
				/* User has hung up, no options to give */
				return cmd;
			}
			if (cmd == '0') {
				break;
			} else if (cmd == '*') {
				break;
			} else {
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
			} else {
				if (!(cmd = ast_play_and_wait(chan, "vm-torerecord"))) {
					cmd = ast_waitfordigit(chan, 600);
				}
			}

			if (!cmd) {
				cmd = ast_waitfordigit(chan, 6000);
			}
			if (!cmd) {
				attempts++;
			}
			if (attempts > max_attempts) {
				cmd = 't';
			}
		}
	}
	if (cmd == 't') {
		cmd = 0;
	}
	return cmd;
}

#define RES_UPONE (1 << 16)
#define RES_EXIT  (1 << 17)
#define RES_REPEAT (1 << 18)
#define RES_RESTART ((1 << 19) | RES_REPEAT)

static int ast_ivr_menu_run_internal(struct ast_channel *chan, struct ast_ivr_menu *menu, void *cbdata);

static int ivr_dispatch(struct ast_channel *chan, struct ast_ivr_option *option, char *exten, void *cbdata)
{
	int res;
	int (*ivr_func)(struct ast_channel *, void *);
	char *c;
	char *n;

	switch (option->action) {
	case AST_ACTION_UPONE:
		return RES_UPONE;
	case AST_ACTION_EXIT:
		return RES_EXIT | (((unsigned long)(option->adata)) & 0xffff);
	case AST_ACTION_REPEAT:
		return RES_REPEAT | (((unsigned long)(option->adata)) & 0xffff);
	case AST_ACTION_RESTART:
		return RES_RESTART ;
	case AST_ACTION_NOOP:
		return 0;
	case AST_ACTION_BACKGROUND:
		res = ast_stream_and_wait(chan, (char *)option->adata, AST_DIGIT_ANY);
		if (res < 0) {
			ast_log(LOG_NOTICE, "Unable to find file '%s'!\n", (char *)option->adata);
			res = 0;
		}
		return res;
	case AST_ACTION_PLAYBACK:
		res = ast_stream_and_wait(chan, (char *)option->adata, "");
		if (res < 0) {
			ast_log(LOG_NOTICE, "Unable to find file '%s'!\n", (char *)option->adata);
			res = 0;
		}
		return res;
	case AST_ACTION_MENU:
		if ((res = ast_ivr_menu_run_internal(chan, (struct ast_ivr_menu *)option->adata, cbdata)) == -2) {
			/* Do not pass entry errors back up, treat as though it was an "UPONE" */
			res = 0;
		}
		return res;
	case AST_ACTION_WAITOPTION:
		if (!(res = ast_waitfordigit(chan, chan->pbx ? chan->pbx->rtimeoutms : 10000))) {
			return 't';
		}
		return res;
	case AST_ACTION_CALLBACK:
		ivr_func = option->adata;
		res = ivr_func(chan, cbdata);
		return res;
	case AST_ACTION_TRANSFER:
		res = ast_parseable_goto(chan, option->adata);
		return 0;
	case AST_ACTION_PLAYLIST:
	case AST_ACTION_BACKLIST:
		res = 0;
		c = ast_strdupa(option->adata);
		while ((n = strsep(&c, ";"))) {
			if ((res = ast_stream_and_wait(chan, n,
					(option->action == AST_ACTION_BACKLIST) ? AST_DIGIT_ANY : ""))) {
				break;
			}
		}
		ast_stopstream(chan);
		return res;
	default:
		ast_log(LOG_NOTICE, "Unknown dispatch function %d, ignoring!\n", option->action);
		return 0;
	}
	return -1;
}

static int option_exists(struct ast_ivr_menu *menu, char *option)
{
	int x;
	for (x = 0; menu->options[x].option; x++) {
		if (!strcasecmp(menu->options[x].option, option)) {
			return x;
		}
	}
	return -1;
}

static int option_matchmore(struct ast_ivr_menu *menu, char *option)
{
	int x;
	for (x = 0; menu->options[x].option; x++) {
		if ((!strncasecmp(menu->options[x].option, option, strlen(option))) &&
				(menu->options[x].option[strlen(option)])) {
			return x;
		}
	}
	return -1;
}

static int read_newoption(struct ast_channel *chan, struct ast_ivr_menu *menu, char *exten, int maxexten)
{
	int res = 0;
	int ms;
	while (option_matchmore(menu, exten)) {
		ms = chan->pbx ? chan->pbx->dtimeoutms : 5000;
		if (strlen(exten) >= maxexten - 1) {
			break;
		}
		if ((res = ast_waitfordigit(chan, ms)) < 1) {
			break;
		}
		exten[strlen(exten) + 1] = '\0';
		exten[strlen(exten)] = res;
	}
	return res > 0 ? 0 : res;
}

static int ast_ivr_menu_run_internal(struct ast_channel *chan, struct ast_ivr_menu *menu, void *cbdata)
{
	/* Execute an IVR menu structure */
	int res = 0;
	int pos = 0;
	int retries = 0;
	char exten[AST_MAX_EXTENSION] = "s";
	if (option_exists(menu, "s") < 0) {
		strcpy(exten, "g");
		if (option_exists(menu, "g") < 0) {
			ast_log(LOG_WARNING, "No 's' nor 'g' extension in menu '%s'!\n", menu->title);
			return -1;
		}
	}
	while (!res) {
		while (menu->options[pos].option) {
			if (!strcasecmp(menu->options[pos].option, exten)) {
				res = ivr_dispatch(chan, menu->options + pos, exten, cbdata);
				ast_debug(1, "IVR Dispatch of '%s' (pos %d) yields %d\n", exten, pos, res);
				if (res < 0) {
					break;
				} else if (res & RES_UPONE) {
					return 0;
				} else if (res & RES_EXIT) {
					return res;
				} else if (res & RES_REPEAT) {
					int maxretries = res & 0xffff;
					if ((res & RES_RESTART) == RES_RESTART) {
						retries = 0;
					} else {
						retries++;
					}
					if (!maxretries) {
						maxretries = 3;
					}
					if ((maxretries > 0) && (retries >= maxretries)) {
						ast_debug(1, "Max retries %d exceeded\n", maxretries);
						return -2;
					} else {
						if (option_exists(menu, "g") > -1) {
							strcpy(exten, "g");
						} else if (option_exists(menu, "s") > -1) {
							strcpy(exten, "s");
						}
					}
					pos = 0;
					continue;
				} else if (res && strchr(AST_DIGIT_ANY, res)) {
					ast_debug(1, "Got start of extension, %c\n", res);
					exten[1] = '\0';
					exten[0] = res;
					if ((res = read_newoption(chan, menu, exten, sizeof(exten)))) {
						break;
					}
					if (option_exists(menu, exten) < 0) {
						if (option_exists(menu, "i")) {
							ast_debug(1, "Invalid extension entered, going to 'i'!\n");
							strcpy(exten, "i");
							pos = 0;
							continue;
						} else {
							ast_debug(1, "Aborting on invalid entry, with no 'i' option!\n");
							res = -2;
							break;
						}
					} else {
						ast_debug(1, "New existing extension: %s\n", exten);
						pos = 0;
						continue;
					}
				}
			}
			pos++;
		}
		ast_debug(1, "Stopping option '%s', res is %d\n", exten, res);
		pos = 0;
		if (!strcasecmp(exten, "s")) {
			strcpy(exten, "g");
		} else {
			break;
		}
	}
	return res;
}

int ast_ivr_menu_run(struct ast_channel *chan, struct ast_ivr_menu *menu, void *cbdata)
{
	int res = ast_ivr_menu_run_internal(chan, menu, cbdata);
	/* Hide internal coding */
	return res > 0 ? 0 : res;
}

char *ast_read_textfile(const char *filename)
{
	int fd, count = 0, res;
	char *output = NULL;
	struct stat filesize;

	if (stat(filename, &filesize) == -1) {
		ast_log(LOG_WARNING, "Error can't stat %s\n", filename);
		return NULL;
	}

	count = filesize.st_size + 1;

	if ((fd = open(filename, O_RDONLY)) < 0) {
		ast_log(LOG_WARNING, "Cannot open file '%s' for reading: %s\n", filename, strerror(errno));
		return NULL;
	}

	if ((output = ast_malloc(count))) {
		res = read(fd, output, count - 1);
		if (res == count - 1) {
			output[res] = '\0';
		} else {
			ast_log(LOG_WARNING, "Short read of %s (%d of %d): %s\n", filename, res, count - 1, strerror(errno));
			ast_free(output);
			output = NULL;
		}
	}

	close(fd);

	return output;
}

static int parse_options(const struct ast_app_option *options, void *_flags, char **args, char *optstr, int flaglen)
{
	char *s, *arg;
	int curarg, res = 0;
	unsigned int argloc;
	struct ast_flags *flags = _flags;
	struct ast_flags64 *flags64 = _flags;

	if (flaglen == 32) {
		ast_clear_flag(flags, AST_FLAGS_ALL);
	} else {
		flags64->flags = 0;
	}

	if (!optstr) {
		return 0;
	}

	s = optstr;
	while (*s) {
		curarg = *s++ & 0x7f;	/* the array (in app.h) has 128 entries */
		argloc = options[curarg].arg_index;
		if (*s == '(') {
			int paren = 1, quote = 0;
			int parsequotes = (s[1] == '"') ? 1 : 0;

			/* Has argument */
			arg = ++s;
			for (; *s; s++) {
				if (*s == '(' && !quote) {
					paren++;
				} else if (*s == ')' && !quote) {
					/* Count parentheses, unless they're within quotes (or backslashed, below) */
					paren--;
				} else if (*s == '"' && parsequotes) {
					/* Leave embedded quotes alone, unless they are the first character */
					quote = quote ? 0 : 1;
					ast_copy_string(s, s + 1, INT_MAX);
					s--;
				} else if (*s == '\\') {
					if (!quote) {
						/* If a backslash is found outside of quotes, remove it */
						ast_copy_string(s, s + 1, INT_MAX);
					} else if (quote && s[1] == '"') {
						/* Backslash for a quote character within quotes, remove the backslash */
						ast_copy_string(s, s + 1, INT_MAX);
					} else {
						/* Backslash within quotes, keep both characters */
						s++;
					}
				}

				if (paren == 0) {
					break;
				}
			}
			/* This will find the closing paren we found above, or none, if the string ended before we found one. */
			if ((s = strchr(s, ')'))) {
				if (argloc) {
					args[argloc - 1] = arg;
				}
				*s++ = '\0';
			} else {
				ast_log(LOG_WARNING, "Missing closing parenthesis for argument '%c' in string '%s'\n", curarg, arg);
				res = -1;
				break;
			}
		} else if (argloc) {
			args[argloc - 1] = "";
		}
		if (flaglen == 32) {
			ast_set_flag(flags, options[curarg].flag);
		} else {
			ast_set_flag64(flags64, options[curarg].flag);
		}
	}

	return res;
}

int ast_app_parse_options(const struct ast_app_option *options, struct ast_flags *flags, char **args, char *optstr)
{
	return parse_options(options, flags, args, optstr, 32);
}

int ast_app_parse_options64(const struct ast_app_option *options, struct ast_flags64 *flags, char **args, char *optstr)
{
	return parse_options(options, flags, args, optstr, 64);
}

void ast_app_options2str64(const struct ast_app_option *options, struct ast_flags64 *flags, char *buf, size_t len)
{
	unsigned int i, found = 0;
	for (i = 32; i < 128 && found < len; i++) {
		if (ast_test_flag64(flags, options[i].flag)) {
			buf[found++] = i;
		}
	}
	buf[found] = '\0';
}

int ast_get_encoded_char(const char *stream, char *result, size_t *consumed)
{
	int i;
	*consumed = 1;
	*result = 0;
	if (ast_strlen_zero(stream)) {
		*consumed = 0;
		return -1;
	}

	if (*stream == '\\') {
		*consumed = 2;
		switch (*(stream + 1)) {
		case 'n':
			*result = '\n';
			break;
		case 'r':
			*result = '\r';
			break;
		case 't':
			*result = '\t';
			break;
		case 'x':
			/* Hexadecimal */
			if (strchr("0123456789ABCDEFabcdef", *(stream + 2)) && *(stream + 2) != '\0') {
				*consumed = 3;
				if (*(stream + 2) <= '9') {
					*result = *(stream + 2) - '0';
				} else if (*(stream + 2) <= 'F') {
					*result = *(stream + 2) - 'A' + 10;
				} else {
					*result = *(stream + 2) - 'a' + 10;
				}
			} else {
				ast_log(LOG_ERROR, "Illegal character '%c' in hexadecimal string\n", *(stream + 2));
				return -1;
			}

			if (strchr("0123456789ABCDEFabcdef", *(stream + 3)) && *(stream + 3) != '\0') {
				*consumed = 4;
				*result <<= 4;
				if (*(stream + 3) <= '9') {
					*result += *(stream + 3) - '0';
				} else if (*(stream + 3) <= 'F') {
					*result += *(stream + 3) - 'A' + 10;
				} else {
					*result += *(stream + 3) - 'a' + 10;
				}
			}
			break;
		case '0':
			/* Octal */
			*consumed = 2;
			for (i = 2; ; i++) {
				if (strchr("01234567", *(stream + i)) && *(stream + i) != '\0') {
					(*consumed)++;
					ast_debug(5, "result was %d, ", *result);
					*result <<= 3;
					*result += *(stream + i) - '0';
					ast_debug(5, "is now %d\n", *result);
				} else {
					break;
				}
			}
			break;
		default:
			*result = *(stream + 1);
		}
	} else {
		*result = *stream;
		*consumed = 1;
	}
	return 0;
}

char *ast_get_encoded_str(const char *stream, char *result, size_t result_size)
{
	char *cur = result;
	size_t consumed;

	while (cur < result + result_size - 1 && !ast_get_encoded_char(stream, cur, &consumed)) {
		cur++;
		stream += consumed;
	}
	*cur = '\0';
	return result;
}

int ast_str_get_encoded_str(struct ast_str **str, int maxlen, const char *stream)
{
	char next, *buf;
	size_t offset = 0;
	size_t consumed;

	if (strchr(stream, '\\')) {
		while (!ast_get_encoded_char(stream, &next, &consumed)) {
			if (offset + 2 > ast_str_size(*str) && maxlen > -1) {
				ast_str_make_space(str, maxlen > 0 ? maxlen : (ast_str_size(*str) + 48) * 2 - 48);
			}
			if (offset + 2 > ast_str_size(*str)) {
				break;
			}
			buf = ast_str_buffer(*str);
			buf[offset++] = next;
			stream += consumed;
		}
		buf = ast_str_buffer(*str);
		buf[offset++] = '\0';
		ast_str_update(*str);
	} else {
		ast_str_set(str, maxlen, "%s", stream);
	}
	return 0;
}

void ast_close_fds_above_n(int n)
{
	closefrom(n + 1);
}

int ast_safe_fork(int stop_reaper)
{
	sigset_t signal_set, old_set;
	int pid;

	/* Don't let the default signal handler for children reap our status */
	if (stop_reaper) {
		ast_replace_sigchld();
	}

	sigfillset(&signal_set);
	pthread_sigmask(SIG_BLOCK, &signal_set, &old_set);

	pid = fork();

	if (pid != 0) {
		/* Fork failed or parent */
		pthread_sigmask(SIG_SETMASK, &old_set, NULL);
		if (!stop_reaper && pid > 0) {
			struct zombie *cur = ast_calloc(1, sizeof(*cur));
			if (cur) {
				cur->pid = pid;
				AST_LIST_LOCK(&zombies);
				AST_LIST_INSERT_TAIL(&zombies, cur, list);
				AST_LIST_UNLOCK(&zombies);
				if (shaun_of_the_dead_thread == AST_PTHREADT_NULL) {
					if (ast_pthread_create_background(&shaun_of_the_dead_thread, NULL, shaun_of_the_dead, NULL)) {
						ast_log(LOG_ERROR, "Shaun of the Dead wants to kill zombies, but can't?!!\n");
						shaun_of_the_dead_thread = AST_PTHREADT_NULL;
					}
				}
			}
		}
		return pid;
	} else {
		/* Child */
#ifdef HAVE_CAP
		cap_t cap = cap_from_text("cap_net_admin-eip");

		if (cap_set_proc(cap)) {
			ast_log(LOG_WARNING, "Unable to remove capabilities.\n");
		}
		cap_free(cap);
#endif

		/* Before we unblock our signals, return our trapped signals back to the defaults */
		signal(SIGHUP, SIG_DFL);
		signal(SIGCHLD, SIG_DFL);
		signal(SIGINT, SIG_DFL);
		signal(SIGURG, SIG_DFL);
		signal(SIGTERM, SIG_DFL);
		signal(SIGPIPE, SIG_DFL);
		signal(SIGXFSZ, SIG_DFL);

		/* unblock important signal handlers */
		if (pthread_sigmask(SIG_UNBLOCK, &signal_set, NULL)) {
			ast_log(LOG_WARNING, "unable to unblock signals: %s\n", strerror(errno));
			_exit(1);
		}

		return pid;
	}
}

void ast_safe_fork_cleanup(void)
{
	ast_unreplace_sigchld();
}

int ast_app_parse_timelen(const char *timestr, int *result, enum ast_timelen unit)
{
	int res;
	char u[10];
#ifdef HAVE_LONG_DOUBLE_WIDER
	long double amount;
	#define FMT "%30Lf%9s"
#else
	double amount;
	#define FMT "%30lf%9s"
#endif
	if (!timestr) {
		return -1;
	}

	if ((res = sscanf(timestr, FMT, &amount, u)) == 0) {
#undef FMT
		return -1;
	} else if (res == 2) {
		switch (u[0]) {
		case 'h':
		case 'H':
			unit = TIMELEN_HOURS;
			break;
		case 's':
		case 'S':
			unit = TIMELEN_SECONDS;
			break;
		case 'm':
		case 'M':
			if (toupper(u[1]) == 'S') {
				unit = TIMELEN_MILLISECONDS;
			} else if (u[1] == '\0') {
				unit = TIMELEN_MINUTES;
			}
			break;
		}
	}

	switch (unit) {
	case TIMELEN_HOURS:
		amount *= 60;
		/* fall-through */
	case TIMELEN_MINUTES:
		amount *= 60;
		/* fall-through */
	case TIMELEN_SECONDS:
		amount *= 1000;
		/* fall-through */
	case TIMELEN_MILLISECONDS:
		;
	}
	*result = amount > INT_MAX ? INT_MAX : (int) amount;
	return 0;
}

