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
#include <asterisk/utils.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <pthread.h>

#ifdef __linux__
#include <linux/zaptel.h>
#else
#include <zaptel.h>
#endif /* __linux__ */

static char *tdesc = "MeetMe conference bridge";

static char *app = "MeetMe";
static char *app2 = "MeetMeCount";
static char *app3 = "MeetMeAdmin";

static char *synopsis = "MeetMe conference bridge";
static char *synopsis2 = "MeetMe participant count";
static char *synopsis3 = "MeetMe conference Administration";

static char *descrip =
"  MeetMe([confno][,[options][,pin]]): Enters the user into a specified MeetMe conference.\n"
"If the conference number is omitted, the user will be prompted to enter\n"
"one. \n"
"MeetMe returns 0 if user pressed # to exit (see option 'p'), otherwise -1.\n"
"Please note: A ZAPTEL INTERFACE MUST BE INSTALLED FOR CONFERENCING TO WORK!\n\n"

"The option string may contain zero or more of the following characters:\n"
"      'm' -- set monitor only mode (Listen only, no talking)\n"
"      't' -- set talk only mode. (Talk only, no listening)\n"
"      'p' -- allow user to exit the conference by pressing '#'\n"
"      'd' -- dynamically add conference\n"
"      'D' -- dynamically add conference, prompting for a PIN\n"
"      'e' -- select an empty conference\n"
"      'E' -- select an empty pinless conference\n"
"      'v' -- video mode\n"
"      'q' -- quiet mode (don't play enter/leave sounds)\n"
"      'M' -- enable music on hold when the conference has a single caller\n"
"      'x' -- exit the conference if the last marked user left\n"
"      'b' -- run AGI script specified in ${MEETME_AGI_BACKGROUND}\n"
"         Default: conf-background.agi\n"
"        (Note: This does not work with non-Zap channels in the same conference)\n"
"      's' -- Present menu (user or admin) when '*' is received ('send' to menu)\n"
"      'a' -- set admin mode\n";

static char *descrip2 =
"  MeetMeCount(confno[|var]): Plays back the number of users in the specifiedi\n"
"MeetMe conference. If var is specified, playback will be skipped and the value\n"
"will be returned in the variable. Returns 0 on success or -1 on a hangup.\n"
"A ZAPTEL INTERFACE MUST BE INSTALLED FOR CONFERENCING FUNCTIONALITY.\n";

static char *descrip3 = 
"  MeetMeAdmin(confno,command[,user]): Run admin command for conference\n"
"      'K' -- Kick all users out of conference\n"
"      'k' -- Kick one user out of conference\n"
"      'L' -- Lock conference\n"
"      'l' -- Unlock conference\n"
"      'M' -- Mute conference\n"
"      'm' -- Unmute conference\n"
"";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static struct ast_conference {
	char confno[AST_MAX_EXTENSION];		/* Conference */
	struct ast_channel *chan;	/* Announcements channel */
	int fd;				/* Announcements fd */
	int zapconf;			/* Zaptel Conf # */
	int users;			/* Number of active users */
	int markedusers;		  /* Number of marked users */
	struct ast_conf_user *firstuser;  /* Pointer to the first user struct */
	struct ast_conf_user *lastuser;   /* Pointer to the last user struct */
	time_t start;			/* Start time (s) */
	int isdynamic;			/* Created on the fly? */
	int locked;			  /* Is the conference locked? */
	char pin[AST_MAX_EXTENSION];			/* If protected by a PIN */
	struct ast_conference *next;
} *confs;

struct ast_conf_user {
	int user_no;		     /* User Number */
	struct ast_conf_user *prevuser;  /* Pointer to the previous user */
	struct ast_conf_user *nextuser;  /* Pointer to the next user */
	int userflags;			 /* Flags as set in the conference */
	int adminflags;			 /* Flags set by the Admin */
	struct ast_channel *chan; 	 /* Connected channel */
	char usrvalue[50];		 /* Custom User Value */
	time_t jointime;		 /* Time the user joined the conference */
};

#define ADMINFLAG_MUTED (1 << 1)	/* User is muted */
#define ADMINFLAG_KICKME (1 << 2)	/* User is kicked */


AST_MUTEX_DEFINE_STATIC(conflock);

static int admin_exec(struct ast_channel *chan, void *data);

#include "enter.h"
#include "leave.h"

#define ENTER	0
#define LEAVE	1

#define CONF_SIZE 320

#define CONFFLAG_ADMIN	(1 << 1)	/* If set the user has admin access on the conference */
#define CONFFLAG_MONITOR (1 << 2)	/* If set the user can only receive audio from the conference */
#define CONFFLAG_POUNDEXIT (1 << 3)	/* If set asterisk will exit conference when '#' is pressed */
#define CONFFLAG_STARMENU (1 << 4)	/* If set asterisk will provide a menu to the user what '*' is pressed */
#define CONFFLAG_TALKER (1 << 5)	/* If set the use can only send audio to the conference */
#define CONFFLAG_QUIET (1 << 6)		/* If set there will be no enter or leave sounds */
#define CONFFLAG_VIDEO (1 << 7)		/* Set to enable video mode */
#define CONFFLAG_AGI (1 << 8)		/* Set to run AGI Script in Background */
#define CONFFLAG_MOH (1 << 9)		/* Set to have music on hold when user is alone in conference */
#define CONFFLAG_ADMINEXIT (1 << 10)    /* If set the MeetMe will return if all marked with this flag left */


static int careful_write(int fd, unsigned char *data, int len)
{
	int res;
	int x;
	while(len) {
		x = ZT_IOMUX_WRITE | ZT_IOMUX_SIGEVENT;
		res = ioctl(fd, ZT_IOMUX, &x);
		if (res >= 0)
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
			cnf->markedusers = -1;
			cnf->chan = ast_request("zap", AST_FORMAT_ULAW, "pseudo");
			if (cnf->chan) {
				cnf->fd = cnf->chan->fds[0];	/* for use by conf_play() */
			} else {
				ast_log(LOG_WARNING, "Unable to open pseudo channel - trying device\n");
				cnf->fd = open("/dev/zap/pseudo", O_RDWR);
				if (cnf->fd < 0) {
					ast_log(LOG_WARNING, "Unable to open pseudo device\n");
					free(cnf);
					cnf = NULL;
					goto cnfout;
				}
			}
			memset(&ztc, 0, sizeof(ztc));
			/* Setup a new zap conference */
			ztc.chan = 0;
			ztc.confno = -1;
			ztc.confmode = ZT_CONF_CONFANN;
			if (ioctl(cnf->fd, ZT_SETCONF, &ztc)) {
				ast_log(LOG_WARNING, "Error setting conference\n");
				if (cnf->chan)
					ast_hangup(cnf->chan);
				else
					close(cnf->fd);
				free(cnf);
				cnf = NULL;
				goto cnfout;
			}
			/* Fill the conference struct */
			cnf->start = time(NULL);
			cnf->zapconf = ztc.confno;
			cnf->isdynamic = dynamic;
			cnf->firstuser = NULL;
			cnf->lastuser = NULL;
			cnf->locked = 0;
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Created MeetMe conference %d for conference '%s'\n", cnf->zapconf, cnf->confno);
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
	ast_cli(fd, "Deprecated! Please use 'meetme' instead.\n");
	return RESULT_SUCCESS;
}

static char show_confs_usage[] =
"Deprecated! Please use 'meetme' instead.\n";

static struct ast_cli_entry cli_show_confs = {
	{ "show", "conferences", NULL }, confs_show,
	"Show status of conferences", show_confs_usage, NULL };
	
static int conf_cmd(int fd, int argc, char **argv) {
	/* Process the command */
	struct ast_conference *cnf;
	struct ast_conf_user *user;
	int hr, min, sec;
	int i = 0, total = 0;
	time_t now;
	char *header_format = "%-14s %-14s %-10s %-8s  %-8s\n";
	char *data_format = "%-12.12s   %4.4d	      %4.4s       %02d:%02d:%02d  %-8s\n";
	char cmdline[1024] = "";

	if (argc > 8)
		ast_cli(fd, "Invalid Arguments.\n");
	/* Check for length so no buffer will overflow... */
	for (i = 0; i < argc; i++) {
		if (strlen(argv[i]) > 100)
			ast_cli(fd, "Invalid Arguments.\n");
	}
	if (argc == 1) {
		/* 'MeetMe': List all the conferences */	
	now = time(NULL);
		cnf = confs;
		if (!cnf) {
		ast_cli(fd, "No active MeetMe conferences.\n");
		return RESULT_SUCCESS;
	}
	ast_cli(fd, header_format, "Conf Num", "Parties", "Marked", "Activity", "Creation");
		while(cnf) {
			if (cnf->markedusers < 0)
				strcpy(cmdline, "N/A ");
			else 
				snprintf(cmdline, sizeof(cmdline), "%4.4d", cnf->markedusers);
			hr = (now - cnf->start) / 3600;
			min = ((now - cnf->start) % 3600) / 60;
			sec = (now - cnf->start) % 60;

			ast_cli(fd, data_format, cnf->confno, cnf->users, cmdline, hr, min, sec, cnf->isdynamic ? "Dynamic" : "Static");

			total += cnf->users; 	
			cnf = cnf->next;
		}
		ast_cli(fd, "* Total number of MeetMe users: %d\n", total);
		return RESULT_SUCCESS;
	}
	if (argc < 3)
		return RESULT_SHOWUSAGE;
	strncpy(cmdline, argv[2], 100);	/* Argv 2: conference number */
	if (strstr(argv[1], "lock")) {	
		if (strcmp(argv[1], "lock") == 0) {
			/* Lock */
			strcat(cmdline, "|L");
		} else {
			/* Unlock */
			strcat(cmdline, "|l");
		}
	} else if (strstr(argv[1], "mute")) { 
		if (argc < 4)
			return RESULT_SHOWUSAGE;
		if (strcmp(argv[1], "mute") == 0) {
			/* Mute */
			strcat(cmdline, "|M|");	
			strcat(cmdline, argv[3]);
		} else {
			/* Unmute */
			strcat(cmdline, "|m|");
			strcat(cmdline, argv[3]);
		}
	} else if (strcmp(argv[1], "kick") == 0) {
		if (argc < 4)
			return RESULT_SHOWUSAGE;
		if (strcmp(argv[3], "all") == 0) {
			/* Kick all */
			strcat(cmdline, "|K");
		} else {
			/* Kick a single user */
			strcat(cmdline, "|k|");
			strcat(cmdline, argv[3]);
		}	
	} else if(strcmp(argv[1], "list") == 0) {
		/* List all the users in a conference */
		if (!confs) {
			ast_cli(fd, "No active conferences.\n");
			return RESULT_SUCCESS;	
		}
		cnf = confs;
		/* Find the right conference */
		while(cnf) {
			if (strcmp(cnf->confno, argv[2]) == 0)
				break;
			if (cnf->next) {
				cnf = cnf->next;	
			} else {
				ast_cli(fd, "No such conference: %s.\n",argv[2]);
				return RESULT_SUCCESS;
			}
		}
		/* Show all the users */
		user = cnf->firstuser;
		while(user) {
			ast_cli(fd, "User #: %i  Channel: %s %s %s\n", user->user_no, user->chan->name, (user->userflags & CONFFLAG_ADMIN) ? "(Admin)" : "", (user->userflags & CONFFLAG_MONITOR) ? "(Listen only)" : "" );
			user = user->nextuser;
		}
		return RESULT_SUCCESS;
	} else 
		return RESULT_SHOWUSAGE;
	ast_log(LOG_DEBUG, "Cmdline: %s\n", cmdline);
	admin_exec(NULL, cmdline);
	return 0;
}

static char *complete_confcmd(char *line, char *word, int pos, int state) {
	#define CONF_COMMANDS 6
	int which = 0, x = 0;
	struct ast_conference *cnf = NULL;
	struct ast_conf_user *usr = NULL;
	char *confno = NULL;
	char usrno[50] = "";
	char cmds[CONF_COMMANDS][20] = {"lock", "unlock", "mute", "unmute", "kick", "list"};
	char *myline;
	
	if (pos == 1) {
		/* Command */
		for (x = 0;x < CONF_COMMANDS; x++) {
			if (!strncasecmp(cmds[x], word, strlen(word))) {
				if (++which > state) {
					return strdup(cmds[x]);
				}
			}
		}
	} else if (pos == 2) {
		/* Conference Number */
		ast_mutex_lock(&conflock);
		cnf = confs;
		while(cnf) {
			if (!strncasecmp(word, cnf->confno, strlen(word))) {
				if (++which > state)
					break;
			}
			cnf = cnf->next;
		}
		ast_mutex_unlock(&conflock);
		return cnf ? strdup(cnf->confno) : NULL;
	} else if (pos == 3) {
		/* User Number || Conf Command option*/
		if (strstr(line, "mute") || strstr(line, "kick")) {
			if ((state == 0) && (strstr(line, "kick")) && !(strncasecmp(word, "all", strlen(word)))) {
				return strdup("all");
			}
			which++;
			ast_mutex_lock(&conflock);
			cnf = confs;

			/* TODO: Find the conf number from the cmdline (ignore spaces) <- test this and make it fail-safe! */
			myline = ast_strdupa(line);
			if (strsep(&myline, " ") && strsep(&myline, " ") && !confno) {
				while((confno = strsep(&myline, " ")) && (strcmp(confno, " ") == 0))
					;
			}
			
			while(cnf) {
				if (strcmp(confno, cnf->confno) == 0) {
					break;
				}
				cnf = cnf->next;
			}
			if (cnf) {
				/* Search for the user */
				usr = cnf->firstuser;
				while(usr) {
					sprintf(usrno, "%i", usr->user_no);
					if (!strncasecmp(word, usrno, strlen(word))) {
						if (++which > state)
							break;
					}
					usr = usr->nextuser;
				}
			}
			ast_mutex_unlock(&conflock);
			return usr ? strdup(usrno) : NULL;
		}
	}
	return NULL;
}
	
static char conf_usage[] =
"Usage: meetme  (un)lock|(un)mute|kick|list <confno> <usernumber>\n"
"       Executes a command for the conference or on a conferee\n";

static struct ast_cli_entry cli_conf = {
	{ "meetme", NULL, NULL }, conf_cmd,
	"Execute a command on a conference or conferee", conf_usage, complete_confcmd };

static int conf_run(struct ast_channel *chan, struct ast_conference *conf, int confflags)
{
	struct ast_conference *prev=NULL, *cur;
	struct ast_conf_user *user = malloc(sizeof(struct ast_conf_user));
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
	int menu_active = 0;
	int using_pseudo = 0;

	struct ast_app *app;
	char *agifile;
	char *agifiledefault = "conf-background.agi";
	char meetmesecs[30];

	ZT_BUFFERINFO bi;
	char __buf[CONF_SIZE + AST_FRIENDLY_OFFSET];
	char *buf = __buf + AST_FRIENDLY_OFFSET;
	
	user->user_no = 0; /* User number 0 means starting up user! (dead - not in the list!) */
	
	if (conf->locked) {
		/* Sorry, but this confernce is locked! */	
		if (!ast_streamfile(chan, "conf-locked", chan->language))
			ast_waitstream(chan, "");
		goto outrun;
	}
	
	conf->users++;
	if (confflags & CONFFLAG_ADMINEXIT) {
		if (conf->markedusers == -1) {
			conf->markedusers = 1;
		} else {
			conf->markedusers++;
		}
	}
      
   	ast_mutex_lock(&conflock);
	if (conf->firstuser == NULL) {
		/* Fill the first new User struct */
		user->user_no = 1;
		user->nextuser = NULL;
		user->prevuser = NULL;
		conf->firstuser = user;
		conf->lastuser = user;
	} else {
		/* Fill the new user struct */	
		user->user_no = conf->lastuser->user_no + 1; 
		user->prevuser = conf->lastuser;
		user->nextuser = NULL;
		if (conf->lastuser->nextuser != NULL) {
			ast_log(LOG_WARNING, "Error in User Management!\n");
		      	goto outrun;
		} else {
			conf->lastuser->nextuser = user;
			conf->lastuser = user;
		}
	}
	strncpy(user->usrvalue, "test", sizeof(user->usrvalue));
	user->chan = chan;
	user->userflags = confflags;
	user->adminflags = 0;
	ast_mutex_unlock(&conflock);
	
	if (!(confflags & CONFFLAG_QUIET) && conf->users == 1) {
		if (!ast_streamfile(chan, "conf-onlyperson", chan->language))
			ast_waitstream(chan, "");
	}

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
		using_pseudo = 1;
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
		bi.bufsize = CONF_SIZE/2;
		bi.txbufpolicy = ZT_POLICY_IMMEDIATE;
		bi.rxbufpolicy = ZT_POLICY_IMMEDIATE;
		bi.numbufs = 4;
		if (ioctl(fd, ZT_SET_BUFINFO, &bi)) {
			ast_log(LOG_WARNING, "Unable to set buffering information: %s\n", strerror(errno));
			close(fd);
			goto outrun;
		}
		x = 1;
		if (ioctl(fd, ZT_SETLINEAR, &x)) {
			ast_log(LOG_WARNING, "Unable to set linear mode: %s\n", strerror(errno));
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
	} else {
		if (!strcasecmp(chan->type,"Zap") && (confflags & CONFFLAG_STARMENU)) {
			/*  Set CONFMUTE mode on Zap channel to mute DTMF tones when the menu is enabled */
			x = 1;
			ast_channel_setoption(chan,AST_OPTION_TONE_VERIFY,&x,sizeof(char),0);
		}	
		for(;;) {
			outfd = -1;
			ms = -1;
			c = ast_waitfor_nandfds(&chan, 1, &fd, nfds, NULL, &outfd, &ms);
			
			/* Update the struct with the actual confflags */
			user->userflags = confflags;
			
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
			
			/* Leave if the last marked user left */
			if (conf->markedusers == 0) {
				ret = -1;
				break;
			}
	
			/* Check if the admin changed my modes */
			if (user->adminflags) {			
				/* Set the new modes */
				if ((user->adminflags & ADMINFLAG_MUTED) && (ztc.confmode & ZT_CONF_TALKER)) {
					ztc.confmode ^= ZT_CONF_TALKER;
					if (ioctl(fd, ZT_SETCONF, &ztc)) {
						ast_log(LOG_WARNING, "Error setting conference - Un/Mute \n");
						ret = -1;
						break;
					}
				}
				if (!(user->adminflags & ADMINFLAG_MUTED) && !(confflags & CONFFLAG_MONITOR) && !(ztc.confmode & ZT_CONF_TALKER)) {
					ztc.confmode |= ZT_CONF_TALKER;
					if (ioctl(fd, ZT_SETCONF, &ztc)) {
						ast_log(LOG_WARNING, "Error setting conference - Un/Mute \n");
						ret = -1;
						break;
					}
				}
				if (user->adminflags & ADMINFLAG_KICKME) {
					//You have been kicked.
					if (!ast_streamfile(chan, "conf-kicked", chan->language))
						ast_waitstream(chan, "");
					ret = 0;
					break;
				}
			} else if (!(confflags & CONFFLAG_MONITOR) && !(ztc.confmode & ZT_CONF_TALKER)) {
				ztc.confmode |= ZT_CONF_TALKER;
				if (ioctl(fd, ZT_SETCONF, &ztc)) {
					ast_log(LOG_WARNING, "Error setting conference - Un/Mute \n");
					ret = -1;
					break;
				}
			}

		if (c) {
			if (c->fds[0] != origfd) {
				if (using_pseudo) {
					/* Kill old pseudo */
					close(fd);
				}
				ast_log(LOG_DEBUG, "Ooh, something swapped out under us, starting over\n");
				retryzap = 0;
				using_pseudo = 0;
				goto zapretry;
			}
			f = ast_read(c);
			if (!f) 
				break;
			if ((f->frametype == AST_FRAME_DTMF) && (f->subclass == '#') && (confflags & CONFFLAG_POUNDEXIT)) {
				ret = 0;
				break;
				} else if (((f->frametype == AST_FRAME_DTMF) && (f->subclass == '*') && (confflags & CONFFLAG_STARMENU)) || ((f->frametype == AST_FRAME_DTMF) && menu_active)) {
						if (musiconhold) {
				   			ast_moh_stop(chan);
						}
					if ((confflags & CONFFLAG_ADMIN)) {
							/* Admin menu */
							if (!menu_active) {
								menu_active = 1;
								/* Record this sound! */
								if (!ast_streamfile(chan, "conf-adminmenu", chan->language))
									ast_waitstream(chan, "");
					} else {
								switch(f->subclass - 48) {
									case 1: /* Un/Mute */
										menu_active = 0;
			 							if (ztc.confmode & ZT_CONF_TALKER) {
			 						       		ztc.confmode = ZT_CONF_CONF | ZT_CONF_LISTENER;
			 						       		confflags |= CONFFLAG_MONITOR ^ CONFFLAG_TALKER;
										} else {
											ztc.confmode = ZT_CONF_CONF | ZT_CONF_TALKER | ZT_CONF_LISTENER;
											confflags ^= CONFFLAG_MONITOR | CONFFLAG_TALKER;
										}
										if (ioctl(fd, ZT_SETCONF, &ztc)) {
											ast_log(LOG_WARNING, "Error setting conference - Un/Mute \n");
											ret = -1;
											break;
										}
										if (ztc.confmode & ZT_CONF_TALKER) {
											if (!ast_streamfile(chan, "conf-unmuted", chan->language))
												ast_waitstream(chan, "");
										} else {
											if (!ast_streamfile(chan, "conf-muted", chan->language))
												ast_waitstream(chan, "");
										}
										break;
									case 2: /* Un/Lock the Conference */
										menu_active = 0;
										if (conf->locked) {
											conf->locked = 0;
											if (!ast_streamfile(chan, "conf-unlockednow", chan->language))
												ast_waitstream(chan, "");
										} else {
											conf->locked = 1;
											if (!ast_streamfile(chan, "conf-lockednow", chan->language))
												ast_waitstream(chan, "");
					}

										break;
									default:
										menu_active = 0;
										/* Play an error message! */
										if (!ast_streamfile(chan, "conf-errormenu", chan->language))
											ast_waitstream(chan, "");
										break;
								}
							}
						} else {
							/* User menu */
							if (!menu_active) {
								menu_active = 1;
								/* Record this sound! */
								if (!ast_streamfile(chan, "conf-usermenu", chan->language))
									ast_waitstream(chan, "");
							} else {
								switch(f->subclass - 48) {
									case 1: /* Un/Mute */
										menu_active = 0;
			 							if (ztc.confmode & ZT_CONF_TALKER) {
			 						       		ztc.confmode = ZT_CONF_CONF | ZT_CONF_LISTENER;
			 						       		confflags |= CONFFLAG_MONITOR ^ CONFFLAG_TALKER;
										} else if (!(user->adminflags & ADMINFLAG_MUTED)) {
											ztc.confmode = ZT_CONF_CONF | ZT_CONF_TALKER | ZT_CONF_LISTENER;
											confflags ^= CONFFLAG_MONITOR | CONFFLAG_TALKER;
										}
										if (ioctl(fd, ZT_SETCONF, &ztc)) {
											ast_log(LOG_WARNING, "Error setting conference - Un/Mute \n");
											ret = -1;
											break;
										}
										if (ztc.confmode & ZT_CONF_TALKER) {
											if (!ast_streamfile(chan, "conf-unmuted", chan->language))
												ast_waitstream(chan, "");
										} else {
											if (!ast_streamfile(chan, "conf-muted", chan->language))
												ast_waitstream(chan, "");
										}
										break;
									default:
										menu_active = 0;
										/* Play an error message! */
										if (!ast_streamfile(chan, "errormenu", chan->language))
											ast_waitstream(chan, "");
										break;
								}
							}
						}
						if (musiconhold) {
				   			ast_moh_start(chan, NULL);
						}
			} else if (using_pseudo) {
				if (f->frametype == AST_FRAME_VOICE) {
					if (f->subclass == AST_FORMAT_SLINEAR) {
						/* Carefully write */
						careful_write(fd, f->data, f->datalen);
					} else
						ast_log(LOG_WARNING, "Huh?  Got a non-linear (%d) frame in the conference\n", f->subclass);
				}
			}
			ast_frfree(f);
		} else if (outfd > -1) {
			res = read(outfd, buf, CONF_SIZE);
			if (res > 0) {
				memset(&fr, 0, sizeof(fr));
				fr.frametype = AST_FRAME_VOICE;
				fr.subclass = AST_FORMAT_SLINEAR;
				fr.datalen = res;
				fr.samples = res/2;
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
	}
	if (using_pseudo)
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
	if (user->user_no) { /* Only cleanup users who really joined! */
		manager_event(EVENT_FLAG_CALL, "MeetmeLeave", 
			"Channel: %s\r\n"
			"Uniqueid: %s\r\n"
			"Meetme: %s\r\n",
			chan->name, chan->uniqueid, conf->confno);
		prev = NULL;
		conf->users--;
		if (confflags & CONFFLAG_ADMINEXIT) 
			conf->markedusers--;
		cur = confs;
		if (!conf->users) {
			/* No more users -- close this one out */
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
			if (conf->chan)
				ast_hangup(conf->chan);
			else
				close(conf->fd);
			free(conf);
		} else {
			/* Remove the user struct */ 
			if (user == conf->firstuser) {
				if (user->nextuser) {
					/* There is another entry */
					user->nextuser->prevuser = NULL;
				} else {
					/* We are the only entry */
					conf->lastuser = NULL;
				}
				/* In either case */
				conf->firstuser = user->nextuser;
			} else if (user == conf->lastuser){
				if (user->prevuser)
					user->prevuser->nextuser = NULL;
				else
					ast_log(LOG_ERROR, "Bad bad bad!  We're the last, not the first, but nobody before us??\n");
				conf->lastuser = user->prevuser;
			} else {
				if (user->nextuser)
					user->nextuser->prevuser = user->prevuser;
				else
					ast_log(LOG_ERROR, "Bad! Bad! Bad! user->nextuser is NULL but we're not the end!\n");
				if (user->prevuser)
					user->prevuser->nextuser = user->nextuser;
				else
					ast_log(LOG_ERROR, "Bad! Bad! Bad! user->prevuser is NULL but we're not the beginning!\n");
			}
			/* Return the number of seconds the user was in the conf */
			sprintf(meetmesecs, "%i", (int) (user->jointime - time(NULL)));
			pbx_builtin_setvar_helper(chan, "MEETMESECS", meetmesecs);
		}
	}
	free(user);
	ast_mutex_unlock(&conflock);
	return ret;
}

static struct ast_conference *find_conf(struct ast_channel *chan, char *confno, int make, int dynamic, char *dynamic_pin)
{
	struct ast_config *cfg;
	struct ast_variable *var;
	struct ast_conference *cnf;

	/* Check first in the conference list */
	ast_mutex_lock(&conflock);
	cnf = confs;
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

/*--- count_exec: The MeetmeCount application */
static int count_exec(struct ast_channel *chan, void *data)
{
	struct localuser *u;
	int res = 0;
	struct ast_conference *conf;
	int count;
	char *confnum, *localdata;
	char val[80] = "0"; 

	if (!data || ast_strlen_zero(data)) {
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

	if (localdata && !ast_strlen_zero(localdata)){
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

/*--- conf_exec: The meetme() application */
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

	if (!data || ast_strlen_zero(data)) {
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
		if (ast_strlen_zero(confno)) {
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
		if (strchr(inflags, 'x'))
			confflags |= CONFFLAG_ADMINEXIT;
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
			if (ast_strlen_zero(confno)) {
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
		while (allowretry && (ast_strlen_zero(confno)) && (++retrycnt < 4)) {
			/* Prompt user for conference number */
			res = ast_app_getdata(chan, "conf-getconfno", confno, sizeof(confno) - 1, 0);
			if (res < 0) {
				/* Don't try to validate when we catch an error */
				strcpy(confno, "");
				allowretry = 0;
				break;
			}
		}
		if (!ast_strlen_zero(confno)) {
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
				if (!ast_strlen_zero(cnf->pin)) {
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

static struct ast_conf_user* find_user(struct ast_conference *conf, char *callerident) {
	struct ast_conf_user *user = NULL;
	char usrno[1024] = "";
	if (conf && callerident) {
		user = conf->firstuser;
		while(user) {
			sprintf(usrno, "%i", user->user_no);
			if (strcmp(usrno, callerident) == 0)
				return user;
			user = user->nextuser;
		}
	}
	return NULL;
}

/*--- admin_exec: The MeetMeadmin application */
/* MeetMeAdmin(confno, command, caller) */
static int admin_exec(struct ast_channel *chan, void *data) {
	char *params, *command = NULL, *caller = NULL, *conf = NULL;
	struct ast_conference *cnf;
	struct ast_conf_user *user = NULL;

	ast_mutex_lock(&conflock);
	/* The param has the conference number the user and the command to execute */
	if (data && !ast_strlen_zero(data)) {		
		params = ast_strdupa((char *) data);
		conf = strsep(&params, "|");
		command = strsep(&params, "|");
		caller = strsep(&params, "|");
		
		ast_mutex_lock(&conflock);
		cnf = confs;
		while (cnf) {
			if (strcmp(cnf->confno, conf) == 0) 
				break;
			cnf = cnf->next;
		}
		ast_mutex_unlock(&conflock);
		
		if (caller)
			user = find_user(cnf, caller);
		
		if (cnf) {
			switch((int) (*command)) {
				case 76: /* L: Lock */ 
					cnf->locked = 1;
					break;
				case 108: /* l: Unlock */ 
					cnf->locked = 0;
					break;
				case 75: /* K: kick all users*/
					user = cnf->firstuser;
					while(user) {
						user->adminflags |= ADMINFLAG_KICKME;
						if (user->nextuser) {
							user = user->nextuser;
						} else {
							break;
						}
					}
					break;
				case 77: /* M: Mute */ 
					if (user) {
						user->adminflags |= ADMINFLAG_MUTED;
					} else {
						ast_log(LOG_NOTICE, "Specified User not found!");
					}
					break;
				case 109: /* m: Unmute */ 
					if (user && (user->adminflags & ADMINFLAG_MUTED)) {
						user->adminflags ^= ADMINFLAG_MUTED;
					} else {
						ast_log(LOG_NOTICE, "Specified User not found or he muted himself!");
					}
					break;
				case 107: /* k: Kick user */ 
					if (user) {
						user->adminflags |= ADMINFLAG_KICKME;
					} else {
						ast_log(LOG_NOTICE, "Specified User not found!");
					}
					break;
			}
		} else {
			ast_log(LOG_NOTICE, "Conference Number not found\n");
		}
	}
	ast_mutex_unlock(&conflock);
	return 0;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	ast_cli_unregister(&cli_show_confs);
	ast_cli_unregister(&cli_conf);
	ast_unregister_application(app3);
	ast_unregister_application(app2);
	return ast_unregister_application(app);
}

int load_module(void)
{
	ast_cli_register(&cli_show_confs);
	ast_cli_register(&cli_conf);
	ast_register_application(app3, admin_exec, synopsis3, descrip3);
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
