/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Voicemail System (did you ever think it could be so easy?)
 * 
 * Copyright (C) 1999, Adtran Inc. and Linux Support Services, LLC
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
#include <asterisk/options.h>
#include <asterisk/config.h>
#include <asterisk/say.h>
#include <asterisk/module.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <time.h>

#include <pthread.h>
#include "../asterisk.h"

#define COMMAND_TIMEOUT 5000

#define VOICEMAIL_CONFIG "voicemail.conf"
#define ASTERISK_USERNAME "asterisk"

/*
#define HOSTNAME_OVERRIDE "linux-support.net"
*/

#define SENDMAIL "/usr/sbin/sendmail -t"

#define INTRO "vm-intro"

#define MAXMSG 100

#define MAX_OTHER_FORMATS 10

#define VM_SPOOL_DIR AST_SPOOL_DIR "/vm"


static char *tdesc = "Comedian Mail (Voicemail System)";

/* Leave a message */
static char *app = "VoiceMail";

/* Check mail, control, etc */
static char *app2 = "VoiceMailMain";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static char *get_dir(char *ext, char *mailbox)
{
	char *tmp = malloc(strlen(ext) + strlen(VM_SPOOL_DIR) + 3 + strlen(mailbox));
	sprintf(tmp, "%s/%s/%s", VM_SPOOL_DIR, ext, mailbox);
	return tmp;
}
static char *get_fn(char *dir, int num)
{
	char *tmp = malloc(strlen(dir) + 10);
	sprintf(tmp, "%s/msg%04d", dir, num);
	return tmp;
}

static int announce_message(struct ast_channel *chan, char *dir, int msgcnt)
{
	char *fn;
	int res;
	res = ast_streamfile(chan, "vm-message");
	if (!res) {
		res = ast_waitstream(chan, AST_DIGIT_ANY);
		if (!res) {
			res = ast_say_number(chan, msgcnt+1);
			if (!res) {
				fn = get_fn(dir, msgcnt);
				if (fn) {
					res = ast_streamfile(chan, fn);
					free(fn);
				}
			}
		}
	}
	if (res < 0)
		ast_log(LOG_WARNING, "Unable to announce message\n");
	return res;
}
static int sendmail(char *email, char *name, int msgnum, char *mailbox)
{
	FILE *p;
	char date[256];
	char host[256];
	time_t t;
	struct tm *tm;
	p = popen(SENDMAIL, "w");
	if (p) {
		gethostname(host, sizeof(host));
		time(&t);
		tm = localtime(&t);
		strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S %z", tm);
		fprintf(p, "Date: %s\n", date);
		fprintf(p, "Message-ID: <Asterisk-%d-%s-%d@%s>\n", msgnum, mailbox, getpid(), host);
		fprintf(p, "From: Asterisk PBX <%s@%s>\n", ASTERISK_USERNAME,
#ifdef HOSTNAME_OVERRIDE
				HOSTNAME_OVERRIDE
#else
				host
#endif
				);
		fprintf(p, "To: %s <%s>\n", name, email);
		fprintf(p, "Subject: [PBX]: New message %d in mailbox %s\n\n", msgnum, mailbox);
		strftime(date, sizeof(date), "%A, %B %d, %Y at %r", tm);
		fprintf(p, "Dear %s:\n\n\tJust wanted to let you know you were just left a message (number %d)\n"
		           "in mailbox %s, on %s so you might\n"
				   "want to check it when you get a chance.  Thanks!\n\n\t\t\t\t--Asterisk\n", name, msgnum, mailbox, date);
		fprintf(p, ".\n");
		pclose(p);
	} else {
		ast_log(LOG_WARNING, "Unable to launch '%s'\n", SENDMAIL);
		return -1;
	}
	return 0;
}

static int leave_voicemail(struct ast_channel *chan, char *ext, int silent)
{
	struct ast_config *cfg;
	char *copy, *name, *passwd, *email, *dir, *fmt, *fmts, *fn=NULL;
	char comment[256];
	struct ast_filestream *writer, *others[MAX_OTHER_FORMATS];
	char *sfmt[MAX_OTHER_FORMATS];
	int res = -1, fmtcnt=0, x;
	int msgnum;
	int outmsg=0;
	struct ast_frame *f;
	
	cfg = ast_load(VOICEMAIL_CONFIG);
	if (!cfg) {
		ast_log(LOG_WARNING, "No such configuration file %s\n", VOICEMAIL_CONFIG);
		return -1;
	}
	if ((copy = ast_variable_retrieve(cfg, chan->context, ext))) {
		/* Make sure they have an entry in the config */
		copy = strdup(copy);
		passwd = strtok(copy, ",");
		name = strtok(NULL, ",");
		email = strtok(NULL, ",");
		dir = get_dir(ext, "");
		/* It's easier just to try to make it than to check for its existence */
		if (mkdir(dir, 0700) && (errno != EEXIST))
			ast_log(LOG_WARNING, "mkdir '%s' failed: %s\n", dir, strerror(errno));
		dir = get_dir(ext, "INBOX");
		if (mkdir(dir, 0700) && (errno != EEXIST))
			ast_log(LOG_WARNING, "mkdir '%s' failed: %s\n", dir, strerror(errno));
		/* Stream an info message */
		if (silent || !ast_streamfile(chan, INTRO)) {
			/* Wait for the message to finish */
			if (silent || !ast_waitstream(chan, "")) {
				fmt = ast_variable_retrieve(cfg, "general", "format");
				if (fmt) {
					fmts = strdup(fmt);
					fmt = strtok(fmts, "|");
					msgnum = 0;
					do {
						if (fn)
							free(fn);
						fn = get_fn(dir, msgnum);
						snprintf(comment, sizeof(comment), "Voicemail from %s to %s (%s) on %s\n",
											(chan->callerid ? chan->callerid : "Unknown"), 
											name, ext, chan->name);
						writer = ast_writefile(fn, fmt, comment, O_EXCL, 1 /* check for other formats */, 0700);
						if (!writer && (errno != EEXIST))
							break;
						msgnum++;
					} while(!writer && (msgnum < MAXMSG));
					if (writer) {
						/* We need to reset these values */
						free(fmts);
						fmt = ast_variable_retrieve(cfg, "general", "format");
						fmts = strdup(fmt);
						strtok(fmts, "|");
						while((fmt = strtok(NULL, "|"))) {
							if (fmtcnt > MAX_OTHER_FORMATS - 1) {
								ast_log(LOG_WARNING, "Please increase MAX_OTHER_FORMATS in app_voicemail.c\n");
								break;
							}
							sfmt[fmtcnt++] = strdup(fmt);
						}
						for (x=0;x<fmtcnt;x++) {
							others[x] = ast_writefile(fn, sfmt[x], comment, 0, 0, 0700);
							if (!others[x]) {
								/* Ick, the other format didn't work, but be sure not
								   to leak memory here */
								int y;
								for(y=x+1;y < fmtcnt;y++)
									free(sfmt[y]);
								break;
							}
							free(sfmt[x]);
						}
						if (x == fmtcnt) {
							/* Loop forever, writing the packets we read to the writer(s), until
							   we read a # or get a hangup */
							if (option_verbose > 2) 
								ast_verbose( VERBOSE_PREFIX_3 "Recording to %s\n", fn);
							while((f = ast_read(chan))) {
								if (f->frametype == AST_FRAME_VOICE) {
									/* Write the primary format */
									res = ast_writestream(writer, f);
									/* And each of the others */
									for (x=0;x<fmtcnt;x++)
										res |= ast_writestream(others[x], f);
									ast_frfree(f);
									/* Exit on any error */
									if (res) {
										ast_log(LOG_WARNING, "Error writing frame\n");
										break;
									}
								}
								if (f->frametype == AST_FRAME_DTMF) {
									if (f->subclass == '#') {
										if (option_verbose > 2) 
											ast_verbose( VERBOSE_PREFIX_3 "User ended message by pressing %c\n", f->subclass);
										outmsg=2;
										break;
									}
								}
							}
							if (!f) {
								if (option_verbose > 2) 
									ast_verbose( VERBOSE_PREFIX_3 "User hung up\n");
								res = -1;
								outmsg=1;
							}
						} else {
							ast_log(LOG_WARNING, "Error creating writestream '%s', format '%s'\n", fn, sfmt[x]); 
							free(sfmt[x]);
						}
						ast_closestream(writer);
						for (x=0;x<fmtcnt;x++) {
							if (!others[x])
								break;
							ast_closestream(others[x]);
						}
						if (outmsg) {
							if (outmsg > 1) {
								/* Let them know it worked */
								ast_streamfile(chan, "vm-msgsaved");
								ast_waitstream(chan, "");
							}
							/* Send e-mail if applicable */
							if (email) 
								sendmail(email, name, msgnum, ext);
						}
					} else {
						if (msgnum < MAXMSG)
							ast_log(LOG_WARNING, "Error writing to mailbox %s\n", ext);
						else
							ast_log(LOG_WARNING, "Too many messages in mailbox %s\n", ext);
					}
					if (fn)
						free(fn);
					free(fmts);
				} else 
					ast_log(LOG_WARNING, "No format to save messages in \n");
			}
		} else
			ast_log(LOG_WARNING, "Unable to playback instructions\n");
			
		free(dir);
		free(copy);
	} else
		ast_log(LOG_WARNING, "No entry in voicemail config file for '%s'\n", ext);
	ast_destroy(cfg);
	/* Leave voicemail for someone */
	return res;
}

static int vm_execmain(struct ast_channel *chan, void *data)
{
	/* XXX This is, admittedly, some pretty horrendus code XXX */
	int res=-1;
	int valid = 0;
	int curmsg = 0;
	int maxmsg = 0;
	int x;
	char *fn, *nfn;
	char d;
	struct localuser *u;
	char username[80];
	char password[80], *copy;
	int deleted[MAXMSG];
	struct ast_config *cfg;
	int state;
	char *dir=NULL;
	
	LOCAL_USER_ADD(u);
	cfg = ast_load(VOICEMAIL_CONFIG);
	if (!cfg) {
		ast_log(LOG_WARNING, "No voicemail configuration\n");
		goto out;
	}
	if (ast_streamfile(chan, "vm-login"))
		goto out;
	do {
		/* Prompt for, and read in the username */
		if (ast_readstring(chan, username, sizeof(username), 2000, 5000, "#"))
			goto out;
		if (ast_streamfile(chan, "vm-password"))
			goto out;
		if (ast_readstring(chan, password, sizeof(password), 2000, 5000, "#"))
			goto out;
		copy = ast_variable_retrieve(cfg, chan->context, username);
		if (copy) {
			copy = strdup(copy);
			strtok(copy, ",");
			if (!strcmp(password,copy))
				valid++;
			else if (option_verbose > 2)
				ast_verbose( VERBOSE_PREFIX_3 "Incorrect password '%s' for user '%s'\n", password, username);
			free(copy);
		} else if (option_verbose > 2)
			ast_verbose( VERBOSE_PREFIX_3 "No such user '%s' in config file\n", username);
		if (!valid) {
			if (ast_streamfile(chan, "vm-incorrect"))
				break;
			if (ast_waitstream(chan, ""))
				break;
		}
	} while (!valid);
	if (valid) {
		dir = get_dir(username, "INBOX");
		if (!dir) 
			goto out;

		deleted[0] = 0;
		/* Find out how many messages are there, mark all as
		   not deleted. */
		do {
			fn = get_fn(dir, maxmsg);
			if ((res = ast_fileexists(fn, NULL))>0) {
				maxmsg++;
				deleted[maxmsg] = 0;
			}
			free(fn);
		} while(res > 0);
		if (ast_streamfile(chan, "vm-youhave"))
			goto out;
		if ((d=ast_waitstream(chan, AST_DIGIT_ANY)) < 0)
			goto out;
		ast_stopstream(chan);
		if (!d) {
			/* If they haven't interrupted us, play the message count */
			if (maxmsg > 0) {
				if ((d = ast_say_number(chan, maxmsg)) < 0)
					goto out;
			} else {
				if (ast_streamfile(chan, "vm-no"))
					goto out;
				if ((d=ast_waitstream(chan, AST_DIGIT_ANY)) < 0)
					goto out;
				ast_stopstream(chan);
			}
			if (!d) {
				/* And if they still haven't, give them the last word */
				if (ast_streamfile(chan, ((maxmsg == 1) ? "vm-message" : "vm-messages")))
					goto out;
				if (ast_waitstream(chan, AST_DIGIT_ANY) < 0)
					goto out;
				ast_stopstream(chan);
			}
		}
		res = -1;
				
#define STATE_STARTING 1
#define STATE_MESSAGE 2
#define STATE_MESSAGE_PLAYING 3
		state = STATE_STARTING;
		ast_log(LOG_EVENT, "User '%s' logged in on channel '%s' with %d message(s).\n", username, chan->name, maxmsg);
		if (option_verbose > 2)
			ast_verbose( VERBOSE_PREFIX_3 "User '%s' logged in on channel %s with %d messages\n", username, chan->name, maxmsg);
		if (!ast_streamfile(chan, "vm-instructions")) {
			for(;;) {
				if (chan->stream || (chan->trans && chan->trans->stream)) {
					d = ast_waitstream(chan, AST_DIGIT_ANY);
					ast_stopstream(chan);
					if (!d && (state == STATE_MESSAGE_PLAYING)) {
						state  = STATE_MESSAGE;
						/* If it runs out playing a message, then give directions */
						if (!(d = ast_streamfile(chan, "vm-msginstruct")))
							d = ast_waitstream(chan, AST_DIGIT_ANY);
						ast_stopstream(chan);
					}
					if (!d)
						d = ast_waitfordigit(chan, COMMAND_TIMEOUT);
				} else
					d = ast_waitfordigit(chan, COMMAND_TIMEOUT);
				if (d < 0)
					goto out;
restart:
				if (!d || (d == '*')) {
					/* If they don't say anything, play back a message.  We'll decide which one is
					   best based up on where they are.  Ditto if they press the '*' key. */
					switch(state) {
					case STATE_STARTING:
						if (ast_streamfile(chan, "vm-instructions"))
							goto out;
						break;
					case STATE_MESSAGE:
					case STATE_MESSAGE_PLAYING:
						if (ast_streamfile(chan, "vm-msginstruct"))
							goto out;
						break;
					default:
						ast_log(LOG_WARNING, "What do I do when they timeout/* in state %d?\n", state);
					}
				} else {
					/* XXX Should we be command-compatible with Meridian mail?  Their system seems
					       very confusing, but also widely used XXX */
					/* They've entered (or started to enter) a command */
					switch(d) {
					case '0':
						if (curmsg < maxmsg) {
							deleted[curmsg] = !deleted[curmsg];
							if (deleted[curmsg]) {
								if (ast_streamfile(chan, "vm-deleted"))
									goto out;
							} else {
								if (ast_streamfile(chan, "vm-undeleted"))
									goto out;
							}
						} else {
							if (ast_streamfile(chan, "vm-nomore"))
								goto out;
						}
						break;
					case '1':
						curmsg = 0;
						if (maxmsg > 0) {
							/* Yuck */
							if ((d = announce_message(chan, dir, curmsg)) > 0)
								goto restart;
							else if (d < 0)
								goto out;
						} else {
							if (ast_streamfile(chan, "vm-nomore"))
								goto out;
						}
						state = STATE_MESSAGE_PLAYING;
						break;
					case '4':
						if (curmsg > 0)
							curmsg--;
						/* Yuck */
						if ((d = announce_message(chan, dir, curmsg)) > 0)
							goto restart;
						else if (d < 0)
							goto out;
						state = STATE_MESSAGE_PLAYING;
						break;
					case '5':
						if ((d = announce_message(chan, dir, curmsg)) > 0)
							goto restart;
						else if (d < 0)
							goto out;
						state = STATE_MESSAGE_PLAYING;
						break;
					case '6':
						if (curmsg < maxmsg - 1) {
							curmsg++;
							if ((d = announce_message(chan, dir, curmsg)) > 0)
								goto restart;
							else if (d < 0)
								goto out;
						} else {
							if (ast_streamfile(chan, "vm-nomore"))
								goto out;
						}
						state = STATE_MESSAGE_PLAYING;
						break;
					/* XXX Message compose? It's easy!  Just read their # and, assuming it's in the config, 
					       call the routine as if it were called from the PBX proper XXX */
					case '#':
						if (ast_streamfile(chan, "vm-goodbye"))
							goto out;
						if (ast_waitstream(chan, ""))
							goto out;
						res = 0;
						goto out;
						break;
					default:
						/* Double yuck */
						d = '*';
						goto restart;
					}
				}
			}
		}
	}
	
out:
	ast_stopstream(chan);
	if (maxmsg) {
		/* Get the deleted messages fixed */
		curmsg = -1;
		for (x=0;x<maxmsg;x++) {
			if (!deleted[x]) {
				curmsg++;
				fn = get_fn(dir, x);
				nfn = get_fn(dir, curmsg);
				if (strcmp(fn, nfn))
					ast_filerename(fn, nfn, NULL);
				free(fn);
				free(nfn);
			}
		}
		for (x = curmsg + 1; x<maxmsg; x++) {
			fn = get_fn(dir, x);
			if (fn) {
				ast_filedelete(fn, NULL);
				free(fn);
			}
		}
	}
	if (dir)
		free(dir);
	if (cfg)
		ast_destroy(cfg);
	LOCAL_USER_REMOVE(u);
	return res;
}

static int vm_exec(struct ast_channel *chan, void *data)
{
	int res=0, silent=0;
	struct localuser *u;
	char *ext = (char *)data;
	
	if (!data) {
		ast_log(LOG_WARNING, "vm requires an argument (extension)\n");
		return -1;
	}
	LOCAL_USER_ADD(u);
	if (*ext == 's') {
		silent++;
		ext++;
	}
	res = leave_voicemail(chan, ext, silent);
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	int res;
	STANDARD_HANGUP_LOCALUSERS;
	res = ast_unregister_application(app);
	res |= ast_unregister_application(app2);
	return res;
}

int load_module(void)
{
	int res;
	res = ast_register_application(app, vm_exec);
	if (!res)
		res = ast_register_application(app2, vm_execmain);
	return res;
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
