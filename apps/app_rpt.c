/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2002-2005, Jim Dixon, WB6NIL
 *
 * Jim Dixon, WB6NIL <jim@lambdatel.com>
 * Serious contributions by Steve RoDgers, WA6ZFT <hwstar@rodgers.sdcoxmail.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*
 *
 * Radio Repeater / Remote Base program 
 *  version 0.37 11/3/05
 * 
 * See http://www.zapatatelephony.org/app_rpt.html
 *
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
 *  4 - Test Tone On
 *  5 - Dump System Variables on Console (debug)
 *  6 - PTT (phone mode only)
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
 *  1 - Recall Memory MM  (*000-*099) (Gets memory from rpt.conf)
 *  2 - Set VFO MMMMM*KKK*O   (Mhz digits, Khz digits, Offset)
 *  3 - Set Rx PL Tone HHH*D*
 *  4 - Set Tx PL Tone HHH*D* (Not currently implemented with DHE RBI-1)
 *  5 - Link Status (long)
 *  6 - Set operating mode M (FM, USB, LSB, AM, etc)
 *  100 - RX PL off (Default)
 *  101 - RX PL On
 *  102 - TX PL Off (Default)
 *  103 - TX PL On
 *  104 - Low Power
 *  105 - Med Power
 *  106 - Hi Power
 *  107 - Bump Down 20 Hz
 *  108 - Bump Down 100 Hz
 *  109 - Bump Down 500 Hz
 *  110 - Bump Up 20 Hz
 *  111 - Bump Up 100 Hz
 *  112 - Bump Up 500 Hz
 *  113 - Scan Down Slow
 *  114 - Scan Down Medium
 *  115 - Scan Down Fast
 *  116 - Scan Up Slow
 *  117 - Scan Up Medium
 *  118 - Scan Up Fast
 *  119 - Transmit allowing auto-tune
 *  140 - Link Status (brief)
 *
 *
*/

/* The following is JUST GROSS!! There is some soft of underlying problem,
   probably in channel_iax2.c, that causes an IAX2 connection to sometimes
   stop transmitting randomly. We have been working for weeks to try to
   locate it and fix it, but to no avail We finally decided to put our
   tail between our legs, and just make the radio system re-connect upon
   network failure. This just shouldnt have to be done. For normal operation,
   comment-out the following line */
#define	RECONNECT_KLUDGE 

/* maximum digits in DTMF buffer, and seconds after * for DTMF command timeout */

#define	MAXDTMF 32
#define	DTMF_TIMEOUT 3

#define	DISC_TIME 10000  /* report disc after 10 seconds of no connect */
#define	MAX_RETRIES 5

#define	REDUNDANT_TX_TIME 2000

#define	RETRY_TIMER_MS 5000

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

#define TELEPARAMSIZE 256

#define REM_SCANTIME 100


enum {REM_OFF,REM_MONITOR,REM_TX};

enum{ID,PROC,TERM,COMPLETE,UNKEY,REMDISC,REMALREADY,REMNOTFOUND,REMGO,
	CONNECTED,CONNFAIL,STATUS,TIMEOUT,ID1, STATS_TIME,
	STATS_VERSION, IDTALKOVER, ARB_ALPHA, TEST_TONE, REV_PATCH};

enum {REM_SIMPLEX,REM_MINUS,REM_PLUS};

enum {REM_LOWPWR,REM_MEDPWR,REM_HIPWR};

enum {DC_INDETERMINATE, DC_REQ_FLUSH, DC_ERROR, DC_COMPLETE, DC_DOKEY};

enum {SOURCE_RPT, SOURCE_LNK, SOURCE_RMT, SOURCE_PHONE, SOURCE_DPHONE};

enum {DLY_TELEM, DLY_ID, DLY_UNKEY, DLY_CALLTERM};

enum {REM_MODE_FM,REM_MODE_USB,REM_MODE_LSB,REM_MODE_AM};

enum {HF_SCAN_OFF,HF_SCAN_DOWN_SLOW,HF_SCAN_DOWN_QUICK,HF_SCAN_DOWN_FAST,HF_SCAN_UP_SLOW,HF_SCAN_UP_QUICK,HF_SCAN_UP_FAST};

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <signal.h>
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
#include <netinet/in.h>
#include <arpa/inet.h>

#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/callerid.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/features.h"
#include "asterisk/options.h"
#include "asterisk/cli.h"
#include "asterisk/config.h"
#include "asterisk/say.h"
#include "asterisk/localtime.h"

static  char *tdesc = "Radio Repeater / Remote Base  version 0.37  11/03/2005";

static char *app = "Rpt";

static char *synopsis = "Radio Repeater/Remote Base Control System";

static char *descrip = 
"  Rpt(nodename[|options]):  Radio Remote Link or Remote Base Link Endpoint Process.\n"
"\n"
"    Not specifying an option puts it in normal endpoint mode (where source\n"
"    IP and nodename are verified).\n"
"\n"
"    Options are as follows:\n"
"\n"
"        X - Normal endpoint mode WITHOUT security check. Only specify\n"
"            this if you have checked security already (like with an IAX2\n"
"            user/password or something).\n"
"\n"
"        Rannounce-string[|timeout[|timeout-destination]] - Amateur Radio\n"
"            Reverse Autopatch. Caller is put on hold, and announcement (as\n"
"            specified by the 'announce-string') is played on radio system.\n"
"            Users of radio system can access autopatch, dial specified\n"
"            code, and pick up call. Announce-string is list of names of\n"
"            recordings, or \"PARKED\" to substitute code for un-parking,\n"
"            or \"NODE\" to substitute node number.\n"
"\n"
"        P - Phone Control mode. This allows a regular phone user to have\n"
"            full control and audio access to the radio system. For the\n"
"            user to have DTMF control, the 'phone_functions' parameter\n"
"            must be specified for the node in 'rpt.conf'. An additional\n"
"            function (cop,6) must be listed so that PTT control is available.\n"
"\n"
"        D - Dumb Phone Control mode. This allows a regular phone user to\n"
"            have full control and audio access to the radio system. In this\n"
"            mode, the PTT is activated for the entire length of the call.\n"
"            For the user to have DTMF control (not generally recomended in\n"
"            this mode), the 'dphone_functions' parameter must be specified\n"
"            for the node in 'rpt.conf'. Otherwise no DTMF control will be\n"
"            available to the phone user.\n"
"\n";

static int debug = 0;  /* Set this >0 for extra debug output */
static int nrpts = 0;

char *discstr = "!!DISCONNECT!!";
static char *remote_rig_ft897="ft897";
static char *remote_rig_rbi="rbi";

struct	ast_config *cfg;

STANDARD_LOCAL_USER;
LOCAL_USER_DECL;

#define	MSWAIT 200
#define	HANGTIME 5000
#define	TOTIME 180000
#define	IDTIME 300000
#define	MAXRPTS 20
#define POLITEID 30000
#define FUNCTDELAY 1500

static  pthread_t rpt_master_thread;

struct rpt;

struct rpt_link
{
	struct rpt_link *next;
	struct rpt_link *prev;
	char	mode;			/* 1 if in tx mode */
	char	isremote;
	char	phonemode;
	char	name[MAXNODESTR];	/* identifier (routing) string */
	char	lasttx;
	char	lastrx;
	char	connected;
	char	hasconnected;
	char	outbound;
	char	disced;
	char	killme;
	long	elaptime;
	long	disctime;
	long 	retrytimer;
	long	retxtimer;
	int	retries;
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
	int (*function)(struct rpt *myrpt, char *param, char *digitbuf, 
		int command_source, struct rpt_link *mylink);
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
	char *phone_functions;
	char *dphone_functions;
	char *nodes;
	struct rpt_link links;
	int hangtime;
	int totime;
	int idtime;
	int unkeytocttimer;
	char keyed;
	char exttx;
	char localtx;
	char remoterx;
	char remotetx;
	char remoteon;
	char simple;
	char *remote;
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
	int tailtimer,totimer,idtimer,txconf,conf,callmode,cidx,scantimer;
	int mustid;
	int politeid;
	int dtmfidx,rem_dtmfidx;
	long	retxtimer;
	char mydtmf;
	int iobase;
	char exten[AST_MAX_EXTENSION];
	char freq[MAXREMSTR],rxpl[MAXREMSTR],txpl[MAXREMSTR];
	char offset;
	char powerlevel;
	char txplon;
	char rxplon;
	char remmode;
	char tunerequest;
	char hfscanmode;
	int hfscanstatus;
	char lastlinknode[MAXNODESTR];
	char funcchar;
	char endchar;
	char stopgen;
	int phone_longestfunc;
	int dphone_longestfunc;
	int link_longestfunc;
	int longestfunc;
	int longestnode;
	int threadrestarts;		
	time_t disgorgetime;
	time_t lastthreadrestarttime;
	char	nobusyout;
} rpt_vars[MAXRPTS];	

/*
* CLI extensions
*/

/* Debug mode */
static int rpt_do_debug(int fd, int argc, char *argv[]);

static char debug_usage[] =
"Usage: rpt debug level {0-7}\n"
"       Enables debug messages in app_rpt\n";
                                                                                                                                
static struct ast_cli_entry  cli_debug =
        { { "rpt", "debug", "level" }, rpt_do_debug, "Enable app_rpt debugging", debug_usage };



/*
* Telemetry defaults
*/


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

static int function_ilink(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);
static int function_autopatchup(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);
static int function_autopatchdn(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);
static int function_status(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);
static int function_cop(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);
static int function_remote(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);
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
	/* leave this %i alone, non-base-10 input is useful here */
	if (sscanf(str,"%i",&ret) != 1) return -1;
	return ret;
}

/*
* Enable or disable debug output at a given level at the console
*/
                                                                                                                                 
static int rpt_do_debug(int fd, int argc, char *argv[])
{
	int newlevel;

        if (argc != 4)
                return RESULT_SHOWUSAGE;
        newlevel = myatoi(argv[3]);
        if((newlevel < 0) || (newlevel > 7))
                return RESULT_SHOWUSAGE;
        if(newlevel)
                ast_cli(fd, "app_rpt Debugging enabled, previous level: %d, new level: %d\n", debug, newlevel);
        else
                ast_cli(fd, "app_rpt Debugging disabled\n");

        debug = newlevel;                                                                                                                          
        return RESULT_SUCCESS;
}
                                                                                                                                 


static int play_tone_pair(struct ast_channel *chan, int f1, int f2, int duration, int amplitude)
{
	int res;

        if ((res = ast_tonepair_start(chan, f1, f2, duration, amplitude)))
                return res;
                                                                                                                                            
        while(chan->generatordata) {
		if (ast_safe_sleep(chan,1)) return -1;
	}

        return 0;
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
	int i;
	int flags;
			
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
	
	/*
	* Wait for the zaptel driver to physically write the tone blocks to the hardware
	*/

	for(i = 0; i < 20 ; i++){
		flags =  ZT_IOMUX_WRITEEMPTY | ZT_IOMUX_NOWAIT; 
		res = ioctl(chan->fds[0], ZT_IOMUX, &flags);
		if(flags & ZT_IOMUX_WRITEEMPTY)
			break;
		if( ast_safe_sleep(chan, 50)){
			res = -1;
			break;
		}
	}

	
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
	int i;
	int flags;
	
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

	/*
	* Wait for the zaptel driver to physically write the tone blocks to the hardware
	*/

	for(i = 0; i < 20 ; i++){
		flags =  ZT_IOMUX_WRITEEMPTY | ZT_IOMUX_NOWAIT; 
		res = ioctl(chan->fds[0], ZT_IOMUX, &flags);
		if(flags & ZT_IOMUX_WRITEEMPTY)
			break;
		if( ast_safe_sleep(chan, 50)){
			res = -1;
			break;
		}
	}
		
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

static int saynum(struct ast_channel *mychannel, int num)
{
	int res;
	res = ast_say_number(mychannel, num, NULL, mychannel->language, NULL);
	if(!res)
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

static int telem_lookup(struct ast_channel *chan, char *node, char *name)
{
	
	int res;
	int i;
	char *entry;
	char *telemetry;
	char *telemetry_save;

	res = 0;
	telemetry_save = NULL;
	entry = NULL;
	
	
	/* Retrieve the section name for telemetry from the node section */
	
	telemetry = ast_variable_retrieve(cfg, node, TELEMETRY);
	if(telemetry){
		telemetry_save = ast_strdupa(telemetry);
		if(!telemetry_save){
			ast_log(LOG_WARNING,"ast_strdupa() failed in telem_lookup()\n");
			return res;
		}
		entry = ast_variable_retrieve(cfg, telemetry_save, name);
	}
	
	/* Try to look up the telemetry name */
	
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

/*
* Retrieve a wait interval
*/

static int get_wait_interval(struct rpt *myrpt, int type)
{
        int interval;
        char *wait_times;
        char *wait_times_save;
                                                                                                                  
        wait_times_save = NULL;
        wait_times = ast_variable_retrieve(cfg, myrpt->name, "wait_times");
                                                                                                                  
        if(wait_times){
                wait_times_save = ast_strdupa(wait_times);
                if(!wait_times_save){
                        ast_log(LOG_WARNING, "Out of memory in wait_interval()\n");
                        wait_times = NULL;
                }
        }
                                                                                                                  
        switch(type){
                case DLY_TELEM:
                        if(wait_times)
                                interval = retrieve_astcfgint(wait_times_save, "telemwait", 500, 5000, 1000);
                        else
                                interval = 1000;
                        break;
                                                                                                                  
                case DLY_ID:
                        if(wait_times)
                                interval = retrieve_astcfgint(wait_times_save, "idwait",250,5000,500);
                        else
                                interval = 500;
                        break;
                                                                                                                  
                case DLY_UNKEY:
                        if(wait_times)
                                interval = retrieve_astcfgint(wait_times_save, "unkeywait",500,5000,1000);
                        else
                                interval = 1000;
                        break;
                                                                                                                  
                case DLY_CALLTERM:
                        if(wait_times)
                                interval = retrieve_astcfgint(wait_times_save, "calltermwait",500,5000,1500);
                        else
                                interval = 1500;
                        break;
                                                                                                                  
                default:
                        return 0;
        }
	return interval;
}                                                                                                                  


/*
* Wait a configurable interval of time 
*/


static void wait_interval(struct rpt *myrpt, int type, struct ast_channel *chan)
{
	int interval;
	if((interval = get_wait_interval(myrpt, type)))
		ast_safe_sleep(chan,interval);
	return;
}


static void *rpt_tele_thread(void *this)
{
ZT_CONFINFO ci;  /* conference info */
int	res = 0,haslink,hastx,hasremote,imdone = 0, unkeys_queued, x;
struct	rpt_tele *mytele = (struct rpt_tele *)this;
struct  rpt_tele *tlist;
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
	mychannel = ast_request("zap",AST_FORMAT_SLINEAR,"pseudo",NULL);
	if (!mychannel)
	{
		fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
		ast_mutex_lock(&myrpt->lock);
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
		wait_interval(myrpt, (mytele->mode == ID) ? DLY_ID : DLY_TELEM,mychannel);
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
		wait_interval(myrpt, DLY_TELEM, mychannel);
		res = ast_streamfile(mychannel, "rpt/callproceeding", mychannel->language);
		break;
	    case TERM:
		/* wait a little bit longer */
		wait_interval(myrpt, DLY_CALLTERM, mychannel);
		res = ast_streamfile(mychannel, "rpt/callterminated", mychannel->language);
		break;
	    case COMPLETE:
		/* wait a little bit */
		wait_interval(myrpt, DLY_TELEM, mychannel);
		res = telem_lookup(mychannel, myrpt->name, "functcomplete");
		break;
	    case UNKEY:

		/*
		* Reset the Unkey to CT timer
		*/

		x = get_wait_interval(myrpt, DLY_UNKEY);
		ast_mutex_lock(&myrpt->lock);
		myrpt->unkeytocttimer = x; /* Must be protected as it is changed below */
		ast_mutex_unlock(&myrpt->lock);

		/*
		* If there's one already queued, don't do another
		*/

		tlist = myrpt->tele.next;
		unkeys_queued = 0;
                if (tlist != &myrpt->tele)
                {
                        ast_mutex_lock(&myrpt->lock);
                        while(tlist != &myrpt->tele){
                                if (tlist->mode == UNKEY) unkeys_queued++;
                                tlist = tlist->next;
                        }
                        ast_mutex_unlock(&myrpt->lock);
		}
		if( unkeys_queued > 1){
			imdone = 1;
			break;
		}

		/* Wait for the telemetry timer to expire */
		/* Periodically check the timer since it can be re-initialized above */

		while(myrpt->unkeytocttimer)
		{
			int ctint;
			if(myrpt->unkeytocttimer > 100)
				ctint = 100;
			else
				ctint = myrpt->unkeytocttimer;
			ast_safe_sleep(mychannel, ctint);
			ast_mutex_lock(&myrpt->lock);
			if(myrpt->unkeytocttimer < ctint)
				myrpt->unkeytocttimer = 0;
			else
				myrpt->unkeytocttimer -= ctint;
			ast_mutex_unlock(&myrpt->lock);
		}
	

		/*
		* Now, the carrier on the rptr rx should be gone. 
		* If it re-appeared, then forget about sending the CT
		*/
		if(myrpt->keyed){
			imdone = 1;
			break;
		}
			
		haslink = 0;
		hastx = 0;
		hasremote = 0;		
		l = myrpt->links.next;
		if (l != &myrpt->links)
		{
			ast_mutex_lock(&myrpt->lock);
			while(l != &myrpt->links)
			{
				if (l->name[0] == '0')
				{
					l = l->next;
					continue;
				}
				haslink = 1;
				if (l->mode) {
					hastx++;
					if (l->isremote) hasremote++;
				}
				l = l->next;
			}
			ast_mutex_unlock(&myrpt->lock);
		}
		if (haslink)
		{

			res = telem_lookup(mychannel, myrpt->name, (!hastx) ? "remotemon" : "remotetx");
			if(res)
				ast_log(LOG_WARNING, "telem_lookup:remotexx failed on %s\n", mychannel->name);
			
		
			/* if in remote cmd mode, indicate it */
			if (myrpt->cmdnode[0])
			{
				ast_safe_sleep(mychannel,200);
				res = telem_lookup(mychannel, myrpt->name, "cmdmode");
				if(res)
				 	ast_log(LOG_WARNING, "telem_lookup:cmdmode failed on %s\n", mychannel->name);
				ast_stopstream(mychannel);
			}
		}
		else if((ct = ast_variable_retrieve(cfg, nodename, "unlinkedct"))){ /* Unlinked Courtesy Tone */
			ct_copy = ast_strdupa(ct);
			res = telem_lookup(mychannel, myrpt->name, ct_copy);
			if(res)
			 	ast_log(LOG_WARNING, "telem_lookup:ctx failed on %s\n", mychannel->name);		
		}	
			
		if (hasremote && (!myrpt->cmdnode[0]))
		{
			/* set for all to hear */
			ci.chan = 0;
			ci.confno = myrpt->conf;
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
			if((ct = ast_variable_retrieve(cfg, nodename, "remotect"))){ /* Unlinked Courtesy Tone */
				ast_safe_sleep(mychannel,200);
				ct_copy = ast_strdupa(ct);
				res = telem_lookup(mychannel, myrpt->name, ct_copy);
				if(res)
				 	ast_log(LOG_WARNING, "telem_lookup:ctx failed on %s\n", mychannel->name);		
			}	
		}
		imdone = 1;
		break;
	    case REMDISC:
		/* wait a little bit */
		wait_interval(myrpt, DLY_TELEM, mychannel);
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
		wait_interval(myrpt, DLY_TELEM, mychannel);
		res = ast_streamfile(mychannel, "rpt/remote_already", mychannel->language);
		break;
	    case REMNOTFOUND:
		/* wait a little bit */
		wait_interval(myrpt, DLY_TELEM, mychannel);
		res = ast_streamfile(mychannel, "rpt/remote_notfound", mychannel->language);
		break;
	    case REMGO:
		/* wait a little bit */
		wait_interval(myrpt, DLY_TELEM, mychannel);
		res = ast_streamfile(mychannel, "rpt/remote_go", mychannel->language);
		break;
	    case CONNECTED:
		/* wait a little bit */
		wait_interval(myrpt, DLY_TELEM,  mychannel);
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
		wait_interval(myrpt, DLY_TELEM, mychannel);
		hastx = 0;
		linkbase.next = &linkbase;
		linkbase.prev = &linkbase;
		ast_mutex_lock(&myrpt->lock);
		/* make our own list of links */
		l = myrpt->links.next;
		while(l != &myrpt->links)
		{
			if (l->name[0] == '0')
			{
				l = l->next;
				continue;
			}
			m = malloc(sizeof(struct rpt_link));
			if (!m)
			{
				ast_log(LOG_WARNING, "Cannot alloc memory on %s\n", mychannel->name);
				ast_mutex_lock(&myrpt->lock);
				remque((struct qelem *)mytele);
				ast_mutex_unlock(&myrpt->lock);
				free(mytele);		
				ast_hangup(mychannel);
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
	    	wait_interval(myrpt, DLY_TELEM, mychannel); /* Wait a little bit */
		t = time(NULL);
		localtime_r(&t, &localtm);
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
    		wait_interval(myrpt, DLY_TELEM, mychannel); /* Wait a little bit */
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
	    	wait_interval(myrpt, DLY_TELEM, mychannel); /* Wait a little bit */
	    	if(mytele->param)
	    		saycharstr(mychannel, mytele->param);
	    	imdone = 1;
		break;
	    case REV_PATCH:
	    	wait_interval(myrpt, DLY_TELEM, mychannel); /* Wait a little bit */
	    	if(mytele->param) {

			/* Parts of this section taken from app_parkandannounce */
			char *tpl_working, *tpl_current;
			char *tmp[100], *myparm;
			int looptemp=0,i=0, dres = 0;
	

			tpl_working = strdupa(mytele->param);
			myparm = strsep(&tpl_working,",");
			tpl_current=strsep(&tpl_working, ":");

			while(tpl_current && looptemp < sizeof(tmp)) {
				tmp[looptemp]=tpl_current;
				looptemp++;
				tpl_current=strsep(&tpl_working,":");
			}

			for(i=0; i<looptemp; i++) {
				if(!strcmp(tmp[i], "PARKED")) {
					ast_say_digits(mychannel, atoi(myparm), "", mychannel->language);
				} else if(!strcmp(tmp[i], "NODE")) {
					ast_say_digits(mychannel, atoi(myrpt->name), "", mychannel->language);
				} else {
					dres = ast_streamfile(mychannel, tmp[i], mychannel->language);
					if(!dres) {
						dres = ast_waitstream(mychannel, "");
					} else {
						ast_log(LOG_WARNING, "ast_streamfile of %s failed on %s\n", tmp[i], mychannel->name);
						dres = 0;
					}
				}
			}
		}
	    	imdone = 1;
		break;
	    case TEST_TONE:
		imdone = 1;
		myrpt->stopgen = 0;
	        if ((res = ast_tonepair_start(mychannel, 1004.0, 0, 99999999, 7200.0))) 
			break;
	        while(mychannel->generatordata && (!myrpt->stopgen)) {
			if (ast_safe_sleep(mychannel,1)) break;
		    	imdone = 1;
			}
		break;
	    default:
	    	break;
	}
	myrpt->stopgen = 0;
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
	else if ((mode == ARB_ALPHA) || (mode == REV_PATCH)) {
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
struct	ast_frame wf;
int stopped,congstarted;
struct ast_channel *mychannel,*genchannel;

	myrpt->mydtmf = 0;
	/* allocate a pseudo-channel thru asterisk */
	mychannel = ast_request("zap",AST_FORMAT_SLINEAR,"pseudo",NULL);
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
	genchannel = ast_request("zap",AST_FORMAT_SLINEAR,"pseudo",NULL);
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
		res = ast_safe_sleep(mychannel, MSWAIT);
		if (res < 0)
		{
			ast_hangup(mychannel);
			ast_hangup(genchannel);
			ast_mutex_lock(&myrpt->lock);
			myrpt->callmode = 0;
			ast_mutex_unlock(&myrpt->lock);
			pthread_exit(NULL);
		}
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

	if (myrpt->ourcallerid && *myrpt->ourcallerid){
		char *name, *loc, *instr;
		instr = strdup(myrpt->ourcallerid);
		if(instr){
			ast_callerid_parse(instr, &name, &loc);
			if(loc){
				if(mychannel->cid.cid_num)
					free(mychannel->cid.cid_num);
				mychannel->cid.cid_num = strdup(loc);
			}
			if(name){
				if(mychannel->cid.cid_name)
					free(mychannel->cid.cid_name);
				mychannel->cid.cid_name = strdup(name);
			}
			free(instr);
		}
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
	usleep(10000);
	ast_mutex_lock(&myrpt->lock);
	myrpt->callmode = 3;
	while(myrpt->callmode)
	{
		if ((!mychannel->pbx) && (myrpt->callmode != 4))
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
		usleep(MSWAIT * 1000);
		ast_mutex_lock(&myrpt->lock);
	}
	ast_mutex_unlock(&myrpt->lock);
	tone_zone_play_tone(genchannel->fds[0],-1);
	if (mychannel->pbx) ast_softhangup(mychannel,AST_SOFTHANGUP_DEV);
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
		if (l->name[0] == '0') 
		{
			l = l->next;
			continue;
		}
		/* if we found it, write it and were done */
		if (!strcmp(l->name,myrpt->cmdnode))
		{
			wf.data = strdup(str);
			if (l->chan) ast_write(l->chan,&wf);
			return;
		}
		l = l->next;
	}
	l = myrpt->links.next;
	/* if not, give it to everyone */
	while(l != &myrpt->links)
	{
		wf.data = strdup(str);
		if (l->chan) ast_write(l->chan,&wf);
		l = l->next;
	}
	return;
}

/*
* Internet linking function 
*/

static int function_ilink(struct rpt *myrpt, char *param, char *digits, int command_source, struct rpt_link *mylink)
{

	char *val, *s, *s1, *s2, *tele;
	char tmp[300], deststr[300] = "",modechange = 0;
	char digitbuf[MAXNODESTR];
	struct rpt_link *l;
	ZT_CONFINFO ci;  /* conference info */

	if(!param)
		return DC_ERROR;
		
			
	if (!myrpt->enable)
		return DC_ERROR;

	strncpy(digitbuf,digits,MAXNODESTR - 1);

	if(debug)
		printf("@@@@ ilink param = %s, digitbuf = %s\n", (param)? param : "(null)", digitbuf);
		
	switch(myatoi(param)){
		case 1: /* Link off */
			if ((digitbuf[0] == '0') && (myrpt->lastlinknode[0]))
				strcpy(digitbuf,myrpt->lastlinknode);
			val = ast_variable_retrieve(cfg, myrpt->nodes, digitbuf);
			if (!val){
				if(strlen(digitbuf) >= myrpt->longestnode)
					return DC_ERROR;
				break;
			}
			strncpy(tmp,val,sizeof(tmp) - 1);
			s = tmp;
			s1 = strsep(&s,",");
			s2 = strsep(&s,",");
			ast_mutex_lock(&myrpt->lock);
			l = myrpt->links.next;
			/* try to find this one in queue */
			while(l != &myrpt->links){
				if (l->name[0] == '0') 
				{
					l = l->next;
					continue;
				}
				/* if found matching string */
				if (!strcmp(l->name, digitbuf))
					break;
				l = l->next;
			}
			if (l != &myrpt->links){ /* if found */
				struct	ast_frame wf;

				strncpy(myrpt->lastlinknode,digitbuf,MAXNODESTR - 1);
				l->retries = MAX_RETRIES + 1;
				l->disced = 1;
				ast_mutex_unlock(&myrpt->lock);
				wf.frametype = AST_FRAME_TEXT;
				wf.subclass = 0;
				wf.offset = 0;
				wf.mallocd = 1;
				wf.datalen = strlen(discstr) + 1;
				wf.samples = 0;
				wf.data = strdup(discstr);
				if (l->chan)
				{
					ast_write(l->chan,&wf);
					if (ast_safe_sleep(l->chan,250) == -1) return DC_ERROR;
					ast_softhangup(l->chan,AST_SOFTHANGUP_DEV);
				}
				rpt_telemetry(myrpt, COMPLETE, NULL);
				return DC_COMPLETE;
			}
			ast_mutex_unlock(&myrpt->lock);	
			return DC_COMPLETE;
		case 2: /* Link Monitor */
			if ((digitbuf[0] == '0') && (myrpt->lastlinknode[0]))
				strcpy(digitbuf,myrpt->lastlinknode);
			val = ast_variable_retrieve(cfg, myrpt->nodes, digitbuf);
			if (!val){
				if(strlen(digitbuf) >= myrpt->longestnode)
					return DC_ERROR;
				break;
			}
			strncpy(tmp,val,sizeof(tmp) - 1);
			s = tmp;
			s1 = strsep(&s,",");
			s2 = strsep(&s,",");
			ast_mutex_lock(&myrpt->lock);
			l = myrpt->links.next;
			/* try to find this one in queue */
			while(l != &myrpt->links){
				if (l->name[0] == '0') 
				{
					l = l->next;
					continue;
				}
				/* if found matching string */
				if (!strcmp(l->name, digitbuf))
					break;
				l = l->next;
			}
			/* if found */
			if (l != &myrpt->links) 
			{
				/* if already in this mode, just ignore */
				if ((!l->mode) || (!l->chan)) {
					ast_mutex_unlock(&myrpt->lock);
					rpt_telemetry(myrpt,REMALREADY,NULL);
					return DC_COMPLETE;
					
				}
				ast_mutex_unlock(&myrpt->lock);
				if (l->chan) ast_softhangup(l->chan,AST_SOFTHANGUP_DEV);
				l->retries = MAX_RETRIES + 1;
				l->disced = 2;
				modechange = 1;
			} else
				ast_mutex_unlock(&myrpt->lock);
			strncpy(myrpt->lastlinknode,digitbuf,MAXNODESTR - 1);
			/* establish call in monitor mode */
			l = malloc(sizeof(struct rpt_link));
			if (!l){
				ast_log(LOG_WARNING, "Unable to malloc\n");
				return DC_ERROR;
			}
			/* zero the silly thing */
			memset((char *)l,0,sizeof(struct rpt_link));
			snprintf(deststr, sizeof(deststr), "IAX2/%s", s1);
			tele = strchr(deststr,'/');
			if (!tele){
				fprintf(stderr,"link2:Dial number (%s) must be in format tech/number\n",deststr);
				return DC_ERROR;
			}
			*tele++ = 0;
			l->isremote = (s && ast_true(s));
			strncpy(l->name, digitbuf, MAXNODESTR - 1);
			l->chan = ast_request(deststr,AST_FORMAT_SLINEAR,tele,NULL);
			if (modechange) l->connected = 1;
			if (l->chan){
				ast_set_read_format(l->chan,AST_FORMAT_SLINEAR);
				ast_set_write_format(l->chan,AST_FORMAT_SLINEAR);
				l->chan->whentohangup = 0;
				l->chan->appl = "Apprpt";
				l->chan->data = "(Remote Rx)";
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "rpt (remote) initiating call to %s/%s on %s\n",
						deststr,tele,l->chan->name);
				if(l->chan->cid.cid_num)
					free(l->chan->cid.cid_num);
				l->chan->cid.cid_num = strdup(myrpt->name);
				ast_call(l->chan,tele,0);
			}
			else
			{
				rpt_telemetry(myrpt,CONNFAIL,l);
				free(l);
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Unable to place call to %s/%s on %s\n",
						deststr,tele,l->chan->name);
				return DC_ERROR;
			}
			/* allocate a pseudo-channel thru asterisk */
			l->pchan = ast_request("zap",AST_FORMAT_SLINEAR,"pseudo",NULL);
			if (!l->pchan){
				fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
				ast_hangup(l->chan);
				free(l);
				return DC_ERROR;
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
				ast_hangup(l->chan);
				ast_hangup(l->pchan);
				free(l);
				return DC_ERROR;
			}
			ast_mutex_lock(&myrpt->lock);
			/* insert at end of queue */
			insque((struct qelem *)l,(struct qelem *)myrpt->links.next);
			ast_mutex_unlock(&myrpt->lock);
			rpt_telemetry(myrpt,COMPLETE,NULL);
			return DC_COMPLETE;
		case 3: /* Link transceive */
			if ((digitbuf[0] == '0') && (myrpt->lastlinknode[0]))
				strcpy(digitbuf,myrpt->lastlinknode);
			val = ast_variable_retrieve(cfg, myrpt->nodes, digitbuf);
			if (!val){
				if(strlen(digitbuf) >= myrpt->longestnode)
					return DC_ERROR;
				break;
			}
			strncpy(tmp,val,sizeof(tmp) - 1);
			s = tmp;
			s1 = strsep(&s,",");
			s2 = strsep(&s,",");
			ast_mutex_lock(&myrpt->lock);
			l = myrpt->links.next;
			/* try to find this one in queue */
			while(l != &myrpt->links){
				if (l->name[0] == '0') 
				{
					l = l->next;
					continue;
				}
				/* if found matching string */
				if (!strcmp(l->name, digitbuf))
					break;
				l = l->next;
			}
			/* if found */
			if (l != &myrpt->links){ 
				/* if already in this mode, just ignore */
				if ((l->mode) || (!l->chan)) {
					ast_mutex_unlock(&myrpt->lock);
					rpt_telemetry(myrpt, REMALREADY, NULL);
					return DC_COMPLETE;
				}
				ast_mutex_unlock(&myrpt->lock);
				if (l->chan) ast_softhangup(l->chan, AST_SOFTHANGUP_DEV);
				l->retries = MAX_RETRIES + 1;
				l->disced = 2;
				modechange = 1;
			} else
				ast_mutex_unlock(&myrpt->lock);
			strncpy(myrpt->lastlinknode,digitbuf,MAXNODESTR - 1);
			/* establish call in tranceive mode */
			l = malloc(sizeof(struct rpt_link));
			if (!l){
				ast_log(LOG_WARNING, "Unable to malloc\n");
				return(DC_ERROR);
			}
			/* zero the silly thing */
			memset((char *)l,0,sizeof(struct rpt_link));
			l->mode = 1;
			l->outbound = 1;
			strncpy(l->name, digitbuf, MAXNODESTR - 1);
			l->isremote = (s && ast_true(s));
			if (modechange) l->connected = 1;
			snprintf(deststr, sizeof(deststr), "IAX2/%s", s1);
			tele = strchr(deststr, '/');
			if (!tele){
				fprintf(stderr,"link3:Dial number (%s) must be in format tech/number\n",deststr);
				free(l);
				return DC_ERROR;
			}
			*tele++ = 0;
			l->chan = ast_request(deststr, AST_FORMAT_SLINEAR, tele,NULL);
			if (l->chan){
				ast_set_read_format(l->chan, AST_FORMAT_SLINEAR);
				ast_set_write_format(l->chan, AST_FORMAT_SLINEAR);
				l->chan->whentohangup = 0;
				l->chan->appl = "Apprpt";
				l->chan->data = "(Remote Rx)";
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "rpt (remote) initiating call to %s/%s on %s\n",
						deststr, tele, l->chan->name);
				if(l->chan->cid.cid_num)
					free(l->chan->cid.cid_num);
				l->chan->cid.cid_num = strdup(myrpt->name);
				ast_call(l->chan,tele,999);
			}
			else{
				rpt_telemetry(myrpt,CONNFAIL,l);
				free(l);
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Unable to place call to %s/%s on %s\n",
						deststr,tele,l->chan->name);
				return DC_ERROR;
			}
			/* allocate a pseudo-channel thru asterisk */
			l->pchan = ast_request("zap",AST_FORMAT_SLINEAR,"pseudo",NULL);
			if (!l->pchan){
				fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
				ast_hangup(l->chan);
				free(l);
				return DC_ERROR;
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
				ast_hangup(l->chan);
				ast_hangup(l->pchan);
				free(l);
				return DC_ERROR;
			}
			ast_mutex_lock(&myrpt->lock);
			/* insert at end of queue */
			insque((struct qelem *)l,(struct qelem *)myrpt->links.next);
			ast_mutex_unlock(&myrpt->lock);
			rpt_telemetry(myrpt,COMPLETE,NULL);
			return DC_COMPLETE;
		case 4: /* Enter Command Mode */
		
			/* if doesnt allow link cmd, or no links active, return */
 			if (((command_source != SOURCE_RPT) && (command_source != SOURCE_PHONE) && (command_source != SOURCE_DPHONE)) || (myrpt->links.next == &myrpt->links))
				return DC_COMPLETE;
			
			/* if already in cmd mode, or selected self, fughetabahtit */
			if ((myrpt->cmdnode[0]) || (!strcmp(myrpt->name, digitbuf))){
			
				rpt_telemetry(myrpt, REMALREADY, NULL);
				return DC_COMPLETE;
			}
			if ((digitbuf[0] == '0') && (myrpt->lastlinknode[0]))
				strcpy(digitbuf,myrpt->lastlinknode);
			/* node must at least exist in list */
			val = ast_variable_retrieve(cfg, myrpt->nodes, digitbuf);
			if (!val){
				if(strlen(digitbuf) >= myrpt->longestnode)
					return DC_ERROR;
				break;
			
			}
			ast_mutex_lock(&myrpt->lock);
			strcpy(myrpt->lastlinknode,digitbuf);
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
				if (l->chan) ast_softhangup(l->chan, AST_SOFTHANGUP_DEV); /* Hang 'em up */
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

static int function_autopatchup(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
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

static int function_autopatchdn(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
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

static int function_status(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
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

static int function_cop(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
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
			
		case 4: /* test tone on */
			rpt_telemetry(myrpt, TEST_TONE, NULL);
			return DC_COMPLETE;

		case 5: /* Disgorge variables to log for debug purposes */
			myrpt->disgorgetime = time(NULL) + 10; /* Do it 10 seconds later */
			return DC_COMPLETE;

		case 6: /* Simulate COR being activated (phone only) */
			if (command_source != SOURCE_PHONE) return DC_INDETERMINATE;
			return DC_DOKEY;	

	}	
	return DC_INDETERMINATE;
}

/*
* Collect digits one by one until something matches
*/

static int collect_function_digits(struct rpt *myrpt, char *digits, 
	int command_source, struct rpt_link *mylink)
{
	int i;
	char *stringp,*action,*param,*functiondigits;
	char function_table_name[30] = "";
	char workstring[80];
	
	struct ast_variable *vp;
	
	if(debug)	
		printf("@@@@ Digits collected: %s, source: %d\n", digits, command_source);
	
	if (command_source == SOURCE_DPHONE) {
		if (!myrpt->dphone_functions) return DC_INDETERMINATE;
		strncpy(function_table_name, myrpt->dphone_functions, sizeof(function_table_name) - 1);
		}
	else if (command_source == SOURCE_PHONE) {
		if (!myrpt->phone_functions) return DC_INDETERMINATE;
		strncpy(function_table_name, myrpt->phone_functions, sizeof(function_table_name) - 1);
		}
	else if (command_source == SOURCE_LNK)
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
		int n;

		n = myrpt->longestfunc;
		if (command_source == SOURCE_LNK) n = myrpt->link_longestfunc;
		else 
		if (command_source == SOURCE_PHONE) n = myrpt->phone_longestfunc;
		else 
		if (command_source == SOURCE_DPHONE) n = myrpt->dphone_longestfunc;
		
		if(strlen(digits) >= n)
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
	return (*function_table[i].function)(myrpt, param, functiondigits, command_source, mylink);
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

        if (!strcmp(tmp,discstr))
        {
                mylink->disced = 1;
		mylink->retries = MAX_RETRIES + 1;
                ast_softhangup(mylink->chan,AST_SOFTHANGUP_DEV);
                return;
        }
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

	if (dest[0] == '0')
	{
		strcpy(dest,myrpt->name);
	}		

	/* if not for me, redistribute to all links */
	if (strcmp(dest,myrpt->name))
	{
		l = myrpt->links.next;
		/* see if this is one in list */
		while(l != &myrpt->links)
		{
			if (l->name[0] == '0') 
			{
				l = l->next;
				continue;
			}
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
					if (l->chan) ast_write(l->chan,&wf);
				}
				return;
			}
			l = l->next;
		}
		l = myrpt->links.next;
		/* otherwise, send it to all of em */
		while(l != &myrpt->links)
		{
			if (l->name[0] == '0') 
			{
				l = l->next;
				continue;
			}
			/* dont send back from where it came */
			if ((l == mylink) || (!strcmp(l->name,mylink->name)))
			{
				l = l->next;
				continue;
			}
			/* send, but not to src */
			if (strcmp(l->name,src)) {
				wf.data = strdup(str);
				if (l->chan) ast_write(l->chan,&wf);
			}
			l = l->next;
		}
		return;
	}
	ast_mutex_lock(&myrpt->lock);
	if (c == myrpt->endchar) myrpt->stopgen = 1;
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
			res = collect_function_digits(myrpt, cmd, SOURCE_LNK, mylink);
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

static void handle_link_phone_dtmf(struct rpt *myrpt, struct rpt_link *mylink,
	char c)
{

char	cmd[300];
int	res;

	ast_mutex_lock(&myrpt->lock);
	if (c == myrpt->endchar)
	{
		if (mylink->lastrx)
		{
			mylink->lastrx = 0;
			ast_mutex_unlock(&myrpt->lock);
			return;
		}
		myrpt->stopgen = 1;
		if (myrpt->cmdnode[0])
		{
			myrpt->cmdnode[0] = 0;
			myrpt->dtmfidx = -1;
			myrpt->dtmfbuf[0] = 0;
			ast_mutex_unlock(&myrpt->lock);
			rpt_telemetry(myrpt,COMPLETE,NULL);
			ast_mutex_unlock(&myrpt->lock);
			return;
		}
	}
	if (myrpt->cmdnode[0])
	{
		ast_mutex_unlock(&myrpt->lock);
		send_link_dtmf(myrpt,c);
		return;
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
			res = collect_function_digits(myrpt, cmd, 
				((mylink->phonemode == 2) ? SOURCE_DPHONE : SOURCE_PHONE), mylink);
			ast_mutex_lock(&myrpt->lock);
			
			switch(res){

				case DC_INDETERMINATE:
					break;
				
				case DC_DOKEY:
					mylink->lastrx = 1;
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

static void rbi_out_parallel(struct rpt *myrpt,unsigned char *data)
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

static void rbi_out(struct rpt *myrpt,unsigned char *data)
{
struct zt_radio_param r;

	memset(&r,0,sizeof(struct zt_radio_param));
	r.radpar = ZT_RADPAR_REMMODE;
	r.data = ZT_RADPAR_REM_RBI1;
	/* if setparam ioctl fails, its probably not a pciradio card */
	if (ioctl(myrpt->rxchannel->fds[0],ZT_RADIO_SETPARAM,&r) == -1)
	{
		rbi_out_parallel(myrpt,data);
		return;
	}
	r.radpar = ZT_RADPAR_REMCOMMAND;
	memcpy(&r.data,data,5);
	if (ioctl(myrpt->rxchannel->fds[0],ZT_RADIO_SETPARAM,&r) == -1)
	{
		ast_log(LOG_WARNING,"Cannot send RBI command for channel %s\n",myrpt->rxchannel->name);
		return;
	}
}

static int serial_remote_io(struct rpt *myrpt, char *txbuf, int txbytes, char *rxbuf,
        int rxmaxbytes, int asciiflag)
{
	int i;
	struct zt_radio_param prm;

	if(debug){
		printf("String output was: ");
		for(i = 0; i < txbytes; i++)
			printf("%02X ", (unsigned char ) txbuf[i]);
		printf("\n");
	}

        prm.radpar = ZT_RADPAR_REMMODE;
        if (asciiflag)  prm.data = ZT_RADPAR_REM_SERIAL_ASCII;
        else prm.data = ZT_RADPAR_REM_SERIAL;
	if (ioctl(myrpt->rxchannel->fds[0],ZT_RADIO_SETPARAM,&prm) == -1) return -1;
        prm.radpar = ZT_RADPAR_REMCOMMAND;
        prm.data = rxmaxbytes;
        memcpy(prm.buf,txbuf,txbytes);
        prm.index = txbytes;
	if (ioctl(myrpt->rxchannel->fds[0],ZT_RADIO_SETPARAM,&prm) == -1) return -1;
        if (rxbuf)
        {
                *rxbuf = 0;
                memcpy(rxbuf,prm.buf,prm.index);
        }
        return(prm.index);
}

static int setrbi(struct rpt *myrpt)
{
char tmp[MAXREMSTR] = "",rbicmd[5],*s;
int	band,txoffset = 0,txpower = 0,txpl;

	/* must be a remote system */
	if (!myrpt->remote) return(0);
	/* must have rbi hardware */
	if (strncmp(myrpt->remote,remote_rig_rbi,3)) return(0);
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
	
	txpl = rbi_pltocode(myrpt->txpl);
	
	if (txpl == -1){
		if(debug)
			printf("@@@@ Bad TX PL: %s\n", myrpt->txpl);
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
	rbicmd[4] = txpl;
	if (myrpt->txplon) rbicmd[4] |= 0x40;
	if (myrpt->rxplon) rbicmd[4] |= 0x80;
	rbi_out(myrpt,rbicmd);
	return 0;
}


/* Check for valid rbi frequency */
/* Hard coded limits now, configurable later, maybe? */

static int check_freq_rbi(int m, int d, int *defmode)
{
	int dflmd = REM_MODE_FM;

	if(m == 50){ /* 6 meters */
		if(d < 10100)
			return -1;
	}
	else if((m >= 51) && ( m < 54)){
                ;
	}
	else if(m == 144){ /* 2 meters */
		if(d < 10100)
			return -1;
	}
	else if((m >= 145) && (m < 148)){
		;
	}
 	else if((m >= 222) && (m < 225)){ /* 1.25 meters */
		;
	}
	else if((m >= 430) && (m < 450)){ /* 70 centimeters */
		;
	}
	else if((m >= 1240) && (m < 1300)){ /* 23 centimeters */
		;
	}
	else
		return -1;
	
	if(defmode)
		*defmode = dflmd;	


	return 0;
}

/*
* Split frequency into mhz and decimals
*/
 
static int split_freq(char *mhz, char *decimals, char *freq)
{
	char freq_copy[MAXREMSTR];
	char *decp;

	decp = strchr(strncpy(freq_copy, freq, MAXREMSTR),'.');
	if(decp){
		*decp++ = 0;
		strncpy(mhz, freq_copy, MAXREMSTR);
		strcpy(decimals, "00000");
		strncpy(decimals, decp, strlen(decp));
		decimals[5] = 0;
		return 0;
	}
	else
		return -1;

}
	
/*
* Split ctcss frequency into hertz and decimal
*/
 
static int split_ctcss_freq(char *hertz, char *decimal, char *freq)
{
	char freq_copy[MAXREMSTR];
	char *decp;

	decp = strchr(strncpy(freq_copy, freq, MAXREMSTR),'.');
	if(decp){
		*decp++ = 0;
		strncpy(hertz, freq_copy, MAXREMSTR);
		strncpy(decimal, decp, strlen(decp));
		decimal[strlen(decp)] = '\0';
		return 0;
	}
	else
		return -1;
}



/*
* FT-897 I/O handlers
*/

/* Check to see that the frequency is valid */
/* Hard coded limits now, configurable later, maybe? */


static int check_freq_ft897(int m, int d, int *defmode)
{
	int dflmd = REM_MODE_FM;

	if(m == 1){ /* 160 meters */
		dflmd =	REM_MODE_LSB; 
		if(d < 80001)
			return -1;
	}
	else if(m == 3){ /* 80 meters */
		dflmd = REM_MODE_LSB;
		if(d < 75001)
			return -1;
	}
	else if(m == 7){ /* 40 meters */
		dflmd = REM_MODE_LSB;
		if((d < 15001) || (d > 29999))
			return -1;
	}
	else if(m == 14){ /* 20 meters */
		dflmd = REM_MODE_USB;
		if((d < 15001) || (d > 34999))
			return -1;
	}
	else if(m == 18){ /* 17 meters */
		dflmd = REM_MODE_USB;
		if((d < 11001) || (d > 16797))
			return -1;
	}
	else if(m == 21){ /* 15 meters */
		dflmd = REM_MODE_USB;
		if((d < 20001) || (d > 44999))
			return -1;
	}
	else if(m == 24){ /* 12 meters */
		dflmd = REM_MODE_USB;
		if((d < 93001) || (d > 98999))
			return -1;
	}
	else if(m == 28){ /* 10 meters */
		dflmd = REM_MODE_USB;
		if(d < 30001)
			return -1;
	}
	else if(m == 29){ 
		if(d >= 51000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
		if(d > 69999)
			return -1;
	}
	else if(m == 50){ /* 6 meters */
		if(d < 10100)
			return -1;
		if(d >= 30000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;

	}
	else if((m >= 51) && ( m < 54)){
		dflmd = REM_MODE_FM;
	}
	else if(m == 144){ /* 2 meters */
		if(d < 10100)
			return -1;
		if(d >= 30000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
	}
	else if((m >= 145) && (m < 148)){
		dflmd = REM_MODE_FM;
	}
	else if((m >= 430) && (m < 450)){ /* 70 centimeters */
		if(m  < 438)
			dflmd = REM_MODE_USB;
		else
			dflmd = REM_MODE_FM;
		;
	}
	else
		return -1;

	if(defmode)
		*defmode = dflmd;

	return 0;
}

/*
* Set a new frequency for the FT897
*/

static int set_freq_ft897(struct rpt *myrpt, char *newfreq)
{
	char mhz[MAXREMSTR];
	char decimals[MAXREMSTR];
	unsigned char cmdstr[5];
	int fd,m,d;

	fd = 0;
	if(debug) 
		printf("New frequency: %s\n",newfreq);

	if(split_freq(mhz, decimals, newfreq))
		return -1; 

	m = atoi(mhz);
	d = atoi(decimals);

	/* The FT-897 likes packed BCD frequencies */

	cmdstr[0] = ((m / 100) << 4) + ((m % 100)/10);			/* 100MHz 10Mhz */
	cmdstr[1] = ((m % 10) << 4) + (d / 10000);			/* 1MHz 100KHz */
	cmdstr[2] = (((d % 10000)/1000) << 4) + ((d % 1000)/ 100);	/* 10KHz 1KHz */
	cmdstr[3] = (((d % 100)/10) << 4) + (d % 10);			/* 100Hz 10Hz */
	cmdstr[4] = 0x01;						/* command */

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);

}

/* ft-897 simple commands */

static int simple_command_ft897(struct rpt *myrpt, char command)
{
	unsigned char cmdstr[5];
	
	memset(cmdstr, 0, 5);

	cmdstr[4] = command;	

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);

}

/* ft-897 offset */

static int set_offset_ft897(struct rpt *myrpt, char offset)
{
	unsigned char cmdstr[5];
	
	memset(cmdstr, 0, 5);

	switch(offset){
		case	REM_SIMPLEX:
			cmdstr[0] = 0x89;
			break;

		case	REM_MINUS:
			cmdstr[0] = 0x09;
			break;
		
		case	REM_PLUS:
			cmdstr[0] = 0x49;
			break;	

		default:
			return -1;
	}

	cmdstr[4] = 0x09;	

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);
}

/* ft-897 mode */

static int set_mode_ft897(struct rpt *myrpt, char newmode)
{
	unsigned char cmdstr[5];
	
	memset(cmdstr, 0, 5);
	
	switch(newmode){
		case	REM_MODE_FM:
			cmdstr[0] = 0x08;
			break;

		case	REM_MODE_USB:
			cmdstr[0] = 0x01;
			break;

		case	REM_MODE_LSB:
			cmdstr[0] = 0x00;
			break;

		case	REM_MODE_AM:
			cmdstr[0] = 0x04;
			break;
		
		default:
			return -1;
	}
	cmdstr[4] = 0x07;	

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);
}

/* Set tone encode and decode modes */

static int set_ctcss_mode_ft897(struct rpt *myrpt, char txplon, char rxplon)
{
	unsigned char cmdstr[5];
	
	memset(cmdstr, 0, 5);
	
	if(rxplon && txplon)
		cmdstr[0] = 0x2A; /* Encode and Decode */
	else if (!rxplon && txplon)
		cmdstr[0] = 0x4A; /* Encode only */
	else if (rxplon && !txplon)
		cmdstr[0] = 0x3A; /* Encode only */
	else
		cmdstr[0] = 0x8A; /* OFF */

	cmdstr[4] = 0x0A;	

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);
}


/* Set transmit and receive ctcss tone frequencies */

static int set_ctcss_freq_ft897(struct rpt *myrpt, char *txtone, char *rxtone)
{
	unsigned char cmdstr[5];
	char hertz[MAXREMSTR],decimal[MAXREMSTR];
	int h,d;	

	memset(cmdstr, 0, 5);

	if(split_ctcss_freq(hertz, decimal, txtone))
		return -1; 

	h = atoi(hertz);
	d = atoi(decimal);
	
	cmdstr[0] = ((h / 100) << 4) + (h % 100)/ 10;
	cmdstr[1] = ((h % 10) << 4) + (d % 10);
	
	if(rxtone){
	
		if(split_ctcss_freq(hertz, decimal, rxtone))
			return -1; 

		h = atoi(hertz);
		d = atoi(decimal);
	
		cmdstr[2] = ((h / 100) << 4) + (h % 100)/ 10;
		cmdstr[3] = ((h % 10) << 4) + (d % 10);
	}
	cmdstr[4] = 0x0B;	

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);
}	



static int set_ft897(struct rpt *myrpt)
{
	int res;
	
	if(debug)
		printf("@@@@ lock on\n");

	res = simple_command_ft897(myrpt, 0x00);				/* LOCK on */	

	if(debug)
		printf("@@@@ ptt off\n");

	if(!res)
		res = simple_command_ft897(myrpt, 0x88);		/* PTT off */

	if(debug)
		printf("Modulation mode\n");

	if(!res)
		res = set_mode_ft897(myrpt, myrpt->remmode);		/* Modulation mode */

	if(debug)
		printf("Split off\n");

	if(!res)
		simple_command_ft897(myrpt, 0x82);			/* Split off */

	if(debug)
		printf("Frequency\n");

	if(!res)
		res = set_freq_ft897(myrpt, myrpt->freq);		/* Frequency */
	if((myrpt->remmode == REM_MODE_FM)){
		if(debug)
			printf("Offset\n");
		if(!res)
			res = set_offset_ft897(myrpt, myrpt->offset);	/* Offset if FM */
		if((!res)&&(myrpt->rxplon || myrpt->txplon)){
			if(debug)
				printf("CTCSS tone freqs.\n");
			res = set_ctcss_freq_ft897(myrpt, myrpt->txpl, myrpt->rxpl); /* CTCSS freqs if CTCSS is enabled */
		}
		if(!res){
			if(debug)
				printf("CTCSS mode\n");
			res = set_ctcss_mode_ft897(myrpt, myrpt->txplon, myrpt->rxplon); /* CTCSS mode */
		}
	}
	if((myrpt->remmode == REM_MODE_USB)||(myrpt->remmode == REM_MODE_LSB)){
		if(debug)
			printf("Clarifier off\n");
		simple_command_ft897(myrpt, 0x85);			/* Clarifier off if LSB or USB */
	}
	return res;
}

static int closerem_ft897(struct rpt *myrpt)
{
	simple_command_ft897(myrpt, 0x88); /* PTT off */
	return 0;
}	

/*
* Bump frequency up or down by a small amount 
* Return 0 if the new frequnecy is valid, or -1 if invalid
* Interval is in Hz, resolution is 10Hz 
*/

static int multimode_bump_freq_ft897(struct rpt *myrpt, int interval)
{
	int m,d;
	char mhz[MAXREMSTR], decimals[MAXREMSTR];

	if(debug)
		printf("Before bump: %s\n", myrpt->freq);

	if(split_freq(mhz, decimals, myrpt->freq))
		return -1;
	
	m = atoi(mhz);
	d = atoi(decimals);

	d += (interval / 10); /* 10Hz resolution */
	if(d < 0){
		m--;
		d += 100000;
	}
	else if(d >= 100000){
		m++;
		d -= 100000;
	}

	if(check_freq_ft897(m, d, NULL)){
		if(debug)
			printf("Bump freq invalid\n");
		return -1;
	}

	snprintf(myrpt->freq, MAXREMSTR, "%d.%05d", m, d);

	if(debug)
		printf("After bump: %s\n", myrpt->freq);

	return set_freq_ft897(myrpt, myrpt->freq);	
}



/*
* Dispatch to correct I/O handler 
*/

static int setrem(struct rpt *myrpt)
{
	if(!strcmp(myrpt->remote, remote_rig_ft897))
		return set_ft897(myrpt);
	else if(!strcmp(myrpt->remote, remote_rig_rbi))
		return setrbi(myrpt);
	else
		return -1;
}

static int closerem(struct rpt *myrpt)
{
	if(!strcmp(myrpt->remote, remote_rig_ft897))
		return closerem_ft897(myrpt);
	else
		return 0;
}

/*
* Dispatch to correct frequency checker
*/

static int check_freq(struct rpt *myrpt, int m, int d, int *defmode)
{
	if(!strcmp(myrpt->remote, remote_rig_ft897))
		return check_freq_ft897(m, d, defmode);
	else if(!strcmp(myrpt->remote, remote_rig_rbi))
		return check_freq_rbi(m, d, defmode);
	else
		return -1;
}

/*
* Return 1 if rig is multimode capable
*/

static int multimode_capable(struct rpt *myrpt)
{
	if(!strcmp(myrpt->remote, remote_rig_ft897))
		return 1;
	return 0;
}	

/*
* Dispatch to correct frequency bumping function
*/

static int multimode_bump_freq(struct rpt *myrpt, int interval)
{
	if(!strcmp(myrpt->remote, remote_rig_ft897))
		return multimode_bump_freq_ft897(myrpt, interval);
	else
		return -1;
}


/*
* Queue announcment that scan has been stopped 
*/

static void stop_scan(struct rpt *myrpt, int flag)
{
	myrpt->hfscanmode = 0;
	myrpt->hfscanstatus = ((flag) ? -2 : -1);
}

/*
* This is called periodically when in scan mode
*/


static int service_scan(struct rpt *myrpt)
{
	int res, interval;
	char mhz[MAXREMSTR], decimals[MAXREMSTR], k10=0i, k100=0;

	switch(myrpt->hfscanmode){

		case HF_SCAN_DOWN_SLOW:
			interval = -10; /* 100Hz /sec */
			break;

		case HF_SCAN_DOWN_QUICK:
			interval = -50; /* 500Hz /sec */
			break;

		case HF_SCAN_DOWN_FAST:
			interval = -200; /* 2KHz /sec */
			break;

		case HF_SCAN_UP_SLOW:
			interval = 10; /* 100Hz /sec */
			break;

		case HF_SCAN_UP_QUICK:
			interval = 50; /* 500 Hz/sec */
			break;

		case HF_SCAN_UP_FAST:
			interval = 200; /* 2KHz /sec */
			break;

		default:
			myrpt->hfscanmode = 0; /* Huh? */
			return -1;
	}

	res = split_freq(mhz, decimals, myrpt->freq);
		
	if(!res){
		k100 =decimals[0];
		k10 = decimals[1];
		res = multimode_bump_freq(myrpt, interval);
	}

	if(!res)
		res = split_freq(mhz, decimals, myrpt->freq);


	if(res){
		stop_scan(myrpt,1);
		return -1;
	}

	/* Announce 10KHz boundaries */
	if(k10 != decimals[1]){
		int myhund = (interval < 0) ? k100 : decimals[0];
		int myten = (interval < 0) ? k10 : decimals[1];
		myrpt->hfscanstatus = (myten == '0') ? (myhund - '0') * 100 : (myten - '0') * 10;
	}
	return res;

}


static int rmt_telem_start(struct rpt *myrpt, struct ast_channel *chan, int delay)
{
			myrpt->remotetx = 0;
			ast_indicate(myrpt->txchannel,AST_CONTROL_RADIO_UNKEY);
			if (!myrpt->remoterx)
				ast_indicate(chan,AST_CONTROL_RADIO_KEY);
			if (ast_safe_sleep(chan, delay) == -1)
					return -1;
			return 0;
}


static int rmt_telem_finish(struct rpt *myrpt, struct ast_channel *chan)
{

struct zt_params par;

	if (ioctl(myrpt->txchannel->fds[0],ZT_GET_PARAMS,&par) == -1)
	{
		return -1;

	}
	if (!par.rxisoffhook)
	{
		ast_indicate(myrpt->remchannel,AST_CONTROL_RADIO_UNKEY);
		myrpt->remoterx = 0;
	}
	else
	{
		myrpt->remoterx = 1;
	}
	return 0;
}


static int rmt_sayfile(struct rpt *myrpt, struct ast_channel *chan, int delay, char *filename)
{
	int res;

	res = rmt_telem_start(myrpt, chan, delay);

	if(!res)
		res = sayfile(chan, filename);
	
	if(!res)
		res = rmt_telem_finish(myrpt, chan);
	return res;
}

static int rmt_saycharstr(struct rpt *myrpt, struct ast_channel *chan, int delay, char *charstr)
{
	int res;

	res = rmt_telem_start(myrpt, chan, delay);

	if(!res)
		res = saycharstr(chan, charstr);
	
	if(!res)
		res = rmt_telem_finish(myrpt, chan);
	return res;
}



/*
* Remote base function
*/

static int function_remote(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{
	char *s,*s1,*s2,*val;
	int i,j,ht,k,l,ls2,m,d,res,offset,offsave, modesave, defmode;
	char multimode = 0;
	char oc;
	char tmp[20], freq[20] = "", savestr[20] = "";
	char mhz[MAXREMSTR], decimals[MAXREMSTR];
	struct ast_channel *mychannel;

	if((!param) || (command_source == SOURCE_RPT) || (command_source == SOURCE_LNK))
		return DC_ERROR;
		
	multimode = multimode_capable(myrpt);

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
			strncpy(myrpt->txpl, s, sizeof(myrpt->rxpl) - 1);
			myrpt->remmode = REM_MODE_FM;
			myrpt->offset = REM_SIMPLEX;
			myrpt->powerlevel = REM_MEDPWR;
			myrpt->txplon = myrpt->rxplon = 0;
			while(*s1)
			{
				switch(*s1++){
					case 'A':
					case 'a':
						strcpy(myrpt->rxpl, "100.0");
						strcpy(myrpt->txpl, "100.0");
						myrpt->remmode = REM_MODE_AM;	
						break;
					
					case 'B':
					case 'b':
						strcpy(myrpt->rxpl, "100.0");
						strcpy(myrpt->txpl, "100.0");
						myrpt->remmode = REM_MODE_LSB;
						break;
			
					case 'F':
						myrpt->remmode = REM_MODE_FM;
						break;

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

					case 'U':
					case 'u':
						strcpy(myrpt->rxpl, "100.0");
						strcpy(myrpt->txpl, "100.0");
						myrpt->remmode = REM_MODE_USB;
						break;
				}
			}
		
		
			if (setrem(myrpt) == -1)
				return DC_ERROR;
		
		
			return DC_COMPLETE;	
			
		case 2:  /* set freq and offset */
	   
			
	    		for(i = 0, j = 0, k = 0, l = 0 ; digitbuf[i] ; i++){ /* look for M+*K+*O or M+*H+* depending on mode */
				if(digitbuf[i] == '*'){
					j++;
					continue;
				}
				if((digitbuf[i] < '0') || (digitbuf[i] > '9'))
					goto invalid_freq;
				else{
					if(j == 0)
						l++; /* # of digits before first * */
					if(j == 1)
						k++; /* # of digits after first * */
				}
			}
		
			i = strlen(digitbuf) - 1;
			if(multimode){
				if((j > 2) || (l > 3) || (k > 6))
					goto invalid_freq; /* &^@#! */
 			}
			else{
				if((j > 2) || (l > 4) || (k > 3))
					goto invalid_freq; /* &^@#! */
			}

			/* Wait for M+*K+* */

			if(j < 2)
				break; /* Not yet */

			/* We have a frequency */

			strncpy(tmp, digitbuf ,sizeof(tmp) - 1);
			
			s = tmp;
			s1 = strsep(&s, "*"); /* Pick off MHz */
			s2 = strsep(&s,"*"); /* Pick off KHz and Hz */
			ls2 = strlen(s2);	
			
			switch(ls2){ /* Allow partial entry of khz and hz digits for laziness support */
				case 1:
					ht = 0;
					k = 100 * atoi(s2);
					break;
				
				case 2:
					ht = 0;
					k = 10 * atoi(s2);
					break;
					
				case 3:
					if(!multimode){
						if((s2[2] != '0')&&(s2[2] != '5'))
							goto invalid_freq;
					}
					ht = 0;
					k = atoi(s2);
						break;
				case 4:
					k = atoi(s2)/10;
					ht = 10 * (atoi(s2+(ls2-1)));
					break;

				case 5:
					k = atoi(s2)/100;
					ht = (atoi(s2+(ls2-2)));
					break;
					
				default:
					goto invalid_freq;
			}

			/* Check frequency for validity and establish a default mode */
			
			snprintf(freq, sizeof(freq), "%s.%03d%02d",s1, k, ht);

			if(debug)
				printf("New frequency: %s\n", freq);		
	
			split_freq(mhz, decimals, freq);
			m = atoi(mhz);
			d = atoi(decimals);

                        if(check_freq(myrpt, m, d, &defmode)) /* Check to see if frequency entered is legit */
                                goto invalid_freq;


 			if((defmode == REM_MODE_FM) && (digitbuf[i] == '*')) /* If FM, user must enter and additional offset digit */
				break; /* Not yet */


			offset = REM_SIMPLEX; /* Assume simplex */

			if(defmode == REM_MODE_FM){
				oc = *s; /* Pick off offset */
			
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
							goto invalid_freq;
					} 
				} 
			}	
			offsave = myrpt->offset;
			modesave = myrpt->remmode;
			strncpy(savestr, myrpt->freq, sizeof(savestr) - 1);
			strncpy(myrpt->freq, freq, sizeof(myrpt->freq) - 1);
			myrpt->offset = offset;
			myrpt->remmode = defmode;

			if (setrem(myrpt) == -1){
				myrpt->offset = offsave;
				myrpt->remmode = modesave;
				strncpy(myrpt->freq, savestr, sizeof(myrpt->freq) - 1);
				goto invalid_freq;
			}

			return DC_COMPLETE;


			invalid_freq:
	
			rmt_sayfile(myrpt, mychannel, 1000, "rpt/invalid-freq");

			return DC_ERROR; 
		
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
			
			if (setrem(myrpt) == -1){
				strncpy(myrpt->rxpl, savestr, sizeof(myrpt->rxpl) - 1);
				return DC_ERROR;
			}
		
		
			return DC_COMPLETE;
		
		case 4: /* set tx PL tone */
			
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
			strncpy(savestr, myrpt->txpl, sizeof(savestr) - 1);
			strncpy(myrpt->txpl, tmp, sizeof(myrpt->txpl) - 1);
			
			if (setrem(myrpt) == -1){
				strncpy(myrpt->txpl, savestr, sizeof(myrpt->txpl) - 1);
				return DC_ERROR;
			}
		
		
			return DC_COMPLETE;
		

		case 6: /* MODE (FM,USB,LSB,AM) */
			if(strlen(digitbuf) < 1)
				break;

			if(!multimode)
				return DC_ERROR; /* Multimode radios only */

			switch(*digitbuf){
				case '1':
					split_freq(mhz, decimals, myrpt->freq); 
					m=atoi(mhz);
					if(m < 29) /* No FM allowed below 29MHz! */
						return DC_ERROR;
					myrpt->remmode = REM_MODE_FM;
					res = rmt_saycharstr(myrpt, mychannel, 1000,"FM");
					break;

				case '2':
					myrpt->remmode = REM_MODE_USB;
					res = rmt_saycharstr(myrpt, mychannel, 1000,"USB");
					break;	

				case '3':
					myrpt->remmode = REM_MODE_LSB;
					res = rmt_saycharstr(myrpt, mychannel, 1000,"LSB");
					break;
				
				case '4':
					myrpt->remmode = REM_MODE_AM;
					res = rmt_saycharstr(myrpt, mychannel, 1000,"AM");
					break;
		
				default:
					return DC_ERROR;
			}
			if(res)
				return DC_ERROR;

			if(setrem(myrpt))
				return DC_ERROR;
			return DC_COMPLETE;

		case 100: /* other stuff */
		case 101: 
		case 102: 
		case 103: 
		case 104: 
		case 105: 
		case 106:
 			res = rmt_telem_start(myrpt, mychannel, 1000);
			switch(myatoi(param)){ /* Quick commands requiring a setrem call */
				case 100: /* RX PL Off */
					myrpt->rxplon = 0;
					if(!res)
						res = sayfile(mychannel, "rpt/rxpl");
					if(!res)
						sayfile(mychannel, "rpt/off");
					break;
					
				case 101: /* RX PL On */
					myrpt->rxplon = 1;
					if(!res)
						res = sayfile(mychannel, "rpt/rxpl");
					if(!res)
						sayfile(mychannel, "rpt/on");
					break;

					
				case 102: /* TX PL Off */
					myrpt->txplon = 0;
					if(!res)
						res = sayfile(mychannel, "rpt/txpl");
					if(!res)
						sayfile(mychannel, "rpt/off");
					break;
					
				case 103: /* TX PL On */
					myrpt->txplon = 1;
					if(!res)
						res = sayfile(mychannel, "rpt/txpl");
					if(!res)
						sayfile(mychannel, "rpt/on");
					break;
					
				case 104: /* Low Power */
					myrpt->powerlevel = REM_LOWPWR;
					if(!res)
						res = sayfile(mychannel, "rpt/lopwr");
					break;
					
				case 105: /* Medium Power */
					myrpt->powerlevel = REM_MEDPWR;
					if(!res)
						res = sayfile(mychannel, "rpt/medpwr");
					break;
					
				case 106: /* Hi Power */
					myrpt->powerlevel = REM_HIPWR;
					if(!res)
						res = sayfile(mychannel, "rpt/hipwr");
					break;
			
				default:
					if(!res)
						rmt_telem_finish(myrpt, mychannel);
					return DC_ERROR;
			}
			if(!res)
				res = rmt_telem_finish(myrpt, mychannel);
			if(res)
				return DC_ERROR;

			if (setrem(myrpt) == -1) 
				return DC_ERROR;
			return DC_COMPLETE;

		case 107: /* Bump down 20Hz */
			multimode_bump_freq(myrpt, -20);
			return DC_COMPLETE;

		case 108: /* Bump down 100Hz */
			multimode_bump_freq(myrpt, -100);
			return DC_COMPLETE;

		case 109: /* Bump down 500Hz */
			multimode_bump_freq(myrpt, -500);
			return DC_COMPLETE;

		case 110: /* Bump up 20Hz */
			multimode_bump_freq(myrpt, 20);
			return DC_COMPLETE;
				
		case 111: /* Bump up 100Hz */
			multimode_bump_freq(myrpt, 100);
			return DC_COMPLETE;

		case 112: /* Bump up 500Hz */
			multimode_bump_freq(myrpt, 500);
			return DC_COMPLETE;


		case 113:
		case 114:
		case 115:
		case 116:
		case 117:
		case 118:
			myrpt->remotetx = 0;
			ast_indicate(myrpt->txchannel,AST_CONTROL_RADIO_UNKEY);
			if (!myrpt->remoterx)
				ast_indicate(mychannel,AST_CONTROL_RADIO_KEY);
			if (ast_safe_sleep(mychannel,1000) == -1)
					return DC_ERROR;
		
			switch(myatoi(param)){

				case 113: /* Scan down slow */
					res = sayfile(mychannel,"rpt/down");
					if(!res)
						res = sayfile(mychannel, "rpt/slow");
					if(!res){
						myrpt->scantimer = REM_SCANTIME;
						myrpt->hfscanmode = HF_SCAN_DOWN_SLOW;
					}
					break;

				case 114: /* Scan down quick */
					res = sayfile(mychannel,"rpt/down");
					if(!res)
						res = sayfile(mychannel, "rpt/quick");
					if(!res){
						myrpt->scantimer = REM_SCANTIME;
						myrpt->hfscanmode = HF_SCAN_DOWN_QUICK;
					}
					break;

				case 115: /* Scan down fast */
					res = sayfile(mychannel,"rpt/down");
					if(!res)
						res = sayfile(mychannel, "rpt/fast");
					if(!res){
						myrpt->scantimer = REM_SCANTIME;
						myrpt->hfscanmode = HF_SCAN_DOWN_FAST;
					}
					break;

				case 116: /* Scan up slow */
					res = sayfile(mychannel,"rpt/up");
					if(!res)
						res = sayfile(mychannel, "rpt/slow");
					if(!res){
						myrpt->scantimer = REM_SCANTIME;
						myrpt->hfscanmode = HF_SCAN_UP_SLOW;
					}
					break;

				case 117: /* Scan up quick */
					res = sayfile(mychannel,"rpt/up");
					if(!res)
						res = sayfile(mychannel, "rpt/quick");
					if(!res){
						myrpt->scantimer = REM_SCANTIME;
						myrpt->hfscanmode = HF_SCAN_UP_QUICK;
					}
					break;

				case 118: /* Scan up fast */
					res = sayfile(mychannel,"rpt/up");
					if(!res)
						res = sayfile(mychannel, "rpt/fast");
					if(!res){
						myrpt->scantimer = REM_SCANTIME;
						myrpt->hfscanmode = HF_SCAN_UP_FAST;
					}
					break;
			}
			rmt_telem_finish(myrpt,mychannel);
			return DC_COMPLETE;


		case 119: /* Tune Request */
			myrpt->tunerequest = 1;
			return DC_COMPLETE;

		case 5: /* Long Status */
		case 140: /* Short Status */
			res = rmt_telem_start(myrpt, mychannel, 1000);

			res = sayfile(mychannel,"rpt/node");
			if(!res)
				res = saycharstr(mychannel, myrpt->name);
			if(!res)
				res = sayfile(mychannel,"rpt/frequency");
			if(!res)
				res = split_freq(mhz, decimals, myrpt->freq);
			if(!res){
				m = atoi(mhz);
				if(m < 100)
					res = saynum(mychannel, m);
				else
					res = saycharstr(mychannel, mhz);
			}
			if(!res)
				res = sayfile(mychannel, "letters/dot");
			if(!res)
				res = saycharstr(mychannel, decimals);
		
			if(res){	
				rmt_telem_finish(myrpt,mychannel);
				return DC_ERROR;
			}
			if(myrpt->remmode == REM_MODE_FM){ /* Mode FM? */
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
			}
			else{ /* Must be USB, LSB, or AM */
				switch(myrpt->remmode){

					case REM_MODE_USB:
						res = saycharstr(mychannel, "USB");
						break;

					case REM_MODE_LSB:
						res = saycharstr(mychannel, "LSB");
						break;

					case REM_MODE_AM:
						res = saycharstr(mychannel, "AM");
						break;


					default:
						return DC_ERROR;
				}
			}

			if (res == -1){
				rmt_telem_finish(myrpt,mychannel);
				return DC_ERROR;
			}

			if(myatoi(param) == 140){ /* Short status? */
				if(!res)
					res = rmt_telem_finish(myrpt, mychannel);
				if(res)
					return DC_ERROR;
				return DC_COMPLETE;
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
				(sayfile(mychannel,"rpt/frequency") == -1) ||
				(saycharstr(mychannel,myrpt->txpl) == -1) ||
				(sayfile(mychannel,"rpt/txpl") == -1) ||
				(sayfile(mychannel,((myrpt->txplon) ? "rpt/on" : "rpt/off")) == -1) ||
				(sayfile(mychannel,"rpt/rxpl") == -1) ||
				(sayfile(mychannel,((myrpt->rxplon) ? "rpt/on" : "rpt/off")) == -1))
				{
					rmt_telem_finish(myrpt,mychannel);
					return DC_ERROR;
				}
			if(!res)
				res = rmt_telem_finish(myrpt,mychannel);
			if(res)
				return DC_ERROR;

			return DC_COMPLETE;
	    	default:
			return DC_ERROR;
	}

	return DC_INDETERMINATE;
}

static int handle_remote_dtmf_digit(struct rpt *myrpt,char c, char *keyed, int phonemode)
{
time_t	now;
int	ret,res = 0,src;

	/* Stop scan mode if in scan mode */
	if(myrpt->hfscanmode){
		stop_scan(myrpt,0);
		return 0;
	}

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
	
	
	src = SOURCE_RMT;
	if (phonemode > 1) src = SOURCE_DPHONE;
	else if (phonemode) src = SOURCE_PHONE;
	ret = collect_function_digits(myrpt, myrpt->dtmfbuf, src, NULL);
	
	switch(ret){
	
		case DC_INDETERMINATE:
			res = 0;
			break;
				
		case DC_DOKEY:
			if (keyed) *keyed = 1;
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
	if (!strcmp(tmp,discstr)) return 0;
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
	res = handle_remote_dtmf_digit(myrpt,c, NULL, 0);
	if (res != 1)
		return res;
	myrpt->remotetx = 0;
	ast_indicate(myrpt->txchannel,AST_CONTROL_RADIO_UNKEY);
	if (!myrpt->remoterx)
	{
		ast_indicate(myrpt->remchannel,AST_CONTROL_RADIO_KEY);
	}
	if (ast_safe_sleep(myrpt->remchannel,1000) == -1) return -1;
	res = telem_lookup(myrpt->remchannel, myrpt->name, "functcomplete");
	rmt_telem_finish(myrpt,myrpt->remchannel);
	return res;
}

static int handle_remote_phone_dtmf(struct rpt *myrpt, char c, char *keyed, int phonemode)
{
int	res;


	if (keyed && *keyed && (c == myrpt->endchar))
	{
		*keyed = 0;
		return DC_INDETERMINATE;
	}

	res = handle_remote_dtmf_digit(myrpt,c,keyed, phonemode);
	if (res != 1)
		return res;
	myrpt->remotetx = 0;
	ast_indicate(myrpt->txchannel,AST_CONTROL_RADIO_UNKEY);
	if (!myrpt->remoterx)
	{
		ast_indicate(myrpt->remchannel,AST_CONTROL_RADIO_KEY);
	}
	if (ast_safe_sleep(myrpt->remchannel,1000) == -1) return -1;
	res = telem_lookup(myrpt->remchannel, myrpt->name, "functcomplete");
	rmt_telem_finish(myrpt,myrpt->remchannel);
	return res;
}

static int attempt_reconnect(struct rpt *myrpt, struct rpt_link *l)
{
	char *val, *s, *s1, *s2, *tele;
	char tmp[300], deststr[300] = "";

	val = ast_variable_retrieve(cfg, myrpt->nodes, l->name);
	if (!val)
	{
		fprintf(stderr,"attempt_reconnect: cannot find node %s\n",l->name);
		return -1;
	}

	ast_mutex_lock(&myrpt->lock);
	/* remove from queue */
	remque((struct qelem *) l);
	ast_mutex_unlock(&myrpt->lock);
	strncpy(tmp,val,sizeof(tmp) - 1);
	s = tmp;
	s1 = strsep(&s,",");
	s2 = strsep(&s,",");
	snprintf(deststr, sizeof(deststr), "IAX2/%s", s1);
	tele = strchr(deststr, '/');
	if (!tele) {
		fprintf(stderr,"attempt_reconnect:Dial number (%s) must be in format tech/number\n",deststr);
		return -1;
	}
	*tele++ = 0;
	l->elaptime = 0;
	l->chan = ast_request(deststr, AST_FORMAT_SLINEAR, tele,NULL);
	if (l->chan){
		ast_set_read_format(l->chan, AST_FORMAT_SLINEAR);
		ast_set_write_format(l->chan, AST_FORMAT_SLINEAR);
		l->chan->whentohangup = 0;
		l->chan->appl = "Apprpt";
		l->chan->data = "(Remote Rx)";
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "rpt (attempt_reconnect) initiating call to %s/%s on %s\n",
				deststr, tele, l->chan->name);
		if(l->chan->cid.cid_num)
			free(l->chan->cid.cid_num);
		l->chan->cid.cid_num = strdup(myrpt->name);
                ast_call(l->chan,tele,999); 

	}
	else 
	{
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Unable to place call to %s/%s on %s\n",
				deststr,tele,l->chan->name);
		return -1;
	}
	ast_mutex_lock(&myrpt->lock);
	/* put back in queue queue */
	insque((struct qelem *)l,(struct qelem *)myrpt->links.next);
	ast_mutex_unlock(&myrpt->lock);
	ast_log(LOG_NOTICE,"Reconnect Attempt to %s in process\n",l->name);
	return 0;
}

/* single thread with one file (request) to dial */
static void *rpt(void *this)
{
struct	rpt *myrpt = (struct rpt *)this;
char *tele,*idtalkover;
int ms = MSWAIT,lasttx=0,val,remrx=0,identqueued,nonidentqueued,res;
struct ast_channel *who;
ZT_CONFINFO ci;  /* conference info */
time_t	dtmf_time,t;
struct rpt_link *l,*m;
struct rpt_tele *telem;
pthread_attr_t attr;
char tmpstr[300];
char cmd[MAXDTMF+1] = "";


	ast_mutex_lock(&myrpt->lock);
	strncpy(tmpstr,myrpt->rxchanname,sizeof(tmpstr) - 1);
	tele = strchr(tmpstr,'/');
	if (!tele)
	{
		fprintf(stderr,"rpt:Dial number (%s) must be in format tech/number\n",myrpt->rxchanname);
		ast_mutex_unlock(&myrpt->lock);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		pthread_exit(NULL);
	}
	*tele++ = 0;
	myrpt->rxchannel = ast_request(tmpstr,AST_FORMAT_SLINEAR,tele,NULL);
	if (myrpt->rxchannel)
	{
		if (myrpt->rxchannel->_state == AST_STATE_BUSY)
		{
			fprintf(stderr,"rpt:Sorry unable to obtain Rx channel\n");
			ast_mutex_unlock(&myrpt->lock);
			ast_hangup(myrpt->rxchannel);
			myrpt->rpt_thread = AST_PTHREADT_STOP;
			pthread_exit(NULL);
		}
		ast_set_read_format(myrpt->rxchannel,AST_FORMAT_SLINEAR);
		ast_set_write_format(myrpt->rxchannel,AST_FORMAT_SLINEAR);
		myrpt->rxchannel->whentohangup = 0;
		myrpt->rxchannel->appl = "Apprpt";
		myrpt->rxchannel->data = "(Repeater Rx)";
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "rpt (Rx) initiating call to %s/%s on %s\n",
				tmpstr,tele,myrpt->rxchannel->name);
		ast_call(myrpt->rxchannel,tele,999);
		if (myrpt->rxchannel->_state != AST_STATE_UP)
		{
			ast_mutex_unlock(&myrpt->lock);
			ast_hangup(myrpt->rxchannel);
			myrpt->rpt_thread = AST_PTHREADT_STOP;
			pthread_exit(NULL);
		}
	}
	else
	{
		fprintf(stderr,"rpt:Sorry unable to obtain Rx channel\n");
		ast_mutex_unlock(&myrpt->lock);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		pthread_exit(NULL);
	}
	if (myrpt->txchanname)
	{
		strncpy(tmpstr,myrpt->txchanname,sizeof(tmpstr) - 1);
		tele = strchr(tmpstr,'/');
		if (!tele)
		{
			fprintf(stderr,"rpt:Dial number (%s) must be in format tech/number\n",myrpt->txchanname);
			ast_mutex_unlock(&myrpt->lock);
			ast_hangup(myrpt->rxchannel);
			myrpt->rpt_thread = AST_PTHREADT_STOP;
			pthread_exit(NULL);
		}
		*tele++ = 0;
		myrpt->txchannel = ast_request(tmpstr,AST_FORMAT_SLINEAR,tele,NULL);
		if (myrpt->txchannel)
		{
			if (myrpt->txchannel->_state == AST_STATE_BUSY)
			{
				fprintf(stderr,"rpt:Sorry unable to obtain Tx channel\n");
				ast_mutex_unlock(&myrpt->lock);
				ast_hangup(myrpt->txchannel);
				ast_hangup(myrpt->rxchannel);
				myrpt->rpt_thread = AST_PTHREADT_STOP;
				pthread_exit(NULL);
			}			
			ast_set_read_format(myrpt->txchannel,AST_FORMAT_SLINEAR);
			ast_set_write_format(myrpt->txchannel,AST_FORMAT_SLINEAR);
			myrpt->txchannel->whentohangup = 0;
			myrpt->txchannel->appl = "Apprpt";
			myrpt->txchannel->data = "(Repeater Tx)";
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "rpt (Tx) initiating call to %s/%s on %s\n",
					tmpstr,tele,myrpt->txchannel->name);
			ast_call(myrpt->txchannel,tele,999);
			if (myrpt->rxchannel->_state != AST_STATE_UP)
			{
				ast_mutex_unlock(&myrpt->lock);
				ast_hangup(myrpt->rxchannel);
				ast_hangup(myrpt->txchannel);
				myrpt->rpt_thread = AST_PTHREADT_STOP;
				pthread_exit(NULL);
			}
		}
		else
		{
			fprintf(stderr,"rpt:Sorry unable to obtain Tx channel\n");
			ast_mutex_unlock(&myrpt->lock);
			ast_hangup(myrpt->rxchannel);
			myrpt->rpt_thread = AST_PTHREADT_STOP;
			pthread_exit(NULL);
		}
	}
	else
	{
		myrpt->txchannel = myrpt->rxchannel;
	}
	ast_indicate(myrpt->txchannel,AST_CONTROL_RADIO_KEY);
	ast_indicate(myrpt->txchannel,AST_CONTROL_RADIO_UNKEY);
	/* allocate a pseudo-channel thru asterisk */
	myrpt->pchannel = ast_request("zap",AST_FORMAT_SLINEAR,"pseudo",NULL);
	if (!myrpt->pchannel)
	{
		fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
		ast_mutex_unlock(&myrpt->lock);
		if (myrpt->txchannel != myrpt->rxchannel) 
			ast_hangup(myrpt->txchannel);
		ast_hangup(myrpt->rxchannel);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
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
		ast_hangup(myrpt->pchannel);
		if (myrpt->txchannel != myrpt->rxchannel) 
			ast_hangup(myrpt->txchannel);
		ast_hangup(myrpt->rxchannel);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
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
		ast_hangup(myrpt->pchannel);
		if (myrpt->txchannel != myrpt->rxchannel) 
			ast_hangup(myrpt->txchannel);
		ast_hangup(myrpt->rxchannel);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		pthread_exit(NULL);
	}
	/* save pseudo channel conference number */
	myrpt->conf = ci.confno;
	/* allocate a pseudo-channel thru asterisk */
	myrpt->txpchannel = ast_request("zap",AST_FORMAT_SLINEAR,"pseudo",NULL);
	if (!myrpt->txpchannel)
	{
		fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
		ast_mutex_unlock(&myrpt->lock);
		ast_hangup(myrpt->pchannel);
		if (myrpt->txchannel != myrpt->rxchannel) 
			ast_hangup(myrpt->txchannel);
		ast_hangup(myrpt->rxchannel);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
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
		ast_hangup(myrpt->txpchannel);
		ast_hangup(myrpt->pchannel);
		if (myrpt->txchannel != myrpt->rxchannel) 
			ast_hangup(myrpt->txchannel);
		ast_hangup(myrpt->rxchannel);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
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
	myrpt->retxtimer = 0;
	lasttx = 0;
	myrpt->keyed = 0;
	idtalkover = ast_variable_retrieve(cfg, myrpt->name, "idtalkover");
	myrpt->dtmfidx = -1;
	myrpt->dtmfbuf[0] = 0;
	myrpt->rem_dtmfidx = -1;
	myrpt->rem_dtmfbuf[0] = 0;
	dtmf_time = 0;
	myrpt->rem_dtmf_time = 0;
	myrpt->enable = 1;
	myrpt->disgorgetime = 0;
	ast_mutex_unlock(&myrpt->lock);
	val = 0;
	ast_channel_setoption(myrpt->rxchannel,AST_OPTION_TONE_VERIFY,&val,sizeof(char),0);
	val = 1;
	ast_channel_setoption(myrpt->rxchannel,AST_OPTION_RELAXDTMF,&val,sizeof(char),0);
	while (ms >= 0)
	{
		struct ast_frame *f;
		struct ast_channel *cs[300];
		int totx=0,elap=0,n,toexit=0;

		/* DEBUG Dump */
		if((myrpt->disgorgetime) && (time(NULL) >= myrpt->disgorgetime)){
			struct rpt_link *zl;
			struct rpt_tele *zt;

			myrpt->disgorgetime = 0;
			ast_log(LOG_NOTICE,"********** Variable Dump Start (app_rpt) **********\n");
			ast_log(LOG_NOTICE,"totx = %d\n",totx);
			ast_log(LOG_NOTICE,"remrx = %d\n",remrx);
			ast_log(LOG_NOTICE,"lasttx = %d\n",lasttx);
			ast_log(LOG_NOTICE,"elap = %d\n",elap);
			ast_log(LOG_NOTICE,"toexit = %d\n",toexit);

			ast_log(LOG_NOTICE,"myrpt->keyed = %d\n",myrpt->keyed);
			ast_log(LOG_NOTICE,"myrpt->localtx = %d\n",myrpt->localtx);
			ast_log(LOG_NOTICE,"myrpt->callmode = %d\n",myrpt->callmode);
			ast_log(LOG_NOTICE,"myrpt->enable = %d\n",myrpt->enable);
			ast_log(LOG_NOTICE,"myrpt->mustid = %d\n",myrpt->mustid);
			ast_log(LOG_NOTICE,"myrpt->tounkeyed = %d\n",myrpt->tounkeyed);
			ast_log(LOG_NOTICE,"myrpt->tonotify = %d\n",myrpt->tonotify);
			ast_log(LOG_NOTICE,"myrpt->retxtimer = %ld\n",myrpt->retxtimer);
			ast_log(LOG_NOTICE,"myrpt->totimer = %d\n",myrpt->totimer);
			ast_log(LOG_NOTICE,"myrpt->tailtimer = %d\n",myrpt->tailtimer);

			zl = myrpt->links.next;
              		while(zl != &myrpt->links){
				ast_log(LOG_NOTICE,"*** Link Name: %s ***\n",zl->name);
				ast_log(LOG_NOTICE,"        link->lasttx %d\n",zl->lasttx);
				ast_log(LOG_NOTICE,"        link->lastrx %d\n",zl->lastrx);
				ast_log(LOG_NOTICE,"        link->connected %d\n",zl->connected);
				ast_log(LOG_NOTICE,"        link->hasconnected %d\n",zl->hasconnected);
				ast_log(LOG_NOTICE,"        link->outbound %d\n",zl->outbound);
				ast_log(LOG_NOTICE,"        link->disced %d\n",zl->disced);
				ast_log(LOG_NOTICE,"        link->killme %d\n",zl->killme);
				ast_log(LOG_NOTICE,"        link->disctime %ld\n",zl->disctime);
				ast_log(LOG_NOTICE,"        link->retrytimer %ld\n",zl->retrytimer);
				ast_log(LOG_NOTICE,"        link->retries = %d\n",zl->retries);

                        	zl = zl->next;
                	}
                                                                                                                               
			zt = myrpt->tele.next;
			if(zt != &myrpt->tele)
				ast_log(LOG_NOTICE,"*** Telemetry Queue ***\n");
              		while(zt != &myrpt->tele){
				ast_log(LOG_NOTICE,"        Telemetry mode: %d\n",zt->mode);
                        	zt = zt->next;
                	}
			ast_log(LOG_NOTICE,"******* Variable Dump End (app_rpt) *******\n");

		}	





		ast_mutex_lock(&myrpt->lock);
		if (ast_check_hangup(myrpt->rxchannel)) break;
		if (ast_check_hangup(myrpt->txchannel)) break;
		if (ast_check_hangup(myrpt->pchannel)) break;
		if (ast_check_hangup(myrpt->txpchannel)) break;
		myrpt->localtx = myrpt->keyed && (myrpt->dtmfidx == -1) && (!myrpt->cmdnode[0]);
		
		/* If someone's connected, and they're transmitting from their end to us, set remrx true */
		
		l = myrpt->links.next;
		remrx = 0;
		while(l != &myrpt->links)
		{
			if (l->lastrx) remrx = 1;
			l = l->next;
		}
		
		/* Create a "must_id" flag for the cleanup ID */	
			
		myrpt->mustid |= (myrpt->idtimer) && (myrpt->keyed || remrx) ;

		/* Build a fresh totx from myrpt->keyed and autopatch activated */
		
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
			ast_mutex_unlock(&myrpt->lock);
			rpt_telemetry(myrpt,TIMEOUT,NULL);
			ast_mutex_lock(&myrpt->lock);
		}
		/* if wants to transmit and in phone call, but timed out, 
			reset time-out timer if keyed */
		if ((!totx) && (!myrpt->totimer) && (!myrpt->tounkeyed) && (!myrpt->keyed))
		{
			myrpt->tounkeyed = 1;
		}
		if ((!totx) && (!myrpt->totimer) && myrpt->tounkeyed && myrpt->keyed)
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
		/* If user or links key up or are keyed up over standard ID, switch to talkover ID, if one is defined */
		if (identqueued && (myrpt->keyed || remrx) && idtalkover) {
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
		
		/* Reconnect kludge */
		l = myrpt->links.next;
		while(l != &myrpt->links)
		{
			if (l->killme)
			{
				/* remove from queue */
				remque((struct qelem *) l);
				if (!strcmp(myrpt->cmdnode,l->name))
					myrpt->cmdnode[0] = 0;
				ast_mutex_unlock(&myrpt->lock);
				/* hang-up on call to device */
				if (l->chan) ast_hangup(l->chan);
				ast_hangup(l->pchan);
				free(l);
				ast_mutex_lock(&myrpt->lock);
				/* re-start link traversal */
				l = myrpt->links.next;
				continue;
			}
			l = l->next;
		}
		n = 0;
		cs[n++] = myrpt->rxchannel;
		cs[n++] = myrpt->pchannel;
		cs[n++] = myrpt->txpchannel;
		if (myrpt->txchannel != myrpt->rxchannel) cs[n++] = myrpt->txchannel;
		l = myrpt->links.next;
		while(l != &myrpt->links)
		{
			if ((!l->killme) && (!l->disctime) && l->chan)
			{
				cs[n++] = l->chan;
				cs[n++] = l->pchan;
			}
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
			if (!l->lasttx)
			{
				if ((l->retxtimer += elap) >= REDUNDANT_TX_TIME)
				{
					l->retxtimer = 0;
					if (l->chan) ast_indicate(l->chan,AST_CONTROL_RADIO_UNKEY);
				}
			} else l->retxtimer = 0;
#ifdef	RECONNECT_KLUDGE
			if (l->disctime) /* Disconnect timer active on a channel ? */
			{
				l->disctime -= elap;
				if (l->disctime <= 0) /* Disconnect timer expired on inbound channel ? */
					l->disctime = 0; /* Yep */
			}

			if (l->retrytimer)
			{
				l->retrytimer -= elap;
				if (l->retrytimer < 0) l->retrytimer = 0;
			}
#endif
			/* ignore non-timing channels */
			if (l->elaptime < 0)
			{
				l = l->next;
				continue;
			}
			l->elaptime += elap;
			/* if connection has taken too long */
			if ((l->elaptime > MAXCONNECTTIME) && 
			   ((!l->chan) || (l->chan->_state != AST_STATE_UP)))
			{
				l->elaptime = 0;
				ast_mutex_unlock(&myrpt->lock);
				if (l->chan) ast_softhangup(l->chan,AST_SOFTHANGUP_DEV);
#ifndef	RECONNECT_KLUDGE
				rpt_telemetry(myrpt,CONNFAIL,l);
#endif
				ast_mutex_lock(&myrpt->lock);
				break;
			}
#ifdef	RECONNECT_KLUDGE
			if ((!l->chan) && (!l->retrytimer) && l->outbound && 
				(l->retries++ < MAX_RETRIES) && (l->hasconnected))
			{
				if (l->chan) ast_hangup(l->chan);
				ast_mutex_unlock(&myrpt->lock);
				if ((l->name[0] != '0') && (!l->isremote))
				{
					l->retrytimer = MAX_RETRIES + 1;
				}
				else 
				{
					if (attempt_reconnect(myrpt,l) == -1)
					{
						l->retrytimer = RETRY_TIMER_MS;
					}
				}
				ast_mutex_lock(&myrpt->lock);
				break;
			}
			if ((!l->chan) && (!l->retrytimer) && l->outbound &&
				(l->retries >= MAX_RETRIES))
			{
				/* remove from queue */
				remque((struct qelem *) l);
				if (!strcmp(myrpt->cmdnode,l->name))
					myrpt->cmdnode[0] = 0;
				ast_mutex_unlock(&myrpt->lock);
				if (l->name[0] != '0')
				{
					if (!l->hasconnected)
						rpt_telemetry(myrpt,CONNFAIL,l);
					else rpt_telemetry(myrpt,REMDISC,l);
				}
				/* hang-up on call to device */
				ast_hangup(l->pchan);
				free(l);
                                ast_mutex_lock(&myrpt->lock);
				break;
			}
                        if ((!l->chan) && (!l->disctime) && (!l->outbound))
                        {
                                /* remove from queue */
                                remque((struct qelem *) l);
                                if (!strcmp(myrpt->cmdnode,l->name))
                                        myrpt->cmdnode[0] = 0;
                                ast_mutex_unlock(&myrpt->lock);
				if (l->name[0] != '0') 
				{
	                                rpt_telemetry(myrpt,REMDISC,l);
				}
                                /* hang-up on call to device */
                                ast_hangup(l->pchan);
                                free(l);
                                ast_mutex_lock(&myrpt->lock);
                                break;
                        }
#endif
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
				if (!myrpt->keyed) continue;
				if (c == myrpt->endchar)
				{
					/* if in simple mode, kill autopatch */
					if (myrpt->simple && myrpt->callmode)
					{
						ast_mutex_lock(&myrpt->lock);
						myrpt->callmode = 0;
						ast_mutex_unlock(&myrpt->lock);
						rpt_telemetry(myrpt,TERM,NULL);
						continue;
					}
					ast_mutex_lock(&myrpt->lock);
					myrpt->stopgen = 1;
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
				ast_mutex_lock(&myrpt->lock);
				if (myrpt->cmdnode[0])
				{
					ast_mutex_unlock(&myrpt->lock);
					send_link_dtmf(myrpt,c);
					continue;
				}
				if (!myrpt->simple)
				{
					if (c == myrpt->funcchar)
					{
						myrpt->dtmfidx = 0;
						myrpt->dtmfbuf[myrpt->dtmfidx] = 0;
						ast_mutex_unlock(&myrpt->lock);
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
							res = collect_function_digits(myrpt, cmd, SOURCE_RPT, NULL);
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
							if(res != DC_INDETERMINATE) {
								ast_mutex_unlock(&myrpt->lock);
								continue;
							}
						} 
					}
				}
				else /* if simple */
				{
					if ((!myrpt->callmode) && (c == myrpt->funcchar))
					{
						myrpt->callmode = 1;
						myrpt->cidx = 0;
						myrpt->exten[myrpt->cidx] = 0;
						ast_mutex_unlock(&myrpt->lock);
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
						ast_mutex_unlock(&myrpt->lock);
						rpt_telemetry(myrpt,PROC,NULL); 
						continue;
					}
					/* if can continue, do so */
					if (!ast_canmatch_extension(myrpt->pchannel,myrpt->ourcontext,myrpt->exten,1,NULL))
					{
						/* call has failed, inform user */
						myrpt->callmode = 4;
					}
					ast_mutex_unlock(&myrpt->lock);
					continue;
				}
				if ((myrpt->callmode == 2) || (myrpt->callmode == 3))
				{
					myrpt->mydtmf = c;
				}
				ast_mutex_unlock(&myrpt->lock);
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
					myrpt->keyed = 1;
				}
				/* if RX un-key */
				if (f->subclass == AST_CONTROL_RADIO_UNKEY)
				{
					if (debug) printf("@@@@ rx un-key\n");
					if(myrpt->keyed) {
						rpt_telemetry(myrpt,UNKEY,NULL);
					}
					myrpt->keyed = 0;
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
		ast_mutex_lock(&myrpt->lock);
		l = myrpt->links.next;
		while(l != &myrpt->links)
		{
			if (l->disctime)
			{
				l = l->next;
				continue;
			}
			if (who == l->chan) /* if it was a read from rx */
			{
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
				if (l->chan && (l->lasttx != totx))
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
#ifdef	RECONNECT_KLUDGE
					if ((!l->disced) && (!l->outbound))
					{
						if ((l->name[0] == '0') || l->isremote)
							l->disctime = 1;
						else
							l->disctime = DISC_TIME;
						ast_mutex_lock(&myrpt->lock);
						ast_hangup(l->chan);
						l->chan = 0;
						break;
					}

					if (l->retrytimer) 
					{
						ast_mutex_lock(&myrpt->lock);
						break; 
					}
					if (l->outbound && (l->retries++ < MAX_RETRIES) && (l->hasconnected))
					{
						ast_mutex_lock(&myrpt->lock);
						ast_hangup(l->chan);
						l->chan = 0;
						ast_mutex_unlock(&myrpt->lock);
						if (attempt_reconnect(myrpt,l) == -1)
						{
							l->retrytimer = RETRY_TIMER_MS;
						}
						ast_mutex_lock(&myrpt->lock);
						break;
					}
#endif
					ast_mutex_lock(&myrpt->lock);
					/* remove from queue */
					remque((struct qelem *) l);
					if (!strcmp(myrpt->cmdnode,l->name))
						myrpt->cmdnode[0] = 0;
					ast_mutex_unlock(&myrpt->lock);
					if (!l->hasconnected)
						rpt_telemetry(myrpt,CONNFAIL,l);
					else if (l->disced != 2) rpt_telemetry(myrpt,REMDISC,l);
					/* hang-up on call to device */
					ast_hangup(l->chan);
					ast_hangup(l->pchan);
					free(l);
					ast_mutex_lock(&myrpt->lock);
					break;
				}
				if (f->frametype == AST_FRAME_VOICE)
				{
					if (l->phonemode && (!l->lastrx))
					{
						memset(f->data,0,f->datalen);
					}
					ast_write(l->pchan,f);
				}
				if (f->frametype == AST_FRAME_TEXT)
				{
					handle_link_data(myrpt,l,f->data);
				}
				if (f->frametype == AST_FRAME_DTMF)
				{
					handle_link_phone_dtmf(myrpt,l,f->subclass);
				}
				if (f->frametype == AST_FRAME_CONTROL)
				{
					if (f->subclass == AST_CONTROL_ANSWER)
					{
						char lconnected = l->connected;
						l->connected = 1;
						l->hasconnected = 1;
						l->elaptime = -1;
						l->retries = 0;
						if (!lconnected) rpt_telemetry(myrpt,CONNECTED,l);
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
#ifdef	RECONNECT_KLUDGE
						if ((!l->outbound) && (!l->disced))
						{
							if ((l->name[0] == '0') || l->isremote)
								l->disctime = 1;
							else
								l->disctime = DISC_TIME;
							ast_mutex_lock(&myrpt->lock);
							ast_hangup(l->chan);
							l->chan = 0;
							break;
						}
						if (l->retrytimer) 
						{
							ast_mutex_lock(&myrpt->lock);
							break;
						}
						if (l->outbound && (l->retries++ < MAX_RETRIES) && (l->hasconnected))
						{
							ast_mutex_lock(&myrpt->lock);
							ast_hangup(l->chan);
							l->chan = 0;
							ast_mutex_unlock(&myrpt->lock);
							if (attempt_reconnect(myrpt,l) == -1)
							{
								l->retrytimer = RETRY_TIMER_MS;
							}
							ast_mutex_lock(&myrpt->lock);
							break;
						}
#endif
						ast_mutex_lock(&myrpt->lock);
						/* remove from queue */
						remque((struct qelem *) l);
						if (!strcmp(myrpt->cmdnode,l->name))
							myrpt->cmdnode[0] = 0;
						ast_mutex_unlock(&myrpt->lock);
						if (!l->hasconnected)
							rpt_telemetry(myrpt,CONNFAIL,l);
						else if (l->disced != 2) rpt_telemetry(myrpt,REMDISC,l);
						/* hang-up on call to device */
						ast_hangup(l->chan);
						ast_hangup(l->pchan);
						free(l);
						ast_mutex_lock(&myrpt->lock);
						break;
					}
				}
				ast_frfree(f);
				ast_mutex_lock(&myrpt->lock);
				break;
			}
			if (who == l->pchan) 
			{
				ast_mutex_unlock(&myrpt->lock);
				f = ast_read(l->pchan);
				if (!f)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					toexit = 1;
					ast_mutex_lock(&myrpt->lock);
					break;
				}
				if (f->frametype == AST_FRAME_VOICE)
				{
					if (l->chan) ast_write(l->chan,f);
				}
				if (f->frametype == AST_FRAME_CONTROL)
				{
					if (f->subclass == AST_CONTROL_HANGUP)
					{
						if (debug) printf("@@@@ rpt:Hung Up\n");
						ast_frfree(f);
						toexit = 1;
						ast_mutex_lock(&myrpt->lock);
						break;
					}
				}
				ast_frfree(f);
				ast_mutex_lock(&myrpt->lock);
				break;
			}
			l = l->next;
		}
		ast_mutex_unlock(&myrpt->lock);
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
	usleep(100000);
	ast_hangup(myrpt->pchannel);
	ast_hangup(myrpt->txpchannel);
	if (myrpt->txchannel != myrpt->rxchannel) ast_hangup(myrpt->txchannel);
	ast_hangup(myrpt->rxchannel);
	ast_mutex_lock(&myrpt->lock);
	l = myrpt->links.next;
	while(l != &myrpt->links)
	{
		struct rpt_link *ll = l;
		/* remove from queue */
		remque((struct qelem *) l);
		/* hang-up on call to device */
		if (l->chan) ast_hangup(l->chan);
		ast_hangup(l->pchan);
		l = l->next;
		free(ll);
	}
	ast_mutex_unlock(&myrpt->lock);
	if (debug) printf("@@@@ rpt:Hung up channel\n");
	myrpt->rpt_thread = AST_PTHREADT_STOP;
	pthread_exit(NULL); 
	return NULL;
}


static void *rpt_master(void *ignore)
{
char *this,*val;
struct ast_variable *vp;
int	i,j,n,longestnode;
pthread_attr_t attr;

	/* start with blank config */
	memset(&rpt_vars,0,sizeof(rpt_vars));

	cfg = ast_config_load("rpt.conf");
	if (!cfg) {
		ast_log(LOG_NOTICE, "Unable to open radio repeater configuration rpt.conf.  Radio Repeater disabled.\n");
		pthread_exit(NULL);
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
		rpt_vars[n].rpt_thread = AST_PTHREADT_NULL;
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
		rpt_vars[n].remote = ast_variable_retrieve(cfg,this,"remote");
		rpt_vars[n].tonezone = ast_variable_retrieve(cfg,this,"tonezone");
		val = ast_variable_retrieve(cfg,this,"iobase");
		/* do not use atoi() here, we need to be able to have
			the input specified in hex or decimal so we use
			sscanf with a %i */
		if ((!val) || (sscanf(val,"%i",&rpt_vars[n].iobase) != 1))
			rpt_vars[n].iobase = DEFAULT_IOBASE;
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
		rpt_vars[n].phone_functions = ast_variable_retrieve(cfg,this,"phone_functions");
		rpt_vars[n].dphone_functions = ast_variable_retrieve(cfg,this,"dphone_functions");
		val = ast_variable_retrieve(cfg,this,"funcchar");
		if (!val) rpt_vars[n].funcchar = FUNCCHAR; else 
			rpt_vars[n].funcchar = *val;		
		val = ast_variable_retrieve(cfg,this,"endchar");
		if (!val) rpt_vars[n].endchar = ENDCHAR; else 
			rpt_vars[n].endchar = *val;		
		val = ast_variable_retrieve(cfg,this,"nobusyout");
		if (val) rpt_vars[n].nobusyout = ast_true(val);
		rpt_vars[n].nodes = ast_variable_retrieve(cfg,this,"nodes");
		if (!rpt_vars[n].nodes) 
			rpt_vars[n].nodes = NODES;
		n++;
	}
	nrpts = n;
	ast_log(LOG_DEBUG, "Total of %d repeaters configured.\n",n);
	/* start em all */
	for(i = 0; i < n; i++)
	{

		/*
		* Go through the node list to determine the longest node
		*/
		longestnode = 0;

		vp = ast_variable_browse(cfg, rpt_vars[i].nodes);
		
		while(vp){
			j = strlen(vp->name);
			if (j > longestnode)
				longestnode = j;
			vp = vp->next;
		}


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
		rpt_vars[i].phone_longestfunc = 0;
		if (rpt_vars[i].phone_functions)
		{
			vp = ast_variable_browse(cfg, rpt_vars[i].phone_functions);
			while(vp){
				j = strlen(vp->name);
				if (j > rpt_vars[i].phone_longestfunc)
					rpt_vars[i].phone_longestfunc = j;
				vp = vp->next;
			}
		}
		rpt_vars[i].dphone_longestfunc = 0;
		if (rpt_vars[i].dphone_functions)
		{
			vp = ast_variable_browse(cfg, rpt_vars[i].dphone_functions);
			while(vp){
				j = strlen(vp->name);
				if (j > rpt_vars[i].dphone_longestfunc)
					rpt_vars[i].dphone_longestfunc = j;
				vp = vp->next;
			}
		}
		if (!rpt_vars[i].rxchanname)
		{
			ast_log(LOG_WARNING,"Did not specify rxchanname for node %s\n",rpt_vars[i].name);
			pthread_exit(NULL);
		}
		/* if is a remote, dont start one for it */
		if (rpt_vars[i].remote)
		{
			strncpy(rpt_vars[i].freq, "146.580", sizeof(rpt_vars[i].freq) - 1);
			strncpy(rpt_vars[i].rxpl, "100.0", sizeof(rpt_vars[i].rxpl) - 1);

			strncpy(rpt_vars[i].txpl, "100.0", sizeof(rpt_vars[i].txpl) - 1);
			rpt_vars[i].remmode = REM_MODE_FM;
			rpt_vars[i].offset = REM_SIMPLEX;
			rpt_vars[i].powerlevel = REM_MEDPWR;
			continue;
		}
		if (!rpt_vars[i].ident)
		{
			ast_log(LOG_WARNING,"Did not specify ident for node %s\n",rpt_vars[i].name);
			pthread_exit(NULL);
		}
	        pthread_attr_init(&attr);
	        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		ast_pthread_create(&rpt_vars[i].rpt_thread,&attr,rpt,(void *) &rpt_vars[i]);
	}
	usleep(500000);
	for(;;)
	{
		/* Now monitor each thread, and restart it if necessary */
		for(i = 0; i < n; i++)
		{ 
			int rv;
			if (rpt_vars[i].remote) continue;
			if (rpt_vars[i].rpt_thread == AST_PTHREADT_STOP) 
				rv = -1;
			else
				rv = pthread_kill(rpt_vars[i].rpt_thread,0);
			if (rv)
			{
				if(time(NULL) - rpt_vars[i].lastthreadrestarttime <= 15)
				{
					if(rpt_vars[i].threadrestarts >= 5)
					{
						ast_log(LOG_ERROR,"Continual RPT thread restarts, killing Asterisk\n");
						exit(1); /* Stuck in a restart loop, kill Asterisk and start over */
					}
					else
					{
						ast_log(LOG_NOTICE,"RPT thread restarted on %s\n",rpt_vars[i].name);
						rpt_vars[i].threadrestarts++;
					}
				}
				else
					rpt_vars[i].threadrestarts = 0;

				rpt_vars[i].lastthreadrestarttime = time(NULL);
			        pthread_attr_init(&attr);
	 		        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
				ast_pthread_create(&rpt_vars[i].rpt_thread,&attr,rpt,(void *) &rpt_vars[i]);
				ast_log(LOG_WARNING, "rpt_thread restarted on node %s\n", rpt_vars[i].name);
			}

		}
		usleep(2000000);
	}
	pthread_exit(NULL);
}

static int rpt_exec(struct ast_channel *chan, void *data)
{
	int res=-1,i,rem_totx,n,phone_mode = 0;
	struct localuser *u;
	char tmp[256], keyed = 0;
	char *options,*stringp,*tele;
	struct	rpt *myrpt;
	struct ast_frame *f;
	struct ast_channel *who;
	struct ast_channel *cs[20];
	struct	rpt_link *l;
	ZT_CONFINFO ci;  /* conference info */
	ZT_PARAMS par;
	int ms,elap;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Rpt requires an argument (system node)\n");
		return -1;
	}
	strncpy(tmp, (char *)data, sizeof(tmp)-1);
	stringp=tmp;
	strsep(&stringp, "|");
	options = stringp;
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

	/* if not phone access, must be an IAX connection */
	if (options && ((*options == 'P') || (*options == 'D') || (*options == 'R')))
	{
		phone_mode = 1;
		if (*options == 'D') phone_mode = 2;
		ast_set_callerid(chan,"0","app_rpt user","0");
	}
	else
	{
		if (strncmp(chan->name,"IAX2",4))
		{
			ast_log(LOG_WARNING, "We only accept links via IAX2!!\n");
			return -1;
		}
	}
	if (options && (*options == 'R'))
	{

		/* Parts of this section taken from app_parkandannounce */
		char *return_context;
		int l, m, lot, timeout = 0;
		char tmp[256],*template;
		char *working, *context, *exten, *priority;
		char *s,*orig_s;


		ast_mutex_lock(&myrpt->lock);
		m = myrpt->callmode;
		ast_mutex_unlock(&myrpt->lock);

		if ((!myrpt->nobusyout) && m)
		{
			if (chan->_state != AST_STATE_UP)
			{
				ast_indicate(chan,AST_CONTROL_BUSY);
			}
			while(ast_safe_sleep(chan,10000) != -1);
			return -1;
		}

		if (chan->_state != AST_STATE_UP)
		{
			ast_answer(chan);
		}

		l=strlen(options)+2;
		orig_s=malloc(l);
		if(!orig_s) {
			ast_log(LOG_WARNING, "Out of memory\n");
			return -1;
		}
		s=orig_s;
		strncpy(s,options,l);

		template=strsep(&s,"|");
		if(!template) {
			ast_log(LOG_WARNING, "An announce template must be defined\n");
			free(orig_s);
			return -1;
		} 
  
		if(s) {
			timeout = atoi(strsep(&s, "|"));
			timeout *= 1000;
		}
	
		return_context = s;
  
		if(return_context != NULL) {
			/* set the return context. Code borrowed from the Goto builtin */
    
			working = return_context;
			context = strsep(&working, "|");
			exten = strsep(&working, "|");
			if(!exten) {
				/* Only a priority in this one */
				priority = context;
				exten = NULL;
				context = NULL;
			} else {
				priority = strsep(&working, "|");
				if(!priority) {
					/* Only an extension and priority in this one */
					priority = exten;
					exten = context;
					context = NULL;
			}
		}
		if(atoi(priority) < 0) {
			ast_log(LOG_WARNING, "Priority '%s' must be a number > 0\n", priority);
			free(orig_s);
			return -1;
		}
		/* At this point we have a priority and maybe an extension and a context */
		chan->priority = atoi(priority);
		if(exten && strcasecmp(exten, "BYEXTENSION"))
			strncpy(chan->exten, exten, sizeof(chan->exten)-1);
		if(context)
			strncpy(chan->context, context, sizeof(chan->context)-1);
		} else {  /* increment the priority by default*/
			chan->priority++;
		}

		if(option_verbose > 2) {
			ast_verbose( VERBOSE_PREFIX_3 "Return Context: (%s,%s,%d) ID: %s\n", chan->context,chan->exten, chan->priority, chan->cid.cid_num);
			if(!ast_exists_extension(chan, chan->context, chan->exten, chan->priority, chan->cid.cid_num)) {
				ast_verbose( VERBOSE_PREFIX_3 "Warning: Return Context Invalid, call will return to default|s\n");
			}
		}
  
		/* we are using masq_park here to protect * from touching the channel once we park it.  If the channel comes out of timeout
		before we are done announcing and the channel is messed with, Kablooeee.  So we use Masq to prevent this.  */

		ast_masq_park_call(chan, NULL, timeout, &lot);

		if (option_verbose > 2) ast_verbose( VERBOSE_PREFIX_3 "Call Parking Called, lot: %d, timeout: %d, context: %s\n", lot, timeout, return_context);

		snprintf(tmp,sizeof(tmp) - 1,"%d,%s",lot,template + 1);

		rpt_telemetry(myrpt,REV_PATCH,tmp);

		free(orig_s);

		return 0;

	}

	if (!options)
	{
                struct ast_hostent ahp;
                struct hostent *hp;
		struct in_addr ia;
		char hisip[100],nodeip[100],*val, *s, *s1, *s2, *b,*b1;

		/* look at callerid to see what node this comes from */
		if (!chan->cid.cid_num) /* if doesn't have caller id */
		{
			ast_log(LOG_WARNING, "Doesnt have callerid on %s\n",tmp);
			return -1;
		}

		/* get his IP from IAX2 module */
		memset(hisip,0,sizeof(hisip));
		pbx_substitute_variables_helper(chan,"${IAXPEER(CURRENTCHANNEL)}",hisip,sizeof(hisip) - 1);
		if (!hisip[0])
		{
			ast_log(LOG_WARNING, "Link IP address cannot be determined!!\n");
			return -1;
		}
		
		ast_callerid_parse(chan->cid.cid_num,&b,&b1);
		ast_shrink_phone_number(b1);
		if (!strcmp(myrpt->name,b1))
		{
			ast_log(LOG_WARNING, "Trying to link to self!!\n");
			return -1;
		}

		if (*b1 < '1')
		{
			ast_log(LOG_WARNING, "Node %s Invalid for connection here!!\n",b1);
			return -1;
		}


		/* look for his reported node string */
		val = ast_variable_retrieve(cfg, myrpt->nodes, b1);
		if (!val)
		{
			ast_log(LOG_WARNING, "Reported node %s cannot be found!!\n",b1);
			return -1;
		}
		strncpy(tmp,val,sizeof(tmp) - 1);
		s = tmp;
		s1 = strsep(&s,",");
		s2 = strsep(&s,",");
		if (!s2)
		{
			ast_log(LOG_WARNING, "Reported node %s not in correct format!!\n",b1);
			return -1;
		}
                if (strcmp(s2,"NONE")) {
			hp = ast_gethostbyname(s2, &ahp);
			if (!hp)
			{
				ast_log(LOG_WARNING, "Reported node %s, name %s cannot be found!!\n",b1,s2);
				return -1;
			}
			memcpy(&ia,hp->h_addr,sizeof(in_addr_t));
			ast_inet_ntoa(nodeip,sizeof(nodeip) - 1,ia);
			if (strcmp(hisip,nodeip))
			{
				char *s3 = strchr(s1,'@');
				if (s3) s1 = s3 + 1;
				s3 = strchr(s1,'/');
				if (s3) *s3 = 0;
				hp = ast_gethostbyname(s1, &ahp);
				if (!hp)
				{
					ast_log(LOG_WARNING, "Reported node %s, name %s cannot be found!!\n",b1,s1);
					return -1;
				}
				memcpy(&ia,hp->h_addr,sizeof(in_addr_t));
				ast_inet_ntoa(nodeip,sizeof(nodeip) - 1,ia);
				if (strcmp(hisip,nodeip))
				{
					ast_log(LOG_WARNING, "Node %s IP %s does not match link IP %s!!\n",b1,nodeip,hisip);
					return -1;
				}
			}
		}
	}

	/* if is not a remote */
	if (!myrpt->remote)
	{

		char *b,*b1;

		/* look at callerid to see what node this comes from */
		if (!chan->cid.cid_num) /* if doesn't have caller id */
		{
			ast_log(LOG_WARNING, "Doesnt have callerid on %s\n",tmp);
			return -1;
		}

		ast_callerid_parse(chan->cid.cid_num,&b,&b1);
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
			if (l->name[0] == '0') 
			{
				l = l->next;
				continue;
			}
			/* if found matching string */
			if (!strcmp(l->name,b1)) break;
			l = l->next;
		}
		/* if found */
		if (l != &myrpt->links) 
		{
			l->killme = 1;
			l->retries = MAX_RETRIES + 1;
			l->disced = 2;
                        ast_mutex_unlock(&myrpt->lock);
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
		l->hasconnected = 1;
		l->phonemode = phone_mode;
		ast_set_read_format(l->chan,AST_FORMAT_SLINEAR);
		ast_set_write_format(l->chan,AST_FORMAT_SLINEAR);
		/* allocate a pseudo-channel thru asterisk */
		l->pchan = ast_request("zap",AST_FORMAT_SLINEAR,"pseudo",NULL);
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
		if (phone_mode > 1) l->lastrx = 1;
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
		if (myrpt->remoteon)
		{
			ast_log(LOG_WARNING, "Trying to use busy link on %s\n",tmp);
			return -1;
		}		
		ast_mutex_lock(&myrpt->lock);
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
	myrpt->rxchannel = ast_request(myrpt->rxchanname,AST_FORMAT_SLINEAR,tele,NULL);
	if (myrpt->rxchannel)
	{
		ast_set_read_format(myrpt->rxchannel,AST_FORMAT_SLINEAR);
		ast_set_write_format(myrpt->rxchannel,AST_FORMAT_SLINEAR);
		myrpt->rxchannel->whentohangup = 0;
		myrpt->rxchannel->appl = "Apprpt";
		myrpt->rxchannel->data = "(Link Rx)";
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
			ast_hangup(myrpt->rxchannel);
			pthread_exit(NULL);
		}
		*tele++ = 0;
		myrpt->txchannel = ast_request(myrpt->txchanname,AST_FORMAT_SLINEAR,tele,NULL);
		if (myrpt->txchannel)
		{
			ast_set_read_format(myrpt->txchannel,AST_FORMAT_SLINEAR);
			ast_set_write_format(myrpt->txchannel,AST_FORMAT_SLINEAR);
			myrpt->txchannel->whentohangup = 0;
			myrpt->txchannel->appl = "Apprpt";
			myrpt->txchannel->data = "(Link Tx)";
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
			ast_hangup(myrpt->rxchannel);
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
	myrpt->retxtimer = 0;
	myrpt->remoteon = 1;
	myrpt->dtmfidx = -1;
	myrpt->dtmfbuf[0] = 0;
	myrpt->dtmf_time_rem = 0;
	myrpt->hfscanmode = 0;
	myrpt->hfscanstatus = 0;
	ast_mutex_unlock(&myrpt->lock);
	setrem(myrpt); 
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

	if (ioctl(myrpt->txchannel->fds[0],ZT_GET_PARAMS,&par) != -1)
	{
		if (par.rxisoffhook)
		{
			ast_indicate(chan,AST_CONTROL_RADIO_KEY);
			myrpt->remoterx = 1;
		}
	}
	n = 0;
	cs[n++] = chan;
	cs[n++] = myrpt->rxchannel;
	if (myrpt->rxchannel != myrpt->txchannel)
		cs[n++] = myrpt->txchannel;
	for(;;) 
	{
		if (ast_check_hangup(chan)) break;
		if (ast_check_hangup(myrpt->rxchannel)) break;
		ms = MSWAIT;
		who = ast_waitfor_n(cs,n,&ms);
		if (who == NULL) ms = 0;
		elap = MSWAIT - ms;
		if (!ms) continue;
		rem_totx = keyed;
		
		
		if ((!myrpt->remoterx) && (!myrpt->remotetx))
		{
			if ((myrpt->retxtimer += elap) >= REDUNDANT_TX_TIME)
			{
				myrpt->retxtimer = 0;
				ast_indicate(chan,AST_CONTROL_RADIO_UNKEY);
			}
		} else myrpt->retxtimer = 0;
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

		if(myrpt->tunerequest && (!strcmp(myrpt->remote, remote_rig_ft897))){ /* ft-897 specific for now... */
			myrpt->tunerequest = 0;
			set_mode_ft897(myrpt, REM_MODE_AM);
			simple_command_ft897(myrpt, 8);
			myrpt->remotetx = 0;
			ast_indicate(myrpt->txchannel,AST_CONTROL_RADIO_UNKEY);
			if (!myrpt->remoterx)
				ast_indicate(chan, AST_CONTROL_RADIO_KEY);
			if(play_tone(chan, 800, 6000, 8192) == -1)
				break;

			rmt_telem_finish(myrpt,chan);
			set_mode_ft897(myrpt, 0x88);
			setrem(myrpt);
		}
	
		if (myrpt->hfscanmode){
			myrpt->scantimer -= elap;
			if(myrpt->scantimer <= 0){
				myrpt->scantimer = REM_SCANTIME;
				service_scan(myrpt);
			}
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
			if (f->frametype == AST_FRAME_DTMF)
			{
				myrpt->remchannel = chan; /* Save copy of channel */
				if (handle_remote_phone_dtmf(myrpt,f->subclass,&keyed,phone_mode) == -1)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
			}
			if (f->frametype == AST_FRAME_TEXT)
			{
				myrpt->remchannel = chan; /* Save copy of channel */
				if (handle_remote_data(myrpt,f->data) == -1)
				{
					if (debug) printf("@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
			}
			if (f->frametype == AST_FRAME_CONTROL)
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
			if (myrpt->hfscanstatus){
				myrpt->remchannel = chan; /* Save copy of channel */
				myrpt->remotetx = 0;
				ast_indicate(myrpt->txchannel,AST_CONTROL_RADIO_UNKEY);
				if (!myrpt->remoterx)
				{
					ast_indicate(myrpt->remchannel,AST_CONTROL_RADIO_KEY);
				}
				if(myrpt->hfscanstatus < 0) {
					if (myrpt->hfscanstatus == -1) {
						if (ast_safe_sleep(myrpt->remchannel,1000) == -1) break;
					}
					sayfile(myrpt->remchannel, "rpt/stop");
				}
				else
				{
					saynum(myrpt->remchannel, myrpt->hfscanstatus );
				}	
				rmt_telem_finish(myrpt,myrpt->remchannel);
				myrpt->hfscanstatus = 0;
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
		if ((myrpt->rxchannel != myrpt->txchannel) && 
			(who == myrpt->txchannel)) /* do this cuz you have to */
		{
			f = ast_read(myrpt->txchannel);
			if (!f)
			{
				if (debug) printf("@@@@ link:Hung Up\n");
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
	if (myrpt->rxchannel != myrpt->txchannel) ast_hangup(myrpt->txchannel);
	ast_hangup(myrpt->rxchannel);
	myrpt->hfscanmode = 0;
	myrpt->hfscanstatus = 0;
	myrpt->remoteon = 0;
	ast_mutex_unlock(&myrpt->lock);
	closerem(myrpt);
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	int i;

	STANDARD_HANGUP_LOCALUSERS;
	for(i = 0; i < nrpts; i++) {
		if (!strcmp(rpt_vars[i].name,rpt_vars[i].nodes)) continue;
                ast_mutex_destroy(&rpt_vars[i].lock);
	}
	i = ast_unregister_application(app);

	/* Unregister cli extensions */
	ast_cli_unregister(&cli_debug);

	return i;
}

int load_module(void)
{
	ast_pthread_create(&rpt_master_thread,NULL,rpt_master,NULL);

	/* Register cli extensions */
	ast_cli_register(&cli_debug);

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

