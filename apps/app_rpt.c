/** @file app_rpt.c 
 *
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Radio Repeater / Remote Base program 
 *  version 0.7 6/25/04
 * 
 * Copyright (C) 2002-2004, Jim Dixon, WB6NIL
 *
 * Jim Dixon, WB6NIL <jim@lambdatel.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 *
 * Repeater / Remote Functions:
 * "Simple" Mode:  * - autopatch access, # - autopatch hangup
 * Normal mode:
 *  *0 - autopatch off
 *  *1XXX - remote link off
 *  *2XXX - remote link monitor
 *  *3XXX - remote link tranceive
 *  *4XXX - remote link command mode
 *  *6 - autopatch access/send (*)
 *  *7 - system status
 *  *8 - force ID
 *  *90 - system disable (and reset)
 *  *91 - system enable
 *  *99 - system reset
 *
 *  To send an asterisk (*) while dialing or talking on phone,
 *  use the autopatch acess code.
 */
 
/* number of digits for function after *. Must be at least 1 */
#define	FUNCTION_LEN 4
/* string containing all of the 1 digit functions */
#define	SHORTFUNCS "05678"
/* string containing all of the 2 digit functions */
#define	MEDFUNCS "9"

/* maximum digits in DTMF buffer, and seconds after * for DTMF command timeout */

#define	MAXDTMF 10
#define	DTMF_TIMEOUT 3

#define	NODES "nodes"

#define	MAXCONNECTTIME 5000

#define MAXNODESTR 300

enum {REM_OFF,REM_MONITOR,REM_TX};

enum{ID,PROC,TERM,COMPLETE,UNKEY,REMDISC,REMALREADY,REMNOTFOUND,REMGO,
	CONNECTED,CONNFAIL,STATUS,TIMEOUT};

#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/callerid.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/translate.h>
#include <asterisk/options.h>
#include <asterisk/config.h>
#include <asterisk/utils.h>
#include <asterisk/say.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <search.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <math.h>
#include <tonezone.h>
#include <linux/zaptel.h>

static  char *tdesc = "Radio Repeater / Remote Base  version 0.7  06/25/2004";
static char *app = "Rpt";

static char *synopsis = "Radio Repeater/Remote Base Control System";

static char *descrip = 
"  Rpt(sysname):  Radio Remote Link or Remote Base Link Endpoint Process.\n";

static int debug = 0;
static int nrpts = 0;

struct	ast_config *cfg;

STANDARD_LOCAL_USER;
LOCAL_USER_DECL;

#define	MSWAIT 200
#define	HANGTIME 5000
#define	TOTIME 180000
#define	IDTIME 300000
#define	MAXRPTS 20

static  pthread_t rpt_master_thread;

struct rpt;

struct rpt_link
{
	struct rpt_link *next;
	struct rpt_link *prev;
	char	mode;			/* 1 if in tx mode */
	char	isremote;
	char	name[MAXNODESTR];	/* identifier (routing) string */
	char	lasttx;
	char	lastrx;
	char	connected;
	char	outbound;
	long elaptime;
	struct ast_channel *chan;	
	struct ast_channel *pchan;	
} ;

struct rpt_tele
{
	struct rpt_tele *next;
	struct rpt_tele *prev;
	struct rpt *rpt;
	int	mode;
	struct rpt_link mylink;
	pthread_t threadid;
} ;

static struct rpt
{
	char *name;
	ast_mutex_t lock;
	char *rxchanname;
	char *txchanname;
	char *ourcontext;
	char *ourcallerid;
	char *acctcode;
	char *ident;
	char *tonezone;
	struct rpt_link links;
	int hangtime;
	int totime;
	int idtime;
	char exttx;
	char localtx;
	char remoterx;
	char remotetx;
	char remoteon;
	char simple;
	char remote;
	char tounkeyed;
	char tonotify;
	char enable;
	char dtmfbuf[MAXDTMF];
	char rem_dtmfbuf[MAXDTMF];
	char cmdnode[50];
	struct ast_channel *rxchannel,*txchannel;
	struct ast_channel *pchannel,*txpchannel;
	struct rpt_tele tele;
	pthread_t rpt_call_thread,rpt_thread;
	time_t rem_dtmf_time;
	int tailtimer,totimer,idtimer,txconf,conf,callmode,cidx;
	int dtmfidx,rem_dtmfidx;
	char mydtmf;
	char exten[AST_MAX_EXTENSION];
} rpt_vars[MAXRPTS];		

static void *rpt_tele_thread(void *this)
{
ZT_CONFINFO ci;  /* conference info */
int	res = 0,hastx,imdone = 0;
struct	rpt_tele *mytele = (struct rpt_tele *)this;
struct	rpt *myrpt;
struct	rpt_link *l,*m,linkbase;
struct	ast_channel *mychannel;

	/* get a pointer to myrpt */
	myrpt = mytele->rpt;
	/* allocate a pseudo-channel thru asterisk */
	mychannel = ast_request("zap",AST_FORMAT_SLINEAR,"pseudo");
	if (!mychannel)
	{
		fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
		ast_mutex_lock(&myrpt->lock);
		remque((struct qelem *)mytele);
		ast_mutex_unlock(&myrpt->lock);
		free(mytele);		
		pthread_exit(NULL);
	}
	/* make a conference for the tx */
	ci.chan = 0;
	ci.confno = myrpt->conf; /* use the tx conference */
	ci.confmode = ZT_CONF_CONFANN;
	/* first put the channel on the conference in announce mode */
	if (ioctl(mychannel->fds[0],ZT_SETCONF,&ci) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		ast_mutex_lock(&myrpt->lock);
		remque((struct qelem *)mytele);
		ast_mutex_unlock(&myrpt->lock);
		free(mytele);		
		ast_hangup(mychannel);
		pthread_exit(NULL);
	}
	ast_stopstream(mychannel);
	switch(mytele->mode)
	{
	    case ID:
		res = ast_streamfile(mychannel, myrpt->ident, mychannel->language);
		break;
	    case PROC:
		/* wait a little bit longer */
		usleep(1500000);
		res = ast_streamfile(mychannel, "rpt/callproceeding", mychannel->language);
		break;
	    case TERM:
		/* wait a little bit longer */
		usleep(1500000);
		res = ast_streamfile(mychannel, "rpt/callterminated", mychannel->language);
		break;
	    case COMPLETE:
		/* wait a little bit */
		usleep(1000000);
		res = ast_streamfile(mychannel, "rpt/functioncomplete", mychannel->language);
		break;
	    case UNKEY:
		/* wait a little bit */
		usleep(1000000);
		hastx = 0;
		l = myrpt->links.next;
		if (l != &myrpt->links)
		{
			ast_mutex_lock(&myrpt->lock);
			while(l != &myrpt->links)
			{
				if (l->mode) hastx++;
				l = l->next;
			}
			ast_mutex_unlock(&myrpt->lock);
			res = ast_streamfile(mychannel, 
				((!hastx) ? "rpt/remote_monitor" : "rpt/remote_tx"),
					mychannel->language);
			if (!res) 
				res = ast_waitstream(mychannel, "");
			else
				 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
			ast_stopstream(mychannel);
		} 
		/* if in remote cmd mode, indicate it */
		if (myrpt->cmdnode[0])
		{
			ast_safe_sleep(mychannel,200);
			res = ast_streamfile(mychannel, "rpt/remote_cmd", mychannel->language);
			if (!res) 
				res = ast_waitstream(mychannel, "");
			else
				 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
			ast_stopstream(mychannel);
		}
		imdone = 1;
		break;
	    case REMDISC:
		/* wait a little bit */
		usleep(1000000);
		res = ast_streamfile(mychannel, "rpt/node", mychannel->language);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		ast_say_character_str(mychannel,mytele->mylink.name,NULL,mychannel->language);
		res = ast_streamfile(mychannel, ((mytele->mylink.connected) ? 
			"rpt/remote_disc" : "rpt/remote_busy"), mychannel->language);
		break;
	    case REMALREADY:
		/* wait a little bit */
		usleep(1000000);
		res = ast_streamfile(mychannel, "rpt/remote_already", mychannel->language);
		break;
	    case REMNOTFOUND:
		/* wait a little bit */
		usleep(1000000);
		res = ast_streamfile(mychannel, "rpt/remote_notfound", mychannel->language);
		break;
	    case REMGO:
		/* wait a little bit */
		usleep(1000000);
		res = ast_streamfile(mychannel, "rpt/remote_go", mychannel->language);
		break;
	    case CONNECTED:
		/* wait a little bit */
		usleep(1000000);
		res = ast_streamfile(mychannel, "rpt/node", mychannel->language);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		ast_say_character_str(mychannel,mytele->mylink.name,NULL,mychannel->language);
		res = ast_streamfile(mychannel, "rpt/connected", mychannel->language);
		break;
	    case CONNFAIL:
		res = ast_streamfile(mychannel, "rpt/node", mychannel->language);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		ast_say_character_str(mychannel,mytele->mylink.name,NULL,mychannel->language);
		res = ast_streamfile(mychannel, "rpt/connection_failed", mychannel->language);
		break;
	    case STATUS:
		/* wait a little bit */
		usleep(1000000);
		hastx = 0;
		linkbase.next = &linkbase;
		linkbase.prev = &linkbase;
		ast_mutex_lock(&myrpt->lock);
		/* make our own list of links */
		l = myrpt->links.next;
		while(l != &myrpt->links)
		{
			m = malloc(sizeof(struct rpt_link));
			if (!m)
			{
				ast_log(LOG_WARNING, "Cannot alloc memory on %s\n", mychannel->name);
				pthread_exit(NULL);
			}
			memcpy(m,l,sizeof(struct rpt_link));
			m->next = m->prev = NULL;
			insque((struct qelem *)m,(struct qelem *)linkbase.next);
			l = l->next;
		}
		ast_mutex_unlock(&myrpt->lock);
		res = ast_streamfile(mychannel, "rpt/node", mychannel->language);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		ast_say_character_str(mychannel,myrpt->name,NULL,mychannel->language);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		if (myrpt->callmode)
		{
			hastx = 1;
			res = ast_streamfile(mychannel, "rpt/autopatch_on", mychannel->language);
			if (!res) 
				res = ast_waitstream(mychannel, "");
			else
				 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
			ast_stopstream(mychannel);
		}
		l = linkbase.next;
		while(l != &linkbase)
		{
			hastx = 1;
			res = ast_streamfile(mychannel, "rpt/node", mychannel->language);
			if (!res) 
				res = ast_waitstream(mychannel, "");
			else
				ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
			ast_stopstream(mychannel);
			ast_say_character_str(mychannel,l->name,NULL,mychannel->language);
			if (!res) 
				res = ast_waitstream(mychannel, "");
			else
				 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
			ast_stopstream(mychannel);
			res = ast_streamfile(mychannel, ((l->mode) ? 
				"rpt/tranceive" : "rpt/monitor"), mychannel->language);
			if (!res) 
				res = ast_waitstream(mychannel, "");
			else
				ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
			ast_stopstream(mychannel);
			l = l->next;
		}			
		if (!hastx)
		{
			res = ast_streamfile(mychannel, "rpt/repeat_only", mychannel->language);
			if (!res) 
				res = ast_waitstream(mychannel, "");
			else
				 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
			ast_stopstream(mychannel);
		}
		/* destroy our local link queue */
		l = linkbase.next;
		while(l != &linkbase)
		{
			m = l;
			l = l->next;
			remque((struct qelem *)m);
			free(m);
		}			
		imdone = 1;
		break;
	    case TIMEOUT:
		res = ast_streamfile(mychannel, "rpt/node", mychannel->language);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		ast_say_character_str(mychannel,myrpt->name,NULL,mychannel->language);
		res = ast_streamfile(mychannel, "rpt/timeout", mychannel->language);
		break;
	}
	if (!imdone)
	{
		if (!res) 
			res = ast_waitstream(mychannel, "");
		else {
			ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
			res = 0;
		}
	}
	ast_stopstream(mychannel);
	ast_mutex_lock(&myrpt->lock);
	remque((struct qelem *)mytele);
	ast_mutex_unlock(&myrpt->lock);
	free(mytele);		
	ast_hangup(mychannel);
	pthread_exit(NULL);
}

static void rpt_telemetry(struct rpt *myrpt,int mode,struct rpt_link *mylink)
{
struct rpt_tele *tele;
pthread_attr_t attr;

	tele = malloc(sizeof(struct rpt_tele));
	if (!tele)
	{
		ast_log(LOG_WARNING, "Unable to allocate memory\n");
		pthread_exit(NULL);
		return;
	}
	/* zero it out */
	memset((char *)tele,0,sizeof(struct rpt_tele));
	tele->rpt = myrpt;
	tele->mode = mode;
	ast_mutex_lock(&myrpt->lock);
	memset(&tele->mylink,0,sizeof(struct rpt_link));
	if (mylink)
	{
		memcpy(&tele->mylink,mylink,sizeof(struct rpt_link));
	}		
	insque((struct qelem *)tele,(struct qelem *)myrpt->tele.next); 
	ast_mutex_unlock(&myrpt->lock);
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(&tele->threadid,&attr,rpt_tele_thread,(void *) tele);
	return;
}

static void *rpt_call(void *this)
{
ZT_CONFINFO ci;  /* conference info */
struct	rpt *myrpt = (struct rpt *)this;
int	res;
struct	ast_frame *f,wf;
int stopped,congstarted;
struct ast_channel *mychannel,*genchannel;

	myrpt->mydtmf = 0;
	/* allocate a pseudo-channel thru asterisk */
	mychannel = ast_request("zap",AST_FORMAT_SLINEAR,"pseudo");
	if (!mychannel)
	{
		fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
		pthread_exit(NULL);
	}
	ci.chan = 0;
	ci.confno = myrpt->conf; /* use the pseudo conference */
	ci.confmode = ZT_CONF_REALANDPSEUDO | ZT_CONF_TALKER | ZT_CONF_LISTENER
		| ZT_CONF_PSEUDO_TALKER | ZT_CONF_PSEUDO_LISTENER; 
	/* first put the channel on the conference */
	if (ioctl(mychannel->fds[0],ZT_SETCONF,&ci) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		ast_hangup(mychannel);
		myrpt->callmode = 0;
		pthread_exit(NULL);
	}
	/* allocate a pseudo-channel thru asterisk */
	genchannel = ast_request("zap",AST_FORMAT_SLINEAR,"pseudo");
	if (!genchannel)
	{
		fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
		ast_hangup(mychannel);
		pthread_exit(NULL);
	}
	ci.chan = 0;
	ci.confno = myrpt->conf;
	ci.confmode = ZT_CONF_REALANDPSEUDO | ZT_CONF_TALKER | ZT_CONF_LISTENER
		| ZT_CONF_PSEUDO_TALKER | ZT_CONF_PSEUDO_LISTENER; 
	/* first put the channel on the conference */
	if (ioctl(genchannel->fds[0],ZT_SETCONF,&ci) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		myrpt->callmode = 0;
		pthread_exit(NULL);
	}
	if (myrpt->tonezone && (tone_zone_set_zone(mychannel->fds[0],myrpt->tonezone) == -1))
	{
		ast_log(LOG_WARNING, "Unable to set tone zone %s\n",myrpt->tonezone);
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		myrpt->callmode = 0;
		pthread_exit(NULL);
	}
	if (myrpt->tonezone && (tone_zone_set_zone(genchannel->fds[0],myrpt->tonezone) == -1))
	{
		ast_log(LOG_WARNING, "Unable to set tone zone %s\n",myrpt->tonezone);
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		myrpt->callmode = 0;
		pthread_exit(NULL);
	}
	/* start dialtone */
	if (tone_zone_play_tone(mychannel->fds[0],ZT_TONE_DIALTONE) < 0)
	{
		ast_log(LOG_WARNING, "Cannot start dialtone\n");
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		myrpt->callmode = 0;
		pthread_exit(NULL);
	}
	stopped = 0;
	congstarted = 0;
	while ((myrpt->callmode == 1) || (myrpt->callmode == 4))
	{

		if ((myrpt->callmode == 1) && (myrpt->cidx > 0) && (!stopped))
		{
			stopped = 1;
			/* stop dial tone */
			tone_zone_play_tone(mychannel->fds[0],-1);
		}
		if ((myrpt->callmode == 4) && (!congstarted))
		{
			congstarted = 1;
			/* start congestion tone */
			tone_zone_play_tone(mychannel->fds[0],ZT_TONE_CONGESTION);
		}
		res = ast_waitfor(mychannel, MSWAIT);
		if (res < 0)
		{
			ast_hangup(mychannel);
			ast_hangup(genchannel);
			ast_mutex_lock(&myrpt->lock);
			myrpt->callmode = 0;
			ast_mutex_unlock(&myrpt->lock);
			pthread_exit(NULL);
		}
		if (res == 0) continue;
		f = ast_read(mychannel);
		if (f == NULL) 
		{
			ast_hangup(mychannel);
			ast_hangup(genchannel);
			ast_mutex_lock(&myrpt->lock);
			myrpt->callmode = 0;
			ast_mutex_unlock(&myrpt->lock);
			pthread_exit(NULL);			
		}
		if ((f->frametype == AST_FRAME_CONTROL) &&
		    (f->subclass == AST_CONTROL_HANGUP))
		{
			ast_frfree(f);
			ast_hangup(mychannel);
			ast_hangup(genchannel);
			ast_mutex_lock(&myrpt->lock);
			myrpt->callmode = 0;
			ast_mutex_unlock(&myrpt->lock);
			pthread_exit(NULL);			
		}
		ast_frfree(f);
	}
	/* stop any tone generation */
	tone_zone_play_tone(mychannel->fds[0],-1);
	/* end if done */
	if (!myrpt->callmode)
	{
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		ast_mutex_lock(&myrpt->lock);
		myrpt->callmode = 0;
		ast_mutex_unlock(&myrpt->lock);
		pthread_exit(NULL);			
	}
	if (myrpt->ourcallerid && *myrpt->ourcallerid)
	{
		if (mychannel->callerid) free(mychannel->callerid);
		mychannel->callerid = strdup(myrpt->ourcallerid);
	}
	strcpy(mychannel->exten,myrpt->exten);
	strcpy(mychannel->context,myrpt->ourcontext);
	if (myrpt->acctcode)
		strcpy(mychannel->accountcode,myrpt->acctcode);
	mychannel->priority = 1;
	ast_channel_undefer_dtmf(mychannel);
	if (ast_pbx_start(mychannel) < 0)
	{
		ast_log(LOG_WARNING, "Unable to start PBX!!\n");
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		ast_mutex_lock(&myrpt->lock);
	 	myrpt->callmode = 0;
		ast_mutex_unlock(&myrpt->lock);
		pthread_exit(NULL);
	}
	ast_mutex_lock(&myrpt->lock);
	myrpt->callmode = 3;
	while(myrpt->callmode)
	{
		if ((!mychannel->pvt) && (myrpt->callmode != 4))
		{
			myrpt->callmode = 4;
			ast_mutex_unlock(&myrpt->lock);
			/* start congestion tone */
			tone_zone_play_tone(genchannel->fds[0],ZT_TONE_CONGESTION);
			ast_mutex_lock(&myrpt->lock);
		}
		if (myrpt->mydtmf)
		{
			wf.frametype = AST_FRAME_DTMF;
			wf.subclass = myrpt->mydtmf;
			wf.offset = 0;
			wf.mallocd = 0;
			wf.data = NULL;
			wf.datalen = 0;
			wf.samples = 0;
			ast_mutex_unlock(&myrpt->lock);
			ast_write(genchannel,&wf); 
			ast_mutex_lock(&myrpt->lock);
			myrpt->mydtmf = 0;
		}
		ast_mutex_unlock(&myrpt->lock);
		usleep(25000);
		ast_mutex_lock(&myrpt->lock);
	}
	ast_mutex_unlock(&myrpt->lock);
	tone_zone_play_tone(genchannel->fds[0],-1);
	if (mychannel->pvt) ast_softhangup(mychannel,AST_SOFTHANGUP_DEV);
	ast_hangup(genchannel);
	ast_mutex_lock(&myrpt->lock);
	myrpt->callmode = 0;
	ast_mutex_unlock(&myrpt->lock);
	pthread_exit(NULL);
}

static void send_link_dtmf(struct rpt *myrpt,char c)
{
char	str[300];
struct	ast_frame wf;
struct	rpt_link *l;

	sprintf(str,"D %s %s %d %c",myrpt->cmdnode,myrpt->name,++(myrpt->dtmfidx),c);
	wf.frametype = AST_FRAME_TEXT;
	wf.subclass = 0;
	wf.offset = 0;
	wf.mallocd = 1;
	wf.datalen = strlen(str) + 1;
	wf.samples = 0;
	l = myrpt->links.next;
	/* first, see if our dude is there */
	while(l != &myrpt->links)
	{
		/* if we found it, write it and were done */
		if (!strcmp(l->name,myrpt->cmdnode))
		{
			wf.data = strdup(str);
			ast_write(l->chan,&wf);
			return;
		}
		l = l->next;
	}
	l = myrpt->links.next;
	/* if not, give it to everyone */
	while(l != &myrpt->links)
	{
		wf.data = strdup(str);
		ast_write(l->chan,&wf);
		l = l->next;
	}
	return;
}

static void process_dtmf(char *cmd,struct rpt *myrpt, int allow_linkcmd)
{
pthread_attr_t attr;
char *tele,tmp[300],deststr[300],*val,*s,*s1;
struct rpt_link *l;
ZT_CONFINFO ci;  /* conference info */

	switch(atoi(cmd) / 1000)
	{
	case 6:	/* autopatch on / send asterisk (*) */
		if (!myrpt->enable) return;
		ast_mutex_lock(&myrpt->lock);
		/* if on call, force * into current audio stream */
		if ((myrpt->callmode == 2) || (myrpt->callmode == 3))
		{
			myrpt->mydtmf = '*';
			ast_mutex_unlock(&myrpt->lock);
			break;
		}
		if (myrpt->callmode)
		{
			ast_mutex_unlock(&myrpt->lock);
			return;
		}
		myrpt->callmode = 1;
		myrpt->cidx = 0;
		myrpt->exten[myrpt->cidx] = 0;
		ast_mutex_unlock(&myrpt->lock);
	        pthread_attr_init(&attr);
	        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		pthread_create(&myrpt->rpt_call_thread,&attr,rpt_call,(void *) myrpt);
		return;
	case 0:	/* autopatch off */
		if (!myrpt->enable) return;
		ast_mutex_lock(&myrpt->lock);
		if (!myrpt->callmode)
		{
			ast_mutex_unlock(&myrpt->lock);
			return;
		}
		myrpt->callmode = 0;
		ast_mutex_unlock(&myrpt->lock);
		rpt_telemetry(myrpt,TERM,NULL);
		return;
	case 9: /* system control group */
		/* if invalid, just ignore */
		if ((cmd[1] >= '2') && (cmd[1] <= '8')) return;
		ast_mutex_lock(&myrpt->lock);
		if (cmd[1] == '1') /* system enable */
		{
			myrpt->enable = 1;
		}
		if (cmd[1] == '0') /* system disable */
		{
			myrpt->enable = 0;
		}
		/* reset system */
		myrpt->callmode = 0;
		ast_mutex_unlock(&myrpt->lock);
		l = myrpt->links.next;
		/* disconnect all of the remote stuff */
		while(l != &myrpt->links)
		{
			ast_softhangup(l->chan,AST_SOFTHANGUP_DEV);
			l = l->next;
		}
		break;
	case 1: /* remote base off */
		if (!myrpt->enable) return;
		val = ast_variable_retrieve(cfg,NODES,cmd + 1);
		if (!val)
		{
			rpt_telemetry(myrpt,REMNOTFOUND,NULL);
			return;
		}
		strncpy(tmp,val,sizeof(tmp) - 1);
		s = tmp;
		s1 = strsep(&s,",");
		ast_mutex_lock(&myrpt->lock);
		l = myrpt->links.next;
		/* try to find this one in queue */
		while(l != &myrpt->links)
		{
			/* if found matching string */
			if (!strcmp(l->name,cmd + 1)) break;
			l = l->next;
		}
		if (l != &myrpt->links) /* if found */
		{
			ast_mutex_unlock(&myrpt->lock);
			ast_softhangup(l->chan,AST_SOFTHANGUP_DEV);
			break;
		}
		ast_mutex_unlock(&myrpt->lock);
		return;
	case 2: /* remote base monitor */
		if (!myrpt->enable) return;
		val = ast_variable_retrieve(cfg,NODES,cmd + 1);
		if (!val)
		{
			rpt_telemetry(myrpt,REMNOTFOUND,NULL);
			return;
		}
		strncpy(tmp,val,sizeof(tmp) - 1);
		s = tmp;
		s1 = strsep(&s,",");
		ast_mutex_lock(&myrpt->lock);
		l = myrpt->links.next;
		/* try to find this one in queue */
		while(l != &myrpt->links)
		{
			/* if found matching string */
			if (!strcmp(l->name,cmd + 1)) break;
			l = l->next;
		}
		/* if found */
		if (l != &myrpt->links) 
		{
			/* if already in this mode, just ignore */
			if (!l->mode) {
				ast_mutex_unlock(&myrpt->lock);
				rpt_telemetry(myrpt,REMALREADY,NULL);
				return;
			}
			ast_softhangup(l->chan,AST_SOFTHANGUP_DEV);
			usleep(500000);	
		}
		ast_mutex_unlock(&myrpt->lock);
		/* establish call in monitor mode */
		l = malloc(sizeof(struct rpt_link));
		if (!l)
		{
			ast_log(LOG_WARNING, "Unable to malloc\n");
			pthread_exit(NULL);
		}
		/* zero the silly thing */
		memset((char *)l,0,sizeof(struct rpt_link));
		sprintf(deststr,"IAX2/%s",s1);
		tele = strchr(deststr,'/');
		if (!tele)
		{
			fprintf(stderr,"rpt:Dial number (%s) must be in format tech/number\n",deststr);
			pthread_exit(NULL);
		}
		*tele++ = 0;
		l->isremote = (s && ast_true(s));
		strncpy(l->name,cmd + 1,MAXNODESTR - 1);
		l->chan = ast_request(deststr,AST_FORMAT_SLINEAR,tele);
		if (l->chan)
		{
			ast_set_read_format(l->chan,AST_FORMAT_SLINEAR);
			ast_set_write_format(l->chan,AST_FORMAT_SLINEAR);
			l->chan->whentohangup = 0;
			l->chan->appl = "Apprpt";
			l->chan->data = "(Remote Rx)";
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "rpt (remote) initiating call to %s/%s on %s\n",
					deststr,tele,l->chan->name);
			l->chan->callerid = strdup(myrpt->name);
			ast_call(l->chan,tele,0);
		}
		else
		{
			free(l);
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Unable to place call to %s/%s on %s\n",
					deststr,tele,l->chan->name);
			return;
		}
		/* allocate a pseudo-channel thru asterisk */
		l->pchan = ast_request("zap",AST_FORMAT_SLINEAR,"pseudo");
		if (!l->pchan)
		{
			fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
			pthread_exit(NULL);
		}
		ast_set_read_format(l->pchan,AST_FORMAT_SLINEAR);
		ast_set_write_format(l->pchan,AST_FORMAT_SLINEAR);
		/* make a conference for the pseudo-one */
		ci.chan = 0;
		ci.confno = myrpt->conf;
		ci.confmode = ZT_CONF_CONF | ZT_CONF_LISTENER | ZT_CONF_TALKER;
		/* first put the channel on the conference in proper mode */
		if (ioctl(l->pchan->fds[0],ZT_SETCONF,&ci) == -1)
		{
			ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
			pthread_exit(NULL);
		}
		ast_mutex_lock(&myrpt->lock);
		/* insert at end of queue */
		insque((struct qelem *)l,(struct qelem *)myrpt->links.next);
		ast_mutex_unlock(&myrpt->lock);
		break;
	case 3: /* remote base tranceieve */
		if (!myrpt->enable) return;
		val = ast_variable_retrieve(cfg,NODES,cmd + 1);
		if (!val)
		{
			rpt_telemetry(myrpt,REMNOTFOUND,NULL);
			return;
		}
		strncpy(tmp,val,sizeof(tmp) - 1);
		s = tmp;
		s1 = strsep(&s,",");
		ast_mutex_lock(&myrpt->lock);
		l = myrpt->links.next;
		/* try to find this one in queue */
		while(l != &myrpt->links)
		{
			/* if found matching string */
			if (!strcmp(l->name,cmd + 1)) break;
			l = l->next;
		}
		/* if found */
		if (l != &myrpt->links) 
		{
			/* if already in this mode, just ignore */
			if (l->mode)
			{
				ast_mutex_unlock(&myrpt->lock);
				rpt_telemetry(myrpt,REMALREADY,NULL);
				return;
			}
			ast_softhangup(l->chan,AST_SOFTHANGUP_DEV);
			usleep(500000);	
		}
		ast_mutex_unlock(&myrpt->lock);
		/* establish call in tranceive mode */
		l = malloc(sizeof(struct rpt_link));
		if (!l)
		{
			ast_log(LOG_WARNING, "Unable to malloc\n");
			pthread_exit(NULL);
		}
		/* zero the silly thing */
		memset((char *)l,0,sizeof(struct rpt_link));
		l->mode = 1;
		strncpy(l->name,cmd + 1,MAXNODESTR - 1);
		l->isremote = (s && ast_true(s));
		sprintf(deststr,"IAX2/%s",s1);
		tele = strchr(deststr,'/');
		if (!tele)
		{
			fprintf(stderr,"rpt:Dial number must be in format tech/number\n");
			pthread_exit(NULL);
		}
		*tele++ = 0;
		l->chan = ast_request(deststr,AST_FORMAT_SLINEAR,tele);
		if (l->chan)
		{
			ast_set_read_format(l->chan,AST_FORMAT_SLINEAR);
			ast_set_write_format(l->chan,AST_FORMAT_SLINEAR);
			l->chan->whentohangup = 0;
			l->chan->appl = "Apprpt";
			l->chan->data = "(Remote Rx)";
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "rpt (remote) initiating call to %s/%s on %s\n",
					deststr,tele,l->chan->name);
			l->chan->callerid = strdup(myrpt->name);
			ast_call(l->chan,tele,999);
		}
		else
		{
			free(l);
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Unable to place call to %s/%s on %s\n",
					deststr,tele,l->chan->name);
			return;
		}
		/* allocate a pseudo-channel thru asterisk */
		l->pchan = ast_request("zap",AST_FORMAT_SLINEAR,"pseudo");
		if (!l->pchan)
		{
			fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
			pthread_exit(NULL);
		}
		ast_set_read_format(l->pchan,AST_FORMAT_SLINEAR);
		ast_set_write_format(l->pchan,AST_FORMAT_SLINEAR);
		/* make a conference for the tx */
		ci.chan = 0;
		ci.confno = myrpt->conf;
		ci.confmode = ZT_CONF_CONF | ZT_CONF_LISTENER | ZT_CONF_TALKER;
		/* first put the channel on the conference in proper mode */
		if (ioctl(l->pchan->fds[0],ZT_SETCONF,&ci) == -1)
		{
			ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
			pthread_exit(NULL);
		}
		ast_mutex_lock(&myrpt->lock);
		/* insert at end of queue */
		insque((struct qelem *)l,(struct qelem *)myrpt->links.next);
		ast_mutex_unlock(&myrpt->lock);
		break;
	case 4: /* remote cmd mode */
		if (!myrpt->enable) return;
		/* if doesnt allow link cmd, return */
 		if ((!allow_linkcmd) || (myrpt->links.next == &myrpt->links)) return;
		/* if already in cmd mode, or selected self, forget it */
		if ((myrpt->cmdnode[0]) || (!strcmp(myrpt->name,cmd + 1)))
		{
			rpt_telemetry(myrpt,REMALREADY,NULL);
			return;
		}
		/* node must at least exist in list */
		val = ast_variable_retrieve(cfg,NODES,cmd + 1);
		if (!val)
		{
			rpt_telemetry(myrpt,REMNOTFOUND,NULL);
			return;
		}
		ast_mutex_lock(&myrpt->lock);
		myrpt->dtmfidx = -1;
		myrpt->dtmfbuf[0] = 0;
		myrpt->rem_dtmfidx = -1;
		myrpt->rem_dtmfbuf[0] = 0;
		strcpy(myrpt->cmdnode,cmd + 1);
		ast_mutex_unlock(&myrpt->lock);
		rpt_telemetry(myrpt,REMGO,NULL);				
		return;
	case 7: /* system status */
		if (!myrpt->enable) return;
		rpt_telemetry(myrpt,STATUS,NULL);
		return;
	case 8: /* force ID */
		if (!myrpt->enable) return;
		myrpt->idtimer = 0;
		return;
	default:
		return;
	}
	if (!myrpt->enable) return;
	rpt_telemetry(myrpt,COMPLETE,NULL);
}

static void handle_link_data(struct rpt *myrpt, struct rpt_link *mylink,
	char *str)
{
char	tmp[300],cmd[300],dest[300],src[300],c;
int	seq;
struct rpt_link *l;
struct	ast_frame wf;

	/* if we are a remote, we dont want to do this */
	if (myrpt->remote) return;
	wf.frametype = AST_FRAME_TEXT;
	wf.subclass = 0;
	wf.offset = 0;
	wf.mallocd = 1;
	wf.datalen = strlen(str) + 1;
	wf.samples = 0;
 	/* put string in our buffer */
	strncpy(tmp,str,sizeof(tmp) - 1);
	if (sscanf(tmp,"%s %s %s %d %c",cmd,dest,src,&seq,&c) != 5)
	{
		ast_log(LOG_WARNING, "Unable to parse link string %s\n",str);
		return;
	}
	if (strcmp(cmd,"D"))
	{
		ast_log(LOG_WARNING, "Unable to parse link string %s\n",str);
		return;
	}
	/* if not for me, redistribute to all links */
	if (strcmp(dest,myrpt->name))
	{
		l = myrpt->links.next;
		/* see if this is one in list */
		while(l != &myrpt->links)
		{
			/* dont send back from where it came */
			if ((l == mylink) || (!strcmp(l->name,mylink->name)))
			{
				l = l->next;
				continue;
			}
			/* if it is, send it and we're done */
			if (!strcmp(l->name,dest))
			{
				/* send, but not to src */
				if (strcmp(l->name,src)) {
					wf.data = strdup(str);
					ast_write(l->chan,&wf);
				}
				return;
			}
			l = l->next;
		}
		l = myrpt->links.next;
		/* otherwise, send it to all of em */
		while(l != &myrpt->links)
		{
			/* dont send back from where it came */
			if ((l == mylink) || (!strcmp(l->name,mylink->name)))
			{
				l = l->next;
				continue;
			}
			/* send, but not to src */
			if (strcmp(l->name,src)) {
				wf.data = strdup(str);
				ast_write(l->chan,&wf);
			}
			l = l->next;
		}
		return;
	}
	ast_mutex_lock(&myrpt->lock);
	if (c == '*')
	{
		myrpt->rem_dtmfidx = 0;
		myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx] = 0;
		time(&myrpt->rem_dtmf_time);
		ast_mutex_unlock(&myrpt->lock);
		return;
	} 
	else if ((c != '#') && (myrpt->rem_dtmfidx >= 0))
	{
		time(&myrpt->rem_dtmf_time);
		if (myrpt->rem_dtmfidx < MAXDTMF)
		{
			myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx++] = c;
			myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx] = 0;
			/* if to terminate function now */
			if ((myrpt->rem_dtmfidx == 1) && strchr(SHORTFUNCS,c))
			{
				while(myrpt->rem_dtmfidx < FUNCTION_LEN)
					myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx++] = '0';
				myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx] = 0;
			}
			/* if to terminate function now */
			if ((myrpt->rem_dtmfidx == 2) && strchr(MEDFUNCS,myrpt->rem_dtmfbuf[0]))
			{
				while(myrpt->rem_dtmfidx < FUNCTION_LEN)
					myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx++] = '0';
				myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx] = 0;
			}
		}
		if (myrpt->rem_dtmfidx == FUNCTION_LEN)
		{
			strcpy(cmd,myrpt->rem_dtmfbuf);
			myrpt->rem_dtmfbuf[0] = 0;
			myrpt->rem_dtmfidx = -1;
			ast_mutex_unlock(&myrpt->lock);
			process_dtmf(cmd,myrpt,0);
			return;
		}
	}
	ast_mutex_unlock(&myrpt->lock);
	return;
}

static void handle_remote_data(struct rpt *myrpt, char *str)
{
char	tmp[300],cmd[300],dest[300],src[300],c;
int	seq;

 	/* put string in our buffer */
	strncpy(tmp,str,sizeof(tmp) - 1);
	if (sscanf(tmp,"%s %s %s %d %c",cmd,dest,src,&seq,&c) != 5)
	{
		ast_log(LOG_WARNING, "Unable to parse link string %s\n",str);
		return;
	}
	if (strcmp(cmd,"D"))
	{
		ast_log(LOG_WARNING, "Unable to parse link string %s\n",str);
		return;
	}
	/* if not for me, ignore */
	if (strcmp(dest,myrpt->name)) return;
	printf("Remote %s got DTMF %c\n",myrpt->name,c);
	return;
}

/* single thread with one file (request) to dial */
static void *rpt(void *this)
{
struct	rpt *myrpt = (struct rpt *)this;
char *tele;
int ms = MSWAIT,lasttx,keyed,val,remrx;
struct ast_channel *who;
ZT_CONFINFO ci;  /* conference info */
time_t	dtmf_time,t;
struct rpt_link *l,*m;
pthread_attr_t attr;

	ast_mutex_lock(&myrpt->lock);
	tele = strchr(myrpt->rxchanname,'/');
	if (!tele)
	{
		fprintf(stderr,"rpt:Dial number must be in format tech/number\n");
		ast_mutex_unlock(&myrpt->lock);
		pthread_exit(NULL);
	}
	*tele++ = 0;
	myrpt->rxchannel = ast_request(myrpt->rxchanname,AST_FORMAT_SLINEAR,tele);
	if (myrpt->rxchannel)
	{
		ast_set_read_format(myrpt->rxchannel,AST_FORMAT_SLINEAR);
		ast_set_write_format(myrpt->rxchannel,AST_FORMAT_SLINEAR);
		myrpt->rxchannel->whentohangup = 0;
		myrpt->rxchannel->appl = "Apprpt";
		myrpt->rxchannel->data = "(Repeater Rx)";
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "rpt (Rx) initiating call to %s/%s on %s\n",
				myrpt->rxchanname,tele,myrpt->rxchannel->name);
		ast_call(myrpt->rxchannel,tele,999);
	}
	else
	{
		fprintf(stderr,"rpt:Sorry unable to obtain Rx channel\n");
		ast_mutex_unlock(&myrpt->lock);
		pthread_exit(NULL);
	}
	if (myrpt->txchanname)
	{
		tele = strchr(myrpt->txchanname,'/');
		if (!tele)
		{
			fprintf(stderr,"rpt:Dial number must be in format tech/number\n");
			ast_mutex_unlock(&myrpt->lock);
			pthread_exit(NULL);
		}
		*tele++ = 0;
		myrpt->txchannel = ast_request(myrpt->txchanname,AST_FORMAT_SLINEAR,tele);
		if (myrpt->txchannel)
		{
			ast_set_read_format(myrpt->txchannel,AST_FORMAT_SLINEAR);
			ast_set_write_format(myrpt->txchannel,AST_FORMAT_SLINEAR);
			myrpt->txchannel->whentohangup = 0;
			myrpt->txchannel->appl = "Apprpt";
			myrpt->txchannel->data = "(Repeater Rx)";
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "rpt (Tx) initiating call to %s/%s on %s\n",
					myrpt->txchanname,tele,myrpt->txchannel->name);
			ast_call(myrpt->txchannel,tele,999);
		}
		else
		{
			fprintf(stderr,"rpt:Sorry unable to obtain Tx channel\n");
			ast_mutex_unlock(&myrpt->lock);
			pthread_exit(NULL);
		}
	}
	else
	{
		myrpt->txchannel = myrpt->rxchannel;
	}
	/* allocate a pseudo-channel thru asterisk */
	myrpt->pchannel = ast_request("zap",AST_FORMAT_SLINEAR,"pseudo");
	if (!myrpt->pchannel)
	{
		fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
		ast_mutex_unlock(&myrpt->lock);
		pthread_exit(NULL);
	}
	/* make a conference for the tx */
	ci.chan = 0;
	ci.confno = -1; /* make a new conf */
	ci.confmode = ZT_CONF_CONF | ZT_CONF_LISTENER;
	/* first put the channel on the conference in proper mode */
	if (ioctl(myrpt->txchannel->fds[0],ZT_SETCONF,&ci) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		ast_mutex_unlock(&myrpt->lock);
		pthread_exit(NULL);
	}
	/* save tx conference number */
	myrpt->txconf = ci.confno;
	/* make a conference for the pseudo */
	ci.chan = 0;
	ci.confno = -1; /* make a new conf */
	ci.confmode = ZT_CONF_CONFANNMON; 
	/* first put the channel on the conference in announce mode */
	if (ioctl(myrpt->pchannel->fds[0],ZT_SETCONF,&ci) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		ast_mutex_unlock(&myrpt->lock);
		pthread_exit(NULL);
	}
	/* save pseudo channel conference number */
	myrpt->conf = ci.confno;
	/* allocate a pseudo-channel thru asterisk */
	myrpt->txpchannel = ast_request("zap",AST_FORMAT_SLINEAR,"pseudo");
	if (!myrpt->txpchannel)
	{
		fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
		ast_mutex_unlock(&myrpt->lock);
		pthread_exit(NULL);
	}
	/* make a conference for the tx */
	ci.chan = 0;
	ci.confno = myrpt->txconf;
	ci.confmode = ZT_CONF_CONF | ZT_CONF_TALKER ;
 	/* first put the channel on the conference in proper mode */
	if (ioctl(myrpt->txpchannel->fds[0],ZT_SETCONF,&ci) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		ast_mutex_unlock(&myrpt->lock);
		pthread_exit(NULL);
	}
	/* Now, the idea here is to copy from the physical rx channel buffer
	   into the pseudo tx buffer, and from the pseudo rx buffer into the 
	   tx channel buffer */
	myrpt->links.next = &myrpt->links;
	myrpt->links.prev = &myrpt->links;
	myrpt->tailtimer = 0;
	myrpt->totimer = 0;
	myrpt->idtimer = 0;
	myrpt->callmode = 0;
	myrpt->tounkeyed = 0;
	myrpt->tonotify = 0;
	lasttx = 0;
	keyed = 0;
	myrpt->dtmfidx = -1;
	myrpt->dtmfbuf[0] = 0;
	myrpt->rem_dtmfidx = -1;
	myrpt->rem_dtmfbuf[0] = 0;
	dtmf_time = 0;
	myrpt->rem_dtmf_time = 0;
	myrpt->enable = 1;
	ast_mutex_unlock(&myrpt->lock);
	val = 0;
	ast_channel_setoption(myrpt->rxchannel,AST_OPTION_TONE_VERIFY,&val,sizeof(char),0);
	val = 1;
	ast_channel_setoption(myrpt->rxchannel,AST_OPTION_RELAXDTMF,&val,sizeof(char),0);
	while (ms >= 0)
	{
		struct ast_frame *f;
		struct ast_channel *cs[300];
		int totx,elap,n,toexit;

		if (ast_check_hangup(myrpt->rxchannel)) break;
		if (ast_check_hangup(myrpt->txchannel)) break;
		if (ast_check_hangup(myrpt->pchannel)) break;
		if (ast_check_hangup(myrpt->txpchannel)) break;
		ast_mutex_lock(&myrpt->lock);
		myrpt->localtx = keyed;
		l = myrpt->links.next;
		remrx = 0;
		while(l != &myrpt->links)
		{
			if (l->lastrx) remrx = 1;
			l = l->next;
		}
		totx = (keyed || myrpt->callmode || 
			(myrpt->tele.next != &myrpt->tele));
		myrpt->exttx = totx;
		totx = totx || remrx;
		if (!totx) 
		{
			myrpt->totimer = myrpt->totime;
			myrpt->tounkeyed = 0;
			myrpt->tonotify = 0;
		}
		else myrpt->tailtimer = myrpt->hangtime;
		totx = totx && myrpt->totimer;
		/* if timed-out and not said already, say it */
		if ((!myrpt->totimer) && (!myrpt->tonotify))
		{
			myrpt->tonotify = 1;
			rpt_telemetry(myrpt,TIMEOUT,NULL);
		}
		/* if wants to transmit and in phone call, but timed out, 
			reset time-out timer if keyed */
		if ((!totx) && (!myrpt->totimer) && (!myrpt->tounkeyed) && (!keyed))
		{
			myrpt->tounkeyed = 1;
		}
		if ((!totx) && (!myrpt->totimer) && myrpt->tounkeyed && keyed)
		{
			myrpt->totimer = myrpt->totime;
			myrpt->tounkeyed = 0;
			myrpt->tonotify = 0;
			ast_mutex_unlock(&myrpt->lock);
			continue;
		}
		/* if timed-out and in circuit busy after call */
		if ((!totx) && (!myrpt->totimer) && (myrpt->callmode == 4))
		{
			myrpt->callmode = 0;
		}
		/* get rid of tail if timed out */
		if (!myrpt->totimer) myrpt->tailtimer = 0;
		/* if not timed-out, add in tail */
		if (myrpt->totimer) totx = totx || myrpt->tailtimer;
		/* if time to ID */
		if (totx && (!myrpt->idtimer))
		{
			myrpt->idtimer = myrpt->idtime;
			ast_mutex_unlock(&myrpt->lock);
			rpt_telemetry(myrpt,ID,NULL);
			ast_mutex_lock(&myrpt->lock);
		}
		/* let telemetry transmit anyway (regardless of timeout) */
		totx = totx || (myrpt->tele.next != &myrpt->tele);
		if (totx && (!lasttx))
		{
			lasttx = 1;
			ast_mutex_unlock(&myrpt->lock);
			ast_indicate(myrpt->txchannel,AST_CONTROL_RADIO_KEY);
			ast_mutex_lock(&myrpt->lock);
		}
		totx = totx && myrpt->enable;
		if ((!totx) && lasttx)
		{
			lasttx = 0;
			ast_mutex_unlock(&myrpt->lock);
			ast_indicate(myrpt->txchannel,AST_CONTROL_RADIO_UNKEY);
			ast_mutex_lock(&myrpt->lock);
		}
		time(&t);
		/* if DTMF timeout */
		if ((!myrpt->cmdnode[0]) && (myrpt->dtmfidx >= 0) && ((dtmf_time + DTMF_TIMEOUT) < t))
		{
			myrpt->dtmfidx = -1;
			myrpt->dtmfbuf[0] = 0;
		}			
		/* if remote DTMF timeout */
		if ((myrpt->rem_dtmfidx >= 0) && ((myrpt->rem_dtmf_time + DTMF_TIMEOUT) < t))
		{
			myrpt->rem_dtmfidx = -1;
			myrpt->rem_dtmfbuf[0] = 0;
		}			
		n = 0;
		cs[n++] = myrpt->rxchannel;
		cs[n++] = myrpt->pchannel;
		cs[n++] = myrpt->txpchannel;
		if (myrpt->txchannel != myrpt->rxchannel) cs[n++] = myrpt->txchannel;
		l = myrpt->links.next;
		while(l != &myrpt->links)
		{
			cs[n++] = l->chan;
			cs[n++] = l->pchan;
			l = l->next;
		}
		ast_mutex_unlock(&myrpt->lock);
		ms = MSWAIT;
		who = ast_waitfor_n(cs,n,&ms);
		if (who == NULL) ms = 0;
		elap = MSWAIT - ms;
		ast_mutex_lock(&myrpt->lock);
		l = myrpt->links.next;
		while(l != &myrpt->links)
		{
			/* ignore non-timing channels */
			if (l->elaptime < 0)
			{
				l = l->next;
				continue;
			}
			l->elaptime += elap;
			/* if connection has taken too long */
			if ((l->elaptime > MAXCONNECTTIME) && 
			   (l->chan->_state != AST_STATE_UP))
			{
				ast_mutex_unlock(&myrpt->lock);
				ast_softhangup(l->chan,AST_SOFTHANGUP_DEV);
				rpt_telemetry(myrpt,CONNFAIL,l);
				ast_mutex_lock(&myrpt->lock);
			}
			l = l->next;
		}
		if (myrpt->tailtimer) myrpt->tailtimer -= elap;
		if (myrpt->tailtimer < 0) myrpt->tailtimer = 0;
		if (myrpt->totimer) myrpt->totimer -= elap;
		if (myrpt->totimer < 0) myrpt->totimer = 0;
		if (myrpt->idtimer) myrpt->idtimer -= elap;
		if (myrpt->idtimer < 0) myrpt->idtimer = 0;
		ast_mutex_unlock(&myrpt->lock);
		if (!ms) continue;
		if (who == myrpt->rxchannel) /* if it was a read from rx */
		{
			f = ast_read(myrpt->rxchannel);
			if (!f)
			{
				if (debug) printf("@@@@ rpt:Hung Up\n");
				break;
			}
			if (f->frametype == AST_FRAME_VOICE)
			{
				ast_write(myrpt->pchannel,f);
			}
			else if (f->frametype == AST_FRAME_DTMF)
			{
				char c;

				c = (char) f->subclass; /* get DTMF char */
				if (c == '#')
				{
					/* if in simple mode, kill autopatch */
					if (myrpt->simple && myrpt->callmode)
					{
						myrpt->callmode = 0;
						rpt_telemetry(myrpt,TERM,NULL);
						continue;
					}
					ast_mutex_lock(&myrpt->lock);
					if (myrpt->cmdnode[0])
					{
						myrpt->cmdnode[0] = 0;
						myrpt->dtmfidx = -1;
						myrpt->dtmfbuf[0] = 0;
						ast_mutex_unlock(&myrpt->lock);
						rpt_telemetry(myrpt,COMPLETE,NULL);
					} else ast_mutex_unlock(&myrpt->lock);
					continue;
				}
				if (myrpt->cmdnode[0])
				{
					send_link_dtmf(myrpt,c);
					continue;
				}
				if (!myrpt->simple)
				{
					if (c == '*')
					{
						myrpt->dtmfidx = 0;
						myrpt->dtmfbuf[myrpt->dtmfidx] = 0;
						time(&dtmf_time);
						continue;
					} 
					else if ((c != '#') && (myrpt->dtmfidx >= 0))
					{
						time(&dtmf_time);
						if (myrpt->dtmfidx < MAXDTMF)
						{
							myrpt->dtmfbuf[myrpt->dtmfidx++] = c;
							myrpt->dtmfbuf[myrpt->dtmfidx] = 0;
							/* if to terminate function now */
							if ((myrpt->dtmfidx == 1) && strchr(SHORTFUNCS,c))
							{
								while(myrpt->dtmfidx < FUNCTION_LEN)
									myrpt->dtmfbuf[myrpt->dtmfidx++] = '0';
								myrpt->dtmfbuf[myrpt->dtmfidx] = 0;
							}
							/* if to terminate function now */
							if ((myrpt->dtmfidx == 2) && strchr(MEDFUNCS,myrpt->dtmfbuf[0]))
							{
								while(myrpt->dtmfidx < FUNCTION_LEN)
									myrpt->dtmfbuf[myrpt->dtmfidx++] = '0';
								myrpt->dtmfbuf[myrpt->dtmfidx] = 0;
							}
						}
						if (myrpt->dtmfidx == FUNCTION_LEN)
						{
							process_dtmf(myrpt->dtmfbuf,myrpt,1);
							myrpt->dtmfbuf[0] = 0;
							myrpt->dtmfidx = -1;
							continue;
						}
					}
				}
				else
				{
					if ((!myrpt->callmode) && (c == '*'))
					{
						myrpt->callmode = 1;
						myrpt->cidx = 0;
						myrpt->exten[myrpt->cidx] = 0;
					        pthread_attr_init(&attr);
			 		        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
						pthread_create(&myrpt->rpt_call_thread,&attr,rpt_call,(void *)myrpt);
						continue;
					}
				}
				if (myrpt->callmode == 1)
				{
					myrpt->exten[myrpt->cidx++] = c;
					myrpt->exten[myrpt->cidx] = 0;
					/* if this exists */
					if (ast_exists_extension(myrpt->pchannel,myrpt->ourcontext,myrpt->exten,1,NULL))
					{
						myrpt->callmode = 2;
						rpt_telemetry(myrpt,PROC,NULL);
					}
					/* if can continue, do so */
					if (ast_canmatch_extension(myrpt->pchannel,myrpt->ourcontext,myrpt->exten,1,NULL)) continue;
					/* call has failed, inform user */
					myrpt->callmode = 4;
					continue;
				}
				if ((myrpt->callmode == 2) || (myrpt->callmode == 3))
				{
					myrpt->mydtmf = f->subclass;
				}
			}						
			else if (f->frametype == AST_FRAME_CONTROL)
			{
				if (f->subclass == AST_CONTROL_HANGUP)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
				/* if RX key */
				if (f->subclass == AST_CONTROL_RADIO_KEY)
				{
					if (debug) printf("@@@@ rx key\n");
					keyed = 1;
				}
				/* if RX un-key */
				if (f->subclass == AST_CONTROL_RADIO_UNKEY)
				{
					if (debug) printf("@@@@ rx un-key\n");
					keyed = 0;
					/* if we have remotes, twiddle */
					if (myrpt->cmdnode[0] || (myrpt->links.next != &myrpt->links))
					{
						rpt_telemetry(myrpt,UNKEY,NULL);
					}
				}
			}
			ast_frfree(f);
			continue;
		}
		if (who == myrpt->pchannel) /* if it was a read from pseudo */
		{
			f = ast_read(myrpt->pchannel);
			if (!f)
			{
				if (debug) printf("@@@@ rpt:Hung Up\n");
				break;
			}
			if (f->frametype == AST_FRAME_VOICE)
			{
				ast_write(myrpt->txpchannel,f);
			}
			if (f->frametype == AST_FRAME_CONTROL)
			{
				if (f->subclass == AST_CONTROL_HANGUP)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
			}
			ast_frfree(f);
			continue;
		}
		if (who == myrpt->txchannel) /* if it was a read from tx */
		{
			f = ast_read(myrpt->txchannel);
			if (!f)
			{
				if (debug) printf("@@@@ rpt:Hung Up\n");
				break;
			}
			if (f->frametype == AST_FRAME_CONTROL)
			{
				if (f->subclass == AST_CONTROL_HANGUP)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
			}
			ast_frfree(f);
			continue;
		}
		toexit = 0;
		l = myrpt->links.next;
		while(l != &myrpt->links)
		{
			if (who == l->chan) /* if it was a read from rx */
			{
				ast_mutex_lock(&myrpt->lock);
				remrx = 0;
				/* see if any other links are receiving */
				m = myrpt->links.next;
				while(m != &myrpt->links)
				{
					/* if not us, count it */
					if ((m != l) && (m->lastrx)) remrx = 1;
					m = m->next;
				}
				ast_mutex_unlock(&myrpt->lock);
				totx = (((l->isremote) ? myrpt->localtx : 
					myrpt->exttx) || remrx) && l->mode;
				if (l->lasttx != totx)
				{
					if (totx)
					{
						ast_indicate(l->chan,AST_CONTROL_RADIO_KEY);
					}
					else
					{
						ast_indicate(l->chan,AST_CONTROL_RADIO_UNKEY);
					}
				}
				l->lasttx = totx;
				f = ast_read(l->chan);
				if (!f)
				{
					ast_mutex_lock(&myrpt->lock);
					/* remove from queue */
					remque((struct qelem *) l);
					ast_mutex_unlock(&myrpt->lock);
					rpt_telemetry(myrpt,REMDISC,l);
					/* hang-up on call to device */
					ast_hangup(l->chan);
					ast_hangup(l->pchan);
					free(l);
					break;
				}
				if (f->frametype == AST_FRAME_VOICE)
				{
					ast_write(l->pchan,f);
				}
				if (f->frametype == AST_FRAME_TEXT)
				{
					handle_link_data(myrpt,l,f->data);
				}
				if (f->frametype == AST_FRAME_CONTROL)
				{
					if (f->subclass == AST_CONTROL_ANSWER)
					{
						l->connected = 1;
						l->elaptime = -1;
						rpt_telemetry(myrpt,CONNECTED,l);
					}
					/* if RX key */
					if (f->subclass == AST_CONTROL_RADIO_KEY)
					{
						if (debug) printf("@@@@ rx key\n");
						l->lastrx = 1;
					}
					/* if RX un-key */
					if (f->subclass == AST_CONTROL_RADIO_UNKEY)
					{
						if (debug) printf("@@@@ rx un-key\n");
						l->lastrx = 0;
					}
					if (f->subclass == AST_CONTROL_HANGUP)
					{
						ast_frfree(f);
						ast_mutex_lock(&myrpt->lock);
						/* remove from queue */
						remque((struct qelem *) l);
						ast_mutex_unlock(&myrpt->lock);
						rpt_telemetry(myrpt,REMDISC,l);
						/* hang-up on call to device */
						ast_hangup(l->chan);
						ast_hangup(l->pchan);
						free(l);
						break;
					}
				}
				ast_frfree(f);
				break;
			}
			if (who == l->pchan) 
			{
				f = ast_read(l->pchan);
				if (!f)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					toexit = 1;
					break;
				}
				if (f->frametype == AST_FRAME_VOICE)
				{
					ast_write(l->chan,f);
				}
				if (f->frametype == AST_FRAME_CONTROL)
				{
					if (f->subclass == AST_CONTROL_HANGUP)
					{
						if (debug) printf("@@@@ rpt:Hung Up\n");
						ast_frfree(f);
						toexit = 1;
						break;
					}
				}
				ast_frfree(f);
				break;
			}
			l = l->next;
		}
		if (toexit) break;
		if (who == myrpt->txpchannel) /* if it was a read from remote tx */
		{
			f = ast_read(myrpt->txpchannel);
			if (!f)
			{
				if (debug) printf("@@@@ rpt:Hung Up\n");
				break;
			}
			if (f->frametype == AST_FRAME_CONTROL)
			{
				if (f->subclass == AST_CONTROL_HANGUP)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
			}
			ast_frfree(f);
			continue;
		}
	}
	ast_mutex_lock(&myrpt->lock);
	ast_hangup(myrpt->pchannel);
	ast_hangup(myrpt->txpchannel);
	ast_hangup(myrpt->rxchannel);
	if (myrpt->txchannel != myrpt->rxchannel) ast_hangup(myrpt->txchannel);
	l = myrpt->links.next;
	while(l != &myrpt->links)
	{
		struct rpt_link *ll = l;
		/* remove from queue */
		remque((struct qelem *) l);
		/* hang-up on call to device */
		ast_hangup(l->chan);
		ast_hangup(l->pchan);
		l = l->next;
		free(ll);
	}
	ast_mutex_unlock(&myrpt->lock);
	if (debug) printf("@@@@ rpt:Hung up channel\n");
	pthread_exit(NULL); 
	return NULL;
}

static void *rpt_master(void *ignore)
{
char *this,*val;
int	i,n;

	/* start with blank config */
	memset(&rpt_vars,0,sizeof(rpt_vars));

	cfg = ast_load("rpt.conf");
	if (!cfg) {
		ast_log(LOG_NOTICE, "Unable to open radio repeater configuration rpt.conf.  Radio Repeater disabled.\n");
		pthread_exit(NULL);
	}

	/* go thru all the specified repeaters */
	this = NULL;
	n = 0;
	while((this = ast_category_browse(cfg,this)) != NULL)
	{
		if (!strcmp(this,NODES)) continue;
		ast_log(LOG_DEBUG,"Loading config for repeater %s\n",this);
		ast_mutex_init(&rpt_vars[n].lock);
		rpt_vars[n].tele.next = &rpt_vars[n].tele;
		rpt_vars[n].tele.prev = &rpt_vars[n].tele;
		rpt_vars[n].name = this;
		rpt_vars[n].rxchanname = ast_variable_retrieve(cfg,this,"rxchannel");
		rpt_vars[n].txchanname = ast_variable_retrieve(cfg,this,"txchannel");
		rpt_vars[n].ourcontext = ast_variable_retrieve(cfg,this,"context");
		if (!rpt_vars[n].ourcontext) rpt_vars[n].ourcontext = this;
		rpt_vars[n].ourcallerid = ast_variable_retrieve(cfg,this,"callerid");
		rpt_vars[n].acctcode = ast_variable_retrieve(cfg,this,"accountcode");
		rpt_vars[n].ident = ast_variable_retrieve(cfg,this,"idrecording");
		val = ast_variable_retrieve(cfg,this,"hangtime");
		if (val) rpt_vars[n].hangtime = atoi(val);
			else rpt_vars[n].hangtime = HANGTIME;
		val = ast_variable_retrieve(cfg,this,"totime");
		if (val) rpt_vars[n].totime = atoi(val);
			else rpt_vars[n].totime = TOTIME;
		val = ast_variable_retrieve(cfg,this,"idtime");
		if (val) rpt_vars[n].idtime = atoi(val);
			else rpt_vars[n].idtime = IDTIME;
		val = ast_variable_retrieve(cfg,this,"simple");
		if (val) rpt_vars[n].simple = ast_true(val); 
			else rpt_vars[n].simple = 0;
		val = ast_variable_retrieve(cfg,this,"remote");
		if (val) rpt_vars[n].remote = ast_true(val); 
			else rpt_vars[n].remote = 0;
		rpt_vars[n].tonezone = ast_variable_retrieve(cfg,this,"tonezone");
		n++;
	}
	nrpts = n;
	ast_log(LOG_DEBUG, "Total of %d repeaters configured.\n",n);
	/* start em all */
	for(i = 0; i < n; i++)
	{
		if (!rpt_vars[i].rxchanname)
		{
			ast_log(LOG_WARNING,"Did not specify rxchanname for node %s\n",rpt_vars[i].name);
			pthread_exit(NULL);
		}
		/* if is a remote, dont start one for it */
		if (rpt_vars[i].remote) continue;
		if (!rpt_vars[i].ident)
		{
			ast_log(LOG_WARNING,"Did not specify ident for node %s\n",rpt_vars[i].name);
			pthread_exit(NULL);
		}
		pthread_create(&rpt_vars[i].rpt_thread,NULL,rpt,(void *) &rpt_vars[i]);
	}
	/* wait for first one to die (should be never) */
	pthread_join(rpt_vars[0].rpt_thread,NULL);
	pthread_exit(NULL);
}

static int rpt_exec(struct ast_channel *chan, void *data)
{
	int res=-1,i,keyed = 0,rem_totx;
	struct localuser *u;
	char tmp[256];
	char *options,*stringp,*tele;
	struct	rpt *myrpt;
	struct ast_frame *f;
	struct ast_channel *who;
	struct ast_channel *cs[20];
	struct	rpt_link *l;
	ZT_CONFINFO ci;  /* conference info */
	int ms,elap;

	if (!data || ast_strlen_zero((char *)data)) {
		ast_log(LOG_WARNING, "Rpt requires an argument (system node)\n");
		return -1;
	}
	strncpy(tmp, (char *)data, sizeof(tmp)-1);
	stringp=tmp;
	strsep(&stringp, "|");
	options = strsep(&stringp, "|");
	myrpt = NULL;
	/* see if we can find our specified one */
	for(i = 0; i < nrpts; i++)
	{
		/* if name matches, assign it and exit loop */
		if (!strcmp(tmp,rpt_vars[i].name))
		{
			myrpt = &rpt_vars[i];
			break;
		}
	}
	if (myrpt == NULL)
	{
		ast_log(LOG_WARNING, "Cannot find specified system node %s\n",tmp);
		return -1;
	}
	/* if is not a remote */
	if (!myrpt->remote)
	{
		char *b,*b1;

		/* look at callerid to see what node this comes from */
		if (!chan->callerid) /* if doesnt have callerid */
		{
			ast_log(LOG_WARNING, "Trying to use busy link on %s\n",tmp);
			return -1;
		}
		ast_callerid_parse(chan->callerid,&b,&b1);
		ast_shrink_phone_number(b1);
		if (!strcmp(myrpt->name,b1))
		{
			ast_log(LOG_WARNING, "Trying to link to self!!\n");
			return -1;
		}
		ast_mutex_lock(&myrpt->lock);
		l = myrpt->links.next;
		/* try to find this one in queue */
		while(l != &myrpt->links)
		{
			/* if found matching string */
			if (!strcmp(l->name,b1)) break;
			l = l->next;
		}
		/* if found */
		if (l != &myrpt->links) 
		{
			/* remove from queue */
			remque((struct qelem *) l);
			ast_mutex_unlock(&myrpt->lock);
			/* hang-up on call to device */
			ast_hangup(l->chan);
			ast_hangup(l->pchan);
			free(l);
			usleep(500000);	
		} else 
			ast_mutex_unlock(&myrpt->lock);
		/* establish call in tranceive mode */
		l = malloc(sizeof(struct rpt_link));
		if (!l)
		{
			ast_log(LOG_WARNING, "Unable to malloc\n");
			pthread_exit(NULL);
		}
		/* zero the silly thing */
		memset((char *)l,0,sizeof(struct rpt_link));
		l->mode = 1;
		strncpy(l->name,b1,MAXNODESTR - 1);
		l->isremote = 0;
		l->chan = chan;
		l->connected = 1;
		ast_set_read_format(l->chan,AST_FORMAT_SLINEAR);
		ast_set_write_format(l->chan,AST_FORMAT_SLINEAR);
		/* allocate a pseudo-channel thru asterisk */
		l->pchan = ast_request("zap",AST_FORMAT_SLINEAR,"pseudo");
		if (!l->pchan)
		{
			fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
			pthread_exit(NULL);
		}
		ast_set_read_format(l->pchan,AST_FORMAT_SLINEAR);
		ast_set_write_format(l->pchan,AST_FORMAT_SLINEAR);
		/* make a conference for the tx */
		ci.chan = 0;
		ci.confno = myrpt->conf;
		ci.confmode = ZT_CONF_CONF | ZT_CONF_LISTENER | ZT_CONF_TALKER;
		/* first put the channel on the conference in proper mode */
		if (ioctl(l->pchan->fds[0],ZT_SETCONF,&ci) == -1)
		{
			ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
			pthread_exit(NULL);
		}
		ast_mutex_lock(&myrpt->lock);
		/* insert at end of queue */
		insque((struct qelem *)l,(struct qelem *)myrpt->links.next);
		ast_mutex_unlock(&myrpt->lock);
		if (chan->_state != AST_STATE_UP) {
			ast_answer(chan);
		}
		return AST_PBX_KEEPALIVE;
	}
	/* if remote, error if anyone else already linked */
	if (myrpt->remoteon)
	{
		ast_mutex_unlock(&myrpt->lock);
		ast_log(LOG_WARNING, "Trying to use busy link on %s\n",tmp);
		return -1;
	}
	LOCAL_USER_ADD(u);
	tele = strchr(myrpt->rxchanname,'/');
	if (!tele)
	{
		fprintf(stderr,"rpt:Dial number must be in format tech/number\n");
		ast_mutex_unlock(&myrpt->lock);
		pthread_exit(NULL);
	}
	*tele++ = 0;
	ast_mutex_lock(&myrpt->lock);
	myrpt->rxchannel = ast_request(myrpt->rxchanname,AST_FORMAT_SLINEAR,tele);
	if (myrpt->rxchannel)
	{
		ast_set_read_format(myrpt->rxchannel,AST_FORMAT_SLINEAR);
		ast_set_write_format(myrpt->rxchannel,AST_FORMAT_SLINEAR);
		myrpt->rxchannel->whentohangup = 0;
		myrpt->rxchannel->appl = "Apprpt";
		myrpt->rxchannel->data = "(Repeater Rx)";
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "rpt (Rx) initiating call to %s/%s on %s\n",
				myrpt->rxchanname,tele,myrpt->rxchannel->name);
		ast_mutex_unlock(&myrpt->lock);
		ast_call(myrpt->rxchannel,tele,999);
		ast_mutex_lock(&myrpt->lock);
	}
	else
	{
		fprintf(stderr,"rpt:Sorry unable to obtain Rx channel\n");
		ast_mutex_unlock(&myrpt->lock);
		pthread_exit(NULL);
	}
	*--tele = '/';
	if (myrpt->txchanname)
	{
		tele = strchr(myrpt->txchanname,'/');
		if (!tele)
		{
			fprintf(stderr,"rpt:Dial number must be in format tech/number\n");
			ast_mutex_unlock(&myrpt->lock);
			pthread_exit(NULL);
		}
		*tele++ = 0;
		myrpt->txchannel = ast_request(myrpt->txchanname,AST_FORMAT_SLINEAR,tele);
		if (myrpt->txchannel)
		{
			ast_set_read_format(myrpt->txchannel,AST_FORMAT_SLINEAR);
			ast_set_write_format(myrpt->txchannel,AST_FORMAT_SLINEAR);
			myrpt->txchannel->whentohangup = 0;
			myrpt->txchannel->appl = "Apprpt";
			myrpt->txchannel->data = "(Repeater Rx)";
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "rpt (Tx) initiating call to %s/%s on %s\n",
					myrpt->txchanname,tele,myrpt->txchannel->name);
			ast_mutex_unlock(&myrpt->lock);
			ast_call(myrpt->txchannel,tele,999);
			ast_mutex_lock(&myrpt->lock);
		}
		else
		{
			fprintf(stderr,"rpt:Sorry unable to obtain Tx channel\n");
			ast_mutex_unlock(&myrpt->lock);
			pthread_exit(NULL);
		}
		*--tele = '/';
	}
	else
	{
		myrpt->txchannel = myrpt->rxchannel;
	}
	myrpt->remoterx = 0;
	myrpt->remotetx = 0;
	myrpt->remoteon = 1;
	ast_mutex_unlock(&myrpt->lock);
	ast_set_write_format(chan, AST_FORMAT_SLINEAR);
	ast_set_read_format(chan, AST_FORMAT_SLINEAR);
	/* if we are on 2w loop and are a remote, turn EC on */
	if (myrpt->remote && (myrpt->rxchannel == myrpt->txchannel))
	{
		i = 128;
		ioctl(myrpt->rxchannel->fds[0],ZT_ECHOCANCEL,&i);
	}
	if (chan->_state != AST_STATE_UP) {
		ast_answer(chan);
	}
	cs[0] = chan;
	cs[1] = myrpt->rxchannel;
	for(;;)
	{
		if (ast_check_hangup(chan)) break;
		if (ast_check_hangup(myrpt->rxchannel)) break;
		ms = MSWAIT;
		who = ast_waitfor_n(cs,2,&ms);
		if (who == NULL) ms = 0;
		elap = MSWAIT - ms;
		if (!ms) continue;
		rem_totx = keyed;
		if (rem_totx && (!myrpt->remotetx))
		{
			myrpt->remotetx = 1;
			ast_indicate(myrpt->txchannel,AST_CONTROL_RADIO_KEY);
		}
		if ((!rem_totx) && myrpt->remotetx)
		{
			myrpt->remotetx = 0;
			ast_indicate(myrpt->txchannel,AST_CONTROL_RADIO_UNKEY);
		}
		if (who == chan) /* if it was a read from incomming */
		{
			f = ast_read(chan);
			if (!f)
			{
				if (debug) printf("@@@@ link:Hung Up\n");
				break;
			}
			if (f->frametype == AST_FRAME_VOICE)
			{
				/* if not transmitting, zero-out audio */
				if (!myrpt->remotetx)
					memset(f->data,0,f->datalen);
				ast_write(myrpt->txchannel,f);
			}
			else if (f->frametype == AST_FRAME_TEXT)
			{
				handle_remote_data(myrpt,f->data);
			}
			else if (f->frametype == AST_FRAME_CONTROL)
			{
				if (f->subclass == AST_CONTROL_HANGUP)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
				/* if RX key */
				if (f->subclass == AST_CONTROL_RADIO_KEY)
				{
					if (debug) printf("@@@@ rx key\n");
					keyed = 1;
				}
				/* if RX un-key */
				if (f->subclass == AST_CONTROL_RADIO_UNKEY)
				{
					if (debug) printf("@@@@ rx un-key\n");
					keyed = 0;
				}
			}
			ast_frfree(f);
			continue;
		}
		if (who == myrpt->rxchannel) /* if it was a read from radio */
		{
			f = ast_read(myrpt->rxchannel);
			if (!f)
			{
				if (debug) printf("@@@@ link:Hung Up\n");
				break;
			}
			if (f->frametype == AST_FRAME_VOICE)
			{
				if ((myrpt->remote) && (myrpt->remotetx))
					memset(f->data,0,f->datalen);
				 ast_write(chan,f);
			}
			else if (f->frametype == AST_FRAME_CONTROL)
			{
				if (f->subclass == AST_CONTROL_HANGUP)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
				/* if RX key */
				if (f->subclass == AST_CONTROL_RADIO_KEY)
				{
					if (debug) printf("@@@@ remote rx key\n");
					if (!myrpt->remotetx)
					{
						ast_indicate(chan,AST_CONTROL_RADIO_KEY);
						myrpt->remoterx = 1;
					}
				}
				/* if RX un-key */
				if (f->subclass == AST_CONTROL_RADIO_UNKEY)
				{
					if (debug) printf("@@@@ remote rx un-key\n");
					if (!myrpt->remotetx) 
					{
						ast_indicate(chan,AST_CONTROL_RADIO_UNKEY);
						myrpt->remoterx = 0;
					}
				}
			}
			ast_frfree(f);
			continue;
		}

	}
	ast_mutex_lock(&myrpt->lock);
	if (myrpt->rxchannel != myrpt->txchannel) ast_hangup(myrpt->txchannel);
	ast_hangup(myrpt->rxchannel);
	myrpt->remoteon = 0;
	ast_mutex_unlock(&myrpt->lock);
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	int i;

	STANDARD_HANGUP_LOCALUSERS;
	for(i = 0; i < nrpts; i++) {
		if (!strcmp(rpt_vars[i].name,NODES)) continue;
                ast_mutex_destroy(&rpt_vars[i].lock);
	}
	return ast_unregister_application(app);
	return 0;
}

int load_module(void)
{
	pthread_create(&rpt_master_thread,NULL,rpt_master,NULL);
	return ast_register_application(app, rpt_exec, synopsis, descrip);
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
