/** @file app_rpt.c 
 *
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Radio Repeater program
 * 
 * Copyright (C) 2002, Jim Dixon
 *
 * Jim Dixon <jim@lambdatel.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 *
 */
 
#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/translate.h>
#include <asterisk/options.h>
#include <asterisk/config.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
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

static  char *tdesc = "Radio Repeater";
static int debug = 0;
STANDARD_LOCAL_USER;
LOCAL_USER_DECL;

#define	MSWAIT 200
#define	HANGTIME 5000
#define	TOTIME 180000
#define	IDTIME 300000
#define	MAXRPTS 20

static  pthread_t rpt_master_thread;

static struct rpt
{
	char *name;
	char *rxchanname;
	char *txchanname;
	char *ourcontext;
	char *ourcallerid;
	char *acctcode;
	char *idrecording;
	int hangtime;
	int totime;
	int idtime;
	struct ast_channel *rxchannel,*txchannel,*pchannel;
	int tailtimer,totimer,idtimer,txconf,pconf,callmode,cidx;
	pthread_t rpt_id_thread,rpt_term_thread,rpt_proc_thread,rpt_call_thread;
	char mydtmf,iding,terming;
	char exten[AST_MAX_EXTENSION];
} rpt_vars[MAXRPTS];		


static void *rpt_id(void *this)
{
ZT_CONFINFO ci;  /* conference info */
int	res;
struct	rpt *myrpt = (struct rpt *)this;
struct ast_channel *mychannel;

	/* allocate a pseudo-channel thru asterisk */
	mychannel = ast_request("zap",AST_FORMAT_SLINEAR,"pseudo");
	if (!mychannel)
	{
		fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
		pthread_exit(NULL);
	}
	/* make a conference for the tx */
	ci.chan = 0;
	ci.confno = myrpt->txconf; /* use the tx conference */
	ci.confmode = ZT_CONF_CONFANN;
	/* first put the channel on the conference in announce mode */
	if (ioctl(mychannel->fds[0],ZT_SETCONF,&ci) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		pthread_exit(NULL);
	}
	myrpt->iding = 1;
	ast_stopstream(mychannel);
	res = ast_streamfile(mychannel, myrpt->idrecording, mychannel->language);
	if (!res) 
		res = ast_waitstream(mychannel, "");
	else {
		ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		res = 0;
	}
	myrpt->iding = 0;
	ast_stopstream(mychannel);
	ast_hangup(mychannel);
	pthread_exit(NULL);
}

static void *rpt_proc(void *this)
{
ZT_CONFINFO ci;  /* conference info */
int	res;
struct	rpt *myrpt = (struct rpt *)this;
struct ast_channel *mychannel;

	/* wait a little bit */
	usleep(1500000);
	/* allocate a pseudo-channel thru asterisk */
	mychannel = ast_request("zap",AST_FORMAT_SLINEAR,"pseudo");
	if (!mychannel)
	{
		fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
		pthread_exit(NULL);
	}
	/* make a conference for the tx */
	ci.chan = 0;
	ci.confno = myrpt->pconf; /* use the tx conference */
	ci.confmode = ZT_CONF_CONFANN;
	/* first put the channel on the conference in announce mode */
	if (ioctl(mychannel->fds[0],ZT_SETCONF,&ci) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		pthread_exit(NULL);
	}
	myrpt->terming = 1;
	ast_stopstream(mychannel);
	res = ast_streamfile(mychannel, "callproceeding", mychannel->language);
	if (!res) 
		res = ast_waitstream(mychannel, "");
	else {
		ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		res = 0;
	}
	myrpt->terming = 0;
	ast_stopstream(mychannel);
	ast_hangup(mychannel);
	pthread_exit(NULL);
}

static void *rpt_term(void *this)
{
ZT_CONFINFO ci;  /* conference info */
int	res;
struct	rpt *myrpt = (struct rpt *)this;
struct ast_channel *mychannel;

	/* wait a little bit */
	usleep(1500000);
	/* allocate a pseudo-channel thru asterisk */
	mychannel = ast_request("zap",AST_FORMAT_SLINEAR,"pseudo");
	if (!mychannel)
	{
		fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
		pthread_exit(NULL);
	}
	/* make a conference for the tx */
	ci.chan = 0;
	ci.confno = myrpt->pconf; /* use the tx conference */
	ci.confmode = ZT_CONF_CONFANN;
	/* first put the channel on the conference in announce mode */
	if (ioctl(mychannel->fds[0],ZT_SETCONF,&ci) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		pthread_exit(NULL);
	}
	myrpt->terming = 1;
	ast_stopstream(mychannel);
	res = ast_streamfile(mychannel, "callterminated", mychannel->language);
	if (!res) 
		res = ast_waitstream(mychannel, "");
	else {
		ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		res = 0;
	}
	myrpt->terming = 0;
	ast_stopstream(mychannel);
	ast_hangup(mychannel);
	pthread_exit(NULL);
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
	ci.confno = myrpt->pconf; /* use the pseudo conference */
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
	ci.confno = myrpt->pconf;
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
			myrpt->callmode = 0;
			pthread_exit(NULL);
		}
		if (res == 0) continue;
		f = ast_read(mychannel);
		if (f == NULL) 
		{
			ast_hangup(mychannel);
			ast_hangup(genchannel);
			myrpt->callmode = 0;
			pthread_exit(NULL);			
		}
		if ((f->frametype == AST_FRAME_CONTROL) &&
		    (f->subclass == AST_CONTROL_HANGUP))
		{
			ast_frfree(f);
			ast_hangup(mychannel);
			ast_hangup(genchannel);
			myrpt->callmode = 0;
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
		myrpt->callmode = 0;
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
	 	myrpt->callmode = 0;
		pthread_exit(NULL);
	}
	myrpt->callmode = 3;

	while(myrpt->callmode)
	{
		if ((!mychannel->pvt) && (myrpt->callmode != 4))
		{
			myrpt->callmode = 4;
			/* start congestion tone */
			tone_zone_play_tone(genchannel->fds[0],ZT_TONE_CONGESTION);
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
			ast_write(genchannel,&wf); 
			myrpt->mydtmf = 0;
		}
		usleep(25000);
	}
	tone_zone_play_tone(genchannel->fds[0],-1);
	if (mychannel->pvt) ast_softhangup(mychannel,AST_SOFTHANGUP_DEV);
	ast_hangup(genchannel);
	myrpt->callmode = 0;
	pthread_exit(NULL);
}

/* single thread with one file (request) to dial */
static void *rpt(void *this)
{
struct	rpt *myrpt = (struct rpt *)this;
char *tele;
int ms = MSWAIT,lasttx,keyed,val;
struct ast_channel *who;
ZT_CONFINFO ci;  /* conference info */
pthread_attr_t attr;

	tele = strchr(myrpt->rxchanname,'/');
	if (!tele)
	{
		fprintf(stderr,"rpt:Dial number must be in format tech/number\n");
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
		fprintf(stderr,"rpt:Sorry unable to obtain channel\n");
		pthread_exit(NULL);
	}
	if (myrpt->txchanname)
	{
		tele = strchr(myrpt->txchanname,'/');
		if (!tele)
		{
			fprintf(stderr,"rpt:Dial number must be in format tech/number\n");
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
			fprintf(stderr,"rpt:Sorry unable to obtain channel\n");
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
		pthread_exit(NULL);
	}
	/* make a conference for the tx */
	ci.chan = 0;
	ci.confno = -1; /* make a new conf */
	ci.confmode = ZT_CONF_CONF | ZT_CONF_LISTENER;
	/* first put the channel on the conference in announce mode */
	if (ioctl(myrpt->txchannel->fds[0],ZT_SETCONF,&ci) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
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
		pthread_exit(NULL);
	}
	/* save pseudo channel conference number */
	myrpt->pconf = ci.confno;
	/* Now, the idea here is to copy from the physical rx channel buffer
	   into the pseudo tx buffer, and from the pseudo rx buffer into the 
	   tx channel buffer */
	myrpt->tailtimer = 0;
	myrpt->totimer = 0;
	myrpt->idtimer = 0;
	lasttx = 0;
	keyed = 0;
	myrpt->callmode = 0;
	val = 0;
	ast_channel_setoption(myrpt->rxchannel,AST_OPTION_TONE_VERIFY,&val,sizeof(char),0);
	val = 1;
	ast_channel_setoption(myrpt->rxchannel,AST_OPTION_RELAXDTMF,&val,sizeof(char),0);
	while (ms >= 0)
	{
		struct ast_frame *f;
		struct ast_channel *cs[3];
		int totx,elap;

		totx = (keyed || myrpt->callmode || myrpt->iding || myrpt->terming);
		if (!totx) myrpt->totimer = myrpt->totime;
		else myrpt->tailtimer = myrpt->hangtime;
		totx = (totx || myrpt->tailtimer) && myrpt->totimer;
		/* if wants to transmit and in phone call, but timed out, 
			reset time-out timer if keyed */
		if ((!totx) && (!myrpt->totimer) && myrpt->callmode && keyed)
		{
			myrpt->totimer = myrpt->totime;
			continue;
		}
		if (totx && (!myrpt->idtimer))
		{
			myrpt->idtimer = myrpt->idtime;
		        pthread_attr_init(&attr);
 		        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
			pthread_create(&myrpt->rpt_id_thread,&attr,rpt_id,(void *) myrpt);
		}
		if (totx && (!lasttx))
		{
			lasttx = 1;
			ast_indicate(myrpt->txchannel,AST_CONTROL_RADIO_KEY);
		}
		if ((!totx) && lasttx)
		{
			lasttx = 0;
			ast_indicate(myrpt->txchannel,AST_CONTROL_RADIO_UNKEY);
		}

		cs[0] = myrpt->rxchannel;
		cs[1] = myrpt->pchannel;
		cs[2] = myrpt->txchannel;
		ms = MSWAIT;
		who = ast_waitfor_n(cs,3,&ms);
		if (who == NULL) ms = 0;
		elap = MSWAIT - ms;
		if (myrpt->tailtimer) myrpt->tailtimer -= elap;
		if (myrpt->tailtimer < 0) myrpt->tailtimer = 0;
		if (myrpt->totimer) myrpt->totimer -= elap;
		if (myrpt->totimer < 0) myrpt->totimer = 0;
		if (myrpt->idtimer) myrpt->idtimer -= elap;
		if (myrpt->idtimer < 0) myrpt->idtimer = 0;
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
				if (myrpt->callmode && (c == '#'))
				{
					myrpt->callmode = 0;
				        pthread_attr_init(&attr);
		 		        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
					pthread_create(&myrpt->rpt_term_thread,&attr,rpt_term,(void *) myrpt);
					continue;
				}
				if (myrpt->callmode == 1)
				{
					myrpt->exten[myrpt->cidx++] = c;
					myrpt->exten[myrpt->cidx] = 0;
					/* if this exists */
					if (ast_exists_extension(myrpt->pchannel,myrpt->ourcontext,myrpt->exten,1,NULL))
					{
						myrpt->callmode = 2;
					        pthread_attr_init(&attr);
			 		        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
						pthread_create(&myrpt->rpt_proc_thread,&attr,rpt_proc,(void *) myrpt);
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
				}
			}
			ast_frfree(f);
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
				ast_write(myrpt->txchannel,f);
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
		}

	}
	ast_hangup(myrpt->pchannel);
	ast_hangup(myrpt->rxchannel);
	ast_hangup(myrpt->txchannel);
	if (debug) printf("@@@@ rpt:Hung up channel\n");
	pthread_exit(NULL);
	return NULL;
}

static void *rpt_master(void *ignore)
{
struct	ast_config *cfg;
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
		ast_log(LOG_DEBUG,"Loading config for repeater %s\n",this);
		rpt_vars[n].name = this;
		rpt_vars[n].rxchanname = ast_variable_retrieve(cfg,this,"rxchannel");
		rpt_vars[n].txchanname = ast_variable_retrieve(cfg,this,"txchannel");
		rpt_vars[n].ourcontext = ast_variable_retrieve(cfg,this,"context");
		if (!rpt_vars[n].ourcontext) rpt_vars[n].ourcontext = this;
		rpt_vars[n].ourcallerid = ast_variable_retrieve(cfg,this,"callerid");
		rpt_vars[n].acctcode = ast_variable_retrieve(cfg,this,"accountcode");
		rpt_vars[n].idrecording = ast_variable_retrieve(cfg,this,"idrecording");
		val = ast_variable_retrieve(cfg,this,"hangtime");
		if (val) rpt_vars[n].hangtime = atoi(val);
			else rpt_vars[n].hangtime = HANGTIME;
		val = ast_variable_retrieve(cfg,this,"totime");
		if (val) rpt_vars[n].totime = atoi(val);
			else rpt_vars[n].totime = TOTIME;
		val = ast_variable_retrieve(cfg,this,"idtime");
		if (val) rpt_vars[n].idtime = atoi(val);
			else rpt_vars[n].idtime = IDTIME;
		n++;
	}
	ast_log(LOG_DEBUG, "Total of %d repeaters configured.\n",n);
	/* start em all */
	for(i = 0; i < n; i++)
	{
		if (!rpt_vars[i].rxchanname)
		{
			ast_log(LOG_WARNING,"Did not specify rxchanname for repeater %s\n",rpt_vars[i].name);
			pthread_exit(NULL);
		}
		if (!rpt_vars[i].idrecording)
		{
			ast_log(LOG_WARNING,"Did not specify idrecording for repeater %s\n",rpt_vars[i].name);
			pthread_exit(NULL);
		}
		pthread_create(&rpt_vars[i].rpt_id_thread,NULL,rpt,(void *) &rpt_vars[i]);
	}
	/* wait for first one to die (should be never) */
	pthread_join(rpt_vars[0].rpt_id_thread,NULL);
	pthread_exit(NULL);
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return 0;
}

int load_module(void)
{
	pthread_create(&rpt_master_thread,NULL,rpt_master,NULL);
	return 0;
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
