/** @file app_rpt.c 
 *
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Radio Repeater / Remote Base program 
 *  version 0.13 7/12/04
 * 
 * See http://www.zapatatelephony.org/app_rpt.html
 *
 * Copyright (C) 2002-2004, Jim Dixon, WB6NIL
 *
 * Jim Dixon, WB6NIL <jim@lambdatel.com>
 * Serious contributions by Steve RoDgers, WA6ZFT <hwstar@rodgers.sdcoxmail.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 *
 * Repeater / Remote Functions:
 * "Simple" Mode:  * - autopatch access, # - autopatch hangup
 * Normal mode:
 * See the function list in rpt.conf
 *
 *  To send an asterisk (*) while dialing or talking on phone,
 *  use the autopatch acess code.
 *
 *
 * status cmds:
 *
 *  1 - Force ID
 *  2 - Give Time of Day
 *  3 - Give software Version
 *
 * cop (control operator) cmds:
 *
 *  1 - System warm boot
 *  2 - System enable
 *  3 - System disable
 *
 * ilink cmds:
 *
 *  1 - Disconnect specified link
 *  2 - Connect specified link -- monitor only
 *  3 - Connect specified link -- tranceive
 *  4 - Enter command mode on specified link
 *  5 - System status
 *  6 - Disconnect all links
 *
 * remote cmds:
 *
 *  0 - Recall Memory MM  (*000-*099) (Gets memory from rpt.conf)
 *  1 - Set VFO MMMMM*KKK*O   (Mhz digits, Khz digits, Offset)
 *  2 - Set Rx PL Tone HHH*D*
 *  3 - Set Tx PL Tone HHH*D* (Not currently implemented with DHE RBI-1)
 *  5 - Link Status
 *  100 - RX PL off (Default)
 *  101 - RX PL On
 *  102 - TX PL Off (Default)
 *  103 - TX PL On
 *  104 - Low Power
 *  105 - Med Power
 *  106 - Hi Power
 *
 *
*/

/* maximum digits in DTMF buffer, and seconds after * for DTMF command timeout */

#define	MAXDTMF 32
#define	DTMF_TIMEOUT 3


#define	MAXREMSTR 15

#define	NODES "nodes"
#define MEMORY "memory"
#define	FUNCTIONS "functions"
#define TELEMETRY "telemetry"
#define MORSE "morse"
#define	FUNCCHAR '*'
#define	ENDCHAR '#'

#define	DEFAULT_IOBASE 0x378

#define	MAXCONNECTTIME 5000

#define MAXNODESTR 300

#define ACTIONSIZE 32

#define TELEPARAMSIZE 32


enum {REM_OFF,REM_MONITOR,REM_TX};

enum{ID,PROC,TERM,COMPLETE,UNKEY,REMDISC,REMALREADY,REMNOTFOUND,REMGO,
	CONNECTED,CONNFAIL,STATUS,TIMEOUT,ID1, STATS_TIME,
	STATS_VERSION, IDTALKOVER, ARB_ALPHA};

enum {REM_SIMPLEX,REM_MINUS,REM_PLUS};

enum {REM_LOWPWR,REM_MEDPWR,REM_HIPWR};

enum {DC_INDETERMINATE, DC_REQ_FLUSH, DC_ERROR, DC_COMPLETE};
enum {SOURCE_RPT, SOURCE_LNK, SOURCE_RMT};

#include <asterisk/utils.h>
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
#include <asterisk/localtime.h>
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
#include <sys/io.h>
#include <math.h>
#include <tonezone.h>
#include <linux/zaptel.h>

static  char *tdesc = "Radio Repeater / Remote Base  version 0.13  07/12/2004";
static char *app = "Rpt";

static char *synopsis = "Radio Repeater/Remote Base Control System";

static char *descrip = 
"  Rpt(sysname):  Radio Remote Link or Remote Base Link Endpoint Process.\n";

static int debug = 0;  /* Set this >0 for extra debug output */
static int nrpts = 0;



struct	ast_config *cfg;

STANDARD_LOCAL_USER;
LOCAL_USER_DECL;

#define	MSWAIT 200
#define	HANGTIME 5000
#define	TOTIME 180000
#define	IDTIME 300000
#define	MAXRPTS 20
#define POLITEID 30000

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
	struct ast_channel *chan;
	int	mode;
	struct rpt_link mylink;
	char param[TELEPARAMSIZE];
	pthread_t threadid;
} ;

struct function_table_tag
{
	char action[ACTIONSIZE];
	int (*function)(struct rpt *myrpt, char *param, char *digitbuf, int command_source);
} ;

/* Used to store the morse code patterns */

struct morse_bits
{		  
	int len;
	int ddcomb;
} ;

struct telem_defaults
{
	char name[20];
	char value[80];
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
	char *functions;
	char *link_functions;
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
	struct ast_channel *pchannel,*txpchannel, *remchannel;
	struct rpt_tele tele;
	pthread_t rpt_call_thread,rpt_thread;
	time_t rem_dtmf_time,dtmf_time_rem;
	int tailtimer,totimer,idtimer,txconf,conf,callmode,cidx;
	int mustid;
	int politeid;
	int dtmfidx,rem_dtmfidx;
	char mydtmf;
	int iobase;
	char exten[AST_MAX_EXTENSION];
	char freq[MAXREMSTR],rxpl[MAXREMSTR],txpl[MAXREMSTR];
	char offset;
	char powerlevel;
	char txplon;
	char rxplon;
	char funcchar;
	char endchar;
	int link_longestfunc;
	int longestfunc;
	int longestnode;	
} rpt_vars[MAXRPTS];	

static struct telem_defaults tele_defs[] = {
	{"ct1","|t(350,0,100,3072)(500,0,100,3072)(660,0,100,3072)"},
	{"ct2","|t(660,880,150,3072)"},
	{"ct3","|t(440,0,150,3072)"},
	{"ct4","|t(550,0,150,3072)"},
	{"ct5","|t(660,0,150,3072)"},
	{"ct6","|t(880,0,150,3072)"},
	{"ct7","|t(660,440,150,3072)"},
	{"ct8","|t(700,1100,150,3072)"},
	{"remotemon","|t(1600,0,75,2048)"},
	{"remotetx","|t(2000,0,75,2048)(0,0,75,0)(1600,0,75,2048)"},
	{"cmdmode","|t(900,904,200,2048)"},
	{"functcomplete","|t(1000,0,100,2048)(0,0,100,0)(1000,0,100,2048)"}
} ;

/*
* Forward decl's - these suppress compiler warnings when funcs coded further down the file than thier invokation
*/

static int setrbi(struct rpt *myrpt);



/*
* Define function protos for function table here
*/

static int function_ilink(struct rpt *myrpt, char *param, char *digitbuf, int command_source);
static int function_autopatchup(struct rpt *myrpt, char *param, char *digitbuf, int command_source);
static int function_autopatchdn(struct rpt *myrpt, char *param, char *digitbuf, int command_source);
static int function_status(struct rpt *myrpt, char *param, char *digitbuf, int command_source);
static int function_cop(struct rpt *myrpt, char *param, char *digitbuf, int command_source);
static int function_remote(struct rpt *myrpt, char *param, char *digitbuf, int command_source);
/*
* Function table
*/

static struct function_table_tag function_table[] = {
	{"cop", function_cop},
	{"autopatchup", function_autopatchup},
	{"autopatchdn", function_autopatchdn},
	{"ilink", function_ilink},
	{"status", function_status},
	{"remote", function_remote}
} ;
	
static int myatoi(char *str)
{
int	ret;

	if (str == NULL) return -1;
	if (sscanf(str,"%i",&ret) != 1) return -1;
	return ret;
}

static int play_tone_pair(struct ast_channel *chan, int f1, int f2, int duration, int amplitude)
{
	return ast_tonepair(chan, f1, f2, duration, amplitude);	
}

static int play_tone(struct ast_channel *chan, int freq, int duration, int amplitude)
{
	return play_tone_pair(chan, freq, 0, duration, amplitude);
}

static int play_silence(struct ast_channel *chan, int duration)
{
	return play_tone_pair(chan, 0, 0, duration, 0);
}


static int send_morse(struct ast_channel *chan, char *string, int speed, int freq, int amplitude)
{

static struct morse_bits mbits[] = {
		{0, 0}, /* SPACE */
		{0, 0}, 
		{6, 18},/* " */
		{0, 0},
		{7, 72},/* $ */
		{0, 0},
		{0, 0},
		{6, 30},/* ' */
		{5, 13},/* ( */
		{6, 29},/* ) */
		{0, 0},
		{5, 10},/* + */
		{6, 51},/* , */
		{6, 33},/* - */
		{6, 42},/* . */
		{5, 9}, /* / */
		{5, 31},/* 0 */
		{5, 30},/* 1 */
		{5, 28},/* 2 */
		{5, 24},/* 3 */
		{5, 16},/* 4 */
		{5, 0}, /* 5 */
		{5, 1}, /* 6 */
		{5, 3}, /* 7 */
		{5, 7}, /* 8 */
		{5, 15},/* 9 */
		{6, 7}, /* : */
		{6, 21},/* ; */
		{0, 0},
		{5, 33},/* = */
		{0, 0},
		{6, 12},/* ? */
		{0, 0},
        	{2, 2}, /* A */
 		{4, 1}, /* B */
		{4, 5}, /* C */
		{3, 1}, /* D */
		{1, 0}, /* E */
		{4, 4}, /* F */
		{3, 3}, /* G */
		{4, 0}, /* H */
		{2, 0}, /* I */
		{4, 14},/* J */
		{3, 5}, /* K */
		{4, 2}, /* L */
		{2, 3}, /* M */
		{2, 1}, /* N */
		{3, 7}, /* O */
		{4, 6}, /* P */
		{4, 11},/* Q */
		{3, 2}, /* R */
		{3, 0}, /* S */
		{1, 1}, /* T */
		{3, 4}, /* U */
		{4, 8}, /* V */
		{3, 6}, /* W */
		{4, 9}, /* X */
		{4, 13},/* Y */
		{4, 3}  /* Z */
	};


	int dottime;
	int dashtime;
	int intralettertime;
	int interlettertime;
	int interwordtime;
	int len, ddcomb;
	int res;
	int c;
		
	
	res = 0;
	
	/* Approximate the dot time from the speed arg. */
	
	dottime = 900/speed;
	
	/* Establish timing releationships */
	
	dashtime = 3 * dottime;
	intralettertime = dottime;
	interlettertime = dottime * 4 ;
	interwordtime = dottime * 7;
	
	for(;(*string) && (!res); string++){
	
		c = *string;
		
		/* Convert lower case to upper case */
		
		if((c >= 'a') && (c <= 'z'))
			c -= 0x20;
		
		/* Can't deal with any char code greater than Z, skip it */
		
		if(c  > 'Z')
			continue;
		
		/* If space char, wait the inter word time */
					
		if(c == ' '){
			if(!res)
				res = play_silence(chan, interwordtime);
			continue;
		}
		
		/* Subtract out control char offset to match our table */
		
		c -= 0x20;
		
		/* Get the character data */
		
		len = mbits[c].len;
		ddcomb = mbits[c].ddcomb;
		
		/* Send the character */
		
		for(; len ; len--){
			if(!res)
				res = play_tone(chan, freq, (ddcomb & 1) ? dashtime : dottime, amplitude);
			if(!res)
				res = play_silence(chan, intralettertime);
			ddcomb >>= 1;
		}
		
		/* Wait the interletter time */
		
		if(!res)
			res = play_silence(chan, interlettertime - intralettertime);
	}
	
	/* Wait for all the frames to be sent */
	
	if (!res) 
		res = ast_waitstream(chan, "");
	ast_stopstream(chan);
		
	return res;
}

static int send_tone_telemetry(struct ast_channel *chan, char *tonestring)
{
	char *stringp;
	char *tonesubset;
	int f1,f2;
	int duration;
	int amplitude;
	int res;
	
	res = 0;
	
	stringp = ast_strdupa(tonestring);

	for(;tonestring;){
		tonesubset = strsep(&stringp,")");
		if(!tonesubset)
			break;
		if(sscanf(tonesubset,"(%d,%d,%d,%d", &f1, &f2, &duration, &amplitude) != 4)
			break;
		res = play_tone_pair(chan, f1, f2, duration, amplitude);
		if(res)
			break;
	}
	if(!res)
		res = play_tone_pair(chan, 0, 0, 100, 0); /* This is needed to ensure the last tone segment is timed correctly */
	
	if (!res) 
		res = ast_waitstream(chan, "");
	ast_stopstream(chan);
			
	return res;
		
}
	

static int sayfile(struct ast_channel *mychannel,char *fname)
{
int	res;

	res = ast_streamfile(mychannel, fname, mychannel->language);
	if (!res) 
		res = ast_waitstream(mychannel, "");
	else
		 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
	ast_stopstream(mychannel);
	return res;
}

static int saycharstr(struct ast_channel *mychannel,char *str)
{
int	res;

	res = ast_say_character_str(mychannel,str,NULL,mychannel->language);
	if (!res) 
		res = ast_waitstream(mychannel, "");
	else
		 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
	ast_stopstream(mychannel);
	return res;
}

/* Retrieve an int from a config file */
                                                                                
static int retrieve_astcfgint(char *category, char *name, int min, int max, int defl)
{
        char *var;
        int ret;
                                                                                
        var = ast_variable_retrieve(cfg, category, name);
        if(var){
                ret = myatoi(var);
                if(ret < min)
                        ret = min;
                if(ret > max)
                        ret = max;
        }
        else
                ret = defl;
        return ret;
}

static int telem_any(struct ast_channel *chan, char *entry)
{
	int res;
	char c;
	
	static int morsespeed;
	static int morsefreq;
	static int morseampl;
	static int morseidfreq = 0;
	static int morseidampl;
	static char mcat[] = MORSE;
	
	res = 0;
	
	if(!morseidfreq){ /* Get the morse parameters if not already loaded */
		morsespeed = retrieve_astcfgint( mcat, "speed", 5, 20, 20);
        	morsefreq = retrieve_astcfgint( mcat, "frequency", 300, 3000, 800);
        	morseampl = retrieve_astcfgint( mcat, "amplitude", 200, 8192, 4096);
		morseidampl = retrieve_astcfgint( mcat, "idamplitude", 200, 8192, 2048);
		morseidfreq = retrieve_astcfgint( mcat, "idfrequency", 300, 3000, 330);	
	}
	
	/* Is it a file, or a tone sequence? */
			
	if(entry[0] == '|'){
		c = entry[1];
		if((c >= 'a')&&(c <= 'z'))
			c -= 0x20;
	
		switch(c){
			case 'I': /* Morse ID */
				res = send_morse(chan, entry + 2, morsespeed, morseidfreq, morseidampl);
				break;
			
			case 'M': /* Morse Message */
				res = send_morse(chan, entry + 2, morsespeed, morsefreq, morseampl);
				break;
			
			case 'T': /* Tone sequence */
				res = send_tone_telemetry(chan, entry + 2);
				break;
			default:
				res = -1;
		}
	}
	else
		res = sayfile(chan, entry); /* File */
	return res;
}

/*
* This function looks up a telemetry name in the config file, and does a telemetry response as configured.
*
* 4 types of telemtry are handled: Morse ID, Morse Message, Tone Sequence, and a File containing a recording.
*/

static int telem_lookup(struct ast_channel *chan, char *name)
{
	
	int res;
	int i;
	char *entry;

	res = 0;
	entry = NULL;
	
	
	/* Try to look up the telemetry name */
	
	entry = ast_variable_retrieve(cfg, TELEMETRY, name);
	if(!entry){
		/* Telemetry name wasn't found in the config file, use the default */
		for(i = 0; i < sizeof(tele_defs)/sizeof(struct telem_defaults) ; i++){
			if(!strcasecmp(tele_defs[i].name, name))
				entry = tele_defs[i].value;
		}
	}
	if(entry)	
		telem_any(chan, entry);
	else{
		ast_log(LOG_WARNING, "Telemetry name not found: %s\n", name);
		res = -1;
	}
	return res;
}

static void *rpt_tele_thread(void *this)
{
ZT_CONFINFO ci;  /* conference info */
int	res = 0,hastx,imdone = 0;
struct	rpt_tele *mytele = (struct rpt_tele *)this;
struct	rpt *myrpt;
struct	rpt_link *l,*m,linkbase;
struct	ast_channel *mychannel;
int vmajor, vminor;
char *p,*ct,*ct_copy,*ident, *nodename;
time_t t;
struct tm localtm;


	/* get a pointer to myrpt */
	myrpt = mytele->rpt;

	/* Snag copies of a few key myrpt variables */
	ast_mutex_lock(&myrpt->lock);
	nodename = ast_strdupa(myrpt->name);
	ident = ast_strdupa(myrpt->ident);
	ast_mutex_unlock(&myrpt->lock);
	
	
	/* allocate a pseudo-channel thru asterisk */
	mychannel = ast_request("zap",AST_FORMAT_SLINEAR,"pseudo");
	if (!mychannel)
	{
		fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
		remque((struct qelem *)mytele);
		ast_mutex_unlock(&myrpt->lock);
		free(mytele);		
		pthread_exit(NULL);
	}
	ast_mutex_lock(&myrpt->lock);
	mytele->chan = mychannel; /* Save a copy of the channel so we can access it externally if need be */
	ast_mutex_unlock(&myrpt->lock);
	
	/* make a conference for the tx */
	ci.chan = 0;
	/* If there's an ID queued, only connect the ID audio to the local tx conference so 
		linked systems can't hear it */
	ci.confno = (((mytele->mode == ID) || (mytele->mode == IDTALKOVER) || (mytele->mode == UNKEY)) ?
		 myrpt->txconf : myrpt->conf);
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
	    case ID1:
		/* wait a bit */
		usleep(500000);
	
		res = telem_any(mychannel, ident); 
		imdone=1;
	
		break;
		
		
	    case IDTALKOVER:
	    	p = ast_variable_retrieve(cfg, nodename, "idtalkover");
	    	if(p)
			res = telem_any(mychannel, p); 
		imdone=1;	
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
		res = telem_lookup(mychannel, "functcomplete");
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

			res = telem_lookup(mychannel,(!hastx) ? "remotemon" : "remotetx");
			if(res)
				ast_log(LOG_WARNING, "telem_lookup:remotexx failed on %s\n", mychannel->name);
			
		
		/* if in remote cmd mode, indicate it */
			if (myrpt->cmdnode[0])
			{
				ast_safe_sleep(mychannel,200);
				res = telem_lookup(mychannel, "cmdmode");
				if(res)
				 	ast_log(LOG_WARNING, "telem_lookup:cmdmode failed on %s\n", mychannel->name);
				ast_stopstream(mychannel);
			}
		}
		else if((ct = ast_variable_retrieve(cfg, nodename, "unlinkedct"))){ /* Unlinked Courtesy Tone */
			ct_copy = ast_strdupa(ct);
			res = telem_lookup(mychannel, ct_copy);
			if(res)
			 	ast_log(LOG_WARNING, "telem_lookup:ctx failed on %s\n", mychannel->name);		
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
		
	    case STATS_TIME:
	    	usleep(1000000); /* Wait a little bit */
		t = time(NULL);
		ast_localtime(&t, &localtm, NULL);
		/* Say the phase of the day is before the time */
		if((localtm.tm_hour >= 0) && (localtm.tm_hour < 12))
			p = "rpt/goodmorning";
		else if((localtm.tm_hour >= 12) && (localtm.tm_hour < 18))
			p = "rpt/goodafternoon";
		else
			p = "rpt/goodevening";
		if (sayfile(mychannel,p) == -1)
		{
			imdone = 1;
			break;
		}
		/* Say the time is ... */		
		if (sayfile(mychannel,"rpt/thetimeis") == -1)
		{
			imdone = 1;
			break;
		}
		/* Say the time */				
	    	res = ast_say_time(mychannel, t, "", mychannel->language);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);		
		imdone = 1;
	    	break;
	    case STATS_VERSION:
		p = strstr(tdesc, "version");	
		if(!p)
			break;	
		if(sscanf(p, "version %d.%d", &vmajor, &vminor) != 2)
			break;
    		usleep(1000000); /* Wait a little bit */
		/* Say "version" */
		if (sayfile(mychannel,"rpt/version") == -1)
		{
			imdone = 1;
			break;
		}
		if(!res) /* Say "X" */
			ast_say_number(mychannel, vmajor, "", mychannel->language, (char *) NULL);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);	
		if (saycharstr(mychannel,".") == -1)
		{
			imdone = 1;
			break;
		}
		if(!res) /* Say "Y" */
			ast_say_number(mychannel, vminor, "", mychannel->language, (char *) NULL);
		if (!res){
			res = ast_waitstream(mychannel, "");
			ast_stopstream(mychannel);
		}	
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		imdone = 1;
	    	break;
	    case ARB_ALPHA:
	    	usleep(1000000); /* Wait a little bit */
	    	if(mytele->param)
	    		saycharstr(mychannel, mytele->param);
	    	imdone = 1;
		break;
	    default:
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

static void rpt_telemetry(struct rpt *myrpt,int mode, void *data)
{
struct rpt_tele *tele;
struct rpt_link *mylink = (struct rpt_link *) data;
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
	if((mode == CONNFAIL) || (mode == REMDISC) || (mode == CONNECTED)){
		memset(&tele->mylink,0,sizeof(struct rpt_link));
		if (mylink){
			memcpy(&tele->mylink,mylink,sizeof(struct rpt_link));
		}
	}
	else if (mode == ARB_ALPHA){
		strncpy(tele->param, (char *) data, TELEPARAMSIZE - 1);
		tele->param[TELEPARAMSIZE - 1] = 0;
	}
	insque((struct qelem *)tele,(struct qelem *)myrpt->tele.next); 
	ast_mutex_unlock(&myrpt->lock);
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	ast_pthread_create(&tele->threadid,&attr,rpt_tele_thread,(void *) tele);
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
	strncpy(mychannel->exten, myrpt->exten, sizeof(mychannel->exten) - 1);
	strncpy(mychannel->context, myrpt->ourcontext, sizeof(mychannel->context) - 1);
	if (myrpt->acctcode)
		strncpy(mychannel->accountcode, myrpt->acctcode, sizeof(mychannel->accountcode) - 1);
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

	snprintf(str, sizeof(str), "D %s %s %d %c", myrpt->cmdnode, myrpt->name, ++(myrpt->dtmfidx), c);
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

/*
* Internet linking function 
*/

static int function_ilink(struct rpt *myrpt, char *param, char *digitbuf, int command_source)
{

	char *val, *s, *s1, *tele;
	char tmp[300], deststr[300] = "";
	struct rpt_link *l;
	ZT_CONFINFO ci;  /* conference info */

	if(!param)
		return DC_ERROR;
		
			
	if (!myrpt->enable)
		return DC_ERROR;

	if(debug)
		printf("@@@@ ilink param = %s, digitbuf = %s\n", (param)? param : "(null)", digitbuf);
		
	switch(myatoi(param)){
		case 1: /* Link off */
		
		
			val = ast_variable_retrieve(cfg, NODES, digitbuf);
			if (!val){
				if(strlen(digitbuf) >= myrpt->longestnode)
					return DC_ERROR;
				break;
			}
			strncpy(tmp,val,sizeof(tmp) - 1);
			s = tmp;
			s1 = strsep(&s,",");
			ast_mutex_lock(&myrpt->lock);
			l = myrpt->links.next;
			/* try to find this one in queue */
			while(l != &myrpt->links){
				/* if found matching string */
				if (!strcmp(l->name, digitbuf))
					break;
				l = l->next;
			}
			if (l != &myrpt->links){ /* if found */
				ast_mutex_unlock(&myrpt->lock);
				ast_softhangup(l->chan,AST_SOFTHANGUP_DEV);
				rpt_telemetry(myrpt, COMPLETE, NULL);
				return DC_COMPLETE;
			}
			ast_mutex_unlock(&myrpt->lock);	
			return DC_COMPLETE;
		case 2: /* Link Monitor */
			val = ast_variable_retrieve(cfg, NODES, digitbuf);
			if (!val){
				if(strlen(digitbuf) >= myrpt->longestnode)
					return DC_ERROR;
				break;
			}
			strncpy(tmp,val,sizeof(tmp) - 1);
			s = tmp;
			s1 = strsep(&s,",");
			ast_mutex_lock(&myrpt->lock);
			l = myrpt->links.next;
			/* try to find this one in queue */
			while(l != &myrpt->links){
				/* if found matching string */
				if (!strcmp(l->name, digitbuf))
					break;
				l = l->next;
			}
			/* if found */
			if (l != &myrpt->links) 
			{
				/* if already in this mode, just ignore */
				if (!l->mode) {
					ast_mutex_unlock(&myrpt->lock);
					rpt_telemetry(myrpt,REMALREADY,NULL);
					return DC_COMPLETE;
					
				}
				ast_softhangup(l->chan,AST_SOFTHANGUP_DEV);
			}
			ast_mutex_unlock(&myrpt->lock);
			/* establish call in monitor mode */
			l = malloc(sizeof(struct rpt_link));
			if (!l){
				ast_log(LOG_WARNING, "Unable to malloc\n");
				pthread_exit(NULL);
			}
			/* zero the silly thing */
			memset((char *)l,0,sizeof(struct rpt_link));
			snprintf(deststr, sizeof(deststr), "IAX2/%s", s1);
			tele = strchr(deststr,'/');
			if (!tele){
				fprintf(stderr,"link2:Dial number (%s) must be in format tech/number\n",deststr);
				pthread_exit(NULL);
			}
			*tele++ = 0;
			l->isremote = (s && ast_true(s));
			strncpy(l->name, digitbuf, MAXNODESTR - 1);
			l->chan = ast_request(deststr,AST_FORMAT_SLINEAR,tele);
			if (l->chan){
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
				return DC_ERROR;
			}
			/* allocate a pseudo-channel thru asterisk */
			l->pchan = ast_request("zap",AST_FORMAT_SLINEAR,"pseudo");
			if (!l->pchan){
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
			rpt_telemetry(myrpt,COMPLETE,NULL);
			return DC_COMPLETE;
		case 3: /* Link transceive */
			val = ast_variable_retrieve(cfg, NODES, digitbuf);
			if (!val){
				if(strlen(digitbuf) >= myrpt->longestnode)
					return DC_ERROR;
				break;
			}
			strncpy(tmp,val,sizeof(tmp) - 1);
			s = tmp;
			s1 = strsep(&s,",");
			ast_mutex_lock(&myrpt->lock);
			l = myrpt->links.next;
			/* try to find this one in queue */
			while(l != &myrpt->links){
				/* if found matching string */
				if (!strcmp(l->name, digitbuf))
					break;
				l = l->next;
			}
			/* if found */
			if (l != &myrpt->links){ 
				/* if already in this mode, just ignore */
				if (l->mode){
					ast_mutex_unlock(&myrpt->lock);
					rpt_telemetry(myrpt, REMALREADY, NULL);
					return DC_COMPLETE;
				}
				ast_softhangup(l->chan, AST_SOFTHANGUP_DEV);
			} 
			ast_mutex_unlock(&myrpt->lock);
			/* establish call in tranceive mode */
			l = malloc(sizeof(struct rpt_link));
			if (!l){
				ast_log(LOG_WARNING, "Unable to malloc\n");
				pthread_exit(NULL);
			}
			/* zero the silly thing */
			memset((char *)l,0,sizeof(struct rpt_link));
			l->mode = 1;
			strncpy(l->name, digitbuf, MAXNODESTR - 1);
			l->isremote = (s && ast_true(s));
			snprintf(deststr, sizeof(deststr), "IAX2/%s", s1);
			tele = strchr(deststr, '/');
			if (!tele){
				fprintf(stderr,"link3:Dial number (%s) must be in format tech/number\n",deststr);
				pthread_exit(NULL);
			}
			*tele++ = 0;
			l->chan = ast_request(deststr, AST_FORMAT_SLINEAR, tele);
			if (l->chan){
				ast_set_read_format(l->chan, AST_FORMAT_SLINEAR);
				ast_set_write_format(l->chan, AST_FORMAT_SLINEAR);
				l->chan->whentohangup = 0;
				l->chan->appl = "Apprpt";
				l->chan->data = "(Remote Rx)";
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "rpt (remote) initiating call to %s/%s on %s\n",
						deststr, tele, l->chan->name);
				l->chan->callerid = strdup(myrpt->name);
				ast_call(l->chan,tele,999);
			}
			else{
				free(l);
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Unable to place call to %s/%s on %s\n",
						deststr,tele,l->chan->name);
				return DC_ERROR;
			}
			/* allocate a pseudo-channel thru asterisk */
			l->pchan = ast_request("zap",AST_FORMAT_SLINEAR,"pseudo");
			if (!l->pchan){
				fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
				pthread_exit(NULL);
			}
			ast_set_read_format(l->pchan, AST_FORMAT_SLINEAR);
			ast_set_write_format(l->pchan, AST_FORMAT_SLINEAR);
			/* make a conference for the tx */
			ci.chan = 0;
			ci.confno = myrpt->conf;
			ci.confmode = ZT_CONF_CONF | ZT_CONF_LISTENER | ZT_CONF_TALKER;
			/* first put the channel on the conference in proper mode */
			if (ioctl(l->pchan->fds[0], ZT_SETCONF, &ci) == -1)
			{
				ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
				pthread_exit(NULL);
			}
			ast_mutex_lock(&myrpt->lock);
			/* insert at end of queue */
			insque((struct qelem *)l,(struct qelem *)myrpt->links.next);
			ast_mutex_unlock(&myrpt->lock);
			rpt_telemetry(myrpt,COMPLETE,NULL);
			return DC_COMPLETE;
		case 4: /* Enter Command Mode */
		
			/* if doesnt allow link cmd, or no links active, return */
 			if ((command_source != SOURCE_RPT) || (myrpt->links.next == &myrpt->links))
				return DC_COMPLETE;
			
			/* if already in cmd mode, or selected self, fughetabahtit */
			if ((myrpt->cmdnode[0]) || (!strcmp(myrpt->name, digitbuf))){
			
				rpt_telemetry(myrpt, REMALREADY, NULL);
				return DC_COMPLETE;
			}
			/* node must at least exist in list */
			val = ast_variable_retrieve(cfg, NODES, digitbuf);
			if (!val){
				if(strlen(digitbuf) >= myrpt->longestnode)
					return DC_ERROR;
				break;
			
			}
			ast_mutex_lock(&myrpt->lock);
			strncpy(myrpt->cmdnode, digitbuf, sizeof(myrpt->cmdnode) - 1);
			ast_mutex_unlock(&myrpt->lock);
			rpt_telemetry(myrpt, REMGO, NULL);	
			return DC_COMPLETE;
			
		case 5: /* Status */
			rpt_telemetry(myrpt, STATUS, NULL);
			return DC_COMPLETE;
			
			
		case 6: /* All Links Off */
			l = myrpt->links.next;
			
			while(l != &myrpt->links){
				ast_softhangup(l->chan, AST_SOFTHANGUP_DEV); /* Hang 'em up */
				l = l->next;
			}
			rpt_telemetry(myrpt, COMPLETE, NULL);
			break;
	
		default:
			return DC_ERROR;
			
	}
	
	return DC_INDETERMINATE;
}	

/*
* Autopatch up
*/

static int function_autopatchup(struct rpt *myrpt, char *param, char *digitbuf, int command_source)
{
	pthread_attr_t attr;
	
		
	if (!myrpt->enable)
		return DC_ERROR;
		
	if(debug)
		printf("@@@@ Autopatch up\n");

	ast_mutex_lock(&myrpt->lock);
	
	/* if on call, force * into current audio stream */
	
	if ((myrpt->callmode == 2) || (myrpt->callmode == 3)){
		myrpt->mydtmf = myrpt->funcchar;
		ast_mutex_unlock(&myrpt->lock);
	}
	if (myrpt->callmode){
		ast_mutex_unlock(&myrpt->lock);
		return DC_COMPLETE;
	}
	myrpt->callmode = 1;
	myrpt->cidx = 0;
	myrpt->exten[myrpt->cidx] = 0;
	ast_mutex_unlock(&myrpt->lock);
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	ast_pthread_create(&myrpt->rpt_call_thread,&attr,rpt_call,(void *) myrpt);
	return DC_COMPLETE;
}

/*
* Autopatch down
*/

static int function_autopatchdn(struct rpt *myrpt, char *param, char *digitbuf, int command_source)
{
	if (!myrpt->enable)
		return DC_ERROR;
	
	if(debug)
		printf("@@@@ Autopatch down\n");
		
	ast_mutex_lock(&myrpt->lock);
	
	if (!myrpt->callmode){
		ast_mutex_unlock(&myrpt->lock);
		return DC_COMPLETE;
	}
	
	myrpt->callmode = 0;
	ast_mutex_unlock(&myrpt->lock);
	rpt_telemetry(myrpt, TERM, NULL);
	return DC_COMPLETE;
}

/*
* Status
*/

static int function_status(struct rpt *myrpt, char *param, char *digitbuf, int command_source)
{

	if(!param)
		return DC_ERROR;
		
			
	if (!myrpt->enable)
		return DC_ERROR;

	if(debug)
		printf("@@@@ status param = %s, digitbuf = %s\n", (param)? param : "(null)", digitbuf);
	
	switch(myatoi(param)){
		case 1: /* System ID */
			rpt_telemetry(myrpt, ID1, NULL);
			return DC_COMPLETE;
		case 2: /* System Time */
			rpt_telemetry(myrpt, STATS_TIME, NULL);
			return DC_COMPLETE;
		case 3: /* app_rpt.c version */
			rpt_telemetry(myrpt, STATS_VERSION, NULL);
		default:
			return DC_ERROR;
	}
	return DC_INDETERMINATE;
}

/*
* COP - Control operator
*/

static int function_cop(struct rpt *myrpt, char *param, char *digitbuf, int command_source)
{
	if(!param)
		return DC_ERROR;
	
	switch(myatoi(param)){
		case 1: /* System reset */
			system("killall -9 asterisk"); /* FIXME to drastic? */
			return DC_COMPLETE;

		case 2:
			myrpt->enable = 1;
			rpt_telemetry(myrpt, ARB_ALPHA, (void *) "RPTENA");
			return DC_COMPLETE;
			
		case 3:
			myrpt->enable = 0;
			return DC_COMPLETE;
			
	}	
	return DC_INDETERMINATE;
}

/*
* Remote base function
*/

static int function_remote(struct rpt *myrpt, char *param, char *digitbuf, int command_source)
{
	char *s,*s1,*s2,*val;
	int i,j,k,l,res,offset,offsave;
	char oc;
	char tmp[20], freq[20] = "", savestr[20] = "";
	struct ast_channel *mychannel;

	if((!param) || (command_source != SOURCE_RMT))
		return DC_ERROR;
		
	mychannel = myrpt->remchannel;
	
	
	switch(myatoi(param)){

		case 1:  /* retrieve memory */
			if(strlen(digitbuf) < 2) /* needs 2 digits */
				break;
			
			for(i = 0 ; i < 2 ; i++){
				if((digitbuf[i] < '0') || (digitbuf[i] > '9'))
					return DC_ERROR;
			}
	    
			val = ast_variable_retrieve(cfg, MEMORY, digitbuf);
			if (!val){
				if (ast_safe_sleep(mychannel,1000) == -1)
					return DC_ERROR;
				sayfile(mychannel,"rpt/memory_notfound");
				return DC_COMPLETE;
			}			
			strncpy(tmp,val,sizeof(tmp) - 1);
			s = strchr(tmp,',');
			if (!s)
				return DC_ERROR;
			*s++ = 0;
			s1 = strchr(s,',');
			if (!s1)
				return DC_ERROR;
			*s1++ = 0;
			strncpy(myrpt->freq, tmp, sizeof(myrpt->freq) - 1);
			strncpy(myrpt->rxpl, s, sizeof(myrpt->rxpl) - 1);
			myrpt->offset = REM_SIMPLEX;
			myrpt->powerlevel = REM_MEDPWR;
			myrpt->rxplon = 0;
			myrpt->txplon = 0;
			while(*s1)
			{
				switch(*s1++){
				
					case 'L':
					case 'l':
						myrpt->powerlevel = REM_LOWPWR;
					break;
					
					case 'H':
					case 'h':
						myrpt->powerlevel = REM_HIPWR;
					break;
					
					case 'M':
					case 'm':
						myrpt->powerlevel = REM_MEDPWR;
						break;
						
					case '-':
						myrpt->offset = REM_MINUS;
						break;
						
					case '+':
						myrpt->offset = REM_PLUS;
						break;
						
					case 'S':
					case 's':
						myrpt->offset = REM_SIMPLEX;
						break;
						
					case 'T':
					case 't':
						myrpt->txplon = 1;
						break;
						
					case 'R':
					case 'r':
						myrpt->rxplon = 1;
					break;
				}
			}
		
		
			if (setrbi(myrpt) == -1)
				return DC_ERROR;
		
		
			return DC_COMPLETE;	
			
		case 2:  /* set freq + offset */
	   
			
	    		for(i = 0, j = 0, k = 0, l = 0 ; digitbuf[i] ; i++){ /* look for N+*N+*N */
				if(digitbuf[i] == '*'){
					j++;
					continue;
				}
				if((digitbuf[i] < '0') || (digitbuf[i] > '9'))
					return DC_ERROR;
				else{
					if(j == 0)
						l++;
					if(j == 1)
						k++;
				}
			}
		
			i = strlen(digitbuf) - 1;
			if((j > 2) || (l > 5) || (k > 3))
				return DC_ERROR; /* &$@^! */
			
			if((j < 2) || (digitbuf[i] == '*'))
				break; /* Not yet */
				
			strncpy(tmp, digitbuf ,sizeof(tmp) - 1);
			
			s = tmp;
			s1 = strsep(&s, "*"); /* Pick off MHz */
			s2 = strsep(&s,"*"); /* Pick off KHz */
			oc = *s; /* Pick off offset */
	
			
			switch(strlen(s2)){ /* Allow partial entry of khz digits for laziness support */
				case 1:
					k = 100 * atoi(s2);
					break;
				
				case 2:
					k = 10 * atoi(s2);
					break;
					
				case 3:
					if((s2[2] != '0')&&(s2[2] != '5'))
						return DC_ERROR;
					k = atoi(s2);
					break;
					
				default:
					return DC_ERROR;
					
			}
				
					
			
			snprintf(freq, sizeof(freq), "%s.%03d", s1, k);
			
			offset = REM_SIMPLEX;
			
			if (oc){
				switch(oc){

					case '1':
						offset = REM_MINUS;
						break;
						
					case '2':
						offset = REM_SIMPLEX;
						break;
						
					case '3':
						offset = REM_PLUS;
						break;
						
					default:
	
						return DC_ERROR;
				} 
			} 
			
			offsave = myrpt->offset;
			strncpy(savestr, myrpt->freq, sizeof(savestr) - 1);
			strncpy(myrpt->freq, freq, sizeof(myrpt->freq) - 1);
			
			if(debug)
				printf("@@@@ Frequency entered: %s\n", myrpt->freq);
	
			
			strncpy(myrpt->freq, freq, sizeof(myrpt->freq) - 1);
			myrpt->offset = offset;
	
			if (setrbi(myrpt) == -1){
				myrpt->offset = offsave;
				strncpy(myrpt->freq, savestr, sizeof(myrpt->freq) - 1);
				return DC_ERROR;
			}

			return DC_COMPLETE;
		
		case 3: /* set rx PL tone */
			
	    		for(i = 0, j = 0, k = 0, l = 0 ; digitbuf[i] ; i++){ /* look for N+*N */
				if(digitbuf[i] == '*'){
					j++;
					continue;
				}
				if((digitbuf[i] < '0') || (digitbuf[i] > '9'))
					return DC_ERROR;
				else{
					if(j)
						l++;
					else
						k++;
				}
			}
			if((j > 1) || (k > 3) || (l > 1))
				return DC_ERROR; /* &$@^! */
			i = strlen(digitbuf) - 1;
			if((j != 1) || (k < 2)|| (l != 1))
				break; /* Not yet */
			if(debug)
				printf("PL digits entered %s\n", digitbuf);
	    		
			strncpy(tmp, digitbuf, sizeof(tmp) - 1);
			/* see if we have at least 1 */
			s = strchr(tmp,'*');
			if(s)
				*s = '.';
			strncpy(savestr, myrpt->rxpl, sizeof(savestr) - 1);
			strncpy(myrpt->rxpl, tmp, sizeof(myrpt->rxpl) - 1);
			
			if (setrbi(myrpt) == -1){
				strncpy(myrpt->rxpl, savestr, sizeof(myrpt->rxpl) - 1);
				return DC_ERROR;
			}
		
		
			return DC_COMPLETE;
		
		case 100: /* other stuff */
		case 101: 
		case 102: 
		case 103: 
		case 104: 
		case 105: 
		case 106: 
			switch(myatoi(param)){
				case 100: /* RX PL Off */
					myrpt->rxplon = 0;
					break;
					
				case 101: /* RX PL On */
					myrpt->rxplon = 1;
					break;
					
				case 102: /* TX PL Off */
					myrpt->txplon = 0;
					break;
					
				case 103: /* TX PL On */
					myrpt->txplon = 1;
					break;
					
				case 104: /* Low Power */
					myrpt->powerlevel = REM_LOWPWR;
					break;
					
				case 105: /* Medium Power */
					myrpt->powerlevel = REM_MEDPWR;
					break;
					
				case 106: /* Hi Power */
					myrpt->powerlevel = REM_HIPWR;
					break;
				default:
					return DC_ERROR;
			}
			if (setrbi(myrpt) == -1) 
				return DC_ERROR;
			return DC_COMPLETE;
		case 5: /* Status */
			myrpt->remotetx = 0;
			ast_indicate(myrpt->txchannel,AST_CONTROL_RADIO_UNKEY);
			if (!myrpt->remoterx){
				ast_indicate(mychannel,AST_CONTROL_RADIO_KEY);
			}
			
			if (ast_safe_sleep(mychannel,1000) == -1)
					return DC_ERROR;
			
			if ((sayfile(mychannel,"rpt/node") == -1) ||
			(saycharstr(mychannel,myrpt->name) == -1) ||
		 	(sayfile(mychannel,"rpt/frequency") == -1) ||
		 	(saycharstr(mychannel,myrpt->freq) == -1)){
			
				if (!myrpt->remoterx){
		
					ast_indicate(mychannel,AST_CONTROL_RADIO_UNKEY);
				}
				return DC_ERROR;
			}
			switch(myrpt->offset){
	
				case REM_MINUS:
					res = sayfile(mychannel,"rpt/minus");
					break;
				
				case REM_SIMPLEX:
					res = sayfile(mychannel,"rpt/simplex");
					break;
					
				case REM_PLUS:
					res = sayfile(mychannel,"rpt/plus");
					break;
					
				default:
					return DC_ERROR;
			}
			if (res == -1){
		
				if (!myrpt->remoterx){
		
					ast_indicate(mychannel,AST_CONTROL_RADIO_UNKEY);
				}
				return -1;
			}
			switch(myrpt->powerlevel){

				case REM_LOWPWR:
					res = sayfile(mychannel,"rpt/lopwr") ;
					break;
					
				case REM_MEDPWR:
					res = sayfile(mychannel,"rpt/medpwr");
					break;
				case REM_HIPWR:
					res = sayfile(mychannel,"rpt/hipwr"); 
					break;
			}
			if (res || (sayfile(mychannel,"rpt/rxpl") == -1) ||
				(sayfile(mychannel,"rpt/frequency") == -1) ||
				(saycharstr(mychannel,myrpt->rxpl) == -1) ||
				(sayfile(mychannel,"rpt/txpl") == -1) ||
				(sayfile(mychannel,((myrpt->txplon) ? "rpt/on" : "rpt/off")) == -1) ||
				(sayfile(mychannel,"rpt/rxpl") == -1) ||
				(sayfile(mychannel,((myrpt->rxplon) ? "rpt/on" : "rpt/off")) == -1)){
				if (!myrpt->remoterx){
					ast_indicate(mychannel,AST_CONTROL_RADIO_UNKEY);
				}
				return -1;
			}
			if (!myrpt->remoterx){
				ast_indicate(mychannel,AST_CONTROL_RADIO_UNKEY);
			}
			return DC_COMPLETE;
			
	    	default:
			return DC_ERROR;
	}

	return DC_INDETERMINATE;
}

	
/*
* Collect digits one by one until something matches
*/

static int collect_function_digits(struct rpt *myrpt, char *digits, int command_source)
{
	int i;
	char *stringp,*action,*param,*functiondigits;
	char function_table_name[30] = "";
	char workstring[80];
	
	struct ast_variable *vp;
	
	if(debug)	
		printf("@@@@ Digits collected: %s, source: %d\n", digits, command_source);
	
	if (command_source == SOURCE_LNK)
		strncpy(function_table_name, myrpt->link_functions, sizeof(function_table_name) - 1);
	else
		strncpy(function_table_name, myrpt->functions, sizeof(function_table_name) - 1);
	vp = ast_variable_browse(cfg, function_table_name);
	while(vp) {
		if(!strncasecmp(vp->name, digits, strlen(vp->name)))
			break;
		vp = vp->next;
	}	
	if(!vp) {
		if(strlen(digits) >= ((command_source == SOURCE_LNK) ? 
		    myrpt->link_longestfunc : myrpt->longestfunc)) /* Get out of function mode if longes func length reached */
			return DC_ERROR;
		else
			return DC_INDETERMINATE;
	}	
	/* Found a match, retrieve value part and parse */
	strncpy(workstring, vp->value, sizeof(workstring) - 1 );
	stringp = workstring;
	action = strsep(&stringp, ",");
	param = stringp;
	if(debug)
		printf("@@@@ action: %s, param = %s\n",action, (param) ? param : "(null)");
	/* Look up the action */
	for(i = 0 ; i < (sizeof(function_table)/sizeof(struct function_table_tag)); i++){
		if(!strncasecmp(action, function_table[i].action, strlen(action)))
			break;
	}
	if(debug)
		printf("@@@@ table index i = %d\n",i);
	if(i == (sizeof(function_table)/sizeof(struct function_table_tag))){
		/* Error, action not in table */
		return DC_ERROR;
	}
	if(function_table[i].function == NULL){
		/* Error, function undefined */
		if(debug)
			printf("@@@@ NULL for action: %s\n",action);
		return DC_ERROR;
	}
	functiondigits = digits + strlen(vp->name);
	return (*function_table[i].function)(myrpt, param, functiondigits, command_source);
}


static void handle_link_data(struct rpt *myrpt, struct rpt_link *mylink,
	char *str)
{
char	tmp[300],cmd[300] = "",dest[300],src[300],c;
int	seq, res;
struct rpt_link *l;
struct	ast_frame wf;

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
		if (!ast_canmatch_extension(myrpt->pchannel,myrpt->ourcontext,myrpt->exten,1,NULL)) 
		{
			/* call has failed, inform user */
			myrpt->callmode = 4;
		}
	}
	if ((myrpt->callmode == 2) || (myrpt->callmode == 3))
	{
		myrpt->mydtmf = c;
	}
	if (c == myrpt->funcchar)
	{
		myrpt->rem_dtmfidx = 0;
		myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx] = 0;
		time(&myrpt->rem_dtmf_time);
		ast_mutex_unlock(&myrpt->lock);
		return;
	} 
	else if ((c != myrpt->endchar) && (myrpt->rem_dtmfidx >= 0))
	{
		time(&myrpt->rem_dtmf_time);
		if (myrpt->rem_dtmfidx < MAXDTMF)
		{
			myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx++] = c;
			myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx] = 0;
			
			ast_mutex_unlock(&myrpt->lock);
			strncpy(cmd, myrpt->rem_dtmfbuf, sizeof(cmd) - 1);
			res = collect_function_digits(myrpt, cmd, SOURCE_LNK);
			ast_mutex_lock(&myrpt->lock);
			
			switch(res){
			
				case DC_INDETERMINATE:
					break;
				
				case DC_REQ_FLUSH:
					myrpt->rem_dtmfidx = 0;
					myrpt->rem_dtmfbuf[0] = 0;
					break;
				
				
				case DC_COMPLETE:
					myrpt->rem_dtmfbuf[0] = 0;
					myrpt->rem_dtmfidx = -1;
					myrpt->rem_dtmf_time = 0;
					break;
				
				case DC_ERROR:
				default:
					myrpt->rem_dtmfbuf[0] = 0;
					myrpt->rem_dtmfidx = -1;
					myrpt->rem_dtmf_time = 0;
					break;
			}
		}

	}
	ast_mutex_unlock(&myrpt->lock);
	return;
}

/* Doug Hall RBI-1 serial data definitions:
 *
 * Byte 0: Expansion external outputs 
 * Byte 1: 
 *	Bits 0-3 are BAND as follows:
 *	Bits 4-5 are POWER bits as follows:
 *		00 - Low Power
 *		01 - Hi Power
 *		02 - Med Power
 *	Bits 6-7 are always set
 * Byte 2:
 *	Bits 0-3 MHZ in BCD format
 *	Bits 4-5 are offset as follows:
 *		00 - minus
 *		01 - plus
 *		02 - simplex
 *		03 - minus minus (whatever that is)
 *	Bit 6 is the 0/5 KHZ bit
 *	Bit 7 is always set
 * Byte 3:
 *	Bits 0-3 are 10 KHZ in BCD format
 *	Bits 4-7 are 100 KHZ in BCD format
 * Byte 4: PL Tone code and encode/decode enable bits
 *	Bits 0-5 are PL tone code (comspec binary codes)
 *	Bit 6 is encode enable/disable
 *	Bit 7 is decode enable/disable
 */

/* take the frequency from the 10 mhz digits (and up) and convert it
   to a band number */

static int rbi_mhztoband(char *str)
{
int	i;

	i = atoi(str) / 10; /* get the 10's of mhz */
	switch(i)
	{
	    case 2:
		return 10;
	    case 5:
		return 11;
	    case 14:
		return 2;
	    case 22:
		return 3;
	    case 44:
		return 4;
	    case 124:
		return 0;
	    case 125:
		return 1;
	    case 126:
		return 8;
	    case 127:
		return 5;
	    case 128:
		return 6;
	    case 129:
		return 7;
	    default:
		break;
	}
	return -1;
}

/* take a PL frequency and turn it into a code */
static int rbi_pltocode(char *str)
{
int i;
char *s;

	s = strchr(str,'.');
	i = 0;
	if (s) i = atoi(s + 1);
	i += atoi(str) * 10;
	switch(i)
	{
	    case 670:
		return 0;
	    case 719:
		return 1;
	    case 744:
		return 2;
	    case 770:
		return 3;
	    case 797:
		return 4;
	    case 825:
		return 5;
	    case 854:
		return 6;
	    case 885:
		return 7;
	    case 915:
		return 8;
	    case 948:
		return 9;
	    case 974:
		return 10;
	    case 1000:
		return 11;
	    case 1035:
		return 12;
	    case 1072:
		return 13;
	    case 1109:
		return 14;
	    case 1148:
		return 15;
	    case 1188:
		return 16;
	    case 1230:
		return 17;
	    case 1273:
		return 18;
	    case 1318:
		return 19;
	    case 1365:
		return 20;
	    case 1413:
		return 21;
	    case 1462:
		return 22;
	    case 1514:
		return 23;
	    case 1567:
		return 24;
	    case 1622:
		return 25;
	    case 1679:
		return 26;
	    case 1738:
		return 27;
	    case 1799:
		return 28;
	    case 1862:
		return 29;
	    case 1928:
		return 30;
	    case 2035:
		return 31;
	    case 2107:
		return 32;
	    case 2181:
		return 33;
	    case 2257:
		return 34;
	    case 2336:
		return 35;
	    case 2418:
		return 36;
	    case 2503:
		return 37;
	}
	return -1;
}

/*
* Shift out a formatted serial bit stream
*/

static void rbi_out(struct rpt *myrpt,unsigned char *data)
    {
    int i,j;
    unsigned char od,d;
    static volatile long long delayvar;

    for(i = 0 ; i < 5 ; i++){
        od = *data++; 
        for(j = 0 ; j < 8 ; j++){
            d = od & 1;
            outb(d,myrpt->iobase);
	    /* >= 15 us */
	    for(delayvar = 1; delayvar < 15000; delayvar++); 
            od >>= 1;
            outb(d | 2,myrpt->iobase);
	    /* >= 30 us */
	    for(delayvar = 1; delayvar < 30000; delayvar++); 
            outb(d,myrpt->iobase);
	    /* >= 10 us */
	    for(delayvar = 1; delayvar < 10000; delayvar++); 
            }
        }
	/* >= 50 us */
        for(delayvar = 1; delayvar < 50000; delayvar++); 
    }

static int setrbi(struct rpt *myrpt)
{
char tmp[MAXREMSTR] = "",rbicmd[5],*s;
int	band,txoffset = 0,txpower = 0,rxpl;

	
	strncpy(tmp, myrpt->freq, sizeof(tmp) - 1);
	s = strchr(tmp,'.');
	/* if no decimal, is invalid */
	
	if (s == NULL){
		if(debug)
			printf("@@@@ Frequency needs a decimal\n");
		return -1;
	}
	
	*s++ = 0;
	if (strlen(tmp) < 2){
		if(debug)
			printf("@@@@ Bad MHz digits: %s\n", tmp);
	 	return -1;
	}
	 
	if (strlen(s) < 3){
		if(debug)
			printf("@@@@ Bad KHz digits: %s\n", s);
	 	return -1;
	}

	if ((s[2] != '0') && (s[2] != '5')){
		if(debug)
			printf("@@@@ KHz must end in 0 or 5: %c\n", s[2]);
	 	return -1;
	}
	 
	band = rbi_mhztoband(tmp);
	if (band == -1){
		if(debug)
			printf("@@@@ Bad Band: %s\n", tmp);
	 	return -1;
	}
	
	rxpl = rbi_pltocode(myrpt->rxpl);
	
	if (rxpl == -1){
		if(debug)
			printf("@@@@ Bad RX PL: %s\n", myrpt->rxpl);
	 	return -1;
	}

	
	switch(myrpt->offset)
	{
	    case REM_MINUS:
		txoffset = 0;
		break;
	    case REM_PLUS:
		txoffset = 0x10;
		break;
	    case REM_SIMPLEX:
		txoffset = 0x20;
		break;
	}
	switch(myrpt->powerlevel)
	{
	    case REM_LOWPWR:
		txpower = 0;
		break;
	    case REM_MEDPWR:
		txpower = 0x20;
		break;
	    case REM_HIPWR:
		txpower = 0x10;
		break;
	}
	rbicmd[0] = 0;
	rbicmd[1] = band | txpower | 0xc0;
	rbicmd[2] = (*(s - 2) - '0') | txoffset | 0x80;
	if (s[2] == '5') rbicmd[2] |= 0x40;
	rbicmd[3] = ((*s - '0') << 4) + (s[1] - '0');
	rbicmd[4] = rxpl;
	if (myrpt->txplon) rbicmd[4] |= 0x40;
	if (myrpt->rxplon) rbicmd[4] |= 0x80;
	rbi_out(myrpt,rbicmd);
	return 0;
}



static int handle_remote_dtmf_digit(struct rpt *myrpt,char c)
{
time_t	now;
int	ret,res = 0;

	time(&now);
	/* if timed-out */
	if ((myrpt->dtmf_time_rem + DTMF_TIMEOUT) < now)
	{
		myrpt->dtmfidx = -1;
		myrpt->dtmfbuf[0] = 0;
		myrpt->dtmf_time_rem = 0;
	}
	/* if decode not active */
	if (myrpt->dtmfidx == -1)
	{
		/* if not lead-in digit, dont worry */
		if (c != myrpt->funcchar) return 0;
		myrpt->dtmfidx = 0;
		myrpt->dtmfbuf[0] = 0;
		myrpt->dtmf_time_rem = now;
		return 0;
	}
	/* if too many in buffer, start over */
	if (myrpt->dtmfidx >= MAXDTMF)
	{
		myrpt->dtmfidx = 0;
		myrpt->dtmfbuf[0] = 0;
		myrpt->dtmf_time_rem = now;
	}
	if (c == myrpt->funcchar)
	{
		/* if star at beginning, or 2 together, erase buffer */
		if ((myrpt->dtmfidx < 1) || 
			(myrpt->dtmfbuf[myrpt->dtmfidx - 1] == myrpt->funcchar))
		{
			myrpt->dtmfidx = 0;
			myrpt->dtmfbuf[0] = 0;
			myrpt->dtmf_time_rem = now;
			return 0;
		}
	}
	myrpt->dtmfbuf[myrpt->dtmfidx++] = c;
	myrpt->dtmfbuf[myrpt->dtmfidx] = 0;
	myrpt->dtmf_time_rem = now;
	
	
	ret = collect_function_digits(myrpt, myrpt->dtmfbuf, SOURCE_RMT);
	
	switch(ret){
	
		case DC_INDETERMINATE:
			res = 0;
			break;
				
		case DC_REQ_FLUSH:
			myrpt->dtmfidx = 0;
			myrpt->dtmfbuf[0] = 0;
			res = 0;
			break;
				
				
		case DC_COMPLETE:
			myrpt->dtmfbuf[0] = 0;
			myrpt->dtmfidx = -1;
			myrpt->dtmf_time_rem = 0;
			res = 1;
			break;
				
		case DC_ERROR:
		default:
			myrpt->dtmfbuf[0] = 0;
			myrpt->dtmfidx = -1;
			myrpt->dtmf_time_rem = 0;
			res = 0;
			break;
	}

	return res;
}

static int handle_remote_data(struct rpt *myrpt, char *str)
{
char	tmp[300],cmd[300],dest[300],src[300],c;
int	seq,res;

 	/* put string in our buffer */
	strncpy(tmp,str,sizeof(tmp) - 1);
	if (sscanf(tmp,"%s %s %s %d %c",cmd,dest,src,&seq,&c) != 5)
	{
		ast_log(LOG_WARNING, "Unable to parse link string %s\n",str);
		return 0;
	}
	if (strcmp(cmd,"D"))
	{
		ast_log(LOG_WARNING, "Unable to parse link string %s\n",str);
		return 0;
	}
	/* if not for me, ignore */
	if (strcmp(dest,myrpt->name)) return 0;
	res = handle_remote_dtmf_digit(myrpt,c);
	if (res != 1)
		return res;
	myrpt->remotetx = 0;
	ast_indicate(myrpt->txchannel,AST_CONTROL_RADIO_UNKEY);
	if (!myrpt->remoterx)
	{
		ast_indicate(myrpt->remchannel,AST_CONTROL_RADIO_KEY);
	}
	if (ast_safe_sleep(myrpt->remchannel,1000) == -1) return -1;
	res = telem_lookup(myrpt->remchannel,"functcomplete");
	if (!myrpt->remoterx)
	{
		ast_indicate(myrpt->remchannel,AST_CONTROL_RADIO_UNKEY);
	}
	return res;
}

/* single thread with one file (request) to dial */
static void *rpt(void *this)
{
struct	rpt *myrpt = (struct rpt *)this;
char *tele,*idtalkover;
int ms = MSWAIT,lasttx,keyed,val,remrx,identqueued,nonidentqueued,res;
struct ast_channel *who;
ZT_CONFINFO ci;  /* conference info */
time_t	dtmf_time,t;
struct rpt_link *l,*m;
struct rpt_tele *telem;
pthread_attr_t attr;
char cmd[MAXDTMF+1] = "";


	ast_mutex_lock(&myrpt->lock);
	tele = strchr(myrpt->rxchanname,'/');
	if (!tele)
	{
		fprintf(stderr,"rpt:Dial number (%s) must be in format tech/number\n",myrpt->rxchanname);
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
			fprintf(stderr,"rpt:Dial number (%s) must be in format tech/number\n",myrpt->txchanname);
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
	myrpt->idtimer = myrpt->politeid;
	myrpt->mustid = 0;
	myrpt->callmode = 0;
	myrpt->tounkeyed = 0;
	myrpt->tonotify = 0;
	lasttx = 0;
	keyed = 0;
	idtalkover = ast_variable_retrieve(cfg, myrpt->name, "idtalkover");
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
		myrpt->localtx = keyed && (myrpt->dtmfidx == -1) && (!myrpt->cmdnode[0]);
		l = myrpt->links.next;
		remrx = 0;
		while(l != &myrpt->links)
		{
			if (l->lastrx) remrx = 1;
			l = l->next;
		}
		
		/* Create a "must_id" flag for the cleanup ID */	
			
		myrpt->mustid |= (myrpt->idtimer) && (keyed || remrx) ;

		/* Build a fresh totx from keyed and autopatch activated */
		
		totx = myrpt->localtx || myrpt->callmode;
		 
		/* Traverse the telemetry list to see if there's an ID queued and if there is not an ID queued */
		
		identqueued = 0;
		nonidentqueued = 0;
		
		telem = myrpt->tele.next;
		while(telem != &myrpt->tele)
		{
			if((telem->mode == ID) || (telem->mode == IDTALKOVER)){
				identqueued = 1;
			}
			else
				nonidentqueued = 1;
			telem = telem->next;
		}
	
		/* Add in any non-id telemetry */
		
		totx = totx || nonidentqueued;
		
		/* Update external transmitter PTT state with everything but ID telemetry */
		
		myrpt->exttx = totx;
		
		/* Add in ID telemetry to local transmitter */
		
		totx = totx || remrx || identqueued;
		
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
		if ((!totx) && (!myrpt->totimer) && myrpt->tounkeyed && myrpt->localtx)
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
		/* If user keys up or is keyed up over standard ID, switch to talkover ID, if one is defined */
		if (identqueued && keyed && idtalkover) {
			int hasid = 0,hastalkover = 0;

			telem = myrpt->tele.next;
			while(telem != &myrpt->tele){
				if(telem->mode == ID){
					if (telem->chan) ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV); /* Whoosh! */
					hasid = 1;
				}
				if (telem->mode == IDTALKOVER) hastalkover = 1;
				telem = telem->next;
			}
			ast_mutex_unlock(&myrpt->lock);
			if (hasid && (!hastalkover)) rpt_telemetry(myrpt, IDTALKOVER, NULL); /* Start Talkover ID */
			ast_mutex_lock(&myrpt->lock);
		}
		/* Try to be polite */
		/* If the repeater has been inactive for longer than the ID time, do an initial ID in the tail*/
		/* If within 30 seconds of the time to ID, try do it in the tail */
		/* else if at ID time limit, do it right over the top of them */
		/* Lastly, if the repeater has been keyed, and the ID timer is expired, do a clean up ID */
		if (((totx && (!myrpt->exttx) && (myrpt->idtimer <= myrpt->politeid) && myrpt->tailtimer)) ||
		   (myrpt->mustid && (!myrpt->idtimer)))
		{
			myrpt->mustid = 0;
			myrpt->idtimer = myrpt->idtime; /* Reset our ID timer */
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
				if (!myrpt->localtx)
					memset(f->data,0,f->datalen);
				ast_write(myrpt->pchannel,f);
			}
			else if (f->frametype == AST_FRAME_DTMF)
			{
				char c;

				c = (char) f->subclass; /* get DTMF char */
				ast_frfree(f);
				if (c == myrpt->endchar)
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
					if (c == myrpt->funcchar)
					{
						myrpt->dtmfidx = 0;
						myrpt->dtmfbuf[myrpt->dtmfidx] = 0;
						time(&dtmf_time);
						continue;
					} 
					else if ((c != myrpt->endchar) && (myrpt->dtmfidx >= 0))
					{
						time(&dtmf_time);
						if (myrpt->dtmfidx < MAXDTMF)
						{
							myrpt->dtmfbuf[myrpt->dtmfidx++] = c;
							myrpt->dtmfbuf[myrpt->dtmfidx] = 0;
							
							strncpy(cmd, myrpt->dtmfbuf, sizeof(cmd) - 1);
							ast_mutex_unlock(&myrpt->lock);
							
							res = collect_function_digits(myrpt, cmd, SOURCE_RPT);

							ast_mutex_lock(&myrpt->lock);
							switch(res){
			
								case DC_INDETERMINATE:
									break;
				
								case DC_REQ_FLUSH:
									myrpt->dtmfidx = 0;
									myrpt->dtmfbuf[0] = 0;
									break;
				
				
								case DC_COMPLETE:
									myrpt->dtmfbuf[0] = 0;
									myrpt->dtmfidx = -1;
									dtmf_time = 0;
									break;
				
								case DC_ERROR:
								default:
									myrpt->dtmfbuf[0] = 0;
									myrpt->dtmfidx = -1;
									dtmf_time = 0;
									break;
							}
							ast_mutex_unlock(&myrpt->lock);
							if(res != DC_INDETERMINATE)	
								continue;
						}
					}
				}
				else
				{
					if ((!myrpt->callmode) && (c == myrpt->funcchar))
					{
						myrpt->callmode = 1;
						myrpt->cidx = 0;
						myrpt->exten[myrpt->cidx] = 0;
					        pthread_attr_init(&attr);
			 		        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
						ast_pthread_create(&myrpt->rpt_call_thread,&attr,rpt_call,(void *)myrpt);
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
				continue;
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
					if(keyed)
						rpt_telemetry(myrpt,UNKEY,NULL);
					keyed = 0;

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
					if (!strcmp(myrpt->cmdnode,l->name))
						myrpt->cmdnode[0] = 0;
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
						if (!strcmp(myrpt->cmdnode,l->name))
							myrpt->cmdnode[0] = 0;
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
struct ast_variable *vp;
int	i,j,n,longestnode;

	/* start with blank config */
	memset(&rpt_vars,0,sizeof(rpt_vars));

	cfg = ast_load("rpt.conf");
	if (!cfg) {
		ast_log(LOG_NOTICE, "Unable to open radio repeater configuration rpt.conf.  Radio Repeater disabled.\n");
		pthread_exit(NULL);
	}

	/*
	* Go through the node list to determine the longest node
	*/
		longestnode = 0;

		vp = ast_variable_browse(cfg, NODES);
		
		while(vp){
			j = strlen(vp->name);
			if (j > longestnode)
				longestnode = j;
			vp = vp->next;
		}
	
	/* go thru all the specified repeaters */
	this = NULL;
	n = 0;
	while((this = ast_category_browse(cfg,this)) != NULL)
	{
	
		for(i = 0 ; i < strlen(this) ; i++){
			if((this[i] < '0') || (this[i] > '9'))
				break;
		}
		if(i != strlen(this))
			continue; /* Not a node defn */
			
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
		
		rpt_vars[n].idtime = retrieve_astcfgint( this, "idtime", 60000, 2400000, IDTIME);	/* Enforce a min max */
		rpt_vars[n].politeid = retrieve_astcfgint( this, "politeid", 30000, 300000, POLITEID); /* Enforce a min max */
		
		val = ast_variable_retrieve(cfg,this,"remote");
		if (val) rpt_vars[n].remote = ast_true(val); 
			else rpt_vars[n].remote = 0;
		rpt_vars[n].tonezone = ast_variable_retrieve(cfg,this,"tonezone");
		val = ast_variable_retrieve(cfg,this,"iobase");
		if (val) rpt_vars[n].iobase = atoi(val);
		else rpt_vars[n].iobase = DEFAULT_IOBASE;
		rpt_vars[n].simple = 0;
		rpt_vars[n].functions = ast_variable_retrieve(cfg,this,"functions");
		if (!rpt_vars[n].functions) 
		{
			rpt_vars[n].functions = FUNCTIONS;
			rpt_vars[n].simple = 1;
		}
		rpt_vars[n].link_functions = ast_variable_retrieve(cfg,this,"link_functions");
		if (!rpt_vars[n].link_functions) 
			rpt_vars[n].link_functions = rpt_vars[n].functions;
		val = ast_variable_retrieve(cfg,this,"funcchar");
		if (!val) rpt_vars[n].funcchar = FUNCCHAR; else 
			rpt_vars[n].funcchar = *val;		
		val = ast_variable_retrieve(cfg,this,"endchar");
		if (!val) rpt_vars[n].endchar = ENDCHAR; else 
			rpt_vars[n].endchar = *val;		
		n++;
	}
	nrpts = n;
	ast_log(LOG_DEBUG, "Total of %d repeaters configured.\n",n);
	/* start em all */
	for(i = 0; i < n; i++)
	{
		rpt_vars[i].longestnode = longestnode;
		
		/*
		* For this repeater, Determine the length of the longest function 
		*/
		rpt_vars[i].longestfunc = 0;
		vp = ast_variable_browse(cfg, rpt_vars[i].functions);
		while(vp){
			j = strlen(vp->name);
			if (j > rpt_vars[i].longestfunc)
				rpt_vars[i].longestfunc = j;
			vp = vp->next;
		}
		/*
		* For this repeater, Determine the length of the longest function 
		*/
		rpt_vars[i].link_longestfunc = 0;
		vp = ast_variable_browse(cfg, rpt_vars[i].link_functions);
		while(vp){
			j = strlen(vp->name);
			if (j > rpt_vars[i].link_longestfunc)
				rpt_vars[i].link_longestfunc = j;
			vp = vp->next;
		}
		if (!rpt_vars[i].rxchanname)
		{
			ast_log(LOG_WARNING,"Did not specify rxchanname for node %s\n",rpt_vars[i].name);
			pthread_exit(NULL);
		}
		/* if is a remote, dont start one for it */
		if (rpt_vars[i].remote)
		{
			strncpy(rpt_vars[i].freq, "146.460", sizeof(rpt_vars[i].freq) - 1);
			strncpy(rpt_vars[i].rxpl, "100.0", sizeof(rpt_vars[i].rxpl) - 1);
			rpt_vars[i].offset = REM_SIMPLEX;
			rpt_vars[i].powerlevel = REM_MEDPWR;
			continue;
		}
		if (!rpt_vars[i].ident)
		{
			ast_log(LOG_WARNING,"Did not specify ident for node %s\n",rpt_vars[i].name);
			pthread_exit(NULL);
		}
		ast_pthread_create(&rpt_vars[i].rpt_thread,NULL,rpt,(void *) &rpt_vars[i]);
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
			ast_log(LOG_WARNING, "Doesnt have callerid on %s\n",tmp);
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
	ast_mutex_lock(&myrpt->lock);
	/* if remote, error if anyone else already linked */
	if (myrpt->remoteon)
	{
		ast_mutex_unlock(&myrpt->lock);
		usleep(500000);
		ast_mutex_lock(&myrpt->lock);
		if (myrpt->remoteon)
		{
			ast_log(LOG_WARNING, "Trying to use busy link on %s\n",tmp);
			return -1;
		}		
	}
	myrpt->remoteon = 1;
	if (ioperm(myrpt->iobase,1,1) == -1)
	{
		ast_mutex_unlock(&myrpt->lock);
		ast_log(LOG_WARNING, "Cant get io permission on IO port %x hex\n",myrpt->iobase);
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
	myrpt->dtmfidx = -1;
	myrpt->dtmfbuf[0] = 0;
	myrpt->dtmf_time_rem = 0;
	ast_mutex_unlock(&myrpt->lock);
	setrbi(myrpt);
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
		
		
		if (rem_totx && (!myrpt->remotetx)) /* Remote base radio TX key */
		{
			myrpt->remotetx = 1;
			ast_indicate(myrpt->txchannel,AST_CONTROL_RADIO_KEY);
		}
		if ((!rem_totx) && myrpt->remotetx) /* Remote base radio TX unkey */
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
				myrpt->remchannel = chan; /* Save copy of channel */
				if (handle_remote_data(myrpt,f->data) == -1)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
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
	ast_pthread_create(&rpt_master_thread,NULL,rpt_master,NULL);
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
