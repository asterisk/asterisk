/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Asterisk Gateway Interface
 * 
 * Copyright (C) 1999-2004, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <sys/types.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/astdb.h>
#include <asterisk/callerid.h>
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
#include <asterisk/cli.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <asterisk/image.h>
#include <asterisk/say.h>
#include <asterisk/app.h>
#include <asterisk/dsp.h>
#include <asterisk/musiconhold.h>
#include <asterisk/manager.h>
#include <asterisk/utils.h>
#include <asterisk/lock.h>
#include <asterisk/agi.h>
#include "../asterisk.h"
#include "../astconf.h"

#define MAX_ARGS 128
#define MAX_COMMANDS 128

/* Recycle some stuff from the CLI interface */
#define fdprintf agi_debug_cli

static char *tdesc = "Asterisk Gateway Interface (AGI)";

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
"Returns -1 on hangup (except for DeadAGI) or if application requested\n"
" hangup, or 0 on non-hangup exit. \n"
"Using 'EAGI' provides enhanced AGI, with incoming audio available out of band"
"on file descriptor 3\n\n"
"Use the CLI command 'show agi' to list available agi commands\n";

static int agidebug = 0;

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;


#define TONE_BLOCK_SIZE 200

/* Max time to connect to an AGI remote host */
#define MAX_AGI_CONNECT 2000

#define AGI_PORT 4573

static void agi_debug_cli(int fd, char *fmt, ...)
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
			ast_verbose("AGI Tx >> %s", stuff);
		ast_carefulwrite(fd, stuff, strlen(stuff), 100);
		free(stuff);
	}
}

static int launch_netscript(char *agiurl, char *argv[], int *fds, int *efd, int *opid)
{
	int s;
	int flags;
	struct pollfd pfds[1];
	char *host;
	char *c; int port = AGI_PORT;
	char *script;
	struct sockaddr_in sin;
	struct hostent *hp;
	struct ast_hostent ahp;
	ast_log(LOG_DEBUG, "Blah\n");
	host = ast_strdupa(agiurl + 6);
	if (!host)
		return -1;
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
		return -1;
	}
	pfds[0].fd = s;
	pfds[0].events = POLLOUT;
	if (poll(pfds, 1, MAX_AGI_CONNECT) != 1) {
		ast_log(LOG_WARNING, "Connect to '%s' failed!\n", agiurl);
		close(s);
		return -1;
	}
	if (write(s, "agi_network: yes\n", strlen("agi_network: yes\n")) < 0) {
		ast_log(LOG_WARNING, "Connect to '%s' failed: %s\n", agiurl, strerror(errno));
		close(s);
		return -1;
	}
	ast_log(LOG_DEBUG, "Wow, connected!\n");
	fds[0] = s;
	fds[1] = s;
	*opid = -1;
	return 0;
}

static int launch_script(char *script, char *argv[], int *fds, int *efd, int *opid)
{
	char tmp[256];
	int pid;
	int toast[2];
	int fromast[2];
	int audio[2];
	int x;
	int res;
	
	if (!strncasecmp(script, "agi://", 6))
		return launch_netscript(script, argv, fds, efd, opid);
	
	if (script[0] != '/') {
		snprintf(tmp, sizeof(tmp), "%s/%s", (char *)ast_config_AST_AGI_DIR, script);
		script = tmp;
	}
	if (pipe(toast)) {
		ast_log(LOG_WARNING, "Unable to create toast pipe: %s\n",strerror(errno));
		return -1;
	}
	if (pipe(fromast)) {
		ast_log(LOG_WARNING, "unable to create fromast pipe: %s\n", strerror(errno));
		close(toast[0]);
		close(toast[1]);
		return -1;
	}
	if (efd) {
		if (pipe(audio)) {
			ast_log(LOG_WARNING, "unable to create audio pipe: %s\n", strerror(errno));
			close(fromast[0]);
			close(fromast[1]);
			close(toast[0]);
			close(toast[1]);
			return -1;
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
			return -1;
		}
	}
	pid = fork();
	if (pid < 0) {
		ast_log(LOG_WARNING, "Failed to fork(): %s\n", strerror(errno));
		return -1;
	}
	if (!pid) {
		/* Redirect stdin and out, provide enhanced audio channel if desired */
		dup2(fromast[0], STDIN_FILENO);
		dup2(toast[1], STDOUT_FILENO);
		if (efd) {
			dup2(audio[0], STDERR_FILENO + 1);
		} else {
			close(STDERR_FILENO + 1);
		}
		/* Close everything but stdin/out/error */
		for (x=STDERR_FILENO + 2;x<1024;x++) 
			close(x);
		/* Execute script */
		execv(script, argv);
		/* Can't use ast_log since FD's are closed */
		fprintf(stderr, "Failed to execute '%s': %s\n", script, strerror(errno));
		exit(1);
	}
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

	if (efd) {
		/* [PHM 12/18/03] */
		close(audio[0]);
	}

	*opid = pid;
	return 0;
		
}

static void setup_env(struct ast_channel *chan, char *request, int fd, int enhanced)
{
	/* Print initial environment, with agi_request always being the first
	   thing */
	fdprintf(fd, "agi_request: %s\n", request);
	fdprintf(fd, "agi_channel: %s\n", chan->name);
	fdprintf(fd, "agi_language: %s\n", chan->language);
	fdprintf(fd, "agi_type: %s\n", chan->type);
	fdprintf(fd, "agi_uniqueid: %s\n", chan->uniqueid);

	/* ANI/DNIS */
	fdprintf(fd, "agi_callerid: %s\n", chan->cid.cid_num ? chan->cid.cid_num : "unknown");
	fdprintf(fd, "agi_calleridname: %s\n", chan->cid.cid_name ? chan->cid.cid_name : "unknown");
	fdprintf(fd, "agi_dnid: %s\n", chan->cid.cid_dnid ? chan->cid.cid_dnid : "unknown");
	fdprintf(fd, "agi_rdnis: %s\n", chan->cid.cid_rdnis ? chan->cid.cid_rdnis : "unknown");

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
	if (res >= 0)
		return RESULT_SUCCESS;
	else
		return RESULT_FAILURE;
}

static int handle_waitfordigit(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	int res;
	int to;
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	if (sscanf(argv[3], "%i", &to) != 1)
		return RESULT_SHOWUSAGE;
	res = ast_waitfordigit_full(chan, to, agi->audio, agi->ctrl);
	fdprintf(agi->fd, "200 result=%d\n", res);
	if (res >= 0)
		return RESULT_SUCCESS;
	else
		return RESULT_FAILURE;
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
	if (res >= 0)
		return RESULT_SUCCESS;
	else
		return RESULT_FAILURE;
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

static int handle_tddmode(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	int res,x;
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	if (!strncasecmp(argv[2],"on",2)) x = 1; else x = 0;
	if (!strncasecmp(argv[2],"mate",4)) x = 2;
	if (!strncasecmp(argv[2],"tdd",3)) x = 1;
	res = ast_channel_setoption(chan,AST_OPTION_TDD,&x,sizeof(char),0);
	fdprintf(agi->fd, "200 result=%d\n", res);
	if (res >= 0) 
		return RESULT_SUCCESS;
	else
		return RESULT_FAILURE;
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
	if (res >= 0)
		return RESULT_SUCCESS;
	else
		return RESULT_FAILURE;
}

static int handle_streamfile(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	int res;
	struct ast_filestream *fs;
	long sample_offset = 0;
	long max_length;

	if (argc < 4)
		return RESULT_SHOWUSAGE;
	if (argc > 5)
		return RESULT_SHOWUSAGE;
	if ((argc > 4) && (sscanf(argv[4], "%ld", &sample_offset) != 1))
		return RESULT_SHOWUSAGE;
	
	fs = ast_openstream(chan, argv[2], chan->language);
	if(!fs){
		fdprintf(agi->fd, "200 result=%d endpos=%ld\n", 0, sample_offset);
		ast_log(LOG_WARNING, "Unable to open %s\n", argv[2]);
		return RESULT_FAILURE;
	}
	ast_seekstream(fs, 0, SEEK_END);
	max_length = ast_tellstream(fs);
	ast_seekstream(fs, sample_offset, SEEK_SET);
	res = ast_applystream(chan, fs);
	res = ast_playstream(fs);
	if (res) {
		fdprintf(agi->fd, "200 result=%d endpos=%ld\n", res, sample_offset);
		if (res >= 0)
			return RESULT_SHOWUSAGE;
		else
			return RESULT_FAILURE;
	}
	res = ast_waitstream_full(chan, argv[3], agi->audio, agi->ctrl);
	/* this is to check for if ast_waitstream closed the stream, we probably are at
	 * the end of the stream, return that amount, else check for the amount */
	sample_offset = (chan->stream)?ast_tellstream(fs):max_length;
	ast_stopstream(chan);
	if (res == 1) {
		/* Stop this command, don't print a result line, as there is a new command */
		return RESULT_SUCCESS;
	}
	fdprintf(agi->fd, "200 result=%d endpos=%ld\n", res, sample_offset);
	if (res >= 0)
		return RESULT_SUCCESS;
	else
		return RESULT_FAILURE;
}

/* get option - really similar to the handle_streamfile, but with a timeout */
static int handle_getoption(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
        int res;
        struct ast_filestream *fs;
        long sample_offset = 0;
        long max_length;
	int timeout = 0;
	char *edigits = NULL;

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
        if(!fs){
                fdprintf(agi->fd, "200 result=%d endpos=%ld\n", 0, sample_offset);
                ast_log(LOG_WARNING, "Unable to open %s\n", argv[2]);
                return RESULT_FAILURE;
        }
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Playing '%s' (escape_digits=%s) (timeout %d)\n", argv[2], edigits, timeout);

        ast_seekstream(fs, 0, SEEK_END);
        max_length = ast_tellstream(fs);
        ast_seekstream(fs, sample_offset, SEEK_SET);
        res = ast_applystream(chan, fs);
        res = ast_playstream(fs);
        if (res) {
                fdprintf(agi->fd, "200 result=%d endpos=%ld\n", res, sample_offset);
                if (res >= 0)
                        return RESULT_SHOWUSAGE;
                else
                        return RESULT_FAILURE;
        }
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
        if (res >= 0)
                return RESULT_SUCCESS;
        else
                return RESULT_FAILURE;
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
	if (sscanf(argv[2], "%i", &num) != 1)
		return RESULT_SHOWUSAGE;
	res = ast_say_number_full(chan, num, argv[3], chan->language, (char *) NULL, agi->audio, agi->ctrl);
	if (res == 1)
		return RESULT_SUCCESS;
	fdprintf(agi->fd, "200 result=%d\n", res);
	if (res >= 0)
		return RESULT_SUCCESS;
	else
		return RESULT_FAILURE;
}

static int handle_saydigits(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	int res;
	int num;

	if (argc != 4)
		return RESULT_SHOWUSAGE;
	if (sscanf(argv[2], "%i", &num) != 1)
		return RESULT_SHOWUSAGE;

	res = ast_say_digit_str_full(chan, argv[2], argv[3], chan->language, agi->audio, agi->ctrl);
	if (res == 1) /* New command */
		return RESULT_SUCCESS;
	fdprintf(agi->fd, "200 result=%d\n", res);
	if (res >= 0)
		return RESULT_SUCCESS;
	else
		return RESULT_FAILURE;
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
	if (res >= 0)
		return RESULT_SUCCESS;
	else
		return RESULT_FAILURE;
}

static int handle_saytime(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	int res;
	int num;
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	if (sscanf(argv[2], "%i", &num) != 1)
		return RESULT_SHOWUSAGE;
	res = ast_say_time(chan, num, argv[3], chan->language);
	if (res == 1)
		return RESULT_SUCCESS;
	fdprintf(agi->fd, "200 result=%d\n", res);
	if (res >= 0)
		return RESULT_SUCCESS;
	else
		return RESULT_FAILURE;
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
	if (res >= 0)
		return RESULT_SUCCESS;
	else
		return RESULT_FAILURE;
}

static int handle_getdata(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	int res;
	char data[1024];
	int max;
	int timeout;

	if (argc < 3)
		return RESULT_SHOWUSAGE;
	if (argc >= 4) timeout = atoi(argv[3]); else timeout = 0;
	if (argc >= 5) max = atoi(argv[4]); else max = 1024;
	res = ast_app_getdata_full(chan, argv[2], data, max, timeout, agi->audio, agi->ctrl);
	if (res == 2)			/* New command */
		return RESULT_SUCCESS;
	else if (res == 1)
		fdprintf(agi->fd, "200 result=%s (timeout)\n", data);
	else if (res < 0 )
		fdprintf(agi->fd, "200 result=-1\n");
	else
		fdprintf(agi->fd, "200 result=%s\n", data);
	if (res >= 0)
		return RESULT_SUCCESS;
	else
		return RESULT_FAILURE;
}

static int handle_setcontext(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{

	if (argc != 3)
		return RESULT_SHOWUSAGE;
	strncpy(chan->context, argv[2], sizeof(chan->context)-1);
	fdprintf(agi->fd, "200 result=0\n");
	return RESULT_SUCCESS;
}
	
static int handle_setextension(struct ast_channel *chan, AGI *agi, int argc, char **argv)
{
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	strncpy(chan->exten, argv[2], sizeof(chan->exten)-1);
	fdprintf(agi->fd, "200 result=0\n");
	return RESULT_SUCCESS;
}

static int handle_setpriority(struct ast_channel *chan, AGI *agi, int argc, char **argv)
{
	int pri;
	if (argc != 3)
		return RESULT_SHOWUSAGE;	
	if (sscanf(argv[2], "%i", &pri) != 1)
		return RESULT_SHOWUSAGE;
	chan->priority = pri - 1;
	fdprintf(agi->fd, "200 result=0\n");
	return RESULT_SUCCESS;
}
		
static int handle_recordfile(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	struct ast_filestream *fs;
	struct ast_frame *f;
	struct timeval tv, start;
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
	if (sscanf(argv[5], "%i", &ms) != 1)
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
	if ((argc >6) && (sscanf(argv[6], "%ld", &sample_offset) != 1) && (!strchr(argv[6], '=')))
		res = ast_streamfile(chan, "beep", chan->language);

	if ((argc > 7) && (!strchr(argv[7], '=')))
		res = ast_streamfile(chan, "beep", chan->language);

	if (!res)
		res = ast_waitstream(chan, argv[4]);
	if (!res) {
		fs = ast_writefile(argv[2], argv[3], NULL, O_CREAT | O_WRONLY | (sample_offset ? O_APPEND : 0), 0, 0644);
		if (!fs) {
			res = -1;
			fdprintf(agi->fd, "200 result=%d (writefile)\n", res);
			if (sildet)
				ast_dsp_free(sildet);
			return RESULT_FAILURE;
		}
		
		chan->stream = fs;
		ast_applystream(chan,fs);
		/* really should have checks */
		ast_seekstream(fs, sample_offset, SEEK_SET);
		ast_truncstream(fs);
		
		gettimeofday(&start, NULL);
		gettimeofday(&tv, NULL);
		while ((ms < 0) || (((tv.tv_sec - start.tv_sec) * 1000 + (tv.tv_usec - start.tv_usec)/1000) < ms)) {
			res = ast_waitfor(chan, -1);
			if (res < 0) {
				ast_closestream(fs);
				fdprintf(agi->fd, "200 result=%d (waitfor) endpos=%ld\n", res,sample_offset);
				if (sildet)
					ast_dsp_free(sildet);
				return RESULT_FAILURE;
			}
			f = ast_read(chan);
			if (!f) {
				fdprintf(agi->fd, "200 result=%d (hangup) endpos=%ld\n", 0, sample_offset);
				ast_closestream(fs);
				if (sildet)
					ast_dsp_free(sildet);
				return RESULT_FAILURE;
			}
			switch(f->frametype) {
			case AST_FRAME_DTMF:
				if (strchr(argv[4], f->subclass)) {
					/* This is an interrupting chracter */
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
                                        	ast_frfree(f);
                                                gotsilence = 1;
                                                break;
                                        }
                            	}
				break;
			}
			ast_frfree(f);
		    	gettimeofday(&tv, NULL);
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
	} else
		fdprintf(agi->fd, "200 result=%d (randomerror) endpos=%ld\n", res, sample_offset);

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
	if (sscanf(argv[2], "%d", &timeout) != 1)
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
        if (argc==1) {
            /* no argument: hangup the current channel */
	    ast_softhangup(chan,AST_SOFTHANGUP_EXPLICIT);
	    fdprintf(agi->fd, "200 result=1\n");
	    return RESULT_SUCCESS;
        } else if (argc==2) {
            /* one argument: look for info on the specified channel */
            c = ast_channel_walk_locked(NULL);
            while (c) {
                if (strcasecmp(argv[1],c->name)==0) {
                    /* we have a matching channel */
		            ast_softhangup(c,AST_SOFTHANGUP_EXPLICIT);
		            fdprintf(agi->fd, "200 result=1\n");
					ast_mutex_unlock(&c->lock);
                    return RESULT_SUCCESS;
                }
				ast_mutex_unlock(&c->lock);
                c = ast_channel_walk_locked(c);
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
		res = pbx_exec(chan, app, argv[2], 1);
	} else {
		ast_log(LOG_WARNING, "Could not find application (%s)\n", argv[1]);
		res = -2;
	}
	fdprintf(agi->fd, "200 result=%d\n", res);

	return res;
}

static int handle_setcallerid(struct ast_channel *chan, AGI *agi, int argc, char **argv)
{
	char tmp[256]="";
	char *l = NULL, *n = NULL;
	if (argv[2]) {
		strncpy(tmp, argv[2], sizeof(tmp) - 1);
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
	if (argc==2) {
		/* no argument: supply info on the current channel */
		fdprintf(agi->fd, "200 result=%d\n", chan->_state);
		return RESULT_SUCCESS;
	} else if (argc==3) {
		/* one argument: look for info on the specified channel */
		c = ast_channel_walk_locked(NULL);
		while (c) {
			if (strcasecmp(argv[2],c->name)==0) {
				fdprintf(agi->fd, "200 result=%d\n", c->_state);
				ast_mutex_unlock(&c->lock);
				return RESULT_SUCCESS;
			}
			ast_mutex_unlock(&c->lock);
			c = ast_channel_walk_locked(c);
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
	pbx_retrieve_variable(chan, argv[2], &ret, tempstr, sizeof(tempstr), NULL);
	if (ret)
		fdprintf(agi->fd, "200 result=1 (%s)\n", ret);
	else
		fdprintf(agi->fd, "200 result=0\n");

	return RESULT_SUCCESS;
}

static int handle_getvariablefull(struct ast_channel *chan, AGI *agi, int argc, char **argv)
{
	char tmp[4096];
	struct ast_channel *chan2=NULL;
	if ((argc != 4) && (argc != 5))
		return RESULT_SHOWUSAGE;
	if (argc == 5) {
		while((chan2 = ast_channel_walk_locked(chan2))) {
			if (!strcmp(chan2->name, argv[4]))
				break;
			ast_mutex_unlock(&chan2->lock);
		}
	} else {
		chan2 = chan;
	}
	if (chan) {
		pbx_substitute_variables_helper(chan2, argv[3], tmp, sizeof(tmp) - 1);
		fdprintf(agi->fd, "200 result=1 (%s)\n", tmp);
	} else {
		fdprintf(agi->fd, "200 result=0\n");
	}
	if (chan2 && (chan2 != chan))
		ast_mutex_unlock(&chan2->lock);
	return RESULT_SUCCESS;
}

static int handle_verbose(struct ast_channel *chan, AGI *agi, int argc, char **argv)
{
	int level = 0;
	char *prefix;

	if (argc < 2)
		return RESULT_SHOWUSAGE;

	if (argv[2])
		sscanf(argv[2], "%d", &level);

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
	char tmp[256];
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	res = ast_db_get(argv[2], argv[3], tmp, sizeof(tmp));
	if (res) 
		fdprintf(agi->fd, "200 result=0\n");
	else
		fdprintf(agi->fd, "200 result=1 (%s)\n", tmp);

	return RESULT_SUCCESS;
}

static int handle_dbput(struct ast_channel *chan, AGI *agi, int argc, char **argv)
{
	int res;
	if (argc != 5)
		return RESULT_SHOWUSAGE;
	res = ast_db_put(argv[2], argv[3], argv[4]);
	if (res) 
		fdprintf(agi->fd, "200 result=0\n");
	else
		fdprintf(agi->fd, "200 result=1\n");

	return RESULT_SUCCESS;
}

static int handle_dbdel(struct ast_channel *chan, AGI *agi, int argc, char **argv)
{
	int res;
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	res = ast_db_del(argv[2], argv[3]);
	if (res) 
		fdprintf(agi->fd, "200 result=0\n");
	else
		fdprintf(agi->fd, "200 result=1\n");

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

	if (res) 
		fdprintf(agi->fd, "200 result=0\n");
	else
		fdprintf(agi->fd, "200 result=1\n");
	return RESULT_SUCCESS;
}

static char debug_usage[] = 
"Usage: agi debug\n"
"       Enables dumping of AGI transactions for debugging purposes\n";

static char no_debug_usage[] = 
"Usage: agi no debug\n"
"       Disables dumping of AGI transactions for debugging purposes\n";

static int agi_do_debug(int fd, int argc, char *argv[])
{
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	agidebug = 1;
	ast_cli(fd, "AGI Debugging Enabled\n");
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

static struct ast_cli_entry  cli_debug =
	{ { "agi", "debug", NULL }, agi_do_debug, "Enable AGI debugging", debug_usage };

static struct ast_cli_entry  cli_no_debug =
	{ { "agi", "no", "debug", NULL }, agi_no_debug, "Disable AGI debugging", no_debug_usage };

static int handle_noop(struct ast_channel *chan, AGI *agi, int arg, char *argv[])
{
	fdprintf(agi->fd, "200 result=0\n");
	return RESULT_SUCCESS;
}

static int handle_setmusic(struct ast_channel *chan, AGI *agi, int argc, char *argv[])
{
	if (!strncasecmp(argv[2],"on",2)) {
		if (argc > 3)
			ast_moh_start(chan, argv[3]);
		else
			ast_moh_start(chan, NULL);
	}
	if (!strncasecmp(argv[2],"off",3)) {
		ast_moh_stop(chan);
	}
	fdprintf(agi->fd, "200 result=0\n");
	return RESULT_SUCCESS;
}

static char usage_setmusic[] =
" Usage: SET MUSIC ON <on|off> <class>\n"
"	Enables/Disables the music on hold generator.  If <class> is\n"
" not specified then the default music on hold class will be used.\n"
" Always returns 0\n";

static char usage_dbput[] =
" Usage: DATABASE PUT <family> <key> <value>\n"
"	Adds or updates an entry in the Asterisk database for a\n"
" given family, key, and value.\n"
" Returns 1 if succesful, 0 otherwise\n";

static char usage_dbget[] =
" Usage: DATABASE GET <family> <key>\n"
"	Retrieves an entry in the Asterisk database for a\n"
" given family and key.\n"
"	Returns 0 if <key> is not set.  Returns 1 if <key>\n"
" is set and returns the variable in parenthesis\n"
" example return code: 200 result=1 (testvariable)\n";

static char usage_dbdel[] =
" Usage: DATABASE DEL <family> <key>\n"
"	Deletes an entry in the Asterisk database for a\n"
" given family and key.\n"
" Returns 1 if succesful, 0 otherwise\n";

static char usage_dbdeltree[] =
" Usage: DATABASE DELTREE <family> [keytree]\n"
"	Deletes a family or specific keytree withing a family\n"
" in the Asterisk database.\n"
" Returns 1 if succesful, 0 otherwise\n";

static char usage_verbose[] =
" Usage: VERBOSE <message> <level>\n"
"	Sends <message> to the console via verbose message system.\n"
"	<level> is the the verbose level (1-4)\n"
"	Always returns 1\n";

static char usage_getvariable[] =
" Usage: GET VARIABLE <variablename>\n"
"	Returns 0 if <variablename> is not set.  Returns 1 if <variablename>\n"
" is set and returns the variable in parenthesis\n"
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
"       If no channel name is given the returns the status of the\n"
"       current channel.\n"
"       Return values:\n"
" 0 Channel is down and available\n"
" 1 Channel is down, but reserved\n"
" 2 Channel is off hook\n"
" 3 Digits (or equivalent) have been dialed\n"
" 4 Line is ringing\n"
" 5 Remote end is ringing\n"
" 6 Line is up\n"
" 7 Line is busy\n";

static char usage_setcallerid[] =
" Usage: SET CALLERID <number>\n"
"	Changes the callerid of the current channel.\n";

static char usage_exec[] =
" Usage: EXEC <application> <options>\n"
"	Executes <application> with given <options>.\n"
"	Returns whatever the application returns, or -2 on failure to find application\n";

static char usage_hangup[] =
" Usage: HANGUP [<channelname>]\n"
"	Hangs up the specified channel.\n"
"       If no channel name is given, hangs up the current channel\n";

static char usage_answer[] = 
" Usage: ANSWER\n"
"        Answers channel if not already in answer state. Returns -1 on\n"
" channel failure, or 0 if successful.\n";

static char usage_waitfordigit[] = 
" Usage: WAIT FOR DIGIT <timeout>\n"
"        Waits up to 'timeout' milliseconds for channel to receive a DTMF digit.\n"
" Returns -1 on channel failure, 0 if no digit is received in the timeout, or\n"
" the numerical value of the ascii of the digit if one is received.  Use -1\n"
" for the timeout value if you desire the call to block indefinitely.\n";

static char usage_sendtext[] =
" Usage: SEND TEXT \"<text to send>\"\n"
"        Sends the given text on a channel.  Most channels do not support the\n"
" transmission of text.  Returns 0 if text is sent, or if the channel does not\n"
" support text transmission.  Returns -1 only on error/hangup.  Text\n"
" consisting of greater than one word should be placed in quotes since the\n"
" command only accepts a single argument.\n";

static char usage_recvchar[] =
" Usage: RECEIVE CHAR <timeout>\n"
"        Receives a character of text on a channel.  Specify timeout to be the\n"
" maximum time to wait for input in milliseconds, or 0 for infinite. Most channels\n"
" do not support the reception of text.  Returns the decimal value of the character\n"
" if one is received, or 0 if the channel does not support text reception.  Returns\n"
" -1 only on error/hangup.\n";

static char usage_tddmode[] =
" Usage: TDD MODE <on|off>\n"
"        Enable/Disable TDD transmission/reception on a channel. Returns 1 if\n"
" successful, or 0 if channel is not TDD-capable.\n";

static char usage_sendimage[] =
" Usage: SEND IMAGE <image>\n"
"        Sends the given image on a channel.  Most channels do not support the\n"
" transmission of images.  Returns 0 if image is sent, or if the channel does not\n"
" support image transmission.  Returns -1 only on error/hangup.  Image names\n"
" should not include extensions.\n";

static char usage_streamfile[] =
" Usage: STREAM FILE <filename> <escape digits> [sample offset]\n"
"        Send the given file, allowing playback to be interrupted by the given\n"
" digits, if any.  Use double quotes for the digits if you wish none to be\n"
" permitted.  If sample offset is provided then the audio will seek to sample\n"
" offset before play starts.  Returns 0 if playback completes without a digit\n"
" being pressed, or the ASCII numerical value of the digit if one was pressed,\n"
" or -1 on error or if the channel was disconnected.  Remember, the file\n"
" extension must not be included in the filename.\n";

static char usage_getoption[] = 
" Usage: GET OPTION <filename> <escape_digits> [timeout]\n"
" Exactly like the STREAM FILE but used with a timeout option\n";

static char usage_saynumber[] =
" Usage: SAY NUMBER <number> <escape digits>\n"
"        Say a given number, returning early if any of the given DTMF digits\n"
" are received on the channel.  Returns 0 if playback completes without a digit\n"
" being pressed, or the ASCII numerical value of the digit if one was pressed or\n"
" -1 on error/hangup.\n";

static char usage_saydigits[] =
" Usage: SAY DIGITS <number> <escape digits>\n"
"        Say a given digit string, returning early if any of the given DTMF digits\n"
" are received on the channel.  Returns 0 if playback completes without a digit\n"
" being pressed, or the ASCII numerical value of the digit if one was pressed or\n"
" -1 on error/hangup.\n";

static char usage_sayalpha[] =
" Usage: SAY ALPHA <number> <escape digits>\n"
"        Say a given character string, returning early if any of the given DTMF digits\n"
" are received on the channel.  Returns 0 if playback completes without a digit\n"
" being pressed, or the ASCII numerical value of the digit if one was pressed or\n"
" -1 on error/hangup.\n";

static char usage_saytime[] =
" Usage: SAY TIME <time> <escape digits>\n"
"        Say a given time, returning early if any of the given DTMF digits are\n"
" received on the channel.  <time> is number of seconds elapsed since 00:00:00\n"
" on January 1, 1970, Coordinated Universal Time (UTC).  Returns 0 if playback\n"
" completes without a digit being pressed, or the ASCII numerical value of the\n"
" digit if one was pressed or -1 on error/hangup.\n";

static char usage_sayphonetic[] =
" Usage: SAY PHONETIC <string> <escape digits>\n"
"        Say a given character string with phonetics, returning early if any of the given DTMF digits\n"
" are received on the channel.  Returns 0 if playback completes without a digit\n"
" being pressed, or the ASCII numerical value of the digit if one was pressed or\n"
" -1 on error/hangup.\n";

static char usage_getdata[] =
" Usage: GET DATA <file to be streamed> [timeout] [max digits]\n"
"	 Stream the given file, and recieve DTMF data. Returns the digits recieved\n"
"from the channel at the other end.\n";

static char usage_setcontext[] =
" Usage: SET CONTEXT <desired context>\n"
"	 Sets the context for continuation upon exiting the application.\n";

static char usage_setextension[] =
" Usage: SET EXTENSION <new extension>\n"
"	 Changes the extension for continuation upon exiting the application.\n";

static char usage_setpriority[] =
" Usage: SET PRIORITY <num>\n"
"	 Changes the priority for continuation upon exiting the application.\n";

static char usage_recordfile[] =
" Usage: RECORD FILE <filename> <format> <escape digits> <timeout> [offset samples] [BEEP] [s=silence]\n"
"        Record to a file until a given dtmf digit in the sequence is received\n"
" Returns -1 on hangup or error.  The format will specify what kind of file\n"
" will be recorded.  The timeout is the maximum record time in milliseconds, or\n"
" -1 for no timeout. Offset samples is optional, and if provided will seek to\n"
" the offset without exceeding the end of the file.  \"silence\" is the number\n"
" of seconds of silence allowed before the function returns despite the\n"
" lack of dtmf digits or reaching timeout.  Silence value must be\n"
" preceeded by \"s=\" and is optional.\n";


static char usage_autohangup[] =
" Usage: SET AUTOHANGUP <time>\n"
"    Cause the channel to automatically hangup at <time> seconds in the\n"
"future.  Of course it can be hungup before then as well.   Setting to\n"
"0 will cause the autohangup feature to be disabled on this channel.\n";

static char usage_noop[] =
" Usage: NOOP\n"
"    Does nothing.\n";

static agi_command commands[MAX_COMMANDS] = {
	{ { "answer", NULL }, handle_answer, "Asserts answer", usage_answer },
	{ { "wait", "for", "digit", NULL }, handle_waitfordigit, "Waits for a digit to be pressed", usage_waitfordigit },
	{ { "send", "text", NULL }, handle_sendtext, "Sends text to channels supporting it", usage_sendtext },
	{ { "receive", "char", NULL }, handle_recvchar, "Receives text from channels supporting it", usage_recvchar },
	{ { "tdd", "mode", NULL }, handle_tddmode, "Sends text to channels supporting it", usage_tddmode },
	{ { "stream", "file", NULL }, handle_streamfile, "Sends audio file on channel", usage_streamfile },
	{ { "get", "option", NULL }, handle_getoption, "Stream File", usage_getoption },
	{ { "send", "image", NULL }, handle_sendimage, "Sends images to channels supporting it", usage_sendimage },
	{ { "say", "digits", NULL }, handle_saydigits, "Says a given digit string", usage_saydigits },
	{ { "say", "alpha", NULL }, handle_sayalpha, "Says a given character string", usage_sayalpha },
	{ { "say", "number", NULL }, handle_saynumber, "Says a given number", usage_saynumber },
	{ { "say", "phonetic", NULL }, handle_sayphonetic, "Says a given character string with phonetics", usage_sayphonetic },
	{ { "say", "time", NULL }, handle_saytime, "Says a given time", usage_saytime },
	{ { "get", "data", NULL }, handle_getdata, "Gets data on a channel", usage_getdata },
	{ { "set", "context", NULL }, handle_setcontext, "Sets channel context", usage_setcontext },
	{ { "set", "extension", NULL }, handle_setextension, "Changes channel extension", usage_setextension },
	{ { "set", "priority", NULL }, handle_setpriority, "Prioritizes the channel", usage_setpriority },
	{ { "record", "file", NULL }, handle_recordfile, "Records to a given file", usage_recordfile },
	{ { "set", "autohangup", NULL }, handle_autohangup, "Autohangup channel in some time", usage_autohangup },
	{ { "hangup", NULL }, handle_hangup, "Hangup the current channel", usage_hangup },
	{ { "exec", NULL }, handle_exec, "Executes a given Application", usage_exec },
	{ { "set", "callerid", NULL }, handle_setcallerid, "Sets callerid for the current channel", usage_setcallerid },
	{ { "channel", "status", NULL }, handle_channelstatus, "Returns status of the connected channel", usage_channelstatus },
	{ { "set", "variable", NULL }, handle_setvariable, "Sets a channel variable", usage_setvariable },
	{ { "get", "variable", NULL }, handle_getvariable, "Gets a channel variable", usage_getvariable },
	{ { "get", "full", "variable", NULL }, handle_getvariablefull, "Evaluates a channel expression", usage_getvariablefull },
	{ { "verbose", NULL }, handle_verbose, "Logs a message to the asterisk verbose log", usage_verbose },
	{ { "database", "get", NULL }, handle_dbget, "Gets database value", usage_dbget },
	{ { "database", "put", NULL }, handle_dbput, "Adds/updates database value", usage_dbput },
	{ { "database", "del", NULL }, handle_dbdel, "Removes database key/value", usage_dbdel },
	{ { "database", "deltree", NULL }, handle_dbdeltree, "Removes database keytree/value", usage_dbdeltree },
	{ { "noop", NULL }, handle_noop, "Does nothing", usage_noop },
	{ { "set", "music", NULL }, handle_setmusic, "Enable/Disable Music on hold generator", usage_setmusic }
};

static void join(char *s, size_t len, char *w[])
{
	int x;
	/* Join words into a string */
	if (!s) {
		return;
	}
	s[0] = '\0';
	for (x=0;w[x];x++) {
		if (x)
			strncat(s, " ", len - strlen(s) - 1);
		strncat(s, w[x], len - strlen(s) - 1);
	}
}

static int help_workhorse(int fd, char *match[])
{
	char fullcmd[80];
	char matchstr[80];
	int x;
	struct agi_command *e;
	if (match)
		join(matchstr, sizeof(matchstr), match);
	for (x=0;x<sizeof(commands)/sizeof(commands[0]);x++) {
		if (!commands[x].cmda[0]) break;
		e = &commands[x]; 
		if (e)
			join(fullcmd, sizeof(fullcmd), e->cmda);
		/* Hide commands that start with '_' */
		if (fullcmd[0] == '_')
			continue;
		if (match) {
			if (strncasecmp(matchstr, fullcmd, strlen(matchstr))) {
				continue;
			}
		}
		ast_cli(fd, "%20.20s   %s\n", fullcmd, e->summary);
	}
	return 0;
}

int agi_register(agi_command *agi)
{
	int x;
	for (x=0;x<MAX_COMMANDS - 1;x++) {
		if (commands[x].cmda[0] == agi->cmda[0]) {
			ast_log(LOG_WARNING, "Command already registered!\n");
			return -1;
		}
	}
	for (x=0;x<MAX_COMMANDS - 1;x++) {
		if (!commands[x].cmda[0]) {
			commands[x] = *agi;
			return 0;
		}
	}
	ast_log(LOG_WARNING, "No more room for new commands!\n");
	return -1;
}

void agi_unregister(agi_command *agi)
{
	int x;
	for (x=0;x<MAX_COMMANDS - 1;x++) {
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
	for (x=0;x < sizeof(commands) / sizeof(commands[0]);x++) {
		if (!commands[x].cmda[0]) break;
		/* start optimistic */
		match = 1;
		for (y=0;match && cmds[y]; y++) {
			/* If there are no more words in the command (and we're looking for
			   an exact match) or there is a difference between the two words,
			   then this is not a match */
			if (!commands[x].cmda[y] && !exact)
				break;
			/* don't segfault if the next part of a command doesn't exist */
			if (!commands[x].cmda[y]) return NULL;
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
	int argc = 0;
	int res;
	agi_command *c;
	argc = MAX_ARGS;
	parse_args(buf, &argc, argv);
#if	0
	{ int x;
	for (x=0;x<argc;x++) 
		fprintf(stderr, "Got Arg%d: %s\n", x, argv[x]); }
#endif
	c = find_command(argv, 0);
	if (c) {
		res = c->handler(chan, agi, argc, argv);
		switch(res) {
		case RESULT_SHOWUSAGE:
			fdprintf(agi->fd, "520-Invalid command syntax.  Proper usage follows:\n");
			fdprintf(agi->fd, c->usage);
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
#define RETRY	3
static int run_agi(struct ast_channel *chan, char *request, AGI *agi, int pid, int dead)
{
	struct ast_channel *c;
	int outfd;
	int ms;
	int returnstatus = 0;
	struct ast_frame *f;
	char buf[2048];
	FILE *readf;
	/* how many times we'll retry if ast_waitfor_nandfs will return without either 
	  channel or file descriptor in case select is interrupted by a system call (EINTR) */
	int retry = RETRY;

	if (!(readf = fdopen(agi->ctrl, "r"))) {
		ast_log(LOG_WARNING, "Unable to fdopen file descriptor\n");
		if (pid > -1)
			kill(pid, SIGHUP);
		close(agi->ctrl);
		return -1;
	}
	setlinebuf(readf);
	setup_env(chan, request, agi->fd, (agi->audio > -1));
	for (;;) {
		ms = -1;
		c = ast_waitfor_nandfds(&chan, dead ? 0 : 1, &agi->ctrl, 1, NULL, &outfd, &ms);
		if (c) {
			retry = RETRY;
			/* Idle the channel until we get a command */
			f = ast_read(c);
			if (!f) {
				ast_log(LOG_DEBUG, "%s hungup\n", chan->name);
				returnstatus = -1;
				break;
			} else {
				/* If it's voice, write it to the audio pipe */
				if ((agi->audio > -1) && (f->frametype == AST_FRAME_VOICE)) {
					/* Write, ignoring errors */
					write(agi->audio, f->data, f->datalen);
				}
				ast_frfree(f);
			}
		} else if (outfd > -1) {
			retry = RETRY;
			if (!fgets(buf, sizeof(buf), readf)) {
				/* Program terminated */
				if (returnstatus)
					returnstatus = -1;
				if (option_verbose > 2) 
					ast_verbose(VERBOSE_PREFIX_3 "AGI Script %s completed, returning %d\n", request, returnstatus);
				/* No need to kill the pid anymore, since they closed us */
				pid = -1;
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
				returnstatus = -1;
				break;
			}
		}
	}
	/* Notify process */
	if (pid > -1)
		kill(pid, SIGHUP);
	fclose(readf);
	return returnstatus;
}

static int handle_showagi(int fd, int argc, char *argv[]) {
	struct agi_command *e;
	char fullcmd[80];
	if ((argc < 2))
		return RESULT_SHOWUSAGE;
	if (argc > 2) {
		e = find_command(argv + 2, 1);
		if (e) 
			ast_cli(fd, e->usage);
		else {
			if (find_command(argv + 2, -1)) {
				return help_workhorse(fd, argv + 1);
			} else {
				join(fullcmd, sizeof(fullcmd), argv+1);
				ast_cli(fd, "No such command '%s'.\n", fullcmd);
			}
		}
	} else {
		return help_workhorse(fd, NULL);
	}
	return RESULT_SUCCESS;
}

static int handle_dumpagihtml(int fd, int argc, char *argv[]) {
	struct agi_command *e;
	char fullcmd[80];
	char *tempstr;
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
		char *stringp=NULL;
		if (!commands[x].cmda[0]) break;
		e = &commands[x]; 
		if (e)
			join(fullcmd, sizeof(fullcmd), e->cmda);
		/* Hide commands that start with '_' */
		if (fullcmd[0] == '_')
			continue;

		fprintf(htmlfile, "<TR><TD><TABLE BORDER=\"1\" CELLPADDING=\"5\" WIDTH=\"100%%\">\n");
		fprintf(htmlfile, "<TR><TH ALIGN=\"CENTER\"><B>%s - %s</B></TD></TR>\n", fullcmd,e->summary);


		stringp=e->usage;
		tempstr = strsep(&stringp, "\n");

		fprintf(htmlfile, "<TR><TD ALIGN=\"CENTER\">%s</TD></TR>\n", tempstr);
		
		fprintf(htmlfile, "<TR><TD ALIGN=\"CENTER\">\n");
		while ((tempstr = strsep(&stringp, "\n")) != NULL) {
		fprintf(htmlfile, "%s<BR>\n",tempstr);

		}
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
	int res=0;
	struct localuser *u;
	char *argv[MAX_ARGS];
	char buf[2048]="";
	char *tmp = (char *)buf;
	int argc = 0;
	int fds[2];
	int efd = -1;
	int pid;
        char *stringp;
	AGI agi;
	if (!data || ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "AGI requires an argument (script)\n");
		return -1;
	}
	strncpy(buf, data, sizeof(buf) - 1);

	memset(&agi, 0, sizeof(agi));
        while ((stringp = strsep(&tmp, "|"))) {
		argv[argc++] = stringp;
        }
	argv[argc] = NULL;

	LOCAL_USER_ADD(u);
#if 0
	 /* Answer if need be */
        if (chan->_state != AST_STATE_UP) {
		if (ast_answer(chan)) {
			LOCAL_USER_REMOVE(u);
			return -1;
		}
	}
#endif
	res = launch_script(argv[0], argv, fds, enhanced ? &efd : NULL, &pid);
	if (!res) {
		agi.fd = fds[1];
		agi.ctrl = fds[0];
		agi.audio = efd;
		res = run_agi(chan, argv[0], &agi, pid, dead);
		close(fds[1]);
		if (efd > -1)
			close(efd);
	}
	LOCAL_USER_REMOVE(u);
	return res;
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
	return agi_exec_full(chan, data, 0, 1);
}

static char showagi_help[] =
"Usage: show agi [topic]\n"
"       When called with a topic as an argument, displays usage\n"
"       information on the given command.  If called without a\n"
"       topic, it provides a list of AGI commands.\n";


static char dumpagihtml_help[] =
"Usage: dump agihtml <filename>\n"
"	Dumps the agi command list in html format to given filename\n";

static struct ast_cli_entry showagi = 
{ { "show", "agi", NULL }, handle_showagi, "Show AGI commands or specific help", showagi_help };

static struct ast_cli_entry dumpagihtml = 
{ { "dump", "agihtml", NULL }, handle_dumpagihtml, "Dumps a list of agi command in html format", dumpagihtml_help };

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	ast_cli_unregister(&showagi);
	ast_cli_unregister(&dumpagihtml);
	ast_cli_unregister(&cli_debug);
	ast_cli_unregister(&cli_no_debug);
	ast_unregister_application(eapp);
	ast_unregister_application(deadapp);
	return ast_unregister_application(app);
}

int load_module(void)
{
	ast_cli_register(&showagi);
	ast_cli_register(&dumpagihtml);
	ast_cli_register(&cli_debug);
	ast_cli_register(&cli_no_debug);
	ast_register_application(deadapp, deadagi_exec, deadsynopsis, descrip);
	ast_register_application(eapp, eagi_exec, esynopsis, descrip);
	return ast_register_application(app, agi_exec, synopsis, descrip);
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

char *key()
{
	return ASTERISK_GPL_KEY;
}

