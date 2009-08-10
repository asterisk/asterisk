/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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
 * \brief AGI - the Asterisk Gateway Interface
 *
 * \author Mark Spencer <markster@digium.com> 
 */

/*** MODULEINFO
	<depend>working_fork</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/types.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#ifdef HAVE_CAP
#include <sys/capability.h>
#endif /* HAVE_CAP */

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/astdb.h"
#include "asterisk/callerid.h"
#include "asterisk/cli.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/image.h"
#include "asterisk/say.h"
#include "asterisk/app.h"
#include "asterisk/dsp.h"
#include "asterisk/musiconhold.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/strings.h"
#include "asterisk/agi.h"
#include "asterisk/features.h"

#define MAX_ARGS 128
#define MAX_COMMANDS 128
#define AGI_NANDFS_RETRY 3
#define AGI_BUF_LEN 2048

/* Recycle some stuff from the CLI interface */
#define fdprintf agi_debug_cli

static char *app = "AGI";

static char *eapp = "EAGI";

static char *deadapp = "DeadAGI";

static char *synopsis = "Executes an AGI compliant application";
static char *esynopsis = "Executes an EAGI compliant application";
static char *deadsynopsis = "Executes AGI on a hungup channel";

static char *descrip =
"  [E|Dead]AGI(command|args): Executes an Asterisk Gateway Interface compliant\n"
"program on a channel. AGI allows Asterisk to launch external programs\n"
"written in any language to control a telephony channel, play audio,\n"
"read DTMF digits, etc. by communicating with the AGI protocol on stdin\n"
"and stdout.\n"
"  This channel will stop dialplan execution on hangup inside of this\n"
"application, except when using DeadAGI.  Otherwise, dialplan execution\n"
"will continue normally.\n"
"  A locally executed AGI script will receive SIGHUP on hangup from the channel\n"
"except when using DeadAGI. This can be disabled by setting the AGISIGHUP channel\n"
"variable to \"no\" before executing the AGI application.\n"
"  Using 'EAGI' provides enhanced AGI, with incoming audio available out of band\n"
"on file descriptor 3\n\n"
"  Use the CLI command 'agi show' to list available agi commands\n"
"  This application sets the following channel variable upon completion:\n"
"     AGISTATUS      The status of the attempt to the run the AGI script\n"
"                    text string, one of SUCCESS | FAILURE | HANGUP\n";

static int agidebug = 0;

static pthread_t shaun_of_the_dead_thread = AST_PTHREADT_NULL;

#define TONE_BLOCK_SIZE 200

/* Max time to connect to an AGI remote host */
#define MAX_AGI_CONNECT 2000

#define AGI_PORT 4573

enum agi_result {
	AGI_RESULT_FAILURE = -1,
	AGI_RESULT_SUCCESS,
	AGI_RESULT_SUCCESS_FAST,
	AGI_RESULT_HANGUP
};

struct zombie {
	pid_t pid;
	AST_LIST_ENTRY(zombie) list;
};

static AST_LIST_HEAD_STATIC(zombies, zombie);

static int __attribute__((format(printf, 2, 3))) agi_debug_cli(int fd, char *fmt, ...)
{
	char *stuff;
	int res = 0;

	va_list ap;
	va_start(ap, fmt);
	res = vasprintf(&stuff, fmt, ap);
	va_end(ap);
	if (res == -1) {
		ast_log(LOG_ERROR, "Out of memory\n");
	} else {
		if (agidebug)
			ast_verbose("AGI Tx >> %s", stuff); /* \n provided by caller */
		res = ast_carefulwrite(fd, stuff, strlen(stuff), 100);
		free(stuff);
	}

	return res;
}

/* launch_netscript: The fastagi handler.
	FastAGI defaults to port 4573 */
static enum agi_result launch_netscript(char *agiurl, char *argv[], int *fds, int *efd, int *opid)
{
	int s;
	int flags;
	struct pollfd pfds[1];
	char *host;
	char *c; int port = AGI_PORT;
	char *script="";
	struct sockaddr_in sin;
	struct hostent *hp;
	struct ast_hostent ahp;
	int res;

	/* agiusl is "agi://host.domain[:port][/script/name]" */
	host = ast_strdupa(agiurl + 6);	/* Remove agi:// */
	/* Strip off any script name */
	if ((c = strchr(host, '/'))) {
		*c = '\0';
		c++;
		script = c;
	}
	if ((c = strchr(host, ':'))) {
		*c = '\0';
		c++;
		port = atoi(c);
	}
	if (efd) {
		ast_log(LOG_WARNING, "AGI URI's don't support Enhanced AGI yet\n");
		return -1;
	}
	hp = ast_gethostbyname(host, &ahp);
	if (!hp) {
		ast_log(LOG_WARNING, "Unable to locate host '%s'\n", host);
		return -1;
	}
	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0) {
		ast_log(LOG_WARNING, "Unable to create socket: %s\n", strerror(errno));
		return -1;
	}
	flags = fcntl(s, F_GETFL);
	if (flags < 0) {
		ast_log(LOG_WARNING, "Fcntl(F_GETFL) failed: %s\n", strerror(errno));
		close(s);
		return -1;
	}
	if (fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0) {
		ast_log(LOG_WARNING, "Fnctl(F_SETFL) failed: %s\n", strerror(errno));
		close(s);
		return -1;
	}
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	memcpy(&sin.sin_addr, hp->h_addr, sizeof(sin.sin_addr));
	if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) && (errno != EINPROGRESS)) {
		ast_log(LOG_WARNING, "Connect failed with unexpected error: %s\n", strerror(errno));
		close(s);
		return AGI_RESULT_FAILURE;
	}

	pfds[0].fd = s;
	pfds[0].events = POLLOUT;
	while ((res = ast_poll(pfds, 1, MAX_AGI_CONNECT)) != 1) {
		if (errno != EINTR) {
			if (!res) {
				ast_log(LOG_WARNING, "FastAGI connection to '%s' timed out after MAX_AGI_CONNECT (%d) milliseconds.\n",
					agiurl, MAX_AGI_CONNECT);
			} else
				ast_log(LOG_WARNING, "Connect to '%s' failed: %s\n", agiurl, strerror(errno));
			close(s);
			return AGI_RESULT_FAILURE;
		}
	}

	if (fdprintf(s, "agi_network: yes\n") < 0) {
		if (errno != EINTR) {
			ast_log(LOG_WARNING, "Connect to '%s' failed: %s\n", agiurl, strerror(errno));
			close(s);
			return AGI_RESULT_FAILURE;
		}
	}

	/* If we have a script parameter, relay it to the fastagi server */
	if (!ast_strlen_zero(script))
		fdprintf(s, "agi_network_script: %s\n", script);

	if (option_debug > 3)
		ast_log(LOG_DEBUG, "Wow, connected!\n");
	fds[0] = s;
	fds[1] = s;
	*opid = -1;
	return AGI_RESULT_SUCCESS_FAST;
}

static enum agi_result launch_script(char *script, char *argv[], int *fds, int *efd, int *opid)
{
	char tmp[256];
	int pid;
	int toast[2];
	int fromast[2];
	int audio[2];
	int x;
	int res;
	sigset_t signal_set, old_set;
	
	if (!strncasecmp(script, "agi://", 6))
		return launch_netscript(script, argv, fds, efd, opid);
	
	if (script[0] != '/') {
		snprintf(tmp, sizeof(tmp), "%s/%s", (char *)ast_config_AST_AGI_DIR, script);
		script = tmp;
	}
	if (pipe(toast)) {
		ast_log(LOG_WARNING, "Unable to create toast pipe: %s\n",strerror(errno));
		return AGI_RESULT_FAILURE;
	}
	if (pipe(fromast)) {
		ast_log(LOG_WARNING, "unable to create fromast pipe: %s\n", strerror(errno));
		close(toast[0]);
		close(toast[1]);
		return AGI_RESULT_FAILURE;
	}
	if (efd) {
		if (pipe(audio)) {
			ast_log(LOG_WARNING, "unable to create audio pipe: %s\n", strerror(errno));
			close(fromast[0]);
			close(fromast[1]);
			close(toast[0]);
			close(toast[1]);
			return AGI_RESULT_FAILURE;
		}
		res = fcntl(audio[1], F_GETFL);
		if (res > -1) 
			res = fcntl(audio[1], F_SETFL, res | O_NONBLOCK);
		if (res < 0) {
			ast_log(LOG_WARNING, "unable to set audio pipe parameters: %s\n", strerror(errno));
			close(fromast[0]);
			close(fromast[1]);
			close(toast[0]);
			close(toast[1]);
			close(audio[0]);
			close(audio[1]);
			return AGI_RESULT_FAILURE;
		}
	}

	/* Block SIGHUP during the fork - prevents a race */
	sigfillset(&signal_set);
	pthread_sigmask(SIG_BLOCK, &signal_set, &old_set);
	pid = fork();
	if (pid < 0) {
		ast_log(LOG_WARNING, "Failed to fork(): %s\n", strerror(errno));
		pthread_sigmask(SIG_SETMASK, &old_set, NULL);
		return AGI_RESULT_FAILURE;
	}
	if (!pid) {
#ifdef HAVE_CAP
		cap_t cap = cap_from_text("cap_net_admin-eip");

		if (cap_set_proc(cap)) {
			/* Careful with order! Logging cannot happen after we close FDs */
			ast_log(LOG_WARNING, "Unable to remove capabilities.\n");
		}
		cap_free(cap);
#endif

		/* Pass paths to AGI via environmental variables */
		setenv("AST_CONFIG_DIR", ast_config_AST_CONFIG_DIR, 1);
		setenv("AST_CONFIG_FILE", ast_config_AST_CONFIG_FILE, 1);
		setenv("AST_MODULE_DIR", ast_config_AST_MODULE_DIR, 1);
		setenv("AST_SPOOL_DIR", ast_config_AST_SPOOL_DIR, 1);
		setenv("AST_MONITOR_DIR", ast_config_AST_MONITOR_DIR, 1);
		setenv("AST_VAR_DIR", ast_config_AST_VAR_DIR, 1);
		setenv("AST_DATA_DIR", ast_config_AST_DATA_DIR, 1);
		setenv("AST_LOG_DIR", ast_config_AST_LOG_DIR, 1);
		setenv("AST_AGI_DIR", ast_config_AST_AGI_DIR, 1);
		setenv("AST_KEY_DIR", ast_config_AST_KEY_DIR, 1);
		setenv("AST_RUN_DIR", ast_config_AST_RUN_DIR, 1);

		/* Don't run AGI scripts with realtime priority -- it causes audio stutter */
		ast_set_priority(0);

		/* Redirect stdin and out, provide enhanced audio channel if desired */
		dup2(fromast[0], STDIN_FILENO);
		dup2(toast[1], STDOUT_FILENO);
		if (efd) {
			dup2(audio[0], STDERR_FILENO + 1);
		} else {
			close(STDERR_FILENO + 1);
		}

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
			ast_log(LOG_WARNING, "unable to unblock signals for AGI script: %s\n", strerror(errno));
			_exit(1);
		}

		/* Close everything but stdin/out/error */
		for (x=STDERR_FILENO + 2;x<1024;x++) 
			close(x);

		/* Execute script */
		execv(script, argv);
		/* Can't use ast_log since FD's are closed */
		fprintf(stdout, "verbose \"Failed to execute '%s': %s\" 2\n", script, strerror(errno));
		/* Special case to set status of AGI to failure */
		fprintf(stdout, "failure\n");
		fflush(stdout);
		_exit(1);
	}
	pthread_sigmask(SIG_SETMASK, &old_set, NULL);
	if (option_verbose > 2) 
		ast_verbose(VERBOSE_PREFIX_3 "Launched AGI Script %s\n", script);
	fds[0] = toast[0];
	fds[1] = fromast[1];
	if (efd) {
		*efd = audio[1];
	}
	/* close what we're not using in the parent */
	close(toast[1]);
	close(fromast[0]);

	if (efd)
		close(audio[0]);

	*opid = pid;
	return AGI_RESULT_SUCCESS;
}

static void setup_env(struct ast_channel *chan, char *request, int fd, int enhanced)
{
	/* Print initial environment, with agi_request always being the first
	   thing */
	fdprintf(fd, "agi_request: %s\n", request);
	fdprintf(fd, "agi_channel: %s\n", chan->name);
	fdprintf(fd, "agi_language: %s\n", chan->language);
	fdprintf(fd, "agi_type: %s\n", chan->tech->type);
	fdprintf(fd, "agi_uniqueid: %s\n", chan->uniqueid);

	/* ANI/DNIS */
	fdprintf(fd, "agi_callerid: %s\n", S_OR(chan->cid.cid_num, "unknown"));
	fdprintf(fd, "agi_calleridname: %s\n", S_OR(chan->cid.cid_name, "unknown"));
	fdprintf(fd, "agi_callingpres: %d\n", chan->cid.cid_pres);
	fdprintf(fd, "agi_callingani2: %d\n", chan->cid.cid_ani2);
	fdprintf(fd, "agi_callington: %d\n", chan->cid.cid_ton);
	fdprintf(fd, "agi_callingtns: %d\n", chan->cid.cid_tns);
	fdprintf(fd, "agi_dnid: %s\n", S_OR(chan->cid.cid_dnid, "unknown"));
	fdprintf(fd, "agi_rdnis: %s\n", S_OR(chan->cid.cid_rdnis, "unknown"));

	/* Context information */
	fdprintf(fd, "agi_context: %s\n", chan->context);
	fdprintf(fd, "agi_extension: %s\n", chan->exten);
	fdprintf(fd, "agi_priority: %d\n", chan->priority);
	fdprintf(fd, "agi_enhanced: %s\n", enhanced ? "1.0" : "0.0");

	/* User information */
	fdprintf(fd, "agi_accountcode: %s\n", chan->accountcode ? chan->accountcode : "");
    
	/* End with empty return */
	fdprintf(fd, "\n");
}

static int handle_answer(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	int res;
	res = 0;
	if (chan->_state != AST_STATE_UP) {
		/* Answer the chan */
		res = ast_answer(chan);
	}
	fdprintf(agi->fd, "200 result=%d\n", res);
	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}

static int handle_waitfordigit(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	int res;
	int to;
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	if (sscanf(argv[3], "%30d", &to) != 1)
		return RESULT_SHOWUSAGE;
	res = ast_waitfordigit_full(chan, to, agi->audio, agi->ctrl);
	fdprintf(agi->fd, "200 result=%d\n", res);
	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}

static int handle_sendtext(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	int res;
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	/* At the moment, the parser (perhaps broken) returns with
	   the last argument PLUS the newline at the end of the input
	   buffer. This probably needs to be fixed, but I wont do that
	   because other stuff may break as a result. The right way
	   would probably be to strip off the trailing newline before
	   parsing, then here, add a newline at the end of the string
	   before sending it to ast_sendtext --DUDE */
	res = ast_sendtext(chan, argv[2]);
	fdprintf(agi->fd, "200 result=%d\n", res);
	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}

static int handle_recvchar(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	int res;
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	res = ast_recvchar(chan,atoi(argv[2]));
	if (res == 0) {
		fdprintf(agi->fd, "200 result=%d (timeout)\n", res);
		return RESULT_SUCCESS;
	}
	if (res > 0) {
		fdprintf(agi->fd, "200 result=%d\n", res);
		return RESULT_SUCCESS;
	}
	else {
		fdprintf(agi->fd, "200 result=%d (hangup)\n", res);
		return RESULT_FAILURE;
	}
}

static int handle_recvtext(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	char *buf;
	
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	buf = ast_recvtext(chan,atoi(argv[2]));
	if (buf) {
		fdprintf(agi->fd, "200 result=1 (%s)\n", buf);
		free(buf);
	} else {	
		fdprintf(agi->fd, "200 result=-1\n");
	}
	return RESULT_SUCCESS;
}

static int handle_tddmode(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	int res,x;
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	if (!strncasecmp(argv[2],"on",2)) 
		x = 1; 
	else 
		x = 0;
	if (!strncasecmp(argv[2],"mate",4)) 
		x = 2;
	if (!strncasecmp(argv[2],"tdd",3))
		x = 1;
	res = ast_channel_setoption(chan, AST_OPTION_TDD, &x, sizeof(char), 0);
	if (res != RESULT_SUCCESS)
		fdprintf(agi->fd, "200 result=0\n");
	else
		fdprintf(agi->fd, "200 result=1\n");
	return RESULT_SUCCESS;
}

static int handle_sendimage(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	int res;
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	res = ast_send_image(chan, argv[2]);
	if (!ast_check_hangup(chan))
		res = 0;
	fdprintf(agi->fd, "200 result=%d\n", res);
	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}

static int handle_controlstreamfile(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	int res = 0;
	int skipms = 3000;
	char *fwd = NULL;
	char *rev = NULL;
	char *pause = NULL;
	char *stop = NULL;

	if (argc < 5 || argc > 9)
		return RESULT_SHOWUSAGE;

	if (!ast_strlen_zero(argv[4]))
		stop = argv[4];
	else
		stop = NULL;
	
	if ((argc > 5) && (sscanf(argv[5], "%30d", &skipms) != 1))
		return RESULT_SHOWUSAGE;

	if (argc > 6 && !ast_strlen_zero(argv[6]))
		fwd = argv[6];
	else
		fwd = "#";

	if (argc > 7 && !ast_strlen_zero(argv[7]))
		rev = argv[7];
	else
		rev = "*";
	
	if (argc > 8 && !ast_strlen_zero(argv[8]))
		pause = argv[8];
	else
		pause = NULL;
	
	res = ast_control_streamfile(chan, argv[3], fwd, rev, stop, pause, NULL, skipms);
	
	fdprintf(agi->fd, "200 result=%d\n", res);

	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}

static int handle_streamfile(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	int res;
	int vres;	
	struct ast_filestream *fs;
	struct ast_filestream *vfs;
	long sample_offset = 0;
	long max_length;
	char *edigits = "";

	if (argc < 4 || argc > 5)
		return RESULT_SHOWUSAGE;

	if (argv[3]) 
		edigits = argv[3];

	if ((argc > 4) && (sscanf(argv[4], "%30ld", &sample_offset) != 1))
		return RESULT_SHOWUSAGE;
	
	fs = ast_openstream(chan, argv[2], chan->language);	
	
	if (!fs) {
		fdprintf(agi->fd, "200 result=%d endpos=%ld\n", 0, sample_offset);
		return RESULT_SUCCESS;
	}	
	vfs = ast_openvstream(chan, argv[2], chan->language);
	if (vfs)
		ast_log(LOG_DEBUG, "Ooh, found a video stream, too\n");
		
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Playing '%s' (escape_digits=%s) (sample_offset %ld)\n", argv[2], edigits, sample_offset);

	ast_seekstream(fs, 0, SEEK_END);
	max_length = ast_tellstream(fs);
	ast_seekstream(fs, sample_offset, SEEK_SET);
	res = ast_applystream(chan, fs);
	if (vfs)
		vres = ast_applystream(chan, vfs);
	ast_playstream(fs);
	if (vfs)
		ast_playstream(vfs);
	
	res = ast_waitstream_full(chan, argv[3], agi->audio, agi->ctrl);
	/* this is to check for if ast_waitstream closed the stream, we probably are at
	 * the end of the stream, return that amount, else check for the amount */
	sample_offset = (chan->stream) ? ast_tellstream(fs) : max_length;
	ast_stopstream(chan);
	if (res == 1) {
		/* Stop this command, don't print a result line, as there is a new command */
		return RESULT_SUCCESS;
	}
	fdprintf(agi->fd, "200 result=%d endpos=%ld\n", res, sample_offset);
	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}

/* get option - really similar to the handle_streamfile, but with a timeout */
static int handle_getoption(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	int res;
	int vres;	
	struct ast_filestream *fs;
	struct ast_filestream *vfs;
	long sample_offset = 0;
	long max_length;
	int timeout = 0;
	char *edigits = "";

	if ( argc < 4 || argc > 5 )
		return RESULT_SHOWUSAGE;

	if ( argv[3] ) 
		edigits = argv[3];

	if ( argc == 5 )
		timeout = atoi(argv[4]);
	else if (chan->pbx->dtimeout) {
		/* by default dtimeout is set to 5sec */
		timeout = chan->pbx->dtimeout * 1000; /* in msec */
	}

	fs = ast_openstream(chan, argv[2], chan->language);
	if (!fs) {
		fdprintf(agi->fd, "200 result=%d endpos=%ld\n", 0, sample_offset);
		ast_log(LOG_WARNING, "Unable to open %s\n", argv[2]);
		return RESULT_SUCCESS;
	}
	vfs = ast_openvstream(chan, argv[2], chan->language);
	if (vfs)
		ast_log(LOG_DEBUG, "Ooh, found a video stream, too\n");
	
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Playing '%s' (escape_digits=%s) (timeout %d)\n", argv[2], edigits, timeout);

	ast_seekstream(fs, 0, SEEK_END);
	max_length = ast_tellstream(fs);
	ast_seekstream(fs, sample_offset, SEEK_SET);
	res = ast_applystream(chan, fs);
	if (vfs)
		vres = ast_applystream(chan, vfs);
	ast_playstream(fs);
	if (vfs)
		ast_playstream(vfs);

	res = ast_waitstream_full(chan, argv[3], agi->audio, agi->ctrl);
	/* this is to check for if ast_waitstream closed the stream, we probably are at
	 * the end of the stream, return that amount, else check for the amount */
	sample_offset = (chan->stream)?ast_tellstream(fs):max_length;
	ast_stopstream(chan);
	if (res == 1) {
		/* Stop this command, don't print a result line, as there is a new command */
		return RESULT_SUCCESS;
	}

	/* If the user didnt press a key, wait for digitTimeout*/
	if (res == 0 ) {
		res = ast_waitfordigit_full(chan, timeout, agi->audio, agi->ctrl);
		/* Make sure the new result is in the escape digits of the GET OPTION */
		if ( !strchr(edigits,res) )
			res=0;
	}

        fdprintf(agi->fd, "200 result=%d endpos=%ld\n", res, sample_offset);
	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}




/*--- handle_saynumber: Say number in various language syntaxes ---*/
/* Need to add option for gender here as well. Coders wanted */
/* While waiting, we're sending a (char *) NULL.  */
static int handle_saynumber(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	int res;
	int num;
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	if (sscanf(argv[2], "%30d", &num) != 1)
		return RESULT_SHOWUSAGE;
	res = ast_say_number_full(chan, num, argv[3], chan->language, (char *) NULL, agi->audio, agi->ctrl);
	if (res == 1)
		return RESULT_SUCCESS;
	fdprintf(agi->fd, "200 result=%d\n", res);
	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}

static int handle_saydigits(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	int res;
	int num;

	if (argc != 4)
		return RESULT_SHOWUSAGE;
	if (sscanf(argv[2], "%30d", &num) != 1)
		return RESULT_SHOWUSAGE;

	res = ast_say_digit_str_full(chan, argv[2], argv[3], chan->language, agi->audio, agi->ctrl);
	if (res == 1) /* New command */
		return RESULT_SUCCESS;
	fdprintf(agi->fd, "200 result=%d\n", res);
	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}

static int handle_sayalpha(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	int res;

	if (argc != 4)
		return RESULT_SHOWUSAGE;

	res = ast_say_character_str_full(chan, argv[2], argv[3], chan->language, agi->audio, agi->ctrl);
	if (res == 1) /* New command */
		return RESULT_SUCCESS;
	fdprintf(agi->fd, "200 result=%d\n", res);
	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}

static int handle_saydate(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	int res;
	int num;
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	if (sscanf(argv[2], "%30d", &num) != 1)
		return RESULT_SHOWUSAGE;
	res = ast_say_date(chan, num, argv[3], chan->language);
	if (res == 1)
		return RESULT_SUCCESS;
	fdprintf(agi->fd, "200 result=%d\n", res);
	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}

static int handle_saytime(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	int res;
	int num;
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	if (sscanf(argv[2], "%30d", &num) != 1)
		return RESULT_SHOWUSAGE;
	res = ast_say_time(chan, num, argv[3], chan->language);
	if (res == 1)
		return RESULT_SUCCESS;
	fdprintf(agi->fd, "200 result=%d\n", res);
	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}

static int handle_saydatetime(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	int res=0;
	time_t unixtime;
	char *format, *zone=NULL;
	
	if (argc < 4)
		return RESULT_SHOWUSAGE;

	if (argc > 4) {
		format = argv[4];
	} else {
		/* XXX this doesn't belong here, but in the 'say' module */
		if (!strcasecmp(chan->language, "de")) {
			format = "A dBY HMS";
		} else {
			format = "ABdY 'digits/at' IMp"; 
		}
	}

	if (argc > 5 && !ast_strlen_zero(argv[5]))
		zone = argv[5];

	if (ast_get_time_t(argv[2], &unixtime, 0, NULL))
		return RESULT_SHOWUSAGE;

	res = ast_say_date_with_format(chan, unixtime, argv[3], chan->language, format, zone);
	if (res == 1)
		return RESULT_SUCCESS;

	fdprintf(agi->fd, "200 result=%d\n", res);
	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}

static int handle_sayphonetic(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	int res;

	if (argc != 4)
		return RESULT_SHOWUSAGE;

	res = ast_say_phonetic_str_full(chan, argv[2], argv[3], chan->language, agi->audio, agi->ctrl);
	if (res == 1) /* New command */
		return RESULT_SUCCESS;
	fdprintf(agi->fd, "200 result=%d\n", res);
	return (res >= 0) ? RESULT_SUCCESS : RESULT_FAILURE;
}

static int handle_getdata(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	int res;
	char data[1024];
	int max;
	int timeout;

	if (argc < 3)
		return RESULT_SHOWUSAGE;
	if (argc >= 4)
		timeout = atoi(argv[3]); 
	else
		timeout = 0;
	if (argc >= 5) 
		max = atoi(argv[4]); 
	else
		max = 1024;
	res = ast_app_getdata_full(chan, argv[2], data, max, timeout, agi->audio, agi->ctrl);
	if (res == 2)			/* New command */
		return RESULT_SUCCESS;
	else if (res == 1)
		fdprintf(agi->fd, "200 result=%s (timeout)\n", data);
	else if (res < 0 )
		fdprintf(agi->fd, "200 result=-1\n");
	else
		fdprintf(agi->fd, "200 result=%s\n", data);
	return RESULT_SUCCESS;
}

static int handle_setcontext(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{

	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_copy_string(chan->context, argv[2], sizeof(chan->context));
	fdprintf(agi->fd, "200 result=0\n");
	return RESULT_SUCCESS;
}
	
static int handle_setextension(struct ast_channel *chan, AGI *agi, int argc, char **argv)
{
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_copy_string(chan->exten, argv[2], sizeof(chan->exten));
	fdprintf(agi->fd, "200 result=0\n");
	return RESULT_SUCCESS;
}

static int handle_setpriority(struct ast_channel *chan, AGI *agi, int argc, char **argv)
{
	int pri;
	if (argc != 3)
		return RESULT_SHOWUSAGE;	

	if (sscanf(argv[2], "%30d", &pri) != 1) {
		if ((pri = ast_findlabel_extension(chan, chan->context, chan->exten, argv[2], chan->cid.cid_num)) < 1)
			return RESULT_SHOWUSAGE;
	}

	ast_explicit_goto(chan, NULL, NULL, pri);
	fdprintf(agi->fd, "200 result=0\n");
	return RESULT_SUCCESS;
}
		
static int handle_recordfile(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	struct ast_filestream *fs;
	struct ast_frame *f;
	struct timeval start;
	long sample_offset = 0;
	int res = 0;
	int ms;

        struct ast_dsp *sildet=NULL;         /* silence detector dsp */
        int totalsilence = 0;
        int dspsilence = 0;
        int silence = 0;                /* amount of silence to allow */
        int gotsilence = 0;             /* did we timeout for silence? */
        char *silencestr=NULL;
        int rfmt=0;


	/* XXX EAGI FIXME XXX */

	if (argc < 6)
		return RESULT_SHOWUSAGE;
	if (sscanf(argv[5], "%30d", &ms) != 1)
		return RESULT_SHOWUSAGE;

	if (argc > 6)
		silencestr = strchr(argv[6],'s');
	if ((argc > 7) && (!silencestr))
		silencestr = strchr(argv[7],'s');
	if ((argc > 8) && (!silencestr))
		silencestr = strchr(argv[8],'s');

	if (silencestr) {
		if (strlen(silencestr) > 2) {
			if ((silencestr[0] == 's') && (silencestr[1] == '=')) {
				silencestr++;
				silencestr++;
				if (silencestr)
	                		silence = atoi(silencestr);
        			if (silence > 0)
	                		silence *= 1000;
        		}
		}
	}

        if (silence > 0) {
        	rfmt = chan->readformat;
                res = ast_set_read_format(chan, AST_FORMAT_SLINEAR);
                if (res < 0) {
                	ast_log(LOG_WARNING, "Unable to set to linear mode, giving up\n");
                        return -1;
                }
               	sildet = ast_dsp_new();
                if (!sildet) {
                	ast_log(LOG_WARNING, "Unable to create silence detector :(\n");
                        return -1;
                }
               	ast_dsp_set_threshold(sildet, 256);
      	}

	/* backward compatibility, if no offset given, arg[6] would have been
	 * caught below and taken to be a beep, else if it is a digit then it is a
	 * offset */
	if ((argc >6) && (sscanf(argv[6], "%30ld", &sample_offset) != 1) && (!strchr(argv[6], '=')))
		res = ast_streamfile(chan, "beep", chan->language);

	if ((argc > 7) && (!strchr(argv[7], '=')))
		res = ast_streamfile(chan, "beep", chan->language);

	if (!res)
		res = ast_waitstream(chan, argv[4]);
	if (res) {
		fdprintf(agi->fd, "200 result=%d (randomerror) endpos=%ld\n", res, sample_offset);
	} else {
		fs = ast_writefile(argv[2], argv[3], NULL, O_CREAT | O_WRONLY | (sample_offset ? O_APPEND : 0), 0, 0644);
		if (!fs) {
			res = -1;
			fdprintf(agi->fd, "200 result=%d (writefile)\n", res);
			if (sildet)
				ast_dsp_free(sildet);
			return RESULT_FAILURE;
		}
		
		/* Request a video update */
		ast_indicate(chan, AST_CONTROL_VIDUPDATE);
	
		chan->stream = fs;
		ast_applystream(chan,fs);
		/* really should have checks */
		ast_seekstream(fs, sample_offset, SEEK_SET);
		ast_truncstream(fs);
		
		start = ast_tvnow();
		while ((ms < 0) || ast_tvdiff_ms(ast_tvnow(), start) < ms) {
			res = ast_waitfor(chan, ms - ast_tvdiff_ms(ast_tvnow(), start));
			if (res < 0) {
				ast_closestream(fs);
				fdprintf(agi->fd, "200 result=%d (waitfor) endpos=%ld\n", res,sample_offset);
				if (sildet)
					ast_dsp_free(sildet);
				return RESULT_FAILURE;
			}
			f = ast_read(chan);
			if (!f) {
				fdprintf(agi->fd, "200 result=%d (hangup) endpos=%ld\n", -1, sample_offset);
				ast_closestream(fs);
				if (sildet)
					ast_dsp_free(sildet);
				return RESULT_FAILURE;
			}
			switch(f->frametype) {
			case AST_FRAME_DTMF:
				if (strchr(argv[4], f->subclass)) {
					/* This is an interrupting chracter, so rewind to chop off any small
					   amount of DTMF that may have been recorded
					*/
					ast_stream_rewind(fs, 200);
					ast_truncstream(fs);
					sample_offset = ast_tellstream(fs);
					fdprintf(agi->fd, "200 result=%d (dtmf) endpos=%ld\n", f->subclass, sample_offset);
					ast_closestream(fs);
					ast_frfree(f);
					if (sildet)
						ast_dsp_free(sildet);
					return RESULT_SUCCESS;
				}
				break;
			case AST_FRAME_VOICE:
				ast_writestream(fs, f);
				/* this is a safe place to check progress since we know that fs
				 * is valid after a write, and it will then have our current
				 * location */
				sample_offset = ast_tellstream(fs);
                                if (silence > 0) {
                                	dspsilence = 0;
                                        ast_dsp_silence(sildet, f, &dspsilence);
                                        if (dspsilence) {
                                       		totalsilence = dspsilence;
                                        } else {
                                              	totalsilence = 0;
                                        }
                                        if (totalsilence > silence) {
                                             /* Ended happily with silence */
                                                gotsilence = 1;
                                                break;
                                        }
                            	}
				break;
			case AST_FRAME_VIDEO:
				ast_writestream(fs, f);
			default:
				/* Ignore all other frames */
				break;
			}
			ast_frfree(f);
			if (gotsilence)
				break;
        	}

              	if (gotsilence) {
                     	ast_stream_rewind(fs, silence-1000);
                	ast_truncstream(fs);
			sample_offset = ast_tellstream(fs);
		}		
		fdprintf(agi->fd, "200 result=%d (timeout) endpos=%ld\n", res, sample_offset);
		ast_closestream(fs);
	}

        if (silence > 0) {
                res = ast_set_read_format(chan, rfmt);
                if (res)
                        ast_log(LOG_WARNING, "Unable to restore read format on '%s'\n", chan->name);
                ast_dsp_free(sildet);
        }
	return RESULT_SUCCESS;
}

static int handle_autohangup(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	int timeout;

	if (argc != 3)
		return RESULT_SHOWUSAGE;
	if (sscanf(argv[2], "%30d", &timeout) != 1)
		return RESULT_SHOWUSAGE;
	if (timeout < 0)
		timeout = 0;
	if (timeout)
		chan->whentohangup = time(NULL) + timeout;
	else
		chan->whentohangup = 0;
	fdprintf(agi->fd, "200 result=0\n");
	return RESULT_SUCCESS;
}

static int handle_hangup(struct ast_channel *chan, AGI *agi, int argc, char **argv)
{
	struct ast_channel *c;
	if (argc == 1) {
		/* no argument: hangup the current channel */
		ast_softhangup(chan,AST_SOFTHANGUP_EXPLICIT);
		fdprintf(agi->fd, "200 result=1\n");
		return RESULT_SUCCESS;
	} else if (argc == 2) {
		/* one argument: look for info on the specified channel */
		c = ast_get_channel_by_name_locked(argv[1]);
		if (c) {
			/* we have a matching channel */
			ast_softhangup(c,AST_SOFTHANGUP_EXPLICIT);
			fdprintf(agi->fd, "200 result=1\n");
			ast_channel_unlock(c);
			return RESULT_SUCCESS;
		}
		/* if we get this far no channel name matched the argument given */
		fdprintf(agi->fd, "200 result=-1\n");
		return RESULT_SUCCESS;
	} else {
		return RESULT_SHOWUSAGE;
	}
}

static int handle_exec(struct ast_channel *chan, AGI *agi, int argc, char **argv)
{
	int res;
	struct ast_app *app;

	if (argc < 2)
		return RESULT_SHOWUSAGE;

	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "AGI Script Executing Application: (%s) Options: (%s)\n", argv[1], argv[2]);

	app = pbx_findapp(argv[1]);

	if (app) {
		if(!strcasecmp(argv[1], PARK_APP_NAME)) {
			ast_masq_park_call(chan, NULL, 0, NULL);
		}
		res = pbx_exec(chan, app, argv[2]);
	} else {
		ast_log(LOG_WARNING, "Could not find application (%s)\n", argv[1]);
		res = -2;
	}
	fdprintf(agi->fd, "200 result=%d\n", res);

	/* Even though this is wrong, users are depending upon this result. */
	return res;
}

static int handle_setcallerid(struct ast_channel *chan, AGI *agi, int argc, char **argv)
{
	char tmp[256]="";
	char *l = NULL, *n = NULL;

	if (argv[2]) {
		ast_copy_string(tmp, argv[2], sizeof(tmp));
		ast_callerid_parse(tmp, &n, &l);
		if (l)
			ast_shrink_phone_number(l);
		else
			l = "";
		if (!n)
			n = "";
		ast_set_callerid(chan, l, n, NULL);
	}

	fdprintf(agi->fd, "200 result=1\n");
	return RESULT_SUCCESS;
}

static int handle_channelstatus(struct ast_channel *chan, AGI *agi, int argc, char **argv)
{
	struct ast_channel *c;
	if (argc == 2) {
		/* no argument: supply info on the current channel */
		fdprintf(agi->fd, "200 result=%d\n", chan->_state);
		return RESULT_SUCCESS;
	} else if (argc == 3) {
		/* one argument: look for info on the specified channel */
		c = ast_get_channel_by_name_locked(argv[2]);
		if (c) {
			fdprintf(agi->fd, "200 result=%d\n", c->_state);
			ast_channel_unlock(c);
			return RESULT_SUCCESS;
		}
		/* if we get this far no channel name matched the argument given */
		fdprintf(agi->fd, "200 result=-1\n");
		return RESULT_SUCCESS;
	} else {
		return RESULT_SHOWUSAGE;
	}
}

static int handle_setvariable(struct ast_channel *chan, AGI *agi, int argc, char **argv)
{
	if (argv[3])
		pbx_builtin_setvar_helper(chan, argv[2], argv[3]);

	fdprintf(agi->fd, "200 result=1\n");
	return RESULT_SUCCESS;
}

static int handle_getvariable(struct ast_channel *chan, AGI *agi, int argc, char **argv)
{
	char *ret;
	char tempstr[1024];

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	/* check if we want to execute an ast_custom_function */
	if (!ast_strlen_zero(argv[2]) && (argv[2][strlen(argv[2]) - 1] == ')')) {
		ret = ast_func_read(chan, argv[2], tempstr, sizeof(tempstr)) ? NULL : tempstr;
	} else {
		pbx_retrieve_variable(chan, argv[2], &ret, tempstr, sizeof(tempstr), NULL);
	}

	if (ret)
		fdprintf(agi->fd, "200 result=1 (%s)\n", ret);
	else
		fdprintf(agi->fd, "200 result=0\n");

	return RESULT_SUCCESS;
}

static int handle_getvariablefull(struct ast_channel *chan, AGI *agi, int argc, char **argv)
{
	char tmp[4096] = "";
	struct ast_channel *chan2=NULL;

	if ((argc != 4) && (argc != 5))
		return RESULT_SHOWUSAGE;
	if (argc == 5) {
		chan2 = ast_get_channel_by_name_locked(argv[4]);
	} else {
		chan2 = chan;
	}
	if (chan2) {
		pbx_substitute_variables_helper(chan2, argv[3], tmp, sizeof(tmp) - 1);
		fdprintf(agi->fd, "200 result=1 (%s)\n", tmp);
	} else {
		fdprintf(agi->fd, "200 result=0\n");
	}
	if (chan2 && (chan2 != chan))
		ast_channel_unlock(chan2);
	return RESULT_SUCCESS;
}

static int handle_verbose(struct ast_channel *chan, AGI *agi, int argc, char **argv)
{
	int level = 0;
	char *prefix;

	if (argc < 2)
		return RESULT_SHOWUSAGE;

	if (argv[2])
		sscanf(argv[2], "%30d", &level);

	switch (level) {
		case 4:
			prefix = VERBOSE_PREFIX_4;
			break;
		case 3:
			prefix = VERBOSE_PREFIX_3;
			break;
		case 2:
			prefix = VERBOSE_PREFIX_2;
			break;
		case 1:
		default:
			prefix = VERBOSE_PREFIX_1;
			break;
	}

	if (level <= option_verbose)
		ast_verbose("%s %s: %s\n", prefix, chan->data, argv[1]);
	
	fdprintf(agi->fd, "200 result=1\n");
	
	return RESULT_SUCCESS;
}

static int handle_dbget(struct ast_channel *chan, AGI *agi, int argc, char **argv)
{
	int res;
	size_t bufsize = 16;
	char *buf, *tmp;

	if (argc != 4)
		return RESULT_SHOWUSAGE;

	if (!(buf = ast_malloc(bufsize))) {
		fdprintf(agi->fd, "200 result=-1\n");
		return RESULT_SUCCESS;
	}

	do {
		res = ast_db_get(argv[2], argv[3], buf, bufsize);
		if (strlen(buf) < bufsize - 1) {
			break;
		}
		bufsize *= 2;
		if (!(tmp = ast_realloc(buf, bufsize))) {
			break;
		}
		buf = tmp;
	} while (1);
	
	if (res) 
		fdprintf(agi->fd, "200 result=0\n");
	else
		fdprintf(agi->fd, "200 result=1 (%s)\n", buf);

	ast_free(buf);
	return RESULT_SUCCESS;
}

static int handle_dbput(struct ast_channel *chan, AGI *agi, int argc, char **argv)
{
	int res;

	if (argc != 5)
		return RESULT_SHOWUSAGE;
	res = ast_db_put(argv[2], argv[3], argv[4]);
	fdprintf(agi->fd, "200 result=%c\n", res ? '0' : '1');
	return RESULT_SUCCESS;
}

static int handle_dbdel(struct ast_channel *chan, AGI *agi, int argc, char **argv)
{
	int res;

	if (argc != 4)
		return RESULT_SHOWUSAGE;
	res = ast_db_del(argv[2], argv[3]);
	fdprintf(agi->fd, "200 result=%c\n", res ? '0' : '1');
	return RESULT_SUCCESS;
}

static int handle_dbdeltree(struct ast_channel *chan, AGI *agi, int argc, char **argv)
{
	int res;
	if ((argc < 3) || (argc > 4))
		return RESULT_SHOWUSAGE;
	if (argc == 4)
		res = ast_db_deltree(argv[2], argv[3]);
	else
		res = ast_db_deltree(argv[2], NULL);

	fdprintf(agi->fd, "200 result=%c\n", res ? '0' : '1');
	return RESULT_SUCCESS;
}

static char debug_usage[] = 
"Usage: agi debug\n"
"       Enables dumping of AGI transactions for debugging purposes\n";

static char no_debug_usage[] = 
"Usage: agi debug off\n"
"       Disables dumping of AGI transactions for debugging purposes\n";

static int agi_do_debug(int fd, int argc, char *argv[])
{
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	agidebug = 1;
	ast_cli(fd, "AGI Debugging Enabled\n");
	return RESULT_SUCCESS;
}

static int agi_no_debug_deprecated(int fd, int argc, char *argv[])
{
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	agidebug = 0;
	ast_cli(fd, "AGI Debugging Disabled\n");
	return RESULT_SUCCESS;
}

static int agi_no_debug(int fd, int argc, char *argv[])
{
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	agidebug = 0;
	ast_cli(fd, "AGI Debugging Disabled\n");
	return RESULT_SUCCESS;
}

static int handle_noop(struct ast_channel *chan, AGI *agi, int arg, char *argv[])
{
	fdprintf(agi->fd, "200 result=0\n");
	return RESULT_SUCCESS;
}

static int handle_setmusic(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	if (!strncasecmp(argv[2], "on", 2))
		ast_moh_start(chan, argc > 3 ? argv[3] : NULL, NULL);
	else if (!strncasecmp(argv[2], "off", 3))
		ast_moh_stop(chan);
	fdprintf(agi->fd, "200 result=0\n");
	return RESULT_SUCCESS;
}

static char usage_setmusic[] =
" Usage: SET MUSIC ON <on|off> <class>\n"
"	Enables/Disables the music on hold generator.  If <class> is\n"
" not specified, then the default music on hold class will be used.\n"
" Always returns 0.\n";

static char usage_dbput[] =
" Usage: DATABASE PUT <family> <key> <value>\n"
"	Adds or updates an entry in the Asterisk database for a\n"
" given family, key, and value.\n"
" Returns 1 if successful, 0 otherwise.\n";

static char usage_dbget[] =
" Usage: DATABASE GET <family> <key>\n"
"	Retrieves an entry in the Asterisk database for a\n"
" given family and key.\n"
" Returns 0 if <key> is not set.  Returns 1 if <key>\n"
" is set and returns the variable in parentheses.\n"
" Example return code: 200 result=1 (testvariable)\n";

static char usage_dbdel[] =
" Usage: DATABASE DEL <family> <key>\n"
"	Deletes an entry in the Asterisk database for a\n"
" given family and key.\n"
" Returns 1 if successful, 0 otherwise.\n";

static char usage_dbdeltree[] =
" Usage: DATABASE DELTREE <family> [keytree]\n"
"	Deletes a family or specific keytree within a family\n"
" in the Asterisk database.\n"
" Returns 1 if successful, 0 otherwise.\n";

static char usage_verbose[] =
" Usage: VERBOSE <message> <level>\n"
"	Sends <message> to the console via verbose message system.\n"
" <level> is the the verbose level (1-4)\n"
" Always returns 1.\n";

static char usage_getvariable[] =
" Usage: GET VARIABLE <variablename>\n"
"	Returns 0 if <variablename> is not set.  Returns 1 if <variablename>\n"
" is set and returns the variable in parentheses.\n"
" example return code: 200 result=1 (testvariable)\n";

static char usage_getvariablefull[] =
" Usage: GET FULL VARIABLE <variablename> [<channel name>]\n"
"	Returns 0 if <variablename> is not set or channel does not exist.  Returns 1\n"
"if <variablename>  is set and returns the variable in parenthesis.  Understands\n"
"complex variable names and builtin variables, unlike GET VARIABLE.\n"
" example return code: 200 result=1 (testvariable)\n";

static char usage_setvariable[] =
" Usage: SET VARIABLE <variablename> <value>\n";

static char usage_channelstatus[] =
" Usage: CHANNEL STATUS [<channelname>]\n"
"	Returns the status of the specified channel.\n" 
" If no channel name is given the returns the status of the\n"
" current channel.  Return values:\n"
"  0 Channel is down and available\n"
"  1 Channel is down, but reserved\n"
"  2 Channel is off hook\n"
"  3 Digits (or equivalent) have been dialed\n"
"  4 Line is ringing\n"
"  5 Remote end is ringing\n"
"  6 Line is up\n"
"  7 Line is busy\n";

static char usage_setcallerid[] =
" Usage: SET CALLERID <number>\n"
"	Changes the callerid of the current channel.\n";

static char usage_exec[] =
" Usage: EXEC <application> <options>\n"
"	Executes <application> with given <options>.\n"
" Returns whatever the application returns, or -2 on failure to find application\n";

static char usage_hangup[] =
" Usage: HANGUP [<channelname>]\n"
"	Hangs up the specified channel.\n"
" If no channel name is given, hangs up the current channel\n";

static char usage_answer[] = 
" Usage: ANSWER\n"
"	Answers channel if not already in answer state. Returns -1 on\n"
" channel failure, or 0 if successful.\n";

static char usage_waitfordigit[] = 
" Usage: WAIT FOR DIGIT <timeout>\n"
"	Waits up to 'timeout' milliseconds for channel to receive a DTMF digit.\n"
" Returns -1 on channel failure, 0 if no digit is received in the timeout, or\n"
" the numerical value of the ascii of the digit if one is received.  Use -1\n"
" for the timeout value if you desire the call to block indefinitely.\n";

static char usage_sendtext[] =
" Usage: SEND TEXT \"<text to send>\"\n"
"	Sends the given text on a channel. Most channels do not support the\n"
" transmission of text.  Returns 0 if text is sent, or if the channel does not\n"
" support text transmission.  Returns -1 only on error/hangup.  Text\n"
" consisting of greater than one word should be placed in quotes since the\n"
" command only accepts a single argument.\n";

static char usage_recvchar[] =
" Usage: RECEIVE CHAR <timeout>\n"
"	Receives a character of text on a channel. Specify timeout to be the\n"
" maximum time to wait for input in milliseconds, or 0 for infinite. Most channels\n"
" do not support the reception of text. Returns the decimal value of the character\n"
" if one is received, or 0 if the channel does not support text reception.  Returns\n"
" -1 only on error/hangup.\n";

static char usage_recvtext[] =
" Usage: RECEIVE TEXT <timeout>\n"
"	Receives a string of text on a channel. Specify timeout to be the\n"
" maximum time to wait for input in milliseconds, or 0 for infinite. Most channels\n"
" do not support the reception of text. Returns -1 for failure or 1 for success, and the string in parentheses.\n";

static char usage_tddmode[] =
" Usage: TDD MODE <on|off>\n"
"	Enable/Disable TDD transmission/reception on a channel. Returns 1 if\n"
" successful, or 0 if channel is not TDD-capable.\n";

static char usage_sendimage[] =
" Usage: SEND IMAGE <image>\n"
"	Sends the given image on a channel. Most channels do not support the\n"
" transmission of images. Returns 0 if image is sent, or if the channel does not\n"
" support image transmission.  Returns -1 only on error/hangup. Image names\n"
" should not include extensions.\n";

static char usage_streamfile[] =
" Usage: STREAM FILE <filename> <escape digits> [sample offset]\n"
"	Send the given file, allowing playback to be interrupted by the given\n"
" digits, if any. Use double quotes for the digits if you wish none to be\n"
" permitted. If sample offset is provided then the audio will seek to sample\n"
" offset before play starts.  Returns 0 if playback completes without a digit\n"
" being pressed, or the ASCII numerical value of the digit if one was pressed,\n"
" or -1 on error or if the channel was disconnected. Remember, the file\n"
" extension must not be included in the filename.\n";

static char usage_controlstreamfile[] =
" Usage: CONTROL STREAM FILE <filename> <escape digits> [skipms] [ffchar] [rewchr] [pausechr]\n"
"	Send the given file, allowing playback to be controled by the given\n"
" digits, if any. Use double quotes for the digits if you wish none to be\n"
" permitted.  Returns 0 if playback completes without a digit\n"
" being pressed, or the ASCII numerical value of the digit if one was pressed,\n"
" or -1 on error or if the channel was disconnected. Remember, the file\n"
" extension must not be included in the filename.\n\n"
" Note: ffchar and rewchar default to * and # respectively.\n";

static char usage_getoption[] = 
" Usage: GET OPTION <filename> <escape_digits> [timeout]\n"
"	Behaves similar to STREAM FILE but used with a timeout option.\n";

static char usage_saynumber[] =
" Usage: SAY NUMBER <number> <escape digits>\n"
"	Say a given number, returning early if any of the given DTMF digits\n"
" are received on the channel.  Returns 0 if playback completes without a digit\n"
" being pressed, or the ASCII numerical value of the digit if one was pressed or\n"
" -1 on error/hangup.\n";

static char usage_saydigits[] =
" Usage: SAY DIGITS <number> <escape digits>\n"
"	Say a given digit string, returning early if any of the given DTMF digits\n"
" are received on the channel. Returns 0 if playback completes without a digit\n"
" being pressed, or the ASCII numerical value of the digit if one was pressed or\n"
" -1 on error/hangup.\n";

static char usage_sayalpha[] =
" Usage: SAY ALPHA <number> <escape digits>\n"
"	Say a given character string, returning early if any of the given DTMF digits\n"
" are received on the channel. Returns 0 if playback completes without a digit\n"
" being pressed, or the ASCII numerical value of the digit if one was pressed or\n"
" -1 on error/hangup.\n";

static char usage_saydate[] =
" Usage: SAY DATE <date> <escape digits>\n"
"	Say a given date, returning early if any of the given DTMF digits are\n"
" received on the channel.  <date> is number of seconds elapsed since 00:00:00\n"
" on January 1, 1970, Coordinated Universal Time (UTC). Returns 0 if playback\n"
" completes without a digit being pressed, or the ASCII numerical value of the\n"
" digit if one was pressed or -1 on error/hangup.\n";

static char usage_saytime[] =
" Usage: SAY TIME <time> <escape digits>\n"
"	Say a given time, returning early if any of the given DTMF digits are\n"
" received on the channel.  <time> is number of seconds elapsed since 00:00:00\n"
" on January 1, 1970, Coordinated Universal Time (UTC). Returns 0 if playback\n"
" completes without a digit being pressed, or the ASCII numerical value of the\n"
" digit if one was pressed or -1 on error/hangup.\n";

static char usage_saydatetime[] =
" Usage: SAY DATETIME <time> <escape digits> [format] [timezone]\n"
"	Say a given time, returning early if any of the given DTMF digits are\n"
" received on the channel.  <time> is number of seconds elapsed since 00:00:00\n"
" on January 1, 1970, Coordinated Universal Time (UTC). [format] is the format\n"
" the time should be said in.  See voicemail.conf (defaults to \"ABdY\n"
" 'digits/at' IMp\").  Acceptable values for [timezone] can be found in\n"
" /usr/share/zoneinfo.  Defaults to machine default. Returns 0 if playback\n"
" completes without a digit being pressed, or the ASCII numerical value of the\n"
" digit if one was pressed or -1 on error/hangup.\n";

static char usage_sayphonetic[] =
" Usage: SAY PHONETIC <string> <escape digits>\n"
"	Say a given character string with phonetics, returning early if any of the\n"
" given DTMF digits are received on the channel. Returns 0 if playback\n"
" completes without a digit pressed, the ASCII numerical value of the digit\n"
" if one was pressed, or -1 on error/hangup.\n";

static char usage_getdata[] =
" Usage: GET DATA <file to be streamed> [timeout] [max digits]\n"
"	Stream the given file, and recieve DTMF data. Returns the digits received\n"
"from the channel at the other end.\n";

static char usage_setcontext[] =
" Usage: SET CONTEXT <desired context>\n"
"	Sets the context for continuation upon exiting the application.\n";

static char usage_setextension[] =
" Usage: SET EXTENSION <new extension>\n"
"	Changes the extension for continuation upon exiting the application.\n";

static char usage_setpriority[] =
" Usage: SET PRIORITY <priority>\n"
"	Changes the priority for continuation upon exiting the application.\n"
" The priority must be a valid priority or label.\n";

static char usage_recordfile[] =
" Usage: RECORD FILE <filename> <format> <escape digits> <timeout> \\\n"
"                                          [offset samples] [BEEP] [s=silence]\n"
"	Record to a file until a given dtmf digit in the sequence is received\n"
" Returns -1 on hangup or error.  The format will specify what kind of file\n"
" will be recorded.  The timeout is the maximum record time in milliseconds, or\n"
" -1 for no timeout. \"Offset samples\" is optional, and, if provided, will seek\n"
" to the offset without exceeding the end of the file.  \"silence\" is the number\n"
" of seconds of silence allowed before the function returns despite the\n"
" lack of dtmf digits or reaching timeout.  Silence value must be\n"
" preceeded by \"s=\" and is also optional.\n";

static char usage_autohangup[] =
" Usage: SET AUTOHANGUP <time>\n"
"	Cause the channel to automatically hangup at <time> seconds in the\n"
" future.  Of course it can be hungup before then as well. Setting to 0 will\n"
" cause the autohangup feature to be disabled on this channel.\n";

static char usage_noop[] =
" Usage: NoOp\n"
"	Does nothing.\n";

static agi_command commands[MAX_COMMANDS] = {
	{ { "answer", NULL }, handle_answer, "Answer channel", usage_answer },
	{ { "channel", "status", NULL }, handle_channelstatus, "Returns status of the connected channel", usage_channelstatus },
	{ { "database", "del", NULL }, handle_dbdel, "Removes database key/value", usage_dbdel },
	{ { "database", "deltree", NULL }, handle_dbdeltree, "Removes database keytree/value", usage_dbdeltree },
	{ { "database", "get", NULL }, handle_dbget, "Gets database value", usage_dbget },
	{ { "database", "put", NULL }, handle_dbput, "Adds/updates database value", usage_dbput },
	{ { "exec", NULL }, handle_exec, "Executes a given Application", usage_exec },
	{ { "get", "data", NULL }, handle_getdata, "Prompts for DTMF on a channel", usage_getdata },
	{ { "get", "full", "variable", NULL }, handle_getvariablefull, "Evaluates a channel expression", usage_getvariablefull },
	{ { "get", "option", NULL }, handle_getoption, "Stream file, prompt for DTMF, with timeout", usage_getoption },
	{ { "get", "variable", NULL }, handle_getvariable, "Gets a channel variable", usage_getvariable },
	{ { "hangup", NULL }, handle_hangup, "Hangup the current channel", usage_hangup },
	{ { "noop", NULL }, handle_noop, "Does nothing", usage_noop },
	{ { "receive", "char", NULL }, handle_recvchar, "Receives one character from channels supporting it", usage_recvchar },
	{ { "receive", "text", NULL }, handle_recvtext, "Receives text from channels supporting it", usage_recvtext },
	{ { "record", "file", NULL }, handle_recordfile, "Records to a given file", usage_recordfile },
	{ { "say", "alpha", NULL }, handle_sayalpha, "Says a given character string", usage_sayalpha },
	{ { "say", "digits", NULL }, handle_saydigits, "Says a given digit string", usage_saydigits },
	{ { "say", "number", NULL }, handle_saynumber, "Says a given number", usage_saynumber },
	{ { "say", "phonetic", NULL }, handle_sayphonetic, "Says a given character string with phonetics", usage_sayphonetic },
	{ { "say", "date", NULL }, handle_saydate, "Says a given date", usage_saydate },
	{ { "say", "time", NULL }, handle_saytime, "Says a given time", usage_saytime },
	{ { "say", "datetime", NULL }, handle_saydatetime, "Says a given time as specfied by the format given", usage_saydatetime },
	{ { "send", "image", NULL }, handle_sendimage, "Sends images to channels supporting it", usage_sendimage },
	{ { "send", "text", NULL }, handle_sendtext, "Sends text to channels supporting it", usage_sendtext },
	{ { "set", "autohangup", NULL }, handle_autohangup, "Autohangup channel in some time", usage_autohangup },
	{ { "set", "callerid", NULL }, handle_setcallerid, "Sets callerid for the current channel", usage_setcallerid },
	{ { "set", "context", NULL }, handle_setcontext, "Sets channel context", usage_setcontext },
	{ { "set", "extension", NULL }, handle_setextension, "Changes channel extension", usage_setextension },
	{ { "set", "music", NULL }, handle_setmusic, "Enable/Disable Music on hold generator", usage_setmusic },
	{ { "set", "priority", NULL }, handle_setpriority, "Set channel dialplan priority", usage_setpriority },
	{ { "set", "variable", NULL }, handle_setvariable, "Sets a channel variable", usage_setvariable },
	{ { "stream", "file", NULL }, handle_streamfile, "Sends audio file on channel", usage_streamfile },
	{ { "control", "stream", "file", NULL }, handle_controlstreamfile, "Sends audio file on channel and allows the listner to control the stream", usage_controlstreamfile },
	{ { "tdd", "mode", NULL }, handle_tddmode, "Toggles TDD mode (for the deaf)", usage_tddmode },
	{ { "verbose", NULL }, handle_verbose, "Logs a message to the asterisk verbose log", usage_verbose },
	{ { "wait", "for", "digit", NULL }, handle_waitfordigit, "Waits for a digit to be pressed", usage_waitfordigit },
};

static int help_workhorse(int fd, char *match[])
{
	char fullcmd[80];
	char matchstr[80];
	int x;
	struct agi_command *e;
	if (match)
		ast_join(matchstr, sizeof(matchstr), match);
	for (x=0;x<sizeof(commands)/sizeof(commands[0]);x++) {
		e = &commands[x]; 
		if (!e->cmda[0])
			break;
		/* Hide commands that start with '_' */
		if ((e->cmda[0])[0] == '_')
			continue;
		ast_join(fullcmd, sizeof(fullcmd), e->cmda);
		if (match && strncasecmp(matchstr, fullcmd, strlen(matchstr)))
			continue;
		ast_cli(fd, "%20.20s   %s\n", fullcmd, e->summary);
	}
	return 0;
}

int ast_agi_register(agi_command *agi)
{
	int x;
	for (x=0; x<MAX_COMMANDS - 1; x++) {
		if (commands[x].cmda[0] == agi->cmda[0]) {
			ast_log(LOG_WARNING, "Command already registered!\n");
			return -1;
		}
	}
	for (x=0; x<MAX_COMMANDS - 1; x++) {
		if (!commands[x].cmda[0]) {
			commands[x] = *agi;
			return 0;
		}
	}
	ast_log(LOG_WARNING, "No more room for new commands!\n");
	return -1;
}

void ast_agi_unregister(agi_command *agi)
{
	int x;
	for (x=0; x<MAX_COMMANDS - 1; x++) {
		if (commands[x].cmda[0] == agi->cmda[0]) {
			memset(&commands[x], 0, sizeof(agi_command));
		}
	}
}

static agi_command *find_command(char *cmds[], int exact)
{
	int x;
	int y;
	int match;

	for (x=0; x < sizeof(commands) / sizeof(commands[0]); x++) {
		if (!commands[x].cmda[0])
			break;
		/* start optimistic */
		match = 1;
		for (y=0; match && cmds[y]; y++) {
			/* If there are no more words in the command (and we're looking for
			   an exact match) or there is a difference between the two words,
			   then this is not a match */
			if (!commands[x].cmda[y] && !exact)
				break;
			/* don't segfault if the next part of a command doesn't exist */
			if (!commands[x].cmda[y])
				return NULL;
			if (strcasecmp(commands[x].cmda[y], cmds[y]))
				match = 0;
		}
		/* If more words are needed to complete the command then this is not
		   a candidate (unless we're looking for a really inexact answer  */
		if ((exact > -1) && commands[x].cmda[y])
			match = 0;
		if (match)
			return &commands[x];
	}
	return NULL;
}


static int parse_args(char *s, int *max, char *argv[])
{
	int x=0;
	int quoted=0;
	int escaped=0;
	int whitespace=1;
	char *cur;

	cur = s;
	while(*s) {
		switch(*s) {
		case '"':
			/* If it's escaped, put a literal quote */
			if (escaped) 
				goto normal;
			else 
				quoted = !quoted;
			if (quoted && whitespace) {
				/* If we're starting a quote, coming off white space start a new word, too */
				argv[x++] = cur;
				whitespace=0;
			}
			escaped = 0;
		break;
		case ' ':
		case '\t':
			if (!quoted && !escaped) {
				/* If we're not quoted, mark this as whitespace, and
				   end the previous argument */
				whitespace = 1;
				*(cur++) = '\0';
			} else
				/* Otherwise, just treat it as anything else */ 
				goto normal;
			break;
		case '\\':
			/* If we're escaped, print a literal, otherwise enable escaping */
			if (escaped) {
				goto normal;
			} else {
				escaped=1;
			}
			break;
		default:
normal:
			if (whitespace) {
				if (x >= MAX_ARGS -1) {
					ast_log(LOG_WARNING, "Too many arguments, truncating\n");
					break;
				}
				/* Coming off of whitespace, start the next argument */
				argv[x++] = cur;
				whitespace=0;
			}
			*(cur++) = *s;
			escaped=0;
		}
		s++;
	}
	/* Null terminate */
	*(cur++) = '\0';
	argv[x] = NULL;
	*max = x;
	return 0;
}

static int agi_handle_command(struct ast_channel *chan, AGI *agi, char *buf)
{
	char *argv[MAX_ARGS];
	int argc = MAX_ARGS;
	int res;
	agi_command *c;

	parse_args(buf, &argc, argv);
	c = find_command(argv, 0);
	if (c) {
		/* If the AGI command being executed is an actual application (using agi exec)
		the app field will be updated in pbx_exec via handle_exec */
		if (chan->cdr && !ast_check_hangup(chan) && strcasecmp(argv[0], "EXEC"))
			ast_cdr_setapp(chan->cdr, "AGI", buf);

		res = c->handler(chan, agi, argc, argv);
		switch(res) {
		case RESULT_SHOWUSAGE:
			fdprintf(agi->fd, "520-Invalid command syntax.  Proper usage follows:\n");
			fdprintf(agi->fd, "%s", c->usage);
			fdprintf(agi->fd, "520 End of proper usage.\n");
			break;
		case AST_PBX_KEEPALIVE:
			/* We've been asked to keep alive, so do so */
			return AST_PBX_KEEPALIVE;
			break;
		case RESULT_FAILURE:
			/* They've already given the failure.  We've been hung up on so handle this
			   appropriately */
			return -1;
		}
	} else {
		fdprintf(agi->fd, "510 Invalid or unknown command\n");
	}
	return 0;
}
static enum agi_result run_agi(struct ast_channel *chan, char *request, AGI *agi, int pid, int *status, int dead)
{
	struct ast_channel *c;
	int outfd;
	int ms;
	enum agi_result returnstatus = AGI_RESULT_SUCCESS;
	struct ast_frame *f;
	char buf[AGI_BUF_LEN];
	char *res = NULL;
	FILE *readf;
	/* how many times we'll retry if ast_waitfor_nandfs will return without either 
	  channel or file descriptor in case select is interrupted by a system call (EINTR) */
	int retry = AGI_NANDFS_RETRY;

	if (!(readf = fdopen(agi->ctrl, "r"))) {
		ast_log(LOG_WARNING, "Unable to fdopen file descriptor\n");
		if (pid > -1)
			kill(pid, SIGHUP);
		close(agi->ctrl);
		return AGI_RESULT_FAILURE;
	}
	setlinebuf(readf);
	setup_env(chan, request, agi->fd, (agi->audio > -1));
	for (;;) {
		ms = -1;
		c = ast_waitfor_nandfds(&chan, dead ? 0 : 1, &agi->ctrl, 1, NULL, &outfd, &ms);
		if (c) {
			retry = AGI_NANDFS_RETRY;
			/* Idle the channel until we get a command */
			f = ast_read(c);
			if (!f) {
				ast_log(LOG_DEBUG, "%s hungup\n", chan->name);
				returnstatus = AGI_RESULT_HANGUP;
				break;
			} else {
				/* If it's voice, write it to the audio pipe */
				if ((agi->audio > -1) && (f->frametype == AST_FRAME_VOICE)) {
					/* Write, ignoring errors */
					if (write(agi->audio, f->data, f->datalen) < 0) {
					}
				}
				ast_frfree(f);
			}
		} else if (outfd > -1) {
			size_t len = sizeof(buf);
			size_t buflen = 0;

			retry = AGI_NANDFS_RETRY;
			buf[0] = '\0';

			while (buflen < (len - 1)) {
				res = fgets(buf + buflen, len, readf);
				if (feof(readf)) 
					break;
				if (ferror(readf) && ((errno != EINTR) && (errno != EAGAIN))) 
					break;
				if (res != NULL && !agi->fast)
					break;
				buflen = strlen(buf);
				if (buflen && buf[buflen - 1] == '\n')
					break;
				len -= buflen;
				if (agidebug)
					ast_verbose( "AGI Rx << temp buffer %s - errno %s\n", buf, strerror(errno));
			}

			if (!buf[0]) {
				/* Program terminated */
				if (returnstatus)
					returnstatus = -1;
				if (option_verbose > 2) 
					ast_verbose(VERBOSE_PREFIX_3 "AGI Script %s completed, returning %d\n", request, returnstatus);
				if (pid > 0)
					waitpid(pid, status, 0);
				/* No need to kill the pid anymore, since they closed us */
				pid = -1;
				break;
			}

			/* Special case for inability to execute child process */
			if (*buf && strncasecmp(buf, "failure", 7) == 0) {
				returnstatus = AGI_RESULT_FAILURE;
				break;
			}

			/* get rid of trailing newline, if any */
			if (*buf && buf[strlen(buf) - 1] == '\n')
				buf[strlen(buf) - 1] = 0;
			if (agidebug)
				ast_verbose("AGI Rx << %s\n", buf);
			returnstatus |= agi_handle_command(chan, agi, buf);
			/* If the handle_command returns -1, we need to stop */
			if ((returnstatus < 0) || (returnstatus == AST_PBX_KEEPALIVE)) {
				break;
			}
		} else {
			if (--retry <= 0) {
				ast_log(LOG_WARNING, "No channel, no fd?\n");
				returnstatus = AGI_RESULT_FAILURE;
				break;
			}
		}
	}
	/* Notify process */
	if (pid > -1) {
		const char *sighup = pbx_builtin_getvar_helper(chan, "AGISIGHUP");
		if (ast_strlen_zero(sighup) || !ast_false(sighup)) {
			if (kill(pid, SIGHUP)) {
				ast_log(LOG_WARNING, "unable to send SIGHUP to AGI process %d: %s\n", pid, strerror(errno));
			} else { /* Give the process a chance to die */
				usleep(1);
			}
		}
		/* This is essentially doing the same as without WNOHANG, except that
		 * it allows the main thread to proceed, even without the child PID
		 * dying immediately (the child may be doing cleanup, etc.).  Without
		 * this code, zombie processes accumulate for as long as child
		 * processes exist (which on busy systems may be always, filling up the
		 * process table).
		 *
		 * Note that in trunk, we don't stop interaction at the hangup event
		 * (instead we transparently switch to DeadAGI operation), so this is a
		 * short-lived code addition.
		 */
		if (waitpid(pid, status, WNOHANG) == 0) {
			struct zombie *cur = ast_calloc(1, sizeof(*cur));
			if (cur) {
				cur->pid = pid;
				AST_LIST_LOCK(&zombies);
				AST_LIST_INSERT_TAIL(&zombies, cur, list);
				AST_LIST_UNLOCK(&zombies);
			}
		}
	}
	fclose(readf);
	return returnstatus;
}

static int handle_showagi(int fd, int argc, char *argv[])
{
	struct agi_command *e;
	char fullcmd[80];
	if ((argc < 2))
		return RESULT_SHOWUSAGE;
	if (argc > 2) {
		e = find_command(argv + 2, 1);
		if (e) 
			ast_cli(fd, "%s", e->usage);
		else {
			if (find_command(argv + 2, -1)) {
				return help_workhorse(fd, argv + 1);
			} else {
				ast_join(fullcmd, sizeof(fullcmd), argv+1);
				ast_cli(fd, "No such command '%s'.\n", fullcmd);
			}
		}
	} else {
		return help_workhorse(fd, NULL);
	}
	return RESULT_SUCCESS;
}

static int handle_agidumphtml(int fd, int argc, char *argv[])
{
	struct agi_command *e;
	char fullcmd[80];
	int x;
	FILE *htmlfile;

	if ((argc < 3))
		return RESULT_SHOWUSAGE;

	if (!(htmlfile = fopen(argv[2], "wt"))) {
		ast_cli(fd, "Could not create file '%s'\n", argv[2]);
		return RESULT_SHOWUSAGE;
	}

	fprintf(htmlfile, "<HTML>\n<HEAD>\n<TITLE>AGI Commands</TITLE>\n</HEAD>\n");
	fprintf(htmlfile, "<BODY>\n<CENTER><B><H1>AGI Commands</H1></B></CENTER>\n\n");


	fprintf(htmlfile, "<TABLE BORDER=\"0\" CELLSPACING=\"10\">\n");

	for (x=0;x<sizeof(commands)/sizeof(commands[0]);x++) {
		char *stringp, *tempstr;

		e = &commands[x]; 
		if (!e->cmda[0])	/* end ? */
			break;
		/* Hide commands that start with '_' */
		if ((e->cmda[0])[0] == '_')
			continue;
		ast_join(fullcmd, sizeof(fullcmd), e->cmda);

		fprintf(htmlfile, "<TR><TD><TABLE BORDER=\"1\" CELLPADDING=\"5\" WIDTH=\"100%%\">\n");
		fprintf(htmlfile, "<TR><TH ALIGN=\"CENTER\"><B>%s - %s</B></TH></TR>\n", fullcmd,e->summary);

		stringp=e->usage;
		tempstr = strsep(&stringp, "\n");

		fprintf(htmlfile, "<TR><TD ALIGN=\"CENTER\">%s</TD></TR>\n", tempstr);
		
		fprintf(htmlfile, "<TR><TD ALIGN=\"CENTER\">\n");
		while ((tempstr = strsep(&stringp, "\n")) != NULL)
			fprintf(htmlfile, "%s<BR>\n",tempstr);
		fprintf(htmlfile, "</TD></TR>\n");
		fprintf(htmlfile, "</TABLE></TD></TR>\n\n");

	}

	fprintf(htmlfile, "</TABLE>\n</BODY>\n</HTML>\n");
	fclose(htmlfile);
	ast_cli(fd, "AGI HTML Commands Dumped to: %s\n", argv[2]);
	return RESULT_SUCCESS;
}

static int agi_exec_full(struct ast_channel *chan, void *data, int enhanced, int dead)
{
	enum agi_result res;
	struct ast_module_user *u;
	char *argv[MAX_ARGS];
	char buf[AGI_BUF_LEN] = "";
	char *tmp = (char *)buf;
	int argc = 0;
	int fds[2];
	int efd = -1;
	int pid;
	char *stringp;
	AGI agi;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "AGI requires an argument (script)\n");
		return -1;
	}
	ast_copy_string(buf, data, sizeof(buf));

	memset(&agi, 0, sizeof(agi));
        while ((stringp = strsep(&tmp, "|")) && argc < MAX_ARGS-1)
		argv[argc++] = stringp;
	argv[argc] = NULL;

	u = ast_module_user_add(chan);
#if 0
	 /* Answer if need be */
        if (chan->_state != AST_STATE_UP) {
		if (ast_answer(chan)) {
			LOCAL_USER_REMOVE(u);
			return -1;
		}
	}
#endif
	ast_replace_sigchld();
	res = launch_script(argv[0], argv, fds, enhanced ? &efd : NULL, &pid);
	if (res == AGI_RESULT_SUCCESS || res == AGI_RESULT_SUCCESS_FAST) {
		int status = 0;
		agi.fd = fds[1];
		agi.ctrl = fds[0];
		agi.audio = efd;
		agi.fast = (res == AGI_RESULT_SUCCESS_FAST) ? 1 : 0;
		res = run_agi(chan, argv[0], &agi, pid, &status, dead);
		/* If the fork'd process returns non-zero, set AGISTATUS to FAILURE */
		if ((res == AGI_RESULT_SUCCESS || res == AGI_RESULT_SUCCESS_FAST) && status)
			res = AGI_RESULT_FAILURE;
		if (fds[1] != fds[0])
			close(fds[1]);
		if (efd > -1)
			close(efd);
	}
	ast_unreplace_sigchld();
	ast_module_user_remove(u);

	switch (res) {
	case AGI_RESULT_SUCCESS:
	case AGI_RESULT_SUCCESS_FAST:
		pbx_builtin_setvar_helper(chan, "AGISTATUS", "SUCCESS");
		break;
	case AGI_RESULT_FAILURE:
		pbx_builtin_setvar_helper(chan, "AGISTATUS", "FAILURE");
		break;
	case AGI_RESULT_HANGUP:
		pbx_builtin_setvar_helper(chan, "AGISTATUS", "HANGUP");
		return -1;
	}

	return 0;
}

static int agi_exec(struct ast_channel *chan, void *data)
{
	if (chan->_softhangup)
		ast_log(LOG_WARNING, "If you want to run AGI on hungup channels you should use DeadAGI!\n");
	return agi_exec_full(chan, data, 0, 0);
}

static int eagi_exec(struct ast_channel *chan, void *data)
{
	int readformat;
	int res;

	if (chan->_softhangup)
		ast_log(LOG_WARNING, "If you want to run AGI on hungup channels you should use DeadAGI!\n");
	readformat = chan->readformat;
	if (ast_set_read_format(chan, AST_FORMAT_SLINEAR)) {
		ast_log(LOG_WARNING, "Unable to set channel '%s' to linear mode\n", chan->name);
		return -1;
	}
	res = agi_exec_full(chan, data, 1, 0);
	if (!res) {
		if (ast_set_read_format(chan, readformat)) {
			ast_log(LOG_WARNING, "Unable to restore channel '%s' to format %s\n", chan->name, ast_getformatname(readformat));
		}
	}
	return res;
}

static int deadagi_exec(struct ast_channel *chan, void *data)
{
	if (!ast_check_hangup(chan))
		ast_log(LOG_WARNING,"Running DeadAGI on a live channel will cause problems, please use AGI\n");
	return agi_exec_full(chan, data, 0, 1);
}

static char showagi_help[] =
"Usage: agi show [topic]\n"
"       When called with a topic as an argument, displays usage\n"
"       information on the given command.  If called without a\n"
"       topic, it provides a list of AGI commands.\n";


static char dumpagihtml_help[] =
"Usage: agi dumphtml <filename>\n"
"	Dumps the agi command list in html format to given filename\n";

static struct ast_cli_entry cli_show_agi_deprecated = {
	{ "show", "agi", NULL },
	handle_showagi, NULL,
	NULL };

static struct ast_cli_entry cli_dump_agihtml_deprecated = {
	{ "dump", "agihtml", NULL },
	handle_agidumphtml, NULL,
	NULL };

static struct ast_cli_entry cli_agi_no_debug_deprecated = {
	{ "agi", "no", "debug", NULL },
	agi_no_debug_deprecated, NULL,
	NULL };

static struct ast_cli_entry cli_agi[] = {
	{ { "agi", "debug", NULL },
	agi_do_debug, "Enable AGI debugging",
	debug_usage },

	{ { "agi", "debug", "off", NULL },
	agi_no_debug, "Disable AGI debugging",
	no_debug_usage, NULL, &cli_agi_no_debug_deprecated },

	{ { "agi", "show", NULL },
	handle_showagi, "List AGI commands or specific help",
	showagi_help, NULL, &cli_show_agi_deprecated },

	{ { "agi", "dumphtml", NULL },
	handle_agidumphtml, "Dumps a list of agi commands in html format",
	dumpagihtml_help, NULL, &cli_dump_agihtml_deprecated },
};

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
					AST_LIST_REMOVE_CURRENT(&zombies, list);
					ast_free(cur);
				}
			}
			AST_LIST_TRAVERSE_SAFE_END
			AST_LIST_UNLOCK(&zombies);
			pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		}
		pthread_testcancel();
		/* Wait for 60 seconds, without engaging in a busy loop. */
		ast_poll(NULL, 0, 60000);
	}
	return NULL;
}

static int unload_module(void)
{
	int res;
	struct zombie *cur;
	ast_module_user_hangup_all();
	ast_cli_unregister_multiple(cli_agi, sizeof(cli_agi) / sizeof(struct ast_cli_entry));
	ast_unregister_application(eapp);
	ast_unregister_application(deadapp);
	res = ast_unregister_application(app);
	if (shaun_of_the_dead_thread != AST_PTHREADT_NULL) {
		pthread_cancel(shaun_of_the_dead_thread);
		pthread_kill(shaun_of_the_dead_thread, SIGURG);
		pthread_join(shaun_of_the_dead_thread, NULL);
	}
	while ((cur = AST_LIST_REMOVE_HEAD(&zombies, list))) {
		ast_free(cur);
	}
	return res;
}

static int load_module(void)
{
	if (ast_pthread_create_background(&shaun_of_the_dead_thread, NULL, shaun_of_the_dead, NULL)) {
		ast_log(LOG_ERROR, "Shaun of the Dead wants to kill zombies, but can't?!!\n");
		shaun_of_the_dead_thread = AST_PTHREADT_NULL;
	}
	ast_cli_register_multiple(cli_agi, sizeof(cli_agi) / sizeof(struct ast_cli_entry));
	ast_register_application(deadapp, deadagi_exec, deadsynopsis, descrip);
	ast_register_application(eapp, eagi_exec, esynopsis, descrip);
	return ast_register_application(app, agi_exec, synopsis, descrip);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS, "Asterisk Gateway Interface (AGI)",
                .load = load_module,
                .unload = unload_module,
		);
