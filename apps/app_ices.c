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
 * ICES - http://www.icecast.org/ices.php
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/
 
#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <signal.h>
#include <fcntl.h>
#include <sys/time.h>

#include "asterisk/paths.h"	/* use ast_config_AST_CONFIG_DIR */
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/frame.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/app.h"
#include "asterisk/format_cache.h"

/*** DOCUMENTATION
	<application name="ICES" language="en_US">
		<synopsis>
			Encode and stream using 'ices'.
		</synopsis>
		<syntax>
			<parameter name="config" required="true">
				<para>ICES configuration file.</para>
			</parameter>
		</syntax>
		<description>
			<para>Streams to an icecast server using ices (available separately).
			A configuration file must be supplied for ices (see contrib/asterisk-ices.xml).</para>
			<note><para>ICES version 2 client and server required.</para></note>
		</description>
	</application>

 ***/

#define path_BIN "/usr/bin/"
#define path_LOCAL "/usr/local/bin/"

static char *app = "ICES";

static int icesencode(char *filename, int fd)
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
	dup2(fd, STDIN_FILENO);
	ast_close_fds_above_n(STDERR_FILENO);

	/* Most commonly installed in /usr/local/bin 
	 * But many places has it in /usr/bin 
	 * As a last-ditch effort, try to use PATH
	 */
	execl(path_LOCAL "ices2", "ices", filename, SENTINEL);
	execl(path_BIN "ices2", "ices", filename, SENTINEL);
	execlp("ices2", "ices", filename, SENTINEL);

	ast_debug(1, "Couldn't find ices version 2, attempting to use ices version 1.\n");

	execl(path_LOCAL "ices", "ices", filename, SENTINEL);
	execl(path_BIN "ices", "ices", filename, SENTINEL);
	execlp("ices", "ices", filename, SENTINEL);

	ast_log(LOG_WARNING, "Execute of ices failed, could not find command.\n");
	close(fd);
	_exit(0);
}

static int ices_exec(struct ast_channel *chan, const char *data)
{
	int res = 0;
	int fds[2];
	int ms = -1;
	int pid = -1;
	int flags;
	struct ast_format *oreadformat;
	struct ast_frame *f;
	char filename[256]="";
	char *c;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "ICES requires an argument (configfile.xml)\n");
		return -1;
	}
	
	if (pipe(fds)) {
		ast_log(LOG_WARNING, "Unable to create pipe\n");
		return -1;
	}
	flags = fcntl(fds[1], F_GETFL);
	fcntl(fds[1], F_SETFL, flags | O_NONBLOCK);
	
	ast_stopstream(chan);

	if (ast_channel_state(chan) != AST_STATE_UP)
		res = ast_answer(chan);
		
	if (res) {
		close(fds[0]);
		close(fds[1]);
		ast_log(LOG_WARNING, "Answer failed!\n");
		return -1;
	}

	oreadformat = ao2_bump(ast_channel_readformat(chan));
	res = ast_set_read_format(chan, ast_format_slin);
	if (res < 0) {
		close(fds[0]);
		close(fds[1]);
		ast_log(LOG_WARNING, "Unable to set write format to signed linear\n");
		ao2_cleanup(oreadformat);
		return -1;
	}
	if (((char *)data)[0] == '/')
		ast_copy_string(filename, (char *) data, sizeof(filename));
	else
		snprintf(filename, sizeof(filename), "%s/%s", ast_config_AST_CONFIG_DIR, (char *)data);
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
				ast_debug(1, "Hangup detected\n");
				res = -1;
				break;
			}
			f = ast_read(chan);
			if (!f) {
				ast_debug(1, "Null frame == hangup() detected\n");
				res = -1;
				break;
			}
			if (f->frametype == AST_FRAME_VOICE) {
				res = write(fds[1], f->data.ptr, f->datalen);
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
	ao2_cleanup(oreadformat);

	return res;
}

static int load_module(void)
{
	return ast_register_application_xml(app, ices_exec);
}

AST_MODULE_INFO_AUTOCLEAN(ASTERISK_GPL_KEY, "Encode and Stream via icecast and ices");
