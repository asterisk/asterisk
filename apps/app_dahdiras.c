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
 * \brief Execute an ISDN RAS
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */

/*** MODULEINFO
	<depend>dahdi</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/ioctl.h>
#include <sys/wait.h>
#ifdef __linux__
#include <sys/signal.h>
#else
#include <signal.h>
#endif /* __linux__ */

#include <fcntl.h>

#include <dahdi/user.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<application name="DAHDIRAS" language="en_US">
		<synopsis>
			Executes DAHDI ISDN RAS application.
		</synopsis>
		<syntax>
			<parameter name="args" required="true">
				<para>A list of parameters to pass to the pppd daemon,
				separated by <literal>,</literal> characters.</para>
			</parameter>
		</syntax>
		<description>
			<para>Executes a RAS server using pppd on the given channel.
			The channel must be a clear channel (i.e. PRI source) and a DAHDI
			channel to be able to use this function (No modem emulation is included).</para>
			<para>Your pppd must be patched to be DAHDI aware.</para>
		</description>
	</application>

 ***/

static const char app[] = "DAHDIRAS";

#define PPP_MAX_ARGS	32
#define PPP_EXEC	"/usr/sbin/pppd"

static pid_t spawn_ras(struct ast_channel *chan, char *args)
{
	pid_t pid;
	char *c;

	char *argv[PPP_MAX_ARGS];
	int argc = 0;
	char *stringp=NULL;

	/* Start by forking */
	pid = ast_safe_fork(1);
	if (pid) {
		return pid;
	}

	/* Execute RAS on File handles */
	dup2(chan->fds[0], STDIN_FILENO);

	/* Drop high priority */
	if (ast_opt_high_priority)
		ast_set_priority(0);

	/* Close other file descriptors */
	ast_close_fds_above_n(STDERR_FILENO);

	/* Reset all arguments */
	memset(argv, 0, sizeof(argv));

	/* First argument is executable, followed by standard
	   arguments for DAHDI PPP */
	argv[argc++] = PPP_EXEC;
	argv[argc++] = "nodetach";

	/* And all the other arguments */
	stringp=args;
	c = strsep(&stringp, ",");
	while(c && strlen(c) && (argc < (PPP_MAX_ARGS - 4))) {
		argv[argc++] = c;
		c = strsep(&stringp, ",");
	}

	argv[argc++] = "plugin";
	argv[argc++] = "dahdi.so";
	argv[argc++] = "stdin";

	/* Finally launch PPP */
	execv(PPP_EXEC, argv);
	fprintf(stderr, "Failed to exec PPPD!\n");
	exit(1);
}

static void run_ras(struct ast_channel *chan, char *args)
{
	pid_t pid;
	int status;
	int res;
	int signalled = 0;
	struct dahdi_bufferinfo savebi;
	int x;
	
	res = ioctl(chan->fds[0], DAHDI_GET_BUFINFO, &savebi);
	if(res) {
		ast_log(LOG_WARNING, "Unable to check buffer policy on channel %s\n", chan->name);
		return;
	}

	pid = spawn_ras(chan, args);
	if (pid < 0) {
		ast_log(LOG_WARNING, "Failed to spawn RAS\n");
	} else {
		for (;;) {
			res = wait4(pid, &status, WNOHANG, NULL);
			if (!res) {
				/* Check for hangup */
				if (ast_check_hangup(chan) && !signalled) {
					ast_debug(1, "Channel '%s' hungup.  Signalling RAS at %d to die...\n", chan->name, pid);
					kill(pid, SIGTERM);
					signalled=1;
				}
				/* Try again */
				sleep(1);
				continue;
			}
			if (res < 0) {
				ast_log(LOG_WARNING, "wait4 returned %d: %s\n", res, strerror(errno));
			}
			if (WIFEXITED(status)) {
				ast_verb(3, "RAS on %s terminated with status %d\n", chan->name, WEXITSTATUS(status));
			} else if (WIFSIGNALED(status)) {
				ast_verb(3, "RAS on %s terminated with signal %d\n", 
					 chan->name, WTERMSIG(status));
			} else {
				ast_verb(3, "RAS on %s terminated weirdly.\n", chan->name);
			}
			/* Throw back into audio mode */
			x = 1;
			ioctl(chan->fds[0], DAHDI_AUDIOMODE, &x);

			/* Restore saved values */
			res = ioctl(chan->fds[0], DAHDI_SET_BUFINFO, &savebi);
			if (res < 0) {
				ast_log(LOG_WARNING, "Unable to set buffer policy on channel %s\n", chan->name);
			}
			break;
		}
	}
	ast_safe_fork_cleanup();
}

static int dahdiras_exec(struct ast_channel *chan, const char *data)
{
	int res=-1;
	char *args;
	struct dahdi_params dahdip;

	if (!data) 
		data = "";

	args = ast_strdupa(data);
	
	/* Answer the channel if it's not up */
	if (chan->_state != AST_STATE_UP)
		ast_answer(chan);
	if (strcasecmp(chan->tech->type, "DAHDI")) {
		/* If it's not a DAHDI channel, we're done.  Wait a couple of
		   seconds and then hangup... */
		ast_verb(2, "Channel %s is not a DAHDI channel\n", chan->name);
		sleep(2);
	} else {
		memset(&dahdip, 0, sizeof(dahdip));
		if (ioctl(chan->fds[0], DAHDI_GET_PARAMS, &dahdip)) {
			ast_log(LOG_WARNING, "Unable to get DAHDI parameters\n");
		} else if (dahdip.sigtype != DAHDI_SIG_CLEAR) {
			ast_verb(2, "Channel %s is not a clear channel\n", chan->name);
		} else {
			/* Everything should be okay.  Run PPP. */
			ast_verb(3, "Starting RAS on %s\n", chan->name);
			/* Execute RAS */
			run_ras(chan, args);
		}
	}
	return res;
}

static int unload_module(void) 
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
	return ((ast_register_application_xml(app, dahdiras_exec)) ? AST_MODULE_LOAD_FAILURE : AST_MODULE_LOAD_SUCCESS);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "DAHDI ISDN Remote Access Server");

