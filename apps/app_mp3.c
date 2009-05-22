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
 * \brief Silly application to play an MP3 file -- uses mpg123
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/time.h>
#include <signal.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/frame.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/app.h"

#define LOCAL_MPG_123 "/usr/local/bin/mpg123"
#define MPG_123 "/usr/bin/mpg123"

/*** DOCUMENTATION
	<application name="MP3Player" language="en_US">
		<synopsis>
			Play an MP3 file or stream.
		</synopsis>
		<syntax>
			<parameter name="Location" required="true">
				<para>Location of the file to be played.
				(argument passed to mpg123)</para>
			</parameter>
		</syntax>
		<description>
			<para>Executes mpg123 to play the given location, which typically would be a filename or a URL.
			User can exit by pressing any key on the dialpad, or by hanging up.</para>
		</description>
	</application>

 ***/
static char *app = "MP3Player";

static int mp3play(const char *filename, int fd)
{
	int res;

	res = ast_safe_fork(0);
	if (res < 0) 
		ast_log(LOG_WARNING, "Fork failed\n");
	if (res) {
		return res;
	}
	if (ast_opt_high_priority)
		ast_set_priority(0);

	dup2(fd, STDOUT_FILENO);
	ast_close_fds_above_n(STDERR_FILENO);

	/* Execute mpg123, but buffer if it's a net connection */
	if (!strncasecmp(filename, "http://", 7)) {
		/* Most commonly installed in /usr/local/bin */
	    execl(LOCAL_MPG_123, "mpg123", "-q", "-s", "-b", "1024", "-f", "8192", "--mono", "-r", "8000", filename, (char *)NULL);
		/* But many places has it in /usr/bin */
	    execl(MPG_123, "mpg123", "-q", "-s", "-b", "1024","-f", "8192", "--mono", "-r", "8000", filename, (char *)NULL);
		/* As a last-ditch effort, try to use PATH */
	    execlp("mpg123", "mpg123", "-q", "-s", "-b", "1024",  "-f", "8192", "--mono", "-r", "8000", filename, (char *)NULL);
	}
	else {
		/* Most commonly installed in /usr/local/bin */
	    execl(MPG_123, "mpg123", "-q", "-s", "-f", "8192", "--mono", "-r", "8000", filename, (char *)NULL);
		/* But many places has it in /usr/bin */
	    execl(LOCAL_MPG_123, "mpg123", "-q", "-s", "-f", "8192", "--mono", "-r", "8000", filename, (char *)NULL);
		/* As a last-ditch effort, try to use PATH */
	    execlp("mpg123", "mpg123", "-q", "-s", "-f", "8192", "--mono", "-r", "8000", filename, (char *)NULL);
	}
	/* Can't use ast_log since FD's are closed */
	fprintf(stderr, "Execute of mpg123 failed\n");
	_exit(0);
}

static int timed_read(int fd, void *data, int datalen, int timeout)
{
	int res;
	struct pollfd fds[1];
	fds[0].fd = fd;
	fds[0].events = POLLIN;
	res = ast_poll(fds, 1, timeout);
	if (res < 1) {
		ast_log(LOG_NOTICE, "Poll timed out/errored out with %d\n", res);
		return -1;
	}
	return read(fd, data, datalen);
	
}

static int mp3_exec(struct ast_channel *chan, const char *data)
{
	int res=0;
	int fds[2];
	int ms = -1;
	int pid = -1;
	int owriteformat;
	int timeout = 2000;
	struct timeval next;
	struct ast_frame *f;
	struct myframe {
		struct ast_frame f;
		char offset[AST_FRIENDLY_OFFSET];
		short frdata[160];
	} myf;
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "MP3 Playback requires an argument (filename)\n");
		return -1;
	}

	if (pipe(fds)) {
		ast_log(LOG_WARNING, "Unable to create pipe\n");
		return -1;
	}
	
	ast_stopstream(chan);

	owriteformat = chan->writeformat;
	res = ast_set_write_format(chan, AST_FORMAT_SLINEAR);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set write format to signed linear\n");
		return -1;
	}
	
	res = mp3play(data, fds[1]);
	if (!strncasecmp(data, "http://", 7)) {
		timeout = 10000;
	}
	/* Wait 1000 ms first */
	next = ast_tvnow();
	next.tv_sec += 1;
	if (res >= 0) {
		pid = res;
		/* Order is important -- there's almost always going to be mp3...  we want to prioritize the
		   user */
		for (;;) {
			ms = ast_tvdiff_ms(next, ast_tvnow());
			if (ms <= 0) {
				res = timed_read(fds[0], myf.frdata, sizeof(myf.frdata), timeout);
				if (res > 0) {
					myf.f.frametype = AST_FRAME_VOICE;
					myf.f.subclass = AST_FORMAT_SLINEAR;
					myf.f.datalen = res;
					myf.f.samples = res / 2;
					myf.f.mallocd = 0;
					myf.f.offset = AST_FRIENDLY_OFFSET;
					myf.f.src = __PRETTY_FUNCTION__;
					myf.f.delivery.tv_sec = 0;
					myf.f.delivery.tv_usec = 0;
					myf.f.data.ptr = myf.frdata;
					if (ast_write(chan, &myf.f) < 0) {
						res = -1;
						break;
					}
				} else {
					ast_debug(1, "No more mp3\n");
					res = 0;
					break;
				}
				next = ast_tvadd(next, ast_samp2tv(myf.f.samples, 8000));
			} else {
				ms = ast_waitfor(chan, ms);
				if (ms < 0) {
					ast_debug(1, "Hangup detected\n");
					res = -1;
					break;
				}
				if (ms) {
					f = ast_read(chan);
					if (!f) {
						ast_debug(1, "Null frame == hangup() detected\n");
						res = -1;
						break;
					}
					if (f->frametype == AST_FRAME_DTMF) {
						ast_debug(1, "User pressed a key\n");
						ast_frfree(f);
						res = 0;
						break;
					}
					ast_frfree(f);
				} 
			}
		}
	}
	close(fds[0]);
	close(fds[1]);
	
	if (pid > -1)
		kill(pid, SIGKILL);
	if (!res && owriteformat)
		ast_set_write_format(chan, owriteformat);
	
	return res;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ast_register_application_xml(app, mp3_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Silly MP3 Application");
