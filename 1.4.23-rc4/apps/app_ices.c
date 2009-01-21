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
 * \brief Stream to an icecast server via ICES (see contrib/asterisk-ices.xml)
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <errno.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/frame.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/options.h"

#define path_BIN "/usr/bin/"
#define path_LOCAL "/usr/local/bin/"

static char *app = "ICES";

static char *synopsis = "Encode and stream using 'ices'";

static char *descrip = 
"  ICES(config.xml) Streams to an icecast server using ices\n"
"(available separately).  A configuration file must be supplied\n"
"for ices (see contrib/asterisk-ices.xml). \n";

static int icesencode(char *filename, int fd)
{
	int res;
	int x;
	sigset_t fullset, oldset;

	sigfillset(&fullset);
	pthread_sigmask(SIG_BLOCK, &fullset, &oldset);

	res = fork();
	if (res < 0) 
		ast_log(LOG_WARNING, "Fork failed\n");
	if (res) {
		pthread_sigmask(SIG_SETMASK, &oldset, NULL);
		return res;
	}

	/* Stop ignoring PIPE */
	signal(SIGPIPE, SIG_DFL);
	pthread_sigmask(SIG_UNBLOCK, &fullset, NULL);

	if (ast_opt_high_priority)
		ast_set_priority(0);
	dup2(fd, STDIN_FILENO);
	for (x=STDERR_FILENO + 1;x<1024;x++) {
		if ((x != STDIN_FILENO) && (x != STDOUT_FILENO))
			close(x);
	}

	/* Most commonly installed in /usr/local/bin 
	 * But many places has it in /usr/bin 
	 * As a last-ditch effort, try to use PATH
	 */
	execl(path_LOCAL "ices2", "ices", filename, (char *)NULL);
	execl(path_BIN "ices2", "ices", filename, (char *)NULL);
	execlp("ices2", "ices", filename, (char *)NULL);

	if (option_debug)
		ast_log(LOG_DEBUG, "Couldn't find ices version 2, attempting to use ices version 1.");

	execl(path_LOCAL "ices", "ices", filename, (char *)NULL);
	execl(path_BIN "ices", "ices", filename, (char *)NULL);
	execlp("ices", "ices", filename, (char *)NULL);

	ast_log(LOG_WARNING, "Execute of ices failed, could not be found.\n");
	close(fd);
	_exit(0);
}

static int ices_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct ast_module_user *u;
	int fds[2];
	int ms = -1;
	int pid = -1;
	int flags;
	int oreadformat;
	struct timeval last;
	struct ast_frame *f;
	char filename[256]="";
	char *c;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "ICES requires an argument (configfile.xml)\n");
		return -1;
	}

	u = ast_module_user_add(chan);
	
	last = ast_tv(0, 0);
	
	if (pipe(fds)) {
		ast_log(LOG_WARNING, "Unable to create pipe\n");
		ast_module_user_remove(u);
		return -1;
	}
	flags = fcntl(fds[1], F_GETFL);
	fcntl(fds[1], F_SETFL, flags | O_NONBLOCK);
	
	ast_stopstream(chan);

	if (chan->_state != AST_STATE_UP)
		res = ast_answer(chan);
		
	if (res) {
		close(fds[0]);
		close(fds[1]);
		ast_log(LOG_WARNING, "Answer failed!\n");
		ast_module_user_remove(u);
		return -1;
	}

	oreadformat = chan->readformat;
	res = ast_set_read_format(chan, AST_FORMAT_SLINEAR);
	if (res < 0) {
		close(fds[0]);
		close(fds[1]);
		ast_log(LOG_WARNING, "Unable to set write format to signed linear\n");
		ast_module_user_remove(u);
		return -1;
	}
	if (((char *)data)[0] == '/')
		ast_copy_string(filename, (char *) data, sizeof(filename));
	else
		snprintf(filename, sizeof(filename), "%s/%s", (char *)ast_config_AST_CONFIG_DIR, (char *)data);
	/* Placeholder for options */		
	c = strchr(filename, '|');
	if (c)
		*c = '\0';	
	res = icesencode(filename, fds[0]);
	if (res >= 0) {
		pid = res;
		for (;;) {
			/* Wait for audio, and stream */
			ms = ast_waitfor(chan, -1);
			if (ms < 0) {
				ast_log(LOG_DEBUG, "Hangup detected\n");
				res = -1;
				break;
			}
			f = ast_read(chan);
			if (!f) {
				ast_log(LOG_DEBUG, "Null frame == hangup() detected\n");
				res = -1;
				break;
			}
			if (f->frametype == AST_FRAME_VOICE) {
				res = write(fds[1], f->data, f->datalen);
				if (res < 0) {
					if (errno != EAGAIN) {
						ast_log(LOG_WARNING, "Write failed to pipe: %s\n", strerror(errno));
						res = -1;
						ast_frfree(f);
						break;
					}
				}
			}
			ast_frfree(f);
		}
	}
	close(fds[0]);
	close(fds[1]);
	
	if (pid > -1)
		kill(pid, SIGKILL);
	if (!res && oreadformat)
		ast_set_read_format(chan, oreadformat);

	ast_module_user_remove(u);

	return res;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);

	ast_module_user_hangup_all();

	return res;
}

static int load_module(void)
{
	return ast_register_application(app, ices_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Encode and Stream via icecast and ices");
