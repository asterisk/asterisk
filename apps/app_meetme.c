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
#include <asterisk/musiconhold.h>
#include <asterisk/manager.h>
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
"  MeetMe(confno[,[options][,pin]]): Enters the user into a specified MeetMe conference.\n"
"If the conference number is omitted, the user will be prompted to enter\n"
"one.  This application always returns -1.  A ZAPTEL INTERFACE MUST BE INSTALLED\n"
"FOR CONFERENCING TO WORK!\n\n"

"The option string may contain zero or more of the following characters:\n"
"      'm' -- set monitor only mode (Listen only, no talking\n"
"      't' -- set talk only mode. (Talk only, no listening)\n"
"      'p' -- allow user to exit the conference by pressing '#'\n"
"      'd' -- dynamically add conference\n"
"      'D' -- dynamically add conference, prompting for a PIN\n"
"      'e' -- select an empty conference\n"
"      'E' -- select an empty pinless conference\n"
"      'v' -- video mode\n"
"      'q' -- quiet mode (don't play enter/leave sounds)\n"
"      'M' -- enable music on hold when the conference has a single caller\n"
"      'b' -- run AGI script specified in ${MEETME_AGI_BACKGROUND}\n"
"	      Default: conf-background.agi\n"
"             (Note: This does not work with non-Zap channels in the same conference)\n"
"      Not implemented yet:\n"
"      's' -- send user to admin/user menu if '*' is received\n"
"      'a' -- set admin mode\n";

static char *descrip2 =
"  MeetMeCount(confno[|var]): Plays back the number of users in the specifiedi\n"
"MeetMe conference. If var is specified, playback will be skipped and the value\n"
"will be returned in the variable. Returns 0 on success or -1 on a hangup.\n"
"A ZAPTEL INTERFACE MUST BE INSTALLED FOR CONFERENCING FUNCTIONALITY.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static struct ast_conference {
	char confno[AST_MAX_EXTENSION];		/* Conference */
	int fd;				/* Announcements fd */
	int zapconf;			/* Zaptel Conf # */
	int users;			/* Number of active users */
	time_t start;			/* Start time (s) */
	int isdynamic;			/* Created on the fly? */
	char pin[AST_MAX_EXTENSION];			/* If protected by a PIN */
	struct ast_conference *next;
} *confs;

static ast_mutex_t conflock = AST_MUTEX_INITIALIZER;

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
#define CONFFLAG_VIDEO (1 << 7)		/* Set to enable video mode */
#define CONFFLAG_AGI (1 << 8)		/* Set to run AGI Script in Background */
#define CONFFLAG_MOH (1 << 9)		/* Set to have music on hold when */


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

static void conf_play(struct ast_conference *conf, int sound)
{
	unsigned char *data;
	int len;
	ast_mutex_lock(&conflock);
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
	ast_mutex_unlock(&conflock);
}

static struct ast_conference *build_conf(char *confno, char *pin, int make, int dynamic)
{
	struct ast_conference *cnf;
	struct zt_confinfo ztc;
	ast_mutex_lock(&conflock);
	cnf = confs;
	while(cnf) {
		if (!strcmp(confno, cnf->confno)) 
			break;
		cnf = cnf->next;
	}
	if (!cnf && (make || dynamic)) {
		cnf = malloc(sizeof(struct ast_conference));
		if (cnf) {
			/* Make a new one */
			memset(cnf, 0, sizeof(struct ast_conference));
			strncpy(cnf->confno, confno, sizeof(cnf->confno) - 1);
			strncpy(cnf->pin, pin, sizeof(cnf->pin) - 1);
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
			cnf->isdynamic = dynamic;
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Created ZapTel conference %d for conference '%s'\n", cnf->zapconf, cnf->confno);
			cnf->next = confs;
			confs = cnf;
		} else	
			ast_log(LOG_WARNING, "Out of memory\n");
	}
cnfout:
	ast_mutex_unlock(&conflock);
	return cnf;
}

static int confs_show(int fd, int argc, char **argv)
{
	struct ast_conference *conf;
	int hr, min, sec;
	time_t now;
	char *header_format = "%-14s %-14s %-8s  %-8s\n";
	char *data_format = "%-12.12s   %4.4d           %02d:%02d:%02d  %-8s\n";

	now = time(NULL);
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	conf = confs;
	if (!conf) {
		ast_cli(fd, "No active conferences.\n");
		return RESULT_SUCCESS;
	}
	ast_cli(fd, header_format, "Conf Num", "Parties", "Activity", "Creation");
	while(conf) {
		hr = (now - conf->start) / 3600;
		min = ((now - conf->start) % 3600) / 60;
		sec = (now - conf->start) % 60;

		if (conf->isdynamic)
			ast_cli(fd, data_format, conf->confno, conf->users, hr, min, sec, "Dynamic");
		else
			ast_cli(fd, data_format, conf->confno, conf->users, hr, min, sec, "Static");

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

static int conf_run(struct ast_channel *chan, struct ast_conference *conf, int confflags)
{
	struct ast_conference *prev=NULL, *cur;
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
	int musiconhold = 0;
	int firstpass = 0;
	int ret = -1;
	int x;

	struct ast_app *app;
	char *agifile;
	char *agifiledefault = "conf-background.agi";

	ZT_BUFFERINFO bi;
	char __buf[CONF_SIZE + AST_FRIENDLY_OFFSET];
	char *buf = __buf + AST_FRIENDLY_OFFSET;
	
	conf->users++;
	if (!(confflags & CONFFLAG_QUIET) && conf->users == 1) {
		if (!ast_streamfile(chan, "conf-onlyperson", chan->language))
			ast_waitstream(chan, "");
	}

	if (confflags & CONFFLAG_VIDEO) {	
		/* Set it into linear mode (write) */
		if (ast_set_write_format(chan, AST_FORMAT_SLINEAR) < 0) {
			ast_log(LOG_WARNING, "Unable to set '%s' to write linear mode\n", chan->name);
			goto outrun;
		}

		/* Set it into linear mode (read) */
		if (ast_set_read_format(chan, AST_FORMAT_SLINEAR) < 0) {
			ast_log(LOG_WARNING, "Unable to set '%s' to read linear mode\n", chan->name);
			goto outrun;
		}
	} else {
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
		if (confflags & CONFFLAG_VIDEO) {	
			x = 1;
			if (ioctl(fd, ZT_SETLINEAR, &x)) {
				ast_log(LOG_WARNING, "Unable to set linear mode: %s\n", strerror(errno));
				close(fd);
				goto outrun;
			}
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

	manager_event(EVENT_FLAG_CALL, "MeetmeJoin", 
			"Channel: %s\r\n"
			"Uniqueid: %s\r\n"
			"Meetme: %s\r\n",
			chan->name, chan->uniqueid, conf->confno);


	if (!firstpass && !(confflags & CONFFLAG_MONITOR) && !(confflags & CONFFLAG_ADMIN)) {
		firstpass = 1;
		if (!(confflags & CONFFLAG_QUIET))
			conf_play(conf, ENTER);
	}

	if (confflags & CONFFLAG_AGI) {

		/* Get name of AGI file to run from $(MEETME_AGI_BACKGROUND)
		  or use default filename of conf-background.agi */

		agifile = pbx_builtin_getvar_helper(chan,"MEETME_AGI_BACKGROUND");
		if (!agifile)
			agifile = agifiledefault;

		if (!strcasecmp(chan->type,"Zap")) {
			/*  Set CONFMUTE mode on Zap channel to mute DTMF tones */
			x = 1;
			ast_channel_setoption(chan,AST_OPTION_TONE_VERIFY,&x,sizeof(char),0);
		}
		/* Find a pointer to the agi app and execute the script */
		app = pbx_findapp("agi");
		if (app) {
			ret = pbx_exec(chan, app, agifile, 1);
		} else {
			ast_log(LOG_WARNING, "Could not find application (agi)\n");
			ret = -2;
		}
                if (!strcasecmp(chan->type,"Zap")) {
                        /*  Remove CONFMUTE mode on Zap channel */
			x = 0;
                        ast_channel_setoption(chan,AST_OPTION_TONE_VERIFY,&x,sizeof(char),0);
                }
	} else for(;;) {
		outfd = -1;
		ms = -1;
		c = ast_waitfor_nandfds(&chan, 1, &fd, nfds, NULL, &outfd, &ms);
		/* trying to add moh for single person conf */
		if (confflags & CONFFLAG_MOH) {
			if (conf->users == 1) {
				if (musiconhold == 0) {
					ast_moh_start(chan, NULL);
					musiconhold = 1;
				} 
			} else {
				if (musiconhold) {
					ast_moh_stop(chan);
					musiconhold = 0;
				}
			}
		}
		/* end modifications */

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

	ast_mutex_lock(&conflock);
	/* Clean up */
	conf->users--;

	ast_log(LOG_DEBUG, "Removed channel %s from ZAP conf %d\n", chan->name, conf->zapconf);

	manager_event(EVENT_FLAG_CALL, "MeetmeLeave", 
			"Channel: %s\r\n"
			"Uniqueid: %s\r\n"
			"Meetme: %s\r\n",
			chan->name, chan->uniqueid, conf->confno);


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
	ast_mutex_unlock(&conflock);
	return ret;
}

static struct ast_conference *find_conf(struct ast_channel *chan, char *confno, int make, int dynamic, char *dynamic_pin)
{
	struct ast_config *cfg;
	struct ast_variable *var;
	struct ast_conference *cnf = confs;

	/* Check first in the conference list */
	ast_mutex_lock(&conflock);
	while (cnf) {
		if (!strcmp(confno, cnf->confno)) 
			break;
		cnf = cnf->next;
	}
	ast_mutex_unlock(&conflock);

	if (!cnf) {
		if (dynamic) {
			/* No need to parse meetme.conf */
			ast_log(LOG_DEBUG, "Building dynamic conference '%s'\n", confno);
			if (dynamic_pin) {
				if (dynamic_pin[0] == 'q') {
					/* Query the user to enter a PIN */
					ast_app_getdata(chan, "conf-getpin", dynamic_pin, AST_MAX_EXTENSION - 1, 0);
				}
				cnf = build_conf(confno, dynamic_pin, make, dynamic);
			} else {
				cnf = build_conf(confno, "", make, dynamic);
			}
		} else {
			/* Check the config */
			cfg = ast_load("meetme.conf");
			if (!cfg) {
				ast_log(LOG_WARNING, "No meetme.conf file :(\n");
				return NULL;
			}
			var = ast_variable_browse(cfg, "rooms");
			while(var) {
				if (!strcasecmp(var->name, "conf")) {
					/* Separate the PIN */
					char *pin, *conf;

					if ((pin = ast_strdupa(var->value))) {
						conf = strsep(&pin, "|,");
						if (!strcasecmp(conf, confno)) {
							/* Bingo it's a valid conference */
							if (pin)
								cnf = build_conf(confno, pin, make, dynamic);
							else
								cnf = build_conf(confno, "", make, dynamic);
							break;
						}
					}
				}
				var = var->next;
			}
			if (!var) {
				ast_log(LOG_DEBUG, "%s isn't a valid conference\n", confno);
			}
			ast_destroy(cfg);
		}
	}
	return cnf;
}

static int count_exec(struct ast_channel *chan, void *data)
{
	struct localuser *u;
	int res = 0;
	struct ast_conference *conf;
	int count;
	char *confnum, *localdata;
	char val[80] = "0"; 

	if (!data || !strlen(data)) {
		ast_log(LOG_WARNING, "MeetMeCount requires an argument (conference number)\n");
		return -1;
	}
	localdata = ast_strdupa(data);
	LOCAL_USER_ADD(u);
	confnum = strsep(&localdata,"|");       
	conf = find_conf(chan, confnum, 0, 0, NULL);
	if (conf)
		count = conf->users;
	else
		count = 0;

	if (localdata && strlen(localdata)){
		/* have var so load it and exit */
		snprintf(val,sizeof(val), "%i",count);
		pbx_builtin_setvar_helper(chan, localdata,val);
	} else {
		if (chan->_state != AST_STATE_UP)
			ast_answer(chan);
		res = ast_say_number(chan, count, "", chan->language, (char *) NULL); /* Needs gender */
	}
	LOCAL_USER_REMOVE(u);
	return res;
}

static int conf_exec(struct ast_channel *chan, void *data)
{
	int res=-1;
	struct localuser *u;
	char confno[AST_MAX_EXTENSION] = "";
	int allowretry = 0;
	int retrycnt = 0;
	struct ast_conference *cnf;
	int confflags = 0;
	int dynamic = 0;
	int empty = 0, empty_no_pin = 0;
	char *notdata, *info, *inflags = NULL, *inpin = NULL, the_pin[AST_MAX_EXTENSION] = "";

	if (!data || !strlen(data)) {
		allowretry = 1;
		notdata = "";
	} else {
		notdata = data;
	}
	LOCAL_USER_ADD(u);
	if (chan->_state != AST_STATE_UP)
		ast_answer(chan);

	info = ast_strdupa((char *)notdata);

	if (info) {
		char *tmp = strsep(&info, "|");
		strncpy(confno, tmp, sizeof(confno));
		if (strlen(confno) == 0) {
			allowretry = 1;
		}
	}
	if (info)
		inflags = strsep(&info, "|");
	if (info)
		inpin = strsep(&info, "|");
	if (inpin)
		strncpy(the_pin, inpin, sizeof(the_pin) - 1);

	if (inflags) {
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
		if (strchr(inflags, 'M'))
			confflags |= CONFFLAG_MOH;
		if (strchr(inflags, 'b'))
			confflags |= CONFFLAG_AGI;
		if (strchr(inflags, 'd'))
			dynamic = 1;
		if (strchr(inflags, 'D')) {
			dynamic = 1;
			if (! inpin) {
				strncpy(the_pin, "q", sizeof(the_pin) - 1);
			}
		}
		if (strchr(inflags, 'e'))
			empty = 1;
		if (strchr(inflags, 'E')) {
			empty = 1;
			empty_no_pin = 1;
		}
	}

	do {
		if (retrycnt > 3)
			allowretry = 0;
		if (empty) {
			int i, map[1024];
			struct ast_config *cfg;
			struct ast_variable *var;
			int confno_int;

			memset(map, 0, sizeof(map));

			ast_mutex_lock(&conflock);
			cnf = confs;
			while (cnf) {
				if (sscanf(cnf->confno, "%d", &confno_int) == 1) {
					/* Disqualify in use conference */
					if (confno_int >= 0 && confno_int < 1024)
						map[confno_int]++;
				}
				cnf = cnf->next;
			}
			ast_mutex_unlock(&conflock);

			/* Disqualify static conferences with pins */
			cfg = ast_load("meetme.conf");
			if (cfg) {
				var = ast_variable_browse(cfg, "rooms");
				while(var) {
					if (!strcasecmp(var->name, "conf")) {
						char *stringp = ast_strdupa(var->value);
						if (stringp) {
							char *confno_tmp = strsep(&stringp, "|,");
							int found = 0;
							if (sscanf(confno_tmp, "%d", &confno_int) == 1) {
								if (confno_int >= 0 && confno_int < 1024) {
									if (stringp && empty_no_pin) {
										map[confno_int]++;
									}
								}
							}
							if (! dynamic) {
								/* For static:  run through the list and see if this conference is empty */
								ast_mutex_lock(&conflock);
								cnf = confs;
								while (cnf) {
									if (!strcmp(confno_tmp, cnf->confno)) {
										found = 1;
										break;
									}
								}
								ast_mutex_unlock(&conflock);
								if (!found) {
									if ((empty_no_pin && (!stringp)) || (!empty_no_pin)) {
										strncpy(confno, confno_tmp, sizeof(confno) - 1);
										break;
									}
								}
							}
						}
					}
					var = var->next;
				}
				ast_destroy(cfg);
			}

			/* Select first conference number not in use */
			if (dynamic) {
				for (i=0;i<1024;i++) {
					if (dynamic && (!map[i])) {
						snprintf(confno, sizeof(confno) - 1, "%d", i);
						break;
					}
				}
			}

			/* Not found? */
			if (!strlen(confno)) {
				res = ast_streamfile(chan, "conf-noempty", chan->language);
				if (!res)
					ast_waitstream(chan, "");
			} else {
				if (sscanf(confno, "%d", &confno_int) == 1) {
					res = ast_streamfile(chan, "conf-enteringno", chan->language);
					if (!res) {
						ast_waitstream(chan, "");
						res = ast_say_digits(chan, confno_int, "", chan->language);
					}
				}
			}
		}
		while (allowretry && (!strlen(confno)) && (++retrycnt < 4)) {
			/* Prompt user for conference number */
			res = ast_app_getdata(chan, "conf-getconfno", confno, sizeof(confno) - 1, 0);
			if (res < 0) {
				/* Don't try to validate when we catch an error */
				strcpy(confno, "");
				allowretry = 0;
				break;
			}
		}
		if (strlen(confno)) {
			/* Check the validity of the conference */
			cnf = find_conf(chan, confno, 1, dynamic, the_pin);
			if (!cnf) {
				res = ast_streamfile(chan, "conf-invalid", chan->language);
				if (!res)
					ast_waitstream(chan, "");
				res = -1;
				if (allowretry)
					strcpy(confno, "");
			} else {
				if (strlen(cnf->pin)) {
					char pin[AST_MAX_EXTENSION];

					if (*the_pin) {
						strncpy(pin, the_pin, sizeof(pin) - 1);
						res = 0;
					} else {
						/* Prompt user for pin if pin is required */
						res = ast_app_getdata(chan, "conf-getpin", pin, sizeof(pin) - 1, 0);
					}
					if (res >= 0) {
						if (!strcasecmp(pin, cnf->pin)) {
							/* Pin correct */
							allowretry = 0;
							/* Run the conference */
							res = conf_run(chan, cnf, confflags);
						} else {
							/* Pin invalid */
							res = ast_streamfile(chan, "conf-invalidpin", chan->language);
							if (!res)
								ast_waitstream(chan, "");
							res = -1;
							if (allowretry)
								strcpy(confno, "");
						}
					} else {
						res = -1;
						allowretry = 0;
					}
				} else {
					/* No pin required */
					allowretry = 0;

					/* Run the conference */
					res = conf_run(chan, cnf, confflags);
				}
			}
		}
	} while (allowretry);
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
