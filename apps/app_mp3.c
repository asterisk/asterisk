/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Silly application to play an MP3 file -- uses mpg123
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */
 
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/frame.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/translate.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/time.h>

#define MPG_123 "/usr/bin/mpg123"

static char *tdesc = "Silly MP3 Application";

static char *app = "MP3Player";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int mp3play(char *filename, int fd)
{
	int res;
	res = fork();
	if (res < 0) 
		ast_log(LOG_WARNING, "Fork failed\n");
	if (res)
		return res;
	dup2(fd, STDOUT_FILENO);
	/* Execute mpg123, but buffer if it's a net connection */
	if (strncmp(filename, "http://", 7)) 
	    execl(MPG_123, MPG_123, "-q", "-s", "-b", "1024", "--mono", "-r", "8000", filename, NULL);
	else
	    execl(MPG_123, MPG_123, "-q", "-s", "--mono", "-r", "8000", filename, NULL);
	ast_log(LOG_WARNING, "Execute of mpg123 failed\n");
	return -1;
}

static int mp3_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	struct ast_channel *trans;
	int fds[2];
	int rfds[2];
	int ms = -1;
	int pid;
	int us;
	struct timeval tv;
	struct timeval last;
	struct ast_frame *f;
	struct myframe {
		struct ast_frame f;
		char offset[AST_FRIENDLY_OFFSET];
		char frdata[160];
	} myf;
	last.tv_usec = 0;
	last.tv_sec = 0;
	if (!data) {
		ast_log(LOG_WARNING, "MP3 Playback requires an argument (filename)\n");
		return -1;
	}
	if (pipe(fds)) {
		ast_log(LOG_WARNING, "Unable to create pipe\n");
		return -1;
	}
	LOCAL_USER_ADD(u);
	ast_stopstream(chan);
	if (chan->format & AST_FORMAT_SLINEAR)
		trans = chan;
	else
		trans = ast_translator_create(chan, AST_FORMAT_SLINEAR, AST_DIRECTION_OUT);
	if (trans) {
		res = mp3play((char *)data, fds[1]);
		if (res >= 0) {
			pid = res;
			/* Order is important -- there's almost always going to be mp3...  we want to prioritize the
			   user */
			rfds[0] = trans->fd;
			rfds[1] = fds[0];
			for (;;) {
				CHECK_BLOCKING(trans);
				res = ast_waitfor_n_fd(rfds, 2, &ms);
				trans->blocking = 0;
				if (res < 1) {
					ast_log(LOG_DEBUG, "Hangup detected\n");
					res = -1;
					break;
				} else if (res == trans->fd) {
					f = ast_read(trans);
					if (!f) {
						ast_log(LOG_DEBUG, "Null frame == hangup() detected\n");
						res = -1;
						break;
					}
					if (f->frametype == AST_FRAME_DTMF) {
						ast_log(LOG_DEBUG, "User pressed a key\n");
						ast_frfree(f);
						res = 0;
						break;
					}
					ast_frfree(f);
				} else if (res == fds[0]) {
					gettimeofday(&tv, NULL);
					if (last.tv_sec || last.tv_usec) {
						/* We should wait at least a frame length */
						us = sizeof(myf.frdata) / 16 * 1000;
						/* Subtract 1,000,000 us for each second late we've passed */
						us -= (tv.tv_sec - last.tv_sec) * 1000000;
						/* And one for each us late we've passed */
						us -= (tv.tv_usec - last.tv_usec);
						/* Sleep that long if needed */
						if (us > 0)
							usleep(us);
					}
					last = tv;
					res = read(fds[0], myf.frdata, sizeof(myf.frdata));
					if (res > 0) {
						myf.f.frametype = AST_FRAME_VOICE;
						myf.f.subclass = AST_FORMAT_SLINEAR;
						myf.f.datalen = res;
						myf.f.timelen = res / 16;
						myf.f.mallocd = 0;
						myf.f.offset = AST_FRIENDLY_OFFSET;
						myf.f.src = __PRETTY_FUNCTION__;
						myf.f.data = myf.frdata;
						if (ast_write(trans, &myf.f) < 0) {
							res = -1;
							break;
						}
					} else {
						ast_log(LOG_DEBUG, "No more mp3\n");
						res = 0;
					}
				} else {
					ast_log(LOG_DEBUG, "HuhHHH?\n");
					res = -1;
					break;
				}
			}
			kill(pid, SIGTERM);
		}
		if (trans != chan) 
			ast_translator_destroy(trans);
	} else 
		ast_log(LOG_WARNING, "No translator channel available\n");
	close(fds[0]);
	close(fds[1]);
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, mp3_exec);
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
