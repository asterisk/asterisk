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
"  MeetMe(confno): Enters the user into a specified MeetMe conference.\n"
"If the conference number is omitted, the user will be prompted to enter\n"
"one.  This application always returns -1. A ZAPTEL INTERFACE MUST BE\n"
"INSTALLED FOR CONFERENCING FUNCTIONALITY.\n";

static char *descrip2 =
"  MeetMe2(confno): Plays back the number of users in the specified MeetMe\n"
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
				ast_verbose(VERBOSE_PREFIX_3 "Crated ZapTel conference %d for conference '%s'\n", cnf->zapconf, cnf->confno);
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

static void conf_run(struct ast_channel *chan, struct conf *conf)
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
	int retryzap=0;

	ZT_BUFFERINFO bi;
	char __buf[CONF_SIZE + AST_FRIENDLY_OFFSET];
	char *buf = __buf + AST_FRIENDLY_OFFSET;

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
zapretry:

	if (retryzap || strcasecmp(chan->type, "Zap")) {
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
	ztc.confmode = ZT_CONF_CONF | ZT_CONF_TALKER | ZT_CONF_LISTENER;
	if (ioctl(fd, ZT_SETCONF, &ztc)) {
		ast_log(LOG_WARNING, "Error setting conference\n");
		close(fd);
		goto outrun;
	}
	ast_log(LOG_DEBUG, "Placed channel %s in ZAP conf %d\n", chan->name, conf->zapconf);
	/* Run the conference enter tone... */
	conf_play(conf, ENTER);

	for(;;) {
		outfd = -1;
		ms = -1;
		c = ast_waitfor_nandfds(&chan, 1, &fd, nfds, NULL, &outfd, &ms);
		if (c) {
			f = ast_read(c);
			if (!f) 
				break;
			if (fd != chan->fds[0]) {
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
				fr.timelen = res / 8;
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
	if (chan->state != AST_STATE_UP)
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

	if (!data || !strlen(data)) {
		allowretry = 1;
		data = "";
	}
	LOCAL_USER_ADD(u);
	if (chan->state != AST_STATE_UP)
		ast_answer(chan);
retry:
	/* Parse out the stuff */
	strncpy(confno, data, sizeof(confno) - 1);

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
			/* Run the conference */
			conf_run(chan, cnf);
			res = -1;
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
