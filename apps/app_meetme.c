/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Meet me conference bridge
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/config.h>
#include <asterisk/app.h>
#include <asterisk/options.h>
#include <asterisk/cli.h>
#include <asterisk/say.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#include <pthread.h>
#include <linux/zaptel.h>

static char *tdesc = "Simple MeetMe conference bridge";

static char *app = "MeetMe";
static char *app2 = "MeetMeCount";

static char *synopsis = "Simple MeetMe conference bridge";
static char *synopsis2 = "MeetMe participant count";

static char *descrip =
"  MeetMe(confno[|options]): Enters the user into a specified MeetMe conference.\n"
"If the conference number is omitted, the user will be prompted to enter\n"
"one.  This application always returns -1. A ZAPTEL INTERFACE MUST BE\n"
"INSTALLED FOR CONFERENCING FUNCTIONALITY.\n"
"The option string may contain zero or more of the following characters:\n"
"      'a' -- set admin mode\n"
"      'm' -- set monitor only mode\n"
"      'p' -- allow user to exit the conference by pressing '#'\n"
"      's' -- send user to admin/user menu if '*' is received\n"
"      't' -- set talk only mode\n"
"      'q' -- quiet mode (don't play enter/leave sounds)\n";

static char *descrip2 =
"  MeetMeCount(confno): Plays back the number of users in the specified MeetMe\n"
"conference.  Returns 0 on success or -1 on a hangup.  A ZAPTEL INTERFACE\n"
"MUST BE INSTALLED FOR CONFERENCING FUNCTIONALITY.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static struct conf {
	char confno[80];		/* Conference */
	int fd;				/* Announcements fd */
	int zapconf;			/* Zaptel Conf # */
	int users;			/* Number of active users */
	time_t start;			/* Start time (s) */
	struct conf *next;
} *confs;

static pthread_mutex_t conflock = AST_MUTEX_INITIALIZER;

#include "enter.h"
#include "leave.h"

#define ENTER	0
#define LEAVE	1

#define CONF_SIZE 160

#define CONFFLAG_ADMIN	(1 << 1)	/* If set the user has admin access on the conference */
#define CONFFLAG_MONITOR (1 << 2)	/* If set the user can only receive audio from the conference */
#define CONFFLAG_POUNDEXIT (1 << 3)	/* If set asterisk will exit conference when '#' is pressed */
#define CONFFLAG_STARMENU (1 << 4)	/* If set asterisk will provide a menu to the user what '*' is pressed */
#define CONFFLAG_TALKER (1 << 5)	/* If set the use can only send audio to the conference */
#define CONFFLAG_QUIET (1 << 6)		/* If set there will be no enter or leave sounds */

static int careful_write(int fd, unsigned char *data, int len)
{
	int res;
	while(len) {
		res = write(fd, data, len);
		if (res < 1) {
			if (errno != EAGAIN) {
				ast_log(LOG_WARNING, "Failed to write audio data to conference: %s\n", strerror(errno));
				return -1;
			} else
				return 0;
		}
		len -= res;
		data += res;
	}
	return 0;
}

static void conf_play(struct conf *conf, int sound)
{
	unsigned char *data;
	int len;
	ast_pthread_mutex_lock(&conflock);
	switch(sound) {
	case ENTER:
		data = enter;
		len = sizeof(enter);
		break;
	case LEAVE:
		data = leave;
		len = sizeof(leave);
		break;
	default:
		data = NULL;
		len = 0;
	}
	if (data) 
		careful_write(conf->fd, data, len);
	pthread_mutex_unlock(&conflock);
}

static struct conf *build_conf(char *confno, int make)
{
	struct conf *cnf;
	struct zt_confinfo ztc;
	ast_pthread_mutex_lock(&conflock);
	cnf = confs;
	while(cnf) {
		if (!strcmp(confno, cnf->confno)) 
			break;
		cnf = cnf->next;
	}
	if (!cnf && make) {
		cnf = malloc(sizeof(struct conf));
		if (cnf) {
			/* Make a new one */
			memset(cnf, 0, sizeof(struct conf));
			strncpy(cnf->confno, confno, sizeof(cnf->confno) - 1);
			cnf->fd = open("/dev/zap/pseudo", O_RDWR);
			if (cnf->fd < 0) {
				ast_log(LOG_WARNING, "Unable to open pseudo channel\n");
				free(cnf);
				cnf = NULL;
				goto cnfout;
			}
			memset(&ztc, 0, sizeof(ztc));
			/* Setup a new zap conference */
			ztc.chan = 0;	
			ztc.confno = -1;
			ztc.confmode = ZT_CONF_CONF | ZT_CONF_TALKER | ZT_CONF_LISTENER;
			if (ioctl(cnf->fd, ZT_SETCONF, &ztc)) {
				ast_log(LOG_WARNING, "Error setting conference\n");
				close(cnf->fd);
				free(cnf);
				cnf = NULL;
				goto cnfout;
			}
			cnf->start = time(NULL);
			cnf->zapconf = ztc.confno;
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Created ZapTel conference %d for conference '%s'\n", cnf->zapconf, cnf->confno);
			cnf->next = confs;
			confs = cnf;
		} else	
			ast_log(LOG_WARNING, "Out of memory\n");
	}
cnfout:
	if (cnf && make) 
		cnf->users++;
	ast_pthread_mutex_unlock(&conflock);
	return cnf;
}

static int confs_show(int fd, int argc, char **argv)
{
	struct conf *conf;
	int hr, min, sec;
	time_t now;

	now = time(NULL);
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	conf = confs;
	if (!conf) {	
		ast_cli(fd, "No active conferences.\n");
		return RESULT_SUCCESS;
	}
	ast_cli(fd, "Conf Num    Parties          Activity\n");
	while(conf) {
		hr = (now - conf->start) / 3600;
		min = ((now - conf->start) % 3600) / 60;
		sec = (now - conf->start) % 60;

		ast_cli(fd, "%-12.12s   %4.4d          %02d:%02d:%02d\n", 
			conf->confno, conf->users, hr, min, sec);
		conf = conf->next;
	}
	return RESULT_SUCCESS;
}

static char show_confs_usage[] = 
"Usage: show conferences\n"
"       Provides summary information on conferences with active\n"
"       participation.\n";

static struct ast_cli_entry cli_show_confs = {
	{ "show", "conferences", NULL }, confs_show, 
	"Show status of conferences", show_confs_usage, NULL };

static int conf_run(struct ast_channel *chan, struct conf *conf, int confflags)
{
	struct conf *prev=NULL, *cur;
	int fd;
	struct zt_confinfo ztc;
	struct ast_frame *f;
	struct ast_channel *c;
	struct ast_frame fr;
	int outfd;
	int ms;
	int nfds;
	int res;
	int flags;
	int retryzap;
	int origfd;
	int firstpass = 0;
	int ret = -1;

	ZT_BUFFERINFO bi;
	char __buf[CONF_SIZE + AST_FRIENDLY_OFFSET];
	char *buf = __buf + AST_FRIENDLY_OFFSET;

	if (!(confflags & CONFFLAG_QUIET) && conf->users == 1) {
		if (!ast_streamfile(chan, "conf-onlyperson", chan->language))
			ast_waitstream(chan, "");
	}
	
	/* Set it into U-law mode (write) */
	if (ast_set_write_format(chan, AST_FORMAT_ULAW) < 0) {
		ast_log(LOG_WARNING, "Unable to set '%s' to write ulaw mode\n", chan->name);
		goto outrun;
	}

	/* Set it into U-law mode (read) */
	if (ast_set_read_format(chan, AST_FORMAT_ULAW) < 0) {
		ast_log(LOG_WARNING, "Unable to set '%s' to read ulaw mode\n", chan->name);
		goto outrun;
	}
	ast_indicate(chan, -1);
	retryzap = strcasecmp(chan->type, "Zap");
zapretry:
	origfd = chan->fds[0];
	if (retryzap) {
		fd = open("/dev/zap/pseudo", O_RDWR);
		if (fd < 0) {
			ast_log(LOG_WARNING, "Unable to open pseudo channel: %s\n", strerror(errno));
			goto outrun;
		}
		/* Make non-blocking */
		flags = fcntl(fd, F_GETFL);
		if (flags < 0) {
			ast_log(LOG_WARNING, "Unable to get flags: %s\n", strerror(errno));
			close(fd);
			goto outrun;
		}
		if (fcntl(fd, F_SETFL, flags | O_NONBLOCK)) {
			ast_log(LOG_WARNING, "Unable to set flags: %s\n", strerror(errno));
			close(fd);
			goto outrun;
		}
		/* Setup buffering information */
		memset(&bi, 0, sizeof(bi));
		bi.bufsize = CONF_SIZE;
		bi.txbufpolicy = ZT_POLICY_IMMEDIATE;
		bi.rxbufpolicy = ZT_POLICY_IMMEDIATE;
		bi.numbufs = 4;
		if (ioctl(fd, ZT_SET_BUFINFO, &bi)) {
			ast_log(LOG_WARNING, "Unable to set buffering information: %s\n", strerror(errno));
			close(fd);
			goto outrun;
		}
		nfds = 1;
	} else {
		/* XXX Make sure we're not running on a pseudo channel XXX */
		fd = chan->fds[0];
		nfds = 0;
	}
	memset(&ztc, 0, sizeof(ztc));
	/* Check to see if we're in a conference... */
	ztc.chan = 0;	
	if (ioctl(fd, ZT_GETCONF, &ztc)) {
		ast_log(LOG_WARNING, "Error getting conference\n");
		close(fd);
		goto outrun;
	}
	if (ztc.confmode) {
		/* Whoa, already in a conference...  Retry... */
		if (!retryzap) {
			ast_log(LOG_DEBUG, "Zap channel is in a conference already, retrying with pseudo\n");
			retryzap = 1;
			goto zapretry;
		}
	}
	memset(&ztc, 0, sizeof(ztc));
	/* Add us to the conference */
	ztc.chan = 0;	
	ztc.confno = conf->zapconf;
	if (confflags & CONFFLAG_MONITOR)
		ztc.confmode = ZT_CONF_CONFMON | ZT_CONF_LISTENER;
	else if (confflags & CONFFLAG_TALKER)
		ztc.confmode = ZT_CONF_CONF | ZT_CONF_TALKER;
	else 
		ztc.confmode = ZT_CONF_CONF | ZT_CONF_TALKER | ZT_CONF_LISTENER;

	if (ioctl(fd, ZT_SETCONF, &ztc)) {
		ast_log(LOG_WARNING, "Error setting conference\n");
		close(fd);
		goto outrun;
	}
	ast_log(LOG_DEBUG, "Placed channel %s in ZAP conf %d\n", chan->name, conf->zapconf);
	if (!firstpass && !(confflags & CONFFLAG_MONITOR) && !(confflags & CONFFLAG_ADMIN)) {
		firstpass = 1;
		if (!(confflags & CONFFLAG_QUIET))
			conf_play(conf, ENTER);
	}

	for(;;) {
		outfd = -1;
		ms = -1;
		c = ast_waitfor_nandfds(&chan, 1, &fd, nfds, NULL, &outfd, &ms);
		if (c) {
			if (c->fds[0] != origfd) {
				if (retryzap) {
					/* Kill old pseudo */
					close(fd);
				}
				ast_log(LOG_DEBUG, "Ooh, something swapped out under us, starting over\n");
				retryzap = 0;
				goto zapretry;
			}
			f = ast_read(c);
			if (!f) 
				break;
			if ((f->frametype == AST_FRAME_DTMF) && (f->subclass == '#') && (confflags & CONFFLAG_POUNDEXIT)) {
				ret = 0;
				break;
			} else if ((f->frametype == AST_FRAME_DTMF) && (f->subclass == '*') && (confflags & CONFFLAG_STARMENU)) {
					if ((confflags & CONFFLAG_ADMIN)) {
					/* Do admin stuff here */
					} else {
					/* Do user menu here */
					}

			} else if (fd != chan->fds[0]) {
				if (f->frametype == AST_FRAME_VOICE) {
					if (f->subclass == AST_FORMAT_ULAW) {
						/* Carefully write */
						careful_write(fd, f->data, f->datalen);
					} else
						ast_log(LOG_WARNING, "Huh?  Got a non-ulaw (%d) frame in the conference\n", f->subclass);
				}
			}
			ast_frfree(f);
		} else if (outfd > -1) {
			res = read(outfd, buf, CONF_SIZE);
			if (res > 0) {
				memset(&fr, 0, sizeof(fr));
				fr.frametype = AST_FRAME_VOICE;
				fr.subclass = AST_FORMAT_ULAW;
				fr.datalen = res;
				fr.samples = res;
				fr.data = buf;
				fr.offset = AST_FRIENDLY_OFFSET;
				if (ast_write(chan, &fr) < 0) {
					ast_log(LOG_WARNING, "Unable to write frame to channel: %s\n", strerror(errno));
					/* break; */
				}
			} else 
				ast_log(LOG_WARNING, "Failed to read frame: %s\n", strerror(errno));
		}
	}
	if (fd != chan->fds[0])
		close(fd);
	else {
		/* Take out of conference */
		/* Add us to the conference */
		ztc.chan = 0;	
		ztc.confno = 0;
		ztc.confmode = 0;
		if (ioctl(fd, ZT_SETCONF, &ztc)) {
			ast_log(LOG_WARNING, "Error setting conference\n");
		}
	}

	if (!(confflags & CONFFLAG_QUIET) && !(confflags & CONFFLAG_MONITOR) && !(confflags & CONFFLAG_ADMIN))
		conf_play(conf, LEAVE);

outrun:

	ast_pthread_mutex_lock(&conflock);
	/* Clean up */
	conf->users--;
	if (!conf->users) {
		/* No more users -- close this one out */
		cur = confs;
		while(cur) {
			if (cur == conf) {
				if (prev)
					prev->next = conf->next;
				else
					confs = conf->next;
				break;
			}
			prev = cur;
			cur = cur->next;
		}
		if (!cur) 
			ast_log(LOG_WARNING, "Conference not found\n");
		close(conf->fd);
		free(conf);
	}
	pthread_mutex_unlock(&conflock);
	return ret;
}

static struct conf *find_conf(char *confno, int make)
{
	struct ast_config *cfg;
	struct ast_variable *var;
	struct conf *cnf = NULL;
	cfg = ast_load("meetme.conf");
	if (!cfg) {
		ast_log(LOG_WARNING, "No meetme.conf file :(\n");
		return NULL;
	}
	var = ast_variable_browse(cfg, "rooms");
	while(var) {
		if (!strcasecmp(var->name, "conf") &&
		    !strcasecmp(var->value, confno)) {
			/* Bingo it's a valid conference */
			cnf = build_conf(confno, make);
			break;
		}
		var = var->next;
	}
	if (!var) {
		ast_log(LOG_DEBUG, "%s isn't a valid conference\n", confno);
	}
	ast_destroy(cfg);
	return cnf;
}

static int count_exec(struct ast_channel *chan, void *data)
{
	struct localuser *u;
	int res = 0;
	struct conf *conf;
	int cnt;
	if (!data || !strlen(data)) {
		ast_log(LOG_WARNING, "MeetMeCount requires an argument (conference number)\n");
		return -1;
	}
	LOCAL_USER_ADD(u);
	conf = find_conf(data, 0);
	if (conf)
		cnt = conf->users;
	else
		cnt = 0;
	if (chan->_state != AST_STATE_UP)
		ast_answer(chan);
	res = ast_say_number(chan, cnt, "", chan->language);
	LOCAL_USER_REMOVE(u);
	return res;
}

static int conf_exec(struct ast_channel *chan, void *data)
{
	int res=-1;
	struct localuser *u;
	char confno[80] = "";
	int allowretry = 0;
	int retrycnt = 0;
	struct conf *cnf;
	int confflags = 0;
	char info[256], *ptr, *inflags, *inpin;

	if (!data || !strlen(data)) {
		allowretry = 1;
		data = "";
	}
	LOCAL_USER_ADD(u);
	if (chan->_state != AST_STATE_UP)
		ast_answer(chan);

	strncpy(info, (char *)data, sizeof(info) - 1);
	ptr = info;

	if (info) {
		inflags = strchr(info, '|');
		if (inflags) {
			*inflags = '\0';
			inflags++;
			if (strchr(inflags, 'a'))
				confflags |= CONFFLAG_ADMIN;
			if (strchr(inflags, 'm'))
				confflags |= CONFFLAG_MONITOR;
			if (strchr(inflags, 'p'))
				confflags |= CONFFLAG_POUNDEXIT;
			if (strchr(inflags, 's'))
				confflags |= CONFFLAG_STARMENU;
			if (strchr(inflags, 't'))
				confflags |= CONFFLAG_TALKER;
			if (strchr(inflags, 'q'))
				confflags |= CONFFLAG_QUIET;

			inpin = strchr(inflags, '|');
			if (inpin) {
				*inpin = '\0';
				inpin++;
				/* XXX Need to do something with pin XXX */
				ast_log(LOG_WARNING, "MEETME WITH PIN=(%s)\n", inpin);
			}
		}
	}

	/* Parse out the stuff */
	strncpy(confno, info, sizeof(confno) - 1);
retry:
	while(!strlen(confno) && (++retrycnt < 4)) {
		/* Prompt user for conference number */
		res = ast_app_getdata(chan, "conf-getconfno",confno, sizeof(confno) - 1, 0);
		if (res <0) goto out;
	}
	if (strlen(confno)) {
		/* Check the validity of the conference */
		cnf = find_conf(confno, 1);
		if (!cnf) {
			res = ast_streamfile(chan, "conf-invalid", chan->language);
			if (res < 0)
				goto out;
			if (!res)
				res = ast_waitstream(chan, "");
			res = -1;
			if (allowretry) {
				strcpy(confno, "");
				goto retry;
			}
		} else {
			/* XXX Should prompt user for pin if pin is required XXX */
			/* Run the conference */
			res = conf_run(chan, cnf, confflags);
		}
	}
out:
	/* Do the conference */
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	ast_cli_unregister(&cli_show_confs);
	ast_unregister_application(app2);
	return ast_unregister_application(app);
}

int load_module(void)
{
	ast_cli_register(&cli_show_confs);
	ast_register_application(app2, count_exec, synopsis2, descrip2);
	return ast_register_application(app, conf_exec, synopsis, descrip);
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
