/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Asterisk Gateway Interface
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
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/astdb.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/signal.h>
#include <sys/time.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <asterisk/cli.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <asterisk/image.h>
#include <asterisk/say.h>
#include "../asterisk.h"
#include "../astconf.h"

#include <pthread.h>

#define MAX_ARGS 128

/* Recycle some stuff from the CLI interface */
#define fdprintf ast_cli

typedef struct agi_command {
	/* Null terminated list of the words of the command */
	char *cmda[AST_MAX_CMD_LEN];
	/* Handler for the command (fd for output, # of arguments, argument list). 
	    Returns RESULT_SHOWUSAGE for improper arguments */
	int (*handler)(struct ast_channel *chan, int fd, int argc, char *argv[]);
	/* Summary of the command (< 60 characters) */
	char *summary;
	/* Detailed usage information */
	char *usage;
} agi_command;

static char *tdesc = "Asterisk Gateway Interface (AGI)";

static char *app = "AGI";

static char *synopsis = "Executes an AGI compliant application";

static char *descrip =
"  AGI(command|args): Executes an Asterisk Gateway Interface compliant\n"
"program on a channel.   AGI allows Asterisk to launch external programs\n"
"written in any language to control a telephony channel, play audio,\n"
"read DTMF digits, etc. by communicating with the AGI protocol on stdin\n"
"and stdout.  Returns -1 on hangup or if application requested hangup, or\n"
"0 on non-hangup exit.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

extern char *pbx_builtin_getvar_helper(struct ast_channel *chan, char *name);
extern void pbx_builtin_setvar_helper(struct ast_channel *chan, char *name, char *value);

#define TONE_BLOCK_SIZE 200

static float loudness = 8192.0;

unsigned char linear2ulaw(short sample);
static void make_tone_block(unsigned char *data, float f1, int *x);

static void make_tone_block(unsigned char *data, float f1, int *x)
{
int	i;
float	val;

	for(i = 0; i < TONE_BLOCK_SIZE; i++)
	{
		val = loudness * sin((f1 * 2.0 * M_PI * (*x)++)/8000.0);
		data[i] = linear2ulaw((int)val);
	 }		
	  /* wrap back around from 8000 */
	if (*x >= 8000) *x = 0;
	return;
}

static int launch_script(char *script, char *args, int *fds, int *opid)
{
	char tmp[256];
	int pid;
	int toast[2];
	int fromast[2];
	int x;
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
	pid = fork();
	if (pid < 0) {
		ast_log(LOG_WARNING, "Failed to fork(): %s\n", strerror(errno));
		return -1;
	}
	if (!pid) {
		/* Redirect stdin and out */
		dup2(fromast[0], STDIN_FILENO);
		dup2(toast[1], STDOUT_FILENO);
		/* Close everything but stdin/out/error */
		for (x=STDERR_FILENO + 1;x<1024;x++) 
			close(x);
		/* Execute script */
		execl(script, script, args, NULL);
		/* Can't use ast_log since FD's are closed */
		fprintf(stderr, "Failed to execute '%s': %s\n", script, strerror(errno));
		exit(1);
	}
	if (option_verbose > 2) 
		ast_verbose(VERBOSE_PREFIX_3 "Launched AGI Script %s\n", script);
	fds[0] = toast[0];
	fds[1] = fromast[1];
	/* close what we're not using in the parent */
	close(toast[1]);
	close(fromast[0]);
	*opid = pid;
	return 0;
		
}

static void setup_env(struct ast_channel *chan, char *request, int fd)
{
	/* Print initial environment, with agi_request always being the first
	   thing */
	fdprintf(fd, "agi_request: %s\n", request);
	fdprintf(fd, "agi_channel: %s\n", chan->name);
	fdprintf(fd, "agi_language: %s\n", chan->language);
	fdprintf(fd, "agi_type: %s\n", chan->type);

	/* ANI/DNIS */
	fdprintf(fd, "agi_callerid: %s\n", chan->callerid ? chan->callerid : "");
	fdprintf(fd, "agi_dnid: %s\n", chan->dnid ? chan->dnid : "");
	fdprintf(fd, "agi_rdnis: %s\n", chan->rdnis ? chan->rdnis : "");

	/* Context information */
	fdprintf(fd, "agi_context: %s\n", chan->context);
	fdprintf(fd, "agi_extension: %s\n", chan->exten);
	fdprintf(fd, "agi_priority: %d\n", chan->priority);

	/* End with empty return */
	fdprintf(fd, "\n");
}

static int handle_answer(struct ast_channel *chan, int fd, int argc, char *argv[])
{
	int res;
	res = 0;
	if (chan->_state != AST_STATE_UP) {
		/* Answer the chan */
		res = ast_answer(chan);
	}
	fdprintf(fd, "200 result=%d\n", res);
	if (res >= 0)
		return RESULT_SUCCESS;
	else
		return RESULT_FAILURE;
}

static int handle_waitfordigit(struct ast_channel *chan, int fd, int argc, char *argv[])
{
	int res;
	int to;
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	if (sscanf(argv[3], "%i", &to) != 1)
		return RESULT_SHOWUSAGE;
	res = ast_waitfordigit(chan, to);
	fdprintf(fd, "200 result=%d\n", res);
	if (res >= 0)
		return RESULT_SUCCESS;
	else
		return RESULT_FAILURE;
}

static int handle_sendtext(struct ast_channel *chan, int fd, int argc, char *argv[])
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
	fdprintf(fd, "200 result=%d\n", res);
	if (res >= 0)
		return RESULT_SUCCESS;
	else
		return RESULT_FAILURE;
}

static int handle_recvchar(struct ast_channel *chan, int fd, int argc, char *argv[])
{
	int res;
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	res = ast_recvchar(chan,atoi(argv[2]));
	if (res == 0) {
		fdprintf(fd, "200 result=%d (timeout)\n", res);
		return RESULT_SUCCESS;
	}
	if (res > 0) {
		fdprintf(fd, "200 result=%d\n", res);
		return RESULT_SUCCESS;
	}
	else {
		fdprintf(fd, "200 result=%d (hangup)\n", res);
		return RESULT_FAILURE;
	}
}

static int handle_tddmode(struct ast_channel *chan, int fd, int argc, char *argv[])
{
	int res,x;
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	if (!strncasecmp(argv[2],"on",2)) x = 1; else x = 0;
	if (!strncasecmp(argv[2],"mate",4)) x = 2;
	if (!strncasecmp(argv[2],"tdd",3)) x = 1;
	res = ast_channel_setoption(chan,AST_OPTION_TDD,&x,sizeof(char),0);
	fdprintf(fd, "200 result=%d\n", res);
	if (res >= 0) 
		return RESULT_SUCCESS;
	else
		return RESULT_FAILURE;
}

static int handle_sendimage(struct ast_channel *chan, int fd, int argc, char *argv[])
{
	int res;
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	res = ast_send_image(chan, argv[2]);
	if (!ast_check_hangup(chan))
		res = 0;
	fdprintf(fd, "200 result=%d\n", res);
	if (res >= 0)
		return RESULT_SUCCESS;
	else
		return RESULT_FAILURE;
}

static int handle_streamfile(struct ast_channel *chan, int fd, int argc, char *argv[])
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
		fdprintf(fd, "200 result=%d endpos=%ld\n", 0, sample_offset);
		ast_log(LOG_WARNING, "Unable to open %s\n", argv[2]);
		return RESULT_FAILURE;
	}
	ast_seekstream(fs, 0, SEEK_END);
	max_length = ast_tellstream(fs);
	ast_seekstream(fs, sample_offset, SEEK_SET);
	res = ast_applystream(chan, fs);
	res = ast_playstream(fs);
	if (res) {
		fdprintf(fd, "200 result=%d endpos=%ld\n", res, sample_offset);
		if (res >= 0)
			return RESULT_SHOWUSAGE;
		else
			return RESULT_FAILURE;
	}
	res = ast_waitstream(chan, argv[3]);
	/* this is to check for if ast_waitstream closed the stream, we probably are at
	 * the end of the stream, return that amount, else check for the amount */
	sample_offset = (chan->stream)?ast_tellstream(fs):max_length;
	ast_stopstream(chan);
	fdprintf(fd, "200 result=%d endpos=%ld\n", res, sample_offset);
	if (res >= 0)
		return RESULT_SUCCESS;
	else
		return RESULT_FAILURE;
}

static int handle_saynumber(struct ast_channel *chan, int fd, int argc, char *argv[])
{
	int res;
	int num;
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	if (sscanf(argv[2], "%i", &num) != 1)
		return RESULT_SHOWUSAGE;
	res = ast_say_number(chan, num, argv[3], chan->language);
	fdprintf(fd, "200 result=%d\n", res);
	if (res >= 0)
		return RESULT_SUCCESS;
	else
		return RESULT_FAILURE;
}

static int handle_saydigits(struct ast_channel *chan, int fd, int argc, char *argv[])
{
	int res;
	int num;
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	if (sscanf(argv[2], "%i", &num) != 1)
		return RESULT_SHOWUSAGE;
	res = ast_say_digit_str(chan, argv[2], argv[3], chan->language);
	fdprintf(fd, "200 result=%d\n", res);
	if (res >= 0)
		return RESULT_SUCCESS;
	else
		return RESULT_FAILURE;
}

int ast_app_getdata(struct ast_channel *c, char *prompt, char *s, int maxlen, int timeout);

static int handle_getdata(struct ast_channel *chan, int fd, int argc, char *argv[])
{
	int res;
	char data[50];
	int max;
	int timeout;

	if (argc < 3)
		return RESULT_SHOWUSAGE;
	if (argc >= 4) timeout = atoi(argv[3]); else timeout = 0;
	if (argc >= 5) max = atoi(argv[4]); else max = 50;
	res = ast_app_getdata(chan, argv[2], data, max, timeout);
	if (res == 1)
		fdprintf(fd, "200 result=%s (timeout)\n", data);
	else
		fdprintf(fd, "200 result=%s\n", data);
	if (res >= 0)
		return RESULT_SUCCESS;
	else
		return RESULT_FAILURE;
}

static int handle_setcontext(struct ast_channel *chan, int fd, int argc, char *argv[])
{

	if (argc != 3)
		return RESULT_SHOWUSAGE;
	strncpy(chan->context, argv[2], sizeof(chan->context)-1);
	fdprintf(fd, "200 result=0\n");
	return RESULT_SUCCESS;
}
	
static int handle_setextension(struct ast_channel *chan, int fd, int argc, char **argv)
{
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	strncpy(chan->exten, argv[2], sizeof(chan->exten)-1);
	fdprintf(fd, "200 result=0\n");
	return RESULT_SUCCESS;
}

static int handle_setpriority(struct ast_channel *chan, int fd, int argc, char **argv)
{
	int pri;
	if (argc != 3)
		return RESULT_SHOWUSAGE;	
	if (sscanf(argv[2], "%i", &pri) != 1)
		return RESULT_SHOWUSAGE;
	chan->priority = pri - 1;
	fdprintf(fd, "200 result=0\n");
	return RESULT_SUCCESS;
}
		
static int ms_diff(struct timeval *tv1, struct timeval *tv2)
{
int	ms;
	
	ms = (tv1->tv_sec - tv2->tv_sec) * 1000;
	ms += (tv1->tv_usec - tv2->tv_usec) / 1000;
	return(ms);
}

static int handle_recordfile(struct ast_channel *chan, int fd, int argc, char *argv[])
{
	struct ast_filestream *fs;
	struct ast_frame *f;
	struct timeval tv, start;
	long sample_offset = 0;
	int res = 0;
	int ms;

	if (argc < 6)
		return RESULT_SHOWUSAGE;
	if (sscanf(argv[5], "%i", &ms) != 1)
		return RESULT_SHOWUSAGE;
	/* backward compatibility, if no offset given, arg[6] would have been
	 * caught below and taken to be a beep, else if it is a digit then it is a
	 * offset */
	if ((argc >6) && (sscanf(argv[6], "%ld", &sample_offset) != 1))
		res = ast_streamfile(chan, "beep", chan->language);

	if (argc > 7)
		res = ast_streamfile(chan, "beep", chan->language);
	if (!res)
		res = ast_waitstream(chan, argv[4]);
	if (!res) {
		fs = ast_writefile(argv[2], argv[3], NULL, O_CREAT | O_WRONLY, 0, 0644);
		if (!fs) {
			res = -1;
			fdprintf(fd, "200 result=%d (writefile)\n", res);
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
				fdprintf(fd, "200 result=%d (waitfor) endpos=%ld\n", res,sample_offset);
				return RESULT_FAILURE;
			}
			f = ast_read(chan);
			if (!f) {
				fdprintf(fd, "200 result=%d (hangup) endpos=%ld\n", 0, sample_offset);
				ast_closestream(fs);
				return RESULT_FAILURE;
			}
			switch(f->frametype) {
			case AST_FRAME_DTMF:
				if (strchr(argv[4], f->subclass)) {
					/* This is an interrupting chracter */
					sample_offset = ast_tellstream(fs);
					fdprintf(fd, "200 result=%d (dtmf) endpos=%ld\n", f->subclass, sample_offset);
					ast_closestream(fs);
					ast_frfree(f);
					return RESULT_SUCCESS;
				}
				break;
			case AST_FRAME_VOICE:
				ast_writestream(fs, f);
				/* this is a safe place to check progress since we know that fs
				 * is valid after a write, and it will then have our current
				 * location */
				sample_offset = ast_tellstream(fs);
				break;
			}
			ast_frfree(f);
		    gettimeofday(&tv, NULL);
        }
		fdprintf(fd, "200 result=%d (timeout) endpos=%ld\n", res, sample_offset);
		ast_closestream(fs);
	} else
		fdprintf(fd, "200 result=%d (randomerror) endpos=%ld\n", res, sample_offset);
	return RESULT_SUCCESS;
}

static int handle_autohangup(struct ast_channel *chan, int fd, int argc, char *argv[])
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
	fdprintf(fd, "200 result=0\n");
	return RESULT_SUCCESS;
}

static int handle_hangup(struct ast_channel *chan, int fd, int argc, char **argv)
{
        struct ast_channel *c;
        if (argc==1) {
            /* no argument: hangup the current channel */
	    ast_softhangup(chan,AST_SOFTHANGUP_EXPLICIT);
	    fdprintf(fd, "200 result=1\n");
	    return RESULT_SUCCESS;
        } else if (argc==2) {
            /* one argument: look for info on the specified channel */
            c = ast_channel_walk(NULL);
            while (c) {
                if (strcasecmp(argv[1],c->name)==0) {
                    /* we have a matching channel */
	            ast_softhangup(c,AST_SOFTHANGUP_EXPLICIT);
	            fdprintf(fd, "200 result=1\n");
                    return RESULT_SUCCESS;
                }
                c = ast_channel_walk(c);
            }
            /* if we get this far no channel name matched the argument given */
            fdprintf(fd, "200 result=-1\n");
            return RESULT_SUCCESS;
        } else {
            return RESULT_SHOWUSAGE;
        }
}

static int handle_exec(struct ast_channel *chan, int fd, int argc, char **argv)
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
	fdprintf(fd, "200 result=%d\n", res);

	return res;
}

static int handle_setcallerid(struct ast_channel *chan, int fd, int argc, char **argv)
{
	if (argv[2])
		ast_set_callerid(chan, argv[2], 0);

/*	strncpy(chan->callerid, argv[2], sizeof(chan->callerid)-1);
*/	fdprintf(fd, "200 result=1\n");
	return RESULT_SUCCESS;
}

static int handle_channelstatus(struct ast_channel *chan, int fd, int argc, char **argv)
{
        struct ast_channel *c;
        if (argc==2) {
            /* no argument: supply info on the current channel */
            fdprintf(fd, "200 result=%d\n", chan->_state);
	    return RESULT_SUCCESS;
        } else if (argc==3) {
            /* one argument: look for info on the specified channel */
            c = ast_channel_walk(NULL);
            while (c) {
                if (strcasecmp(argv[2],c->name)==0) {
                    fdprintf(fd, "200 result=%d\n", c->_state);
                    return RESULT_SUCCESS;
                }
                c = ast_channel_walk(c);
            }
            /* if we get this far no channel name matched the argument given */
            fdprintf(fd, "200 result=-1\n");
            return RESULT_SUCCESS;
        } else {
            return RESULT_SHOWUSAGE;
        }
}

static int handle_setvariable(struct ast_channel *chan, int fd, int argc, char **argv)
{
	if (argv[3])
		pbx_builtin_setvar_helper(chan, argv[2], argv[3]);

	fdprintf(fd, "200 result=1\n");
	return RESULT_SUCCESS;
}

static int handle_getvariable(struct ast_channel *chan, int fd, int argc, char **argv)
{
	char *tempstr;

	if ((tempstr = pbx_builtin_getvar_helper(chan, argv[2])) ) 
			fdprintf(fd, "200 result=1 (%s)\n", tempstr);
	else
			fdprintf(fd, "200 result=0\n");

	return RESULT_SUCCESS;
}

static int handle_verbose(struct ast_channel *chan, int fd, int argc, char **argv)
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
	
	fdprintf(fd, "200 result=1\n");
	
	return RESULT_SUCCESS;
}

static int handle_dbget(struct ast_channel *chan, int fd, int argc, char **argv)
{
	int res;
	char tmp[256];
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	res = ast_db_get(argv[2], argv[3], tmp, sizeof(tmp));
	if (res) 
			fdprintf(fd, "200 result=0\n");
	else
			fdprintf(fd, "200 result=1 (%s)\n", tmp);

	return RESULT_SUCCESS;
}

static int handle_dbput(struct ast_channel *chan, int fd, int argc, char **argv)
{
	int res;
	if (argc != 5)
		return RESULT_SHOWUSAGE;
	res = ast_db_put(argv[2], argv[3], argv[4]);
	if (res) 
			fdprintf(fd, "200 result=0\n");
	else
			fdprintf(fd, "200 result=1\n");

	return RESULT_SUCCESS;
}

static int handle_dbdel(struct ast_channel *chan, int fd, int argc, char **argv)
{
	int res;
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	res = ast_db_del(argv[2], argv[3]);
	if (res) 
		fdprintf(fd, "200 result=0\n");
	else
		fdprintf(fd, "200 result=1\n");

	return RESULT_SUCCESS;
}

static int handle_dbdeltree(struct ast_channel *chan, int fd, int argc, char **argv)
{
	int res;
	if ((argc < 3) || (argc > 4))
		return RESULT_SHOWUSAGE;
	if (argc == 4)
		res = ast_db_deltree(argv[2], argv[3]);
	else
		res = ast_db_deltree(argv[2], NULL);

	if (res) 
		fdprintf(fd, "200 result=0\n");
	else
		fdprintf(fd, "200 result=1\n");
	return RESULT_SUCCESS;
}

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
" Usage: RECORD FILE <filename> <format> <escape digits> <timeout> [offset samples] [BEEP]\n"
"        Record to a file until a given dtmf digit in the sequence is received\n"
" Returns -1 on hangup or error.  The format will specify what kind of file\n"
" will be recorded.  The timeout is the maximum record time in milliseconds, or\n"
" -1 for no timeout. Offset samples is optional, and if provided will seek to\n"
" the offset without exceeding the end of the file\n";

static char usage_autohangup[] =
" Usage: SET AUTOHANGUP <time>\n"
"    Cause the channel to automatically hangup at <time> seconds in the\n"
"future.  Of course it can be hungup before then as well.   Setting to\n"
"0 will cause the autohangup feature to be disabled on this channel.\n";

agi_command commands[] = {
	{ { "answer", NULL }, handle_answer, "Asserts answer", usage_answer },
	{ { "wait", "for", "digit", NULL }, handle_waitfordigit, "Waits for a digit to be pressed", usage_waitfordigit },
	{ { "send", "text", NULL }, handle_sendtext, "Sends text to channels supporting it", usage_sendtext },
	{ { "receive", "char", NULL }, handle_recvchar, "Receives text from channels supporting it", usage_recvchar },
	{ { "tdd", "mode", NULL }, handle_tddmode, "Sends text to channels supporting it", usage_tddmode },
	{ { "stream", "file", NULL }, handle_streamfile, "Sends audio file on channel", usage_streamfile },
	{ { "send", "image", NULL }, handle_sendimage, "Sends images to channels supporting it", usage_sendimage },
	{ { "say", "digits", NULL }, handle_saydigits, "Says a given digit string", usage_saydigits },
	{ { "say", "number", NULL }, handle_saynumber, "Says a given number", usage_saynumber },
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
	{ { "verbose", NULL }, handle_verbose, "Logs a message to the asterisk verbose log", usage_verbose },
	{ { "database", "get", NULL }, handle_dbget, "Gets database value", usage_dbget },
	{ { "database", "put", NULL }, handle_dbput, "Adds/updates database value", usage_dbput },
	{ { "database", "del", NULL }, handle_dbdel, "Removes database key/value", usage_dbdel },
	{ { "database", "deltree", NULL }, handle_dbdeltree, "Removes database keytree/value", usage_dbdeltree }
};

static void join(char *s, int len, char *w[])
{
	int x;
	/* Join words into a string */
	strcpy(s, "");
	for (x=0;w[x];x++) {
		if (x)
			strncat(s, " ", len - strlen(s));
		strncat(s, w[x], len - strlen(s));
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

static agi_command *find_command(char *cmds[], int exact)
{
	int x;
	int y;
	int match;
	for (x=0;x < sizeof(commands) / sizeof(commands[0]);x++) {
		/* start optimistic */
		match = 1;
		for (y=0;match && cmds[y]; y++) {
			/* If there are no more words in the command (and we're looking for
			   an exact match) or there is a difference between the two words,
			   then this is not a match */
			if (!commands[x].cmda[y] && !exact)
				break;
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

static int agi_handle_command(struct ast_channel *chan, int fd, char *buf)
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
		res = c->handler(chan, fd, argc, argv);
		switch(res) {
		case RESULT_SHOWUSAGE:
			fdprintf(fd, "520-Invalid command syntax.  Proper usage follows:\n");
			fdprintf(fd, c->usage);
			fdprintf(fd, "520 End of proper usage.\n");
			break;
		case RESULT_FAILURE:
			/* They've already given the failure.  We've been hung up on so handle this
			   appropriately */
			return -1;
		}
	} else {
		fdprintf(fd, "510 Invalid or unknown command\n");
	}
	return 0;
}

static int run_agi(struct ast_channel *chan, char *request, int *fds, int pid)
{
	struct ast_channel *c;
	int outfd;
	int ms;
	int returnstatus = 0;
	struct ast_frame *f;
	char buf[2048];
	FILE *readf;
	if (!(readf = fdopen(fds[0], "r"))) {
		ast_log(LOG_WARNING, "Unable to fdopen file descriptor\n");
		kill(pid, SIGHUP);
		return -1;
	}
	setlinebuf(readf);
	setup_env(chan, request, fds[1]);
	for (;;) {
		ms = -1;
		c = ast_waitfor_nandfds(&chan, 1, &fds[0], 1, NULL, &outfd, &ms);
		if (c) {
			/* Idle the channel until we get a command */
			f = ast_read(c);
			if (!f) {
				ast_log(LOG_DEBUG, "%s hungup\n", chan->name);
				returnstatus = -1;
				break;
			} else {
				ast_frfree(f);
			}
		} else if (outfd > -1) {
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

			returnstatus |= agi_handle_command(chan, fds[1], buf);
			/* If the handle_command returns -1, we need to stop */
			if (returnstatus < 0) {
				break;
			}
		} else {
			ast_log(LOG_WARNING, "No channel, no fd?\n");
			returnstatus = -1;
			break;
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

static int agi_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	char *args,*ringy;
	char tmp[256];
	int fds[2];
	int pid;
	char *stringp=tmp;
	if (!data || !strlen(data)) {
		ast_log(LOG_WARNING, "AGI requires an argument (script)\n");
		return -1;
	}


	strncpy(tmp, data, sizeof(tmp)-1);
	strsep(&stringp, "|");
	args = strsep(&stringp, "|");
	ringy = strsep(&stringp,"|");
	if (!args)
		args = "";
	LOCAL_USER_ADD(u);
#if 0
	 /* Answer if need be */
        if (chan->_state != AST_STATE_UP) {
		if (ringy) { /* if for ringing first */
			/* a little ringy-dingy first */
		        ast_indicate(chan, AST_CONTROL_RINGING);  
			sleep(3); 
		}
		if (ast_answer(chan)) {
			LOCAL_USER_REMOVE(u);
			return -1;
		}
	}
#endif
	res = launch_script(tmp, args, fds, &pid);
	if (!res) {
		res = run_agi(chan, tmp, fds, pid);
		close(fds[0]);
		close(fds[1]);
	}
	LOCAL_USER_REMOVE(u);
	return res;
}

static char showagi_help[] =
"Usage: show agi [topic]\n"
"       When called with a topic as an argument, displays usage\n"
"       information on the given command.  If called without a\n"
"       topic, it provides a list of AGI commands.\n";


static char dumpagihtml_help[] =
"Usage: dump agihtml <filename>\n"
"	Dumps the agi command list in html format to given filename\n";

struct ast_cli_entry showagi = 
{ { "show", "agi", NULL }, handle_showagi, "Show AGI commands or specific help", showagi_help };

struct ast_cli_entry dumpagihtml = 
{ { "dump", "agihtml", NULL }, handle_dumpagihtml, "Dumps a list of agi command in html format", dumpagihtml_help };

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	ast_cli_unregister(&showagi);
	ast_cli_unregister(&dumpagihtml);
	return ast_unregister_application(app);
}

int load_module(void)
{
	ast_cli_register(&showagi);
	ast_cli_register(&dumpagihtml);
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

#define CLIP 32635
#define BIAS 0x84

unsigned char
linear2ulaw(sample)
short sample; {
  static int exp_lut[256] = {0,0,1,1,2,2,2,2,3,3,3,3,3,3,3,3,
                             4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
                             5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
                             5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
                             6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
                             6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
                             6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
                             6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                             7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7};
  int sign, exponent, mantissa;
  unsigned char ulawbyte;
 
  /* Get the sample into sign-magnitude. */
  sign = (sample >> 8) & 0x80;          /* set aside the sign */
  if (sign != 0) sample = -sample;              /* get magnitude */
  if (sample > CLIP) sample = CLIP;             /* clip the magnitude */
 
  /* Convert from 16 bit linear to ulaw. */
  sample = sample + BIAS;
  exponent = exp_lut[(sample >> 7) & 0xFF];
  mantissa = (sample >> (exponent + 3)) & 0x0F;
  ulawbyte = ~(sign | (exponent << 4) | mantissa);
#ifdef ZEROTRAP
  if (ulawbyte == 0) ulawbyte = 0x02;   /* optional CCITT trap */
#endif
 
  return(ulawbyte);
}
