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

/*! \file
 *
 * \brief Radio Repeater / Remote Base program 
 *  version 0.48 06/13/06
 * 
 * \author Jim Dixon, WB6NIL <jim@lambdatel.com>
 *
 * \note Serious contributions by Steve RoDgers, WA6ZFT <hwstar@rodgers.sdcoxmail.com>
 * 
 * See http://www.zapatatelephony.org/app_rpt.html
 *
 *
 * Repeater / Remote Functions:
 * "Simple" Mode:  * - autopatch access, # - autopatch hangup
 * Normal mode:
 * See the function list in rpt.conf (autopatchup, autopatchdn)
 * autopatchup can optionally take comma delimited setting=value pairs:
 *  
 *
 * context=string		:	Override default context with "string"
 * dialtime=ms			:	Specify the max number of milliseconds between phone number digits (1000 milliseconds = 1 second)
 * farenddisconnect=1		:	Automatically disconnect when called party hangs up
 * noct=1			:	Don't send repeater courtesy tone during autopatch calls
 * quiet=1			:	Don't send dial tone, or connect messages. Do not send patch down message when called party hangs up
 *
 *
 * Example: 123=autopatchup,dialtime=20000,noct=1,farenddisconnect=1
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
 *
 * 'duplex' modes:  (defaults to duplex=2)
 *
 * 0 - Only remote links key Tx and no main repeat audio.
 * 1 - Everything other then main Rx keys Tx, no main repeat audio.
 * 2 - Normal mode
 * 3 - Normal except no main repeat audio.
 * 4 - Normal except no main repeat audio during autopatch only
 *
*/

/*** MODULEINFO
	<depend>dahdi</depend>
	<depend>tonezone</depend>
	<defaultenabled>no</defaultenabled>
 ***/

/* Un-comment the following to include support for MDC-1200 digital tone
   signalling protocol (using KA6SQG's GPL'ed implementation) */
/* #include "mdc_decode.c" */

/* Un-comment the following to include support for notch filters in the
   rx audio stream (using Tony Fisher's mknotch (mkfilter) implementation) */
/* #include "rpt_notch.c" */

/* maximum digits in DTMF buffer, and seconds after * for DTMF command timeout */

#define	MAXDTMF 32
#define	MAXMACRO 2048
#define	MAXGOSUB 2048
#define	MACROTIME 100
#define	GOSUBTIME 100
#define	MACROPTIME 500
#define	GOSUBPTIME 500
#define	DTMF_TIMEOUT 3

#ifdef	__RPT_NOTCH
#define	MAXFILTERS 10
#endif

#define	DISC_TIME 10000  /* report disc after 10 seconds of no connect */
#define	MAX_RETRIES 5

#define	REDUNDANT_TX_TIME 2000

#define	RETRY_TIMER_MS 5000

#define MAXPEERSTR 31
#define	MAXREMSTR 15

#define	DELIMCHR ','
#define	QUOTECHR 34

#define	NODES "nodes"
#define MEMORY "memory"
#define MACRO "macro"
#define GOSUB "gosub"
#define	FUNCTIONS "functions"
#define TELEMETRY "telemetry"
#define MORSE "morse"
#define	FUNCCHAR '*'
#define	ENDCHAR '#'

#define	DEFAULT_IOBASE 0x378

#define	MAXCONNECTTIME 5000

#define MAXNODESTR 300

#define MAXPATCHCONTEXT 100

#define ACTIONSIZE 32

#define TELEPARAMSIZE 256

#define REM_SCANTIME 100


enum {REM_OFF, REM_MONITOR, REM_TX};

enum {ID, PROC, TERM, COMPLETE, UNKEY, REMDISC, REMALREADY, REMNOTFOUND, REMGO,
	CONNECTED, CONNFAIL, STATUS, TIMEOUT, ID1, STATS_TIME,
	STATS_VERSION, IDTALKOVER, ARB_ALPHA, TEST_TONE, REV_PATCH,
	TAILMSG, MACRO_NOTFOUND, GOSUB_NOTFOUND, MACRO_BUSY, GOSUB_BUSY, LASTNODEKEY};

enum {REM_SIMPLEX, REM_MINUS, REM_PLUS};

enum {REM_LOWPWR, REM_MEDPWR, REM_HIPWR};

enum {DC_INDETERMINATE, DC_REQ_FLUSH, DC_ERROR, DC_COMPLETE, DC_DOKEY};

enum {SOURCE_RPT, SOURCE_LNK, SOURCE_RMT, SOURCE_PHONE, SOURCE_DPHONE};

enum {DLY_TELEM, DLY_ID, DLY_UNKEY, DLY_CALLTERM};

enum {REM_MODE_FM, REM_MODE_USB, REM_MODE_LSB, REM_MODE_AM};

enum {HF_SCAN_OFF, HF_SCAN_DOWN_SLOW, HF_SCAN_DOWN_QUICK,
      HF_SCAN_DOWN_FAST, HF_SCAN_UP_SLOW, HF_SCAN_UP_QUICK, HF_SCAN_UP_FAST};

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <signal.h>
#include <search.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/io.h>
#include <math.h>
#include <dahdi/user.h>
#include <dahdi/tonezone.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/callerid.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/features.h"
#include "asterisk/cli.h"
#include "asterisk/config.h"
#include "asterisk/say.h"
#include "asterisk/localtime.h"
#include "asterisk/app.h"

static char *app = "Rpt";

static char *synopsis = "Radio Repeater/Remote Base Control System";

static char *descrip = 
"  Rpt(nodename[,options]):  Radio Remote Link or Remote Base Link Endpoint Process.\n"
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
"        Rannounce-string[,timeout[,timeout-destination]] - Amateur Radio\n"
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

static unsigned int vmajor = 0;
static unsigned int vminor = 47;

static int debug = 0;  /* FIXME Set this >0 for extra debug output */
static int nrpts = 0;

char *discstr = "!!DISCONNECT!!";
static char *remote_rig_ft897 = "ft897";
static char *remote_rig_rbi = "rbi";

#define	MSWAIT 200
#define	HANGTIME 5000
#define	TOTIME 180000
#define	IDTIME 300000
#define	MAXRPTS 20
#define MAX_STAT_LINKS 32
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
	int	reconnects;
	long long connecttime;
	struct ast_channel *chan;	
	struct ast_channel *pchan;	
} ;

struct rpt_lstat
{
	struct	rpt_lstat *next;
	struct	rpt_lstat *prev;
	char	peer[MAXPEERSTR];
	char	name[MAXNODESTR];
	char	mode;
	char	outbound;
	char	reconnects;
	long long	connecttime;
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
	ast_mutex_t lock;
	struct ast_config *cfg;
	char reload;

	char *name;
	char *rxchanname;
	char *txchanname;
	char *remote;

	struct {
		char ourcontext[80];
		char ourcallerid[80];
		char acctcode[21];
		char ident[80];
		char tonezone[80];
		char simple;
		char functions[80];
		char link_functions[80];
		char phone_functions[80];
		char dphone_functions[80];
		char nodes[80];
		int hangtime;
		int totime;
		int idtime;
		int tailmessagetime;
		int tailsquashedtime;
		int duplex;
		int politeid;
		char *tailmsgbuf;
		AST_DECLARE_APP_ARGS(tailmsg,
			AST_APP_ARG(msgs)[100];
		);
		char memory[80];
		char macro[80];
		char gosub[80];
		char startupmacro[80];
		char startupgosub[80];
		int iobase;
		char funcchar;
		char endchar;
		char nobusyout;
	} p;
	struct rpt_link links;
	int unkeytocttimer;
	char keyed;
	char exttx;
	char localtx;
	char remoterx;
	char remotetx;
	char remoteon;
	char tounkeyed;
	char tonotify;
	char enable;
	char dtmfbuf[MAXDTMF];
	char macrobuf[MAXMACRO];
	char gosubbuf[MAXGOSUB];
	char rem_dtmfbuf[MAXDTMF];
	char lastdtmfcommand[MAXDTMF];
	char cmdnode[50];
	struct ast_channel *rxchannel, *txchannel;
	struct ast_channel *pchannel, *txpchannel, *remchannel;
	struct rpt_tele tele;
	struct timeval lasttv, curtv;
	pthread_t rpt_call_thread, rpt_thread;
	time_t dtmf_time, rem_dtmf_time, dtmf_time_rem;
	int tailtimer, totimer, idtimer, txconf, conf, callmode, cidx, scantimer, tmsgtimer, skedtimer;
	int mustid, tailid;
	int tailevent;
	int telemrefcount;
	int dtmfidx, rem_dtmfidx;
	int dailytxtime, dailykerchunks, totalkerchunks, dailykeyups, totalkeyups, timeouts;
	int totalexecdcommands, dailyexecdcommands;
	long retxtimer;
	long long totaltxtime;
	char mydtmf;
	char exten[AST_MAX_EXTENSION];
	char freq[MAXREMSTR], rxpl[MAXREMSTR], txpl[MAXREMSTR];
	char offset;
	char powerlevel;
	char txplon;
	char rxplon;
	char remmode;
	char tunerequest;
	char hfscanmode;
	int hfscanstatus;
	char lastlinknode[MAXNODESTR];
	char stopgen;
	char patchfarenddisconnect;
	char patchnoct;
	char patchquiet;
	char patchcontext[MAXPATCHCONTEXT];
	int patchdialtime;
	int macro_longest;
	int gosub_longest;
	int phone_longestfunc;
	int dphone_longestfunc;
	int link_longestfunc;
	int longestfunc;
	int longestnode;
	int threadrestarts;		
	int tailmessagen;
	time_t disgorgetime;
	time_t lastthreadrestarttime;
	long macrotimer;
	long gosubtimer;
	char lastnodewhichkeyedusup[MAXNODESTR];
#ifdef	__RPT_NOTCH
	struct rptfilter
	{
		char desc[100];
		float x0;
		float x1;
		float x2;
		float y0;
		float y1;
		float y2;
		float gain;
		float const0;
		float const1;
		float const2;
	} filters[MAXFILTERS];
#endif
#ifdef	_MDC_DECODE_H_
	mdc_decoder_t *mdc;
	unsigned short lastunit;
#endif
} rpt_vars[MAXRPTS];	


#ifdef	APP_RPT_LOCK_DEBUG

#warning COMPILING WITH LOCK-DEBUGGING ENABLED!!

#define	MAXLOCKTHREAD 100

#define	rpt_mutex_lock(x)	_rpt_mutex_lock(x, myrpt, __LINE__)
#define	rpt_mutex_unlock(x)	_rpt_mutex_unlock(x, myrpt, __LINE__)

struct lockthread
{
	pthread_t id;
	int lockcount;
	int lastlock;
	int lastunlock;
} lockthreads[MAXLOCKTHREAD];


struct by_lightning
{
	int line;
	struct timeval tv;
	struct rpt *rpt;
	struct lockthread lockthread;
} lock_ring[32];


int lock_ring_index = 0;

AST_MUTEX_DEFINE_STATIC(locklock);

static struct lockthread *get_lockthread(pthread_t id)
{
	int	i;

	for (i = 0; i < MAXLOCKTHREAD; i++) {
		if (lockthreads[i].id == id)
			return(&lockthreads[i]);
	}
	return NULL;
}

static struct lockthread *put_lockthread(pthread_t id)
{
	int	i;

	for (i = 0; i < MAXLOCKTHREAD; i++) {
		if (lockthreads[i].id == id)
			return(&lockthreads[i]);
	}
	for (i = 0; i < MAXLOCKTHREAD; i++) {
		if (!lockthreads[i].id) {
			lockthreads[i].lockcount = 0;
			lockthreads[i].lastlock = 0;
			lockthreads[i].lastunlock = 0;
			lockthreads[i].id = id;
			return &lockthreads[i];
		}
	}
	return NULL;
}


static void rpt_mutex_spew(void)
{
	struct by_lightning lock_ring_copy[32];
	int lock_ring_index_copy;
	int i, j;
	long long diff;
	char a[100] = "";
	struct ast_tm tm;
	struct timeval lasttv;

	ast_mutex_lock(&locklock);
	memcpy(&lock_ring_copy, &lock_ring, sizeof(lock_ring_copy));
	lock_ring_index_copy = lock_ring_index;
	ast_mutex_unlock(&locklock);

	lasttv.tv_sec = lasttv.tv_usec = 0;
	for (i = 0; i < 32; i++) {
		j = (i + lock_ring_index_copy) % 32;
		ast_strftime(a, sizeof(a) - 1, "%m/%d/%Y %H:%M:%S",
			ast_localtime(&lock_ring_copy[j].tv, &tm, NULL));
		diff = 0;
		if (lasttv.tv_sec) {
			diff = (lock_ring_copy[j].tv.tv_sec - lasttv.tv_sec) * 1000000;
			diff += (lock_ring_copy[j].tv.tv_usec - lasttv.tv_usec);
		}
		lasttv.tv_sec = lock_ring_copy[j].tv.tv_sec;
		lasttv.tv_usec = lock_ring_copy[j].tv.tv_usec;
		if (!lock_ring_copy[j].tv.tv_sec)
			continue;
		if (lock_ring_copy[j].line < 0) {
			ast_log(LOG_NOTICE, "LOCKDEBUG [#%d] UNLOCK app_rpt.c:%d node %s pid %x diff %lld us at %s.%06d\n",
				i - 31, -lock_ring_copy[j].line, lock_ring_copy[j].rpt->name,
				(int) lock_ring_copy[j].lockthread.id, diff, a, (int)lock_ring_copy[j].tv.tv_usec);
		} else {
			ast_log(LOG_NOTICE, "LOCKDEBUG [#%d] LOCK app_rpt.c:%d node %s pid %x diff %lld us at %s.%06d\n",
				i - 31, lock_ring_copy[j].line, lock_ring_copy[j].rpt->name,
				(int) lock_ring_copy[j].lockthread.id, diff, a, (int)lock_ring_copy[j].tv.tv_usec);
		}
	}
}


static void _rpt_mutex_lock(ast_mutex_t *lockp, struct rpt *myrpt, int line)
{
	struct lockthread *t;
	pthread_t id;

	id = pthread_self();
	ast_mutex_lock(&locklock);
	t = put_lockthread(id);
	if (!t) {
		ast_mutex_unlock(&locklock);
		return;
	}
	if (t->lockcount) {
		int lastline = t->lastlock;
		ast_mutex_unlock(&locklock);
		ast_log(LOG_NOTICE, "rpt_mutex_lock: Double lock request line %d node %s pid %x, last lock was line %d\n",
				line, myrpt->name, (int) t->id, lastline);
		rpt_mutex_spew();
		return;
	}
	t->lastlock = line;
	t->lockcount = 1;
	gettimeofday(&lock_ring[lock_ring_index].tv, NULL);
	lock_ring[lock_ring_index].rpt = myrpt;
	memcpy(&lock_ring[lock_ring_index].lockthread, t, sizeof(struct lockthread));
	lock_ring[lock_ring_index++].line = line;
	if (lock_ring_index == 32)
		lock_ring_index = 0;
	ast_mutex_unlock(&locklock);
	ast_mutex_lock(lockp);
}


static void _rpt_mutex_unlock(ast_mutex_t *lockp, struct rpt *myrpt, int line)
{
	struct lockthread *t;
	pthread_t id;

	id = pthread_self();
	ast_mutex_lock(&locklock);
	t = put_lockthread(id);
	if (!t) {
		ast_mutex_unlock(&locklock);
		return;
	}
	if (!t->lockcount) {
		int lastline = t->lastunlock;
		ast_mutex_unlock(&locklock);
		ast_log(LOG_NOTICE, "rpt_mutex_lock: Double un-lock request line %d node %s pid %x, last un-lock was line %d\n",
				line, myrpt->name, (int) t->id, lastline);
		rpt_mutex_spew();
		return;
	}
	t->lastunlock = line;
	t->lockcount = 0;
	gettimeofday(&lock_ring[lock_ring_index].tv, NULL);
	lock_ring[lock_ring_index].rpt = myrpt;
	memcpy(&lock_ring[lock_ring_index].lockthread, t, sizeof(struct lockthread));
	lock_ring[lock_ring_index++].line = -line;
	if (lock_ring_index == 32)
		lock_ring_index = 0;
	ast_mutex_unlock(&locklock);
	ast_mutex_unlock(lockp);
}

#else  /* APP_RPT_LOCK_DEBUG */

#define rpt_mutex_lock(x) ast_mutex_lock(x)
#define rpt_mutex_unlock(x) ast_mutex_unlock(x)

#endif  /* APP_RPT_LOCK_DEBUG */

/*
* CLI extensions
*/

/* Debug mode */
static char *handle_cli_rpt_debug_level(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *handle_cli_rpt_dump(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *handle_cli_rpt_stats(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *handle_cli_rpt_lstats(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *handle_cli_rpt_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *handle_cli_rpt_restart(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);

static struct ast_cli_entry cli_rpt[] = {
	AST_CLI_DEFINE(handle_cli_rpt_debug_level, "Enable app_rpt debuggin"),
	AST_CLI_DEFINE(handle_cli_rpt_dump,        "Dump app_rpt structs for debugging"),
	AST_CLI_DEFINE(handle_cli_rpt_stats,       "Dump node statistics"),
	AST_CLI_DEFINE(handle_cli_rpt_lstats,      "Dump link statistics"),
	AST_CLI_DEFINE(handle_cli_rpt_reload,      "Reload app_rpt config"),
	AST_CLI_DEFINE(handle_cli_rpt_restart,     "Restart app_rpt")
};

/*
* Telemetry defaults
*/


static struct telem_defaults tele_defs[] = {
	{"ct1", "|t(350,0,100,3072)(500,0,100,3072)(660,0,100,3072)"},
	{"ct2", "|t(660,880,150,3072)"},
	{"ct3", "|t(440,0,150,3072)"},
	{"ct4", "|t(550,0,150,3072)"},
	{"ct5", "|t(660,0,150,3072)"},
	{"ct6", "|t(880,0,150,3072)"},
	{"ct7", "|t(660,440,150,3072)"},
	{"ct8", "|t(700,1100,150,3072)"},
	{"remotemon", "|t(1600,0,75,2048)"},
	{"remotetx", "|t(2000,0,75,2048)(0,0,75,0)(1600,0,75,2048)"},
	{"cmdmode", "|t(900,904,200,2048)"},
	{"functcomplete", "|t(1000,0,100,2048)(0,0,100,0)(1000,0,100,2048)"}
} ;

/*
* Forward decl's - these suppress compiler warnings when funcs coded further down the file than their invocation
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
static int function_macro(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);
static int function_gosub(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);
/*
* Function table
*/

static struct function_table_tag function_table[] = {
	{"cop", function_cop},
	{"autopatchup", function_autopatchup},
	{"autopatchdn", function_autopatchdn},
	{"ilink", function_ilink},
	{"status", function_status},
	{"remote", function_remote},
	{"macro", function_macro},
	{"gosub", function_gosub},
} ;

/*
* Match a keyword in a list, and return index of string plus 1 if there was a match,
* else return 0. If param is passed in non-null, then it will be set to the first character past the match
*/

static int matchkeyword(char *string, char **param, char *keywords[])
{
	int	i, ls;
	for (i = 0; keywords[i]; i++) {
		ls = strlen(keywords[i]);
		if (!ls) {
			*param = NULL;
			return 0;
		}
		if (!strncmp(string, keywords[i], ls)) {
			if (param)
				*param = string + ls;
			return i + 1; 
		}
	}
	param = NULL;
	return 0;
}

/*
* Skip characters in string which are in charlist, and return a pointer to the
* first non-matching character
*/

static char *skipchars(char *string, char *charlist)
{
	int i;	
	while (*string) {
		for (i = 0; charlist[i] ; i++) {
			if (*string == charlist[i]) {
				string++;
				break;
			}
		}
		if (!charlist[i])
			return string;
	}
	return string;
}	
					


static int myatoi(const char *str)
{
	int	ret;

	if (str == NULL)
		return -1;
	/* leave this %i alone, non-base-10 input is useful here */
	if (sscanf(str, "%i", &ret) != 1)
		return -1;
	return ret;
}


#ifdef	__RPT_NOTCH

/* rpt filter routine */
static void rpt_filter(struct rpt *myrpt, volatile short *buf, int len)
{
	int	i, j;
	struct rptfilter *f;

	for (i = 0; i < len; i++) {
		for (j = 0; j < MAXFILTERS; j++) {
			f = &myrpt->filters[j];
			if (!*f->desc)
				continue;
			f->x0 = f->x1; f->x1 = f->x2;
			f->x2 = ((float)buf[i]) / f->gain;
			f->y0 = f->y1; f->y1 = f->y2;
			f->y2 =   (f->x0 + f->x2)     +  f->const0 * f->x1
			        + (f->const1 * f->y0) + (f->const2 * f->y1);
			buf[i] = (short)f->y2;
		}
	}
}

#endif

/* Retrieve an int from a config file */
static int retrieve_astcfgint(struct rpt *myrpt, const char *category, const char *name, int min, int max, int defl)
{
	const char *var = ast_variable_retrieve(myrpt->cfg, category, name);
	int ret;

	if (var) {
		ret = myatoi(var);
		if (ret < min)
			ret = min;
		else if (ret > max)
			ret = max;
	} else
		ret = defl;
	return ret;
}


static void load_rpt_vars(int n, int init)
{
	int	j;
	struct ast_variable *vp, *var;
	struct ast_config *cfg;
	struct ast_flags config_flags = { CONFIG_FLAG_NOCACHE };
#ifdef	__RPT_NOTCH
	AST_DECLARE_APP_ARGS(strs,
		AST_APP_ARG(str)[100];
	);
#endif

	ast_verb(3, "%s config for repeater %s\n",
			(init) ? "Loading initial" : "Re-Loading", rpt_vars[n].name);
	ast_mutex_lock(&rpt_vars[n].lock);
	if (rpt_vars[n].cfg)
		ast_config_destroy(rpt_vars[n].cfg);
	cfg = ast_config_load("rpt.conf", config_flags);
	if (!cfg) {
		ast_mutex_unlock(&rpt_vars[n].lock);
		ast_log(LOG_NOTICE, "Unable to open radio repeater configuration rpt.conf.  Radio Repeater disabled.\n");
		pthread_exit(NULL);
	}
	rpt_vars[n].cfg = cfg;
	/* Free previously malloc'ed buffer */
	if (!init && rpt_vars[n].p.tailmsgbuf)
		ast_free(rpt_vars[n].p.tailmsgbuf);
	memset(&rpt_vars[n].p, 0, sizeof(rpt_vars[n].p));
	if (init) {
		/* clear all the fields in the structure after 'p' */
		memset(&rpt_vars[n].p + sizeof(rpt_vars[0].p), 0, sizeof(rpt_vars[0]) - sizeof(rpt_vars[0].p) - offsetof(typeof(rpt_vars[0]), p));
		rpt_vars[n].tele.next = &rpt_vars[n].tele;
		rpt_vars[n].tele.prev = &rpt_vars[n].tele;
		rpt_vars[n].rpt_thread = AST_PTHREADT_NULL;
		rpt_vars[n].tailmessagen = 0;
	}
#ifdef	__RPT_NOTCH
	/* zot out filters stuff */
	memset(&rpt_vars[n].filters, 0, sizeof(rpt_vars[n].filters));
#endif

	/* Defaults */
	ast_copy_string(rpt_vars[n].p.ourcontext, rpt_vars[n].name, sizeof(rpt_vars[n].p.ourcontext));
	rpt_vars[n].p.hangtime = HANGTIME;
	rpt_vars[n].p.totime = TOTIME;
	rpt_vars[n].p.duplex = 2;
	rpt_vars[n].p.idtime = IDTIME;
	rpt_vars[n].p.politeid = POLITEID;
	ast_copy_string(rpt_vars[n].p.memory, MEMORY, sizeof(rpt_vars[n].p.memory));
	ast_copy_string(rpt_vars[n].p.macro, MACRO, sizeof(rpt_vars[n].p.macro));
	ast_copy_string(rpt_vars[n].p.gosub, GOSUB, sizeof(rpt_vars[n].p.gosub));
	rpt_vars[n].p.iobase = DEFAULT_IOBASE;
	ast_copy_string(rpt_vars[n].p.functions, FUNCTIONS, sizeof(rpt_vars[n].p.functions));
	rpt_vars[n].p.simple = 1;
	rpt_vars[n].p.funcchar = FUNCCHAR;
	rpt_vars[n].p.endchar = ENDCHAR;
	ast_copy_string(rpt_vars[n].p.nodes, NODES, sizeof(rpt_vars[n].p.nodes));

	for (var = ast_variable_browse(cfg, rpt_vars[n].name); var; var = var->next) {
		if (!strcmp(var->name, "context")) {
			ast_copy_string(rpt_vars[n].p.ourcontext, var->value, sizeof(rpt_vars[n].p.ourcontext));
		} else if (!strcmp(var->name, "callerid")) {
			ast_copy_string(rpt_vars[n].p.ourcallerid, var->value, sizeof(rpt_vars[n].p.ourcallerid));
		} else if (!strcmp(var->name, "accountcode")) {
			ast_copy_string(rpt_vars[n].p.acctcode, var->value, sizeof(rpt_vars[n].p.acctcode));
		} else if (!strcmp(var->name, "idrecording")) {
			ast_copy_string(rpt_vars[n].p.ident, var->value, sizeof(rpt_vars[n].p.ident));
		} else if (!strcmp(var->name, "hangtime")) {
			rpt_vars[n].p.hangtime = atoi(var->value);
		} else if (!strcmp(var->name, "totime")) {
			rpt_vars[n].p.totime = atoi(var->value);
		} else if (!strcmp(var->name, "tailmessagetime")) {
			rpt_vars[n].p.tailmessagetime = atoi(var->value);
			if (rpt_vars[n].p.tailmessagetime < 0)
				rpt_vars[n].p.tailmessagetime = 0;
			else if (rpt_vars[n].p.tailmessagetime > 2400000)
				rpt_vars[n].p.tailmessagetime = 2400000;
		} else if (!strcmp(var->name, "tailsquashedtime")) {
			rpt_vars[n].p.tailsquashedtime = atoi(var->value);
			if (rpt_vars[n].p.tailsquashedtime < 0)
				rpt_vars[n].p.tailsquashedtime = 0;
			else if (rpt_vars[n].p.tailsquashedtime > 2400000)
				rpt_vars[n].p.tailsquashedtime = 2400000;
		} else if (!strcmp(var->name, "duplex")) {
			rpt_vars[n].p.duplex = atoi(var->value);
			if (rpt_vars[n].p.duplex < 0)
				rpt_vars[n].p.duplex = 0;
			else if (rpt_vars[n].p.duplex > 4)
				rpt_vars[n].p.duplex = 4;
		} else if (!strcmp(var->name, "idtime")) {
			rpt_vars[n].p.idtime = atoi(var->value);
			if (rpt_vars[n].p.idtime < 60000)
				rpt_vars[n].p.idtime = 60000;
			else if (rpt_vars[n].p.idtime > 2400000)
				rpt_vars[n].p.idtime = 2400000;
		} else if (!strcmp(var->name, "politeid")) {
			rpt_vars[n].p.politeid = atoi(var->value);
			if (rpt_vars[n].p.politeid < 30000)
				rpt_vars[n].p.politeid = 30000;
			else if (rpt_vars[n].p.politeid > 300000)
				rpt_vars[n].p.politeid = 300000;
		} else if (!strcmp(var->name, "tonezone")) {
			ast_copy_string(rpt_vars[n].p.tonezone, var->value, sizeof(rpt_vars[n].p.tonezone));
		} else if (!strcmp(var->name, "tailmessagelist")) {
			rpt_vars[n].p.tailmsgbuf = ast_strdup(var->value);
			AST_STANDARD_APP_ARGS(rpt_vars[n].p.tailmsg, rpt_vars[n].p.tailmsgbuf);
		} else if (!strcmp(var->name, "memory")) {
			ast_copy_string(rpt_vars[n].p.memory, var->value, sizeof(rpt_vars[n].p.memory));
		} else if (!strcmp(var->name, "macro")) {
			ast_copy_string(rpt_vars[n].p.macro, var->value, sizeof(rpt_vars[n].p.macro));
		} else if (!strcmp(var->name, "gosub")) {
			ast_copy_string(rpt_vars[n].p.gosub, var->value, sizeof(rpt_vars[n].p.gosub));
		} else if (!strcmp(var->name, "startup_macro")) {
			ast_copy_string(rpt_vars[n].p.startupmacro, var->value, sizeof(rpt_vars[n].p.startupmacro));
		} else if (!strcmp(var->name, "startup_gosub")) {
			ast_copy_string(rpt_vars[n].p.startupgosub, var->value, sizeof(rpt_vars[n].p.startupgosub));
		} else if (!strcmp(var->name, "iobase")) {
			/* do not use atoi() here, we need to be able to have
			   the input specified in hex or decimal so we use
			   sscanf with a %i */
			if (sscanf(var->value, "%i", &rpt_vars[n].p.iobase) != 1)
				rpt_vars[n].p.iobase = DEFAULT_IOBASE;
		} else if (!strcmp(var->name, "functions")) {
			rpt_vars[n].p.simple = 0;
			ast_copy_string(rpt_vars[n].p.functions, var->value, sizeof(rpt_vars[n].p.functions));
		} else if (!strcmp(var->name, "link_functions")) {
			ast_copy_string(rpt_vars[n].p.link_functions, var->value, sizeof(rpt_vars[n].p.link_functions));
		} else if (!strcmp(var->name, "phone_functions")) {
			ast_copy_string(rpt_vars[n].p.phone_functions, var->value, sizeof(rpt_vars[n].p.phone_functions));
		} else if (!strcmp(var->name, "dphone_functions")) {
			ast_copy_string(rpt_vars[n].p.dphone_functions, var->value, sizeof(rpt_vars[n].p.dphone_functions));
		} else if (!strcmp(var->name, "funcchar")) {
			rpt_vars[n].p.funcchar = *var->value;
		} else if (!strcmp(var->name, "endchar")) {
			rpt_vars[n].p.endchar = *var->value;
		} else if (!strcmp(var->name, "nobusyout")) {
			rpt_vars[n].p.nobusyout = ast_true(var->value);
		} else if (!strcmp(var->name, "nodes")) {
			ast_copy_string(rpt_vars[n].p.nodes, var->value, sizeof(rpt_vars[n].p.nodes));
#ifdef	__RPT_NOTCH
		} else if (!strcmp(var->name, "rxnotch")) {
			char *tmp = ast_strdupa(val);
			AST_STANDARD_APP_ARGS(strs, tmp);
			strs.argc &= ~1; /* force an even number, rounded down */
			if (strs.argc >= 2) {
				for (j = 0; j < strs.argc; j += 2) {
					rpt_mknotch(atof(strs.str[j]), atof(strs.str[j + 1]),
						&rpt_vars[n].filters[j >> 1].gain,
						&rpt_vars[n].filters[j >> 1].const0,
						&rpt_vars[n].filters[j >> 1].const1,
						&rpt_vars[n].filters[j >> 1].const2);
					sprintf(rpt_vars[n].filters[j >> 1].desc, "%s Hz, BW = %s",
						strs.str[j], strs.str[j + 1]);
				}
			}
#endif
		}
	}

	/* If these aren't specified, copy them from the functions property. */
	if (ast_strlen_zero(rpt_vars[n].p.link_functions))
		ast_copy_string(rpt_vars[n].p.link_functions, rpt_vars[n].p.functions, sizeof(rpt_vars[n].p.link_functions));

	rpt_vars[n].longestnode = 0;
	for (vp = ast_variable_browse(cfg, rpt_vars[n].p.nodes); vp; vp = vp->next) {
		if ((j = strlen(vp->name)) > rpt_vars[n].longestnode)
			rpt_vars[n].longestnode = j;
	}

	/*
	* For this repeater, Determine the length of the longest function 
	*/
	rpt_vars[n].longestfunc = 0;
	for (vp = ast_variable_browse(cfg, rpt_vars[n].p.functions); vp; vp = vp->next) {
		if ((j = strlen(vp->name)) > rpt_vars[n].longestfunc)
			rpt_vars[n].longestfunc = j;
	}

	rpt_vars[n].link_longestfunc = 0;
	for (vp = ast_variable_browse(cfg, rpt_vars[n].p.link_functions); vp; vp = vp->next) {
		if ((j = strlen(vp->name)) > rpt_vars[n].link_longestfunc)
			rpt_vars[n].link_longestfunc = j;
	}

	rpt_vars[n].phone_longestfunc = 0;
	for (vp = ast_variable_browse(cfg, rpt_vars[n].p.phone_functions); vp; vp = vp->next) {
		if ((j = strlen(vp->name)) > rpt_vars[n].phone_longestfunc)
			rpt_vars[n].phone_longestfunc = j;
	}

	rpt_vars[n].dphone_longestfunc = 0;
	for (vp = ast_variable_browse(cfg, rpt_vars[n].p.dphone_functions); vp; vp = vp->next) {
		if ((j = strlen(vp->name)) > rpt_vars[n].dphone_longestfunc)
			rpt_vars[n].dphone_longestfunc = j;
	}

	rpt_vars[n].macro_longest = 1;
	for (vp = ast_variable_browse(cfg, rpt_vars[n].p.macro); vp; vp = vp->next) {
		if ((j = strlen(vp->name)) > rpt_vars[n].macro_longest)
			rpt_vars[n].macro_longest = j;
	}

	rpt_vars[n].gosub_longest = 1;
	for (vp = ast_variable_browse(cfg, rpt_vars[n].p.gosub); vp; vp = vp->next) {
		if ((j = strlen(vp->name)) > rpt_vars[n].gosub_longest)
			rpt_vars[n].gosub_longest = j;
	}
	ast_mutex_unlock(&rpt_vars[n].lock);
}

/*
* Enable or disable debug output at a given level at the console
*/
static char *handle_cli_rpt_debug_level(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int newlevel;

	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt debug level";
		e->usage =
			"Usage: rpt debug level {0-7}\n"
			"       Enables debug messages in app_rpt\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc != e->args)
		return CLI_SHOWUSAGE;
	newlevel = myatoi(a->argv[3]);
	if ((newlevel < 0) || (newlevel > 7))
		return CLI_SHOWUSAGE;
	if (newlevel)
		ast_cli(a->fd, "app_rpt Debugging enabled, previous level: %d, new level: %d\n", debug, newlevel);
	else
		ast_cli(a->fd, "app_rpt Debugging disabled\n");

	debug = newlevel;

	return CLI_SUCCESS;
}

/*
* Dump rpt struct debugging onto console
*/
static char *handle_cli_rpt_dump(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt dump";
		e->usage =
			"Usage: rpt dump <nodename>\n"
			"       Dumps struct debug info to log\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3)
		return CLI_SHOWUSAGE;

	for (i = 0; i < nrpts; i++) {
		if (!strcmp(a->argv[2], rpt_vars[i].name)) {
			rpt_vars[i].disgorgetime = time(NULL) + 10; /* Do it 10 seconds later */
			ast_cli(a->fd, "app_rpt struct dump requested for node %s\n", a->argv[2]);
			return CLI_SUCCESS;
		}
	}
	return CLI_FAILURE;
}

/*
* Dump statistics onto console
*/
static char *handle_cli_rpt_stats(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int i, j;
	int dailytxtime, dailykerchunks;
	int totalkerchunks, dailykeyups, totalkeyups, timeouts;
	int totalexecdcommands, dailyexecdcommands, hours, minutes, seconds;
	long long totaltxtime;
	struct rpt_link *l;
	char *listoflinks[MAX_STAT_LINKS];	
	char *lastnodewhichkeyedusup, *lastdtmfcommand;
	char *tot_state, *ider_state, *patch_state;
	char *reverse_patch_state, *enable_state, *input_signal, *called_number;
	struct rpt *myrpt;

	static char *not_applicable = "N/A";

	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt stats";
		e->usage =
			"Usage: rpt stats <nodename>\n"
			"       Dumps node statistics to console\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3)
		return CLI_SHOWUSAGE;

	for (i = 0 ; i <= MAX_STAT_LINKS; i++)
		listoflinks[i] = NULL;

	tot_state = ider_state = 
	patch_state = reverse_patch_state = 
	input_signal = called_number = 
	lastdtmfcommand = not_applicable;

	for (i = 0; i < nrpts; i++) {
		if (!strcmp(a->argv[2], rpt_vars[i].name)) {
			/* Make a copy of all stat variables while locked */
			myrpt = &rpt_vars[i];
			rpt_mutex_lock(&myrpt->lock); /* LOCK */

			dailytxtime = myrpt->dailytxtime;
			totaltxtime = myrpt->totaltxtime;
			dailykeyups = myrpt->dailykeyups;
			totalkeyups = myrpt->totalkeyups;
			dailykerchunks = myrpt->dailykerchunks;
			totalkerchunks = myrpt->totalkerchunks;
			dailyexecdcommands = myrpt->dailyexecdcommands;
			totalexecdcommands = myrpt->totalexecdcommands;
			timeouts = myrpt->timeouts;

			/* Traverse the list of connected nodes */
			reverse_patch_state = "DOWN";
			j = 0;
			l = myrpt->links.next;
			while (l != &myrpt->links) {
				if (l->name[0] == '0') { /* Skip '0' nodes */
					reverse_patch_state = "UP";
					l = l->next;
					continue;
				}
				listoflinks[j] = ast_strdupa(l->name);
				if (listoflinks[j])
					j++;
				l = l->next;
			}

			lastnodewhichkeyedusup = ast_strdupa(myrpt->lastnodewhichkeyedusup);			
			if ((!lastnodewhichkeyedusup) || (ast_strlen_zero(lastnodewhichkeyedusup)))
				lastnodewhichkeyedusup = not_applicable;

			if (myrpt->keyed)
				input_signal = "YES";
			else
				input_signal = "NO";

			if (myrpt->enable)
				enable_state = "YES";
			else
				enable_state = "NO";

			if (!myrpt->totimer)
				tot_state = "TIMED OUT!";
			else if (myrpt->totimer != myrpt->p.totime)
				tot_state = "ARMED";
			else
				tot_state = "RESET";

			if (myrpt->tailid)
				ider_state = "QUEUED IN TAIL";
			else if (myrpt->mustid)
				ider_state = "QUEUED FOR CLEANUP";
			else
				ider_state = "CLEAN";

			switch (myrpt->callmode) {
			case 1:
				patch_state = "DIALING";
				break;
			case 2:
				patch_state = "CONNECTING";
				break;
			case 3:
				patch_state = "UP";
				break;
			case 4:
				patch_state = "CALL FAILED";
				break;
			default:
				patch_state = "DOWN";
			}

			if (!ast_strlen_zero(myrpt->exten))
				called_number = ast_strdupa(myrpt->exten);

			if (!ast_strlen_zero(myrpt->lastdtmfcommand))
				lastdtmfcommand = ast_strdupa(myrpt->lastdtmfcommand);

			rpt_mutex_unlock(&myrpt->lock); /* UNLOCK */

			ast_cli(a->fd, "************************ NODE %s STATISTICS *************************\n\n", myrpt->name);
			ast_cli(a->fd, "Signal on input..................................: %s\n", input_signal);
			ast_cli(a->fd, "Transmitter enabled..............................: %s\n", enable_state);
			ast_cli(a->fd, "Time out timer state.............................: %s\n", tot_state);
			ast_cli(a->fd, "Time outs since system initialization............: %d\n", timeouts);
			ast_cli(a->fd, "Identifier state.................................: %s\n", ider_state);
			ast_cli(a->fd, "Kerchunks today..................................: %d\n", dailykerchunks);
			ast_cli(a->fd, "Kerchunks since system initialization............: %d\n", totalkerchunks);
			ast_cli(a->fd, "Keyups today.....................................: %d\n", dailykeyups);
			ast_cli(a->fd, "Keyups since system initialization...............: %d\n", totalkeyups);
			ast_cli(a->fd, "DTMF commands today..............................: %d\n", dailyexecdcommands);
			ast_cli(a->fd, "DTMF commands since system initialization........: %d\n", totalexecdcommands);
			ast_cli(a->fd, "Last DTMF command executed.......................: %s\n", lastdtmfcommand);

			hours = dailytxtime / 3600000;
			dailytxtime %= 3600000;
			minutes = dailytxtime / 60000;
			dailytxtime %= 60000;
			seconds = dailytxtime / 1000;
			dailytxtime %= 1000;

			ast_cli(a->fd, "TX time today ...................................: %02d:%02d:%02d.%d\n",
				hours, minutes, seconds, dailytxtime);

			hours = (int) totaltxtime / 3600000;
			totaltxtime %= 3600000;
			minutes = (int) totaltxtime / 60000;
			totaltxtime %= 60000;
			seconds = (int)  totaltxtime / 1000;
			totaltxtime %= 1000;

			ast_cli(a->fd, "TX time since system initialization..............: %02d:%02d:%02d.%d\n",
				 hours, minutes, seconds, (int) totaltxtime);
			ast_cli(a->fd, "Nodes currently connected to us..................: ");
			for (j = 0;; j++) {
				if (!listoflinks[j]) {
					if (!j) {
						ast_cli(a->fd, "<NONE>");
					}
					break;
				}
				ast_cli(a->fd, "%s", listoflinks[j]);
				if (j % 4 == 3) {
					ast_cli(a->fd, "\n");
					ast_cli(a->fd, "                                                 : ");
				} else {
					if (listoflinks[j + 1])
						ast_cli(a->fd, ", ");
				}
			}
			ast_cli(a->fd, "\n");

			ast_cli(a->fd, "Last node which transmitted to us................: %s\n", lastnodewhichkeyedusup);
			ast_cli(a->fd, "Autopatch state..................................: %s\n", patch_state);
			ast_cli(a->fd, "Autopatch called number..........................: %s\n", called_number);
			ast_cli(a->fd, "Reverse patch/IAXRPT connected...................: %s\n\n", reverse_patch_state);

			return CLI_SUCCESS;
		}
	}
	return CLI_FAILURE;
}

/*
* Link stats function
*/
static char *handle_cli_rpt_lstats(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int i, j;
	struct rpt *myrpt;
	struct rpt_link *l;
	struct rpt_lstat *s, *t;
	struct rpt_lstat s_head;

	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt lstats";
		e->usage =
			"Usage: rpt lstats <nodename>\n"
			"       Dumps link statistics to console\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3)
		return CLI_SHOWUSAGE;

	s = NULL;
	s_head.next = &s_head;
	s_head.prev = &s_head;

	for (i = 0; i < nrpts; i++) {
		if (!strcmp(a->argv[2], rpt_vars[i].name)) {
			/* Make a copy of all stat variables while locked */
			myrpt = &rpt_vars[i];
			rpt_mutex_lock(&myrpt->lock); /* LOCK */
			/* Traverse the list of connected nodes */
			j = 0;
			l = myrpt->links.next;
			while (l != &myrpt->links) {
				if (l->name[0] == '0') { /* Skip '0' nodes */
					l = l->next;
					continue;
				}
				if ((s = ast_calloc(1, sizeof(*s))) == NULL) {
					ast_log(LOG_ERROR, "Malloc failed in rpt_do_lstats\n");
					rpt_mutex_unlock(&myrpt->lock); /* UNLOCK */
					return CLI_FAILURE;
				}
				ast_copy_string(s->name, l->name, MAXREMSTR);
				pbx_substitute_variables_helper(l->chan, "${IAXPEER(CURRENTCHANNEL)}", s->peer, MAXPEERSTR - 1);
				s->mode = l->mode;
				s->outbound = l->outbound;
				s->reconnects = l->reconnects;
				s->connecttime = l->connecttime;
				insque((struct qelem *) s, (struct qelem *) s_head.next);
				l = l->next;
			}
			rpt_mutex_unlock(&myrpt->lock); /* UNLOCK */
			ast_cli(a->fd, "NODE      PEER                RECONNECTS  DIRECTION  CONNECT TIME\n");
			ast_cli(a->fd, "----      ----                ----------  ---------  ------------\n");

			for (s = s_head.next; s != &s_head; s = s->next) {
				int hours, minutes, seconds;
				long long connecttime = s->connecttime;
				char conntime[31];
				hours = (int) connecttime/3600000;
				connecttime %= 3600000;
				minutes = (int) connecttime/60000;
				connecttime %= 60000;
				seconds = (int)  connecttime/1000;
				connecttime %= 1000;
				snprintf(conntime, sizeof(conntime), "%02d:%02d:%02d.%d",
					hours, minutes, seconds, (int) connecttime);
				ast_cli(a->fd, "%-10s%-20s%-12d%-11s%-30s\n",
					s->name, s->peer, s->reconnects, (s->outbound)? "OUT":"IN", conntime);
			}	
			/* destroy our local link queue */
			s = s_head.next;
			while (s != &s_head) {
				t = s;
				s = s->next;
				remque((struct qelem *)t);
				ast_free(t);
			}			
			return CLI_SUCCESS;
		}
	}

	return CLI_FAILURE;
}

/*
* reload vars 
*/
static char *handle_cli_rpt_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int	n;

	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt reload";
		e->usage =
			"Usage: rpt reload\n"
			"       Reloads app_rpt running config parameters\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc > 2)
		return CLI_SHOWUSAGE;

	for (n = 0; n < nrpts; n++)
		rpt_vars[n].reload = 1;

	return CLI_SUCCESS;
}

/*
* restart app_rpt
*/
static char *handle_cli_rpt_restart(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int	i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "rpt restart";
		e->usage =
			"Usage: rpt restart\n"
			"       Restarts app_rpt\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc > 2)
		return CLI_SHOWUSAGE;
	for (i = 0; i < nrpts; i++) {
		if (rpt_vars[i].rxchannel)
			ast_softhangup(rpt_vars[i].rxchannel, AST_SOFTHANGUP_DEV);
	}
	return CLI_SUCCESS;
}

static int play_tone_pair(struct ast_channel *chan, int f1, int f2, int duration, int amplitude)
{
	int res;

	if ((res = ast_tonepair_start(chan, f1, f2, duration, amplitude)))
		return res;

	while (chan->generatordata) {
		if (ast_safe_sleep(chan, 1))
			return -1;
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


static int send_morse(struct ast_channel *chan, const char *string, int speed, int freq, int amplitude)
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
	
	dottime = 900 / speed;
	
	/* Establish timing relationships */
	
	dashtime = 3 * dottime;
	intralettertime = dottime;
	interlettertime = dottime * 4 ;
	interwordtime = dottime * 7;
	
	for (; (*string) && (!res); string++) {
	
		c = *string;
		
		/* Convert lower case to upper case */
		
		if ((c >= 'a') && (c <= 'z'))
			c -= 0x20;
		
		/* Can't deal with any char code greater than Z, skip it */
		
		if (c  > 'Z')
			continue;
		
		/* If space char, wait the inter word time */
					
		if (c == ' ') {
			if (!res)
				res = play_silence(chan, interwordtime);
			continue;
		}
		
		/* Subtract out control char offset to match our table */
		
		c -= 0x20;
		
		/* Get the character data */
		
		len = mbits[c].len;
		ddcomb = mbits[c].ddcomb;
		
		/* Send the character */
		
		for (; len ; len--) {
			if (!res)
				res = play_tone(chan, freq, (ddcomb & 1) ? dashtime : dottime, amplitude);
			if (!res)
				res = play_silence(chan, intralettertime);
			ddcomb >>= 1;
		}
		
		/* Wait the interletter time */
		
		if (!res)
			res = play_silence(chan, interlettertime - intralettertime);
	}
	
	/* Wait for all the frames to be sent */
	
	if (!res) 
		res = ast_waitstream(chan, "");
	ast_stopstream(chan);
	
	/*
	* Wait for the dahdi driver to physically write the tone blocks to the hardware
	*/

	for (i = 0; i < 20 ; i++) {
		flags =  DAHDI_IOMUX_WRITEEMPTY | DAHDI_IOMUX_NOWAIT; 
		res = ioctl(chan->fds[0], DAHDI_IOMUX, &flags);
		if (flags & DAHDI_IOMUX_WRITEEMPTY)
			break;
		if ( ast_safe_sleep(chan, 50)) {
			res = -1;
			break;
		}
	}

	
	return res;
}

static int send_tone_telemetry(struct ast_channel *chan, const char *tonestring)
{
	char *stringp;
	char *tonesubset;
	int f1, f2;
	int duration;
	int amplitude;
	int res;
	int i;
	int flags;
	
	res = 0;
	
	stringp = ast_strdupa(tonestring);

	for (;tonestring;) {
		tonesubset = strsep(&stringp, ")");
		if (!tonesubset)
			break;
		if (sscanf(tonesubset, "(%d,%d,%d,%d", &f1, &f2, &duration, &amplitude) != 4)
			break;
		res = play_tone_pair(chan, f1, f2, duration, amplitude);
		if (res)
			break;
	}
	if (!res)
		res = play_tone_pair(chan, 0, 0, 100, 0); /* This is needed to ensure the last tone segment is timed correctly */
	
	if (!res) 
		res = ast_waitstream(chan, "");
	ast_stopstream(chan);

	/*
	* Wait for the dahdi driver to physically write the tone blocks to the hardware
	*/

	for (i = 0; i < 20 ; i++) {
		flags =  DAHDI_IOMUX_WRITEEMPTY | DAHDI_IOMUX_NOWAIT; 
		res = ioctl(chan->fds[0], DAHDI_IOMUX, &flags);
		if (flags & DAHDI_IOMUX_WRITEEMPTY)
			break;
		if (ast_safe_sleep(chan, 50)) {
			res = -1;
			break;
		}
	}
		
	return res;
}
	

static int sayfile(struct ast_channel *mychannel, const char *fname)
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

static int saycharstr(struct ast_channel *mychannel, char *str)
{
	int	res;

	res = ast_say_character_str(mychannel, str, NULL, mychannel->language);
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
	if (!res)
		res = ast_waitstream(mychannel, "");
	else
		ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
	ast_stopstream(mychannel);
	return res;
}

static int saydigits(struct ast_channel *mychannel, int num)
{
	int res;
	res = ast_say_digits(mychannel, num, NULL, mychannel->language);
	if (!res)
		res = ast_waitstream(mychannel, "");
	else
		ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
	ast_stopstream(mychannel);
	return res;
}


static int telem_any(struct rpt *myrpt, struct ast_channel *chan, const char *entry)
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
	
	if (!morseidfreq) { /* Get the morse parameters if not already loaded */
		morsespeed = retrieve_astcfgint(myrpt, mcat, "speed", 5, 20, 20);
		morsefreq = retrieve_astcfgint(myrpt, mcat, "frequency", 300, 3000, 800);
		morseampl = retrieve_astcfgint(myrpt, mcat, "amplitude", 200, 8192, 4096);
		morseidampl = retrieve_astcfgint(myrpt, mcat, "idamplitude", 200, 8192, 2048);
		morseidfreq = retrieve_astcfgint(myrpt, mcat, "idfrequency", 300, 3000, 330);	
	}
	
	/* Is it a file, or a tone sequence? */
			
	if (entry[0] == '|') {
		c = entry[1];
		if ((c >= 'a') && (c <= 'z'))
			c -= 0x20;
	
		switch (c) {
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
	} else
		res = sayfile(chan, entry); /* File */
	return res;
}

/*
* This function looks up a telemetry name in the config file, and does a telemetry response as configured.
*
* 4 types of telemtry are handled: Morse ID, Morse Message, Tone Sequence, and a File containing a recording.
*/

static int telem_lookup(struct rpt *myrpt, struct ast_channel *chan, const char *node, const char *name)
{
	int res = 0;
	int i;
	const char *entry = NULL;
	const char *telemetry;

	/* Retrieve the section name for telemetry from the node section */
	if ((telemetry = ast_variable_retrieve(myrpt->cfg, node, TELEMETRY)))
		entry = ast_variable_retrieve(myrpt->cfg, telemetry, name);

	/* Try to look up the telemetry name */	

	if (!entry) {
		/* Telemetry name wasn't found in the config file, use the default */
		for (i = 0; i < sizeof(tele_defs) / sizeof(struct telem_defaults); i++) {
			if (!strcasecmp(tele_defs[i].name, name))
				entry = tele_defs[i].value;
		}
	}
	if (entry) {	
		if (!ast_strlen_zero(entry))
			telem_any(myrpt, chan, entry);
	} else {
		res = -1;
	}
	return res;
}

/*
* Retrieve a wait interval
*/

static int get_wait_interval(struct rpt *myrpt, int type)
{
	int interval = 1000;
	const char *wait_times = ast_variable_retrieve(myrpt->cfg, myrpt->name, "wait_times");

	switch (type) {
	case DLY_TELEM:
		if (wait_times)
			interval = retrieve_astcfgint(myrpt, wait_times, "telemwait", 500, 5000, 1000);
		break;
	case DLY_ID:
		if (wait_times)
			interval = retrieve_astcfgint(myrpt, wait_times, "idwait", 250, 5000, 500);
		else
			interval = 500;
		break;
	case DLY_UNKEY:
		if (wait_times)
			interval = retrieve_astcfgint(myrpt, wait_times, "unkeywait", 500, 5000, 1000);
		break;
	case DLY_CALLTERM:
		if (wait_times)
			interval = retrieve_astcfgint(myrpt, wait_times, "calltermwait", 500, 5000, 1500);
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
	interval = get_wait_interval(myrpt, type);
	if (debug)
		ast_log(LOG_NOTICE, " Delay interval = %d\n", interval);
	if (interval)
		ast_safe_sleep(chan, interval);
	if (debug)
		ast_log(LOG_NOTICE, "Delay complete\n");
	return;
}


static void *rpt_tele_thread(void *this)
{
	struct dahdi_confinfo ci;  /* conference info */
	int	res = 0, haslink, hastx, hasremote, imdone = 0, unkeys_queued, x;
	struct rpt_tele *mytele = (struct rpt_tele *)this;
	struct rpt_tele *tlist;
	struct rpt *myrpt;
	struct rpt_link *l, *m, linkbase;
	struct ast_channel *mychannel;
	const char *p, *ct;
	struct timeval tv;
	struct ast_tm localtm;
#ifdef  APP_RPT_LOCK_DEBUG
	struct lockthread *t;
#endif

	/* get a pointer to myrpt */
	myrpt = mytele->rpt;

	/* Snag copies of a few key myrpt variables */
	rpt_mutex_lock(&myrpt->lock);
	rpt_mutex_unlock(&myrpt->lock);
	
	/* allocate a pseudo-channel thru asterisk */
	mychannel = ast_request("zap", AST_FORMAT_SLINEAR, "pseudo", NULL);
	if (!mychannel) {
		ast_log(LOG_WARNING, "rpt: unable to obtain pseudo channel\n");
		rpt_mutex_lock(&myrpt->lock);
		remque((struct qelem *)mytele);
		ast_log(LOG_NOTICE, "Telemetry thread aborted at line %d, mode: %d\n", __LINE__, mytele->mode); /*@@@@@@@@@@@*/
		rpt_mutex_unlock(&myrpt->lock);
		ast_free(mytele);
		pthread_exit(NULL);
	}
	rpt_mutex_lock(&myrpt->lock);
	mytele->chan = mychannel; /* Save a copy of the channel so we can access it externally if need be */
	rpt_mutex_unlock(&myrpt->lock);
	
	/* make a conference for the tx */
	ci.chan = 0;
	/* If there's an ID queued, or tail message queued, */
	/* only connect the ID audio to the local tx conference so */
	/* linked systems can't hear it */
	ci.confno = (((mytele->mode == ID) || (mytele->mode == IDTALKOVER) || (mytele->mode == UNKEY) || 
		(mytele->mode == TAILMSG)) ?
		 	myrpt->txconf : myrpt->conf);
	ci.confmode = DAHDI_CONF_CONFANN;
	/* first put the channel on the conference in announce mode */
	if (ioctl(mychannel->fds[0], DAHDI_SETCONF, &ci) == -1) {
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		rpt_mutex_lock(&myrpt->lock);
		remque((struct qelem *)mytele);
		rpt_mutex_unlock(&myrpt->lock);
		ast_log(LOG_NOTICE, "Telemetry thread aborted at line %d, mode: %d\n", __LINE__, mytele->mode); /*@@@@@@@@@@@*/
		ast_free(mytele);
		ast_hangup(mychannel);
		pthread_exit(NULL);
	}
	ast_stopstream(mychannel);
	switch (mytele->mode) {
	case ID:
	case ID1:
		/* wait a bit */
		wait_interval(myrpt, (mytele->mode == ID) ? DLY_ID : DLY_TELEM, mychannel);
		res = telem_any(myrpt, mychannel, myrpt->p.ident); 
		imdone=1;	
		break;
		
	case TAILMSG:
		res = ast_streamfile(mychannel, myrpt->p.tailmsg.msgs[myrpt->tailmessagen], mychannel->language); 
		break;
		
	case IDTALKOVER:
		p = ast_variable_retrieve(myrpt->cfg, myrpt->name, "idtalkover");
		if (p)
			res = telem_any(myrpt, mychannel, p); 
		imdone = 1;	
		break;
	case PROC:
		/* wait a little bit longer */
		wait_interval(myrpt, DLY_TELEM, mychannel);
		res = telem_lookup(myrpt, mychannel, myrpt->name, "patchup");
		if (res < 0) { /* Then default message */
			res = ast_streamfile(mychannel, "rpt/callproceeding", mychannel->language);
		}
		break;
	case TERM:
		/* wait a little bit longer */
		wait_interval(myrpt, DLY_CALLTERM, mychannel);
		res = telem_lookup(myrpt, mychannel, myrpt->name, "patchdown");
		if (res < 0) { /* Then default message */
			res = ast_streamfile(mychannel, "rpt/callterminated", mychannel->language);
		}
		break;
	case COMPLETE:
		/* wait a little bit */
		wait_interval(myrpt, DLY_TELEM, mychannel);
		res = telem_lookup(myrpt, mychannel, myrpt->name, "functcomplete");
		break;
	case MACRO_NOTFOUND:
		/* wait a little bit */
		wait_interval(myrpt, DLY_TELEM, mychannel);
		res = ast_streamfile(mychannel, "rpt/macro_notfound", mychannel->language);
		break;
	case GOSUB_NOTFOUND:
		/* wait a little bit */
		wait_interval(myrpt, DLY_TELEM, mychannel);
		res = ast_streamfile(mychannel, "rpt/gosub_notfound", mychannel->language);
		break;
	case MACRO_BUSY:
		/* wait a little bit */
		wait_interval(myrpt, DLY_TELEM, mychannel);
		res = ast_streamfile(mychannel, "rpt/macro_busy", mychannel->language);
		break;
	case GOSUB_BUSY:
		/* wait a little bit */
		wait_interval(myrpt, DLY_TELEM, mychannel);
		res = ast_streamfile(mychannel, "rpt/gosub_busy", mychannel->language);
		break;
	case UNKEY:
		if (myrpt->patchnoct && myrpt->callmode) { /* If no CT during patch configured, then don't send one */
			imdone = 1;
			break;
		}
			
		/*
		* Reset the Unkey to CT timer
		*/

		x = get_wait_interval(myrpt, DLY_UNKEY);
		rpt_mutex_lock(&myrpt->lock);
		myrpt->unkeytocttimer = x; /* Must be protected as it is changed below */
		rpt_mutex_unlock(&myrpt->lock);

		/*
		* If there's one already queued, don't do another
		*/

		tlist = myrpt->tele.next;
		unkeys_queued = 0;
		if (tlist != &myrpt->tele) {
			rpt_mutex_lock(&myrpt->lock);
			while (tlist != &myrpt->tele) {
				if (tlist->mode == UNKEY)
					unkeys_queued++;
				tlist = tlist->next;
			}
			rpt_mutex_unlock(&myrpt->lock);
		}
		if (unkeys_queued > 1) {
			imdone = 1;
			break;
		}

		/* Wait for the telemetry timer to expire */
		/* Periodically check the timer since it can be re-initialized above */
		while (myrpt->unkeytocttimer) {
			int ctint;
			if (myrpt->unkeytocttimer > 100)
				ctint = 100;
			else
				ctint = myrpt->unkeytocttimer;
			ast_safe_sleep(mychannel, ctint);
			rpt_mutex_lock(&myrpt->lock);
			if (myrpt->unkeytocttimer < ctint)
				myrpt->unkeytocttimer = 0;
			else
				myrpt->unkeytocttimer -= ctint;
			rpt_mutex_unlock(&myrpt->lock);
		}
	
		/*
		* Now, the carrier on the rptr rx should be gone. 
		* If it re-appeared, then forget about sending the CT
		*/
		if (myrpt->keyed) {
			imdone = 1;
			break;
		}
		
		rpt_mutex_lock(&myrpt->lock); /* Update the kerchunk counters */
		myrpt->dailykerchunks++;
		myrpt->totalkerchunks++;
		rpt_mutex_unlock(&myrpt->lock);
	
		haslink = 0;
		hastx = 0;
		hasremote = 0;		
		l = myrpt->links.next;
		if (l != &myrpt->links) {
			rpt_mutex_lock(&myrpt->lock);
			while (l != &myrpt->links) {
				if (l->name[0] == '0') {
					l = l->next;
					continue;
				}
				haslink = 1;
				if (l->mode) {
					hastx++;
					if (l->isremote)
						hasremote++;
				}
				l = l->next;
			}
			rpt_mutex_unlock(&myrpt->lock);
		}
		if (haslink) {
			res = telem_lookup(myrpt, mychannel, myrpt->name, (!hastx) ? "remotemon" : "remotetx");
			if (res)
				ast_log(LOG_WARNING, "telem_lookup:remotexx failed on %s\n", mychannel->name);

			/* if in remote cmd mode, indicate it */
			if (myrpt->cmdnode[0]) {
				ast_safe_sleep(mychannel, 200);
				res = telem_lookup(myrpt, mychannel, myrpt->name, "cmdmode");
				if (res)
				 	ast_log(LOG_WARNING, "telem_lookup:cmdmode failed on %s\n", mychannel->name);
				ast_stopstream(mychannel);
			}
		} else if ((ct = ast_variable_retrieve(myrpt->cfg, myrpt->name, "unlinkedct"))) { /* Unlinked Courtesy Tone */
			res = telem_lookup(myrpt, mychannel, myrpt->name, ct);
			if (res)
			 	ast_log(LOG_WARNING, "telem_lookup:ctx failed on %s\n", mychannel->name);		
		}	
		if (hasremote && (!myrpt->cmdnode[0])) {
			/* set for all to hear */
			ci.chan = 0;
			ci.confno = myrpt->conf;
			ci.confmode = DAHDI_CONF_CONFANN;
			/* first put the channel on the conference in announce mode */
			if (ioctl(mychannel->fds[0], DAHDI_SETCONF, &ci) == -1) {
				ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
				rpt_mutex_lock(&myrpt->lock);
				remque((struct qelem *)mytele);
				rpt_mutex_unlock(&myrpt->lock);
				ast_log(LOG_NOTICE, "Telemetry thread aborted at line %d, mode: %d\n", __LINE__, mytele->mode); /*@@@@@@@@@@@*/
				ast_free(mytele);
				ast_hangup(mychannel);
				pthread_exit(NULL);
			}
			if ((ct = ast_variable_retrieve(myrpt->cfg, myrpt->name, "remotect"))) { /* Unlinked Courtesy Tone */
				ast_safe_sleep(mychannel, 200);
				res = telem_lookup(myrpt, mychannel, myrpt->name, ct);
				if (res)
				 	ast_log(LOG_WARNING, "telem_lookup:ctx failed on %s\n", mychannel->name);		
			}	
		}
#ifdef	_MDC_DECODE_H_
		if (myrpt->lastunit) {
			char mystr[10];

			ast_safe_sleep(mychannel, 200);
			/* set for all to hear */
			ci.chan = 0;
			ci.confno = myrpt->txconf;
			ci.confmode = DAHDI_CONF_CONFANN;
			/* first put the channel on the conference in announce mode */
			if (ioctl(mychannel->fds[0], DAHDI_SETCONF, &ci) == -1) {
				ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
				rpt_mutex_lock(&myrpt->lock);
				remque((struct qelem *)mytele);
				rpt_mutex_unlock(&myrpt->lock);
				ast_log(LOG_NOTICE, "Telemetry thread aborted at line %d, mode: %d\n", __LINE__, mytele->mode); /*@@@@@@@@@@@*/
				ast_free(mytele);		
				ast_hangup(mychannel);
				pthread_exit(NULL);
			}
			snprintf(mystr, sizeof(mystr), "%04x", myrpt->lastunit);
			myrpt->lastunit = 0;
			ast_say_character_str(mychannel, mystr, NULL, mychannel->language);
			break;
		}
#endif
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
		ast_say_character_str(mychannel, mytele->mylink.name, NULL, mychannel->language);
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
		ast_say_character_str(mychannel, mytele->mylink.name, NULL, mychannel->language);
		res = ast_streamfile(mychannel, "rpt/connected", mychannel->language);
		break;
	case CONNFAIL:
		res = ast_streamfile(mychannel, "rpt/node", mychannel->language);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		ast_say_character_str(mychannel, mytele->mylink.name, NULL, mychannel->language);
		res = ast_streamfile(mychannel, "rpt/connection_failed", mychannel->language);
		break;
	case STATUS:
		/* wait a little bit */
		wait_interval(myrpt, DLY_TELEM, mychannel);
		hastx = 0;
		linkbase.next = &linkbase;
		linkbase.prev = &linkbase;
		rpt_mutex_lock(&myrpt->lock);
		/* make our own list of links */
		l = myrpt->links.next;
		while (l != &myrpt->links) {
			if (l->name[0] == '0') {
				l = l->next;
				continue;
			}
			m = ast_malloc(sizeof(*m));
			if (!m) {
				ast_log(LOG_WARNING, "Cannot alloc memory on %s\n", mychannel->name);
				remque((struct qelem *)mytele);
				rpt_mutex_unlock(&myrpt->lock);
				ast_log(LOG_NOTICE, "Telemetry thread aborted at line %d, mode: %d\n", __LINE__, mytele->mode); /*@@@@@@@@@@@*/
				ast_free(mytele);
				ast_hangup(mychannel);
				pthread_exit(NULL);
			}
			memcpy(m, l, sizeof(struct rpt_link));
			m->next = m->prev = NULL;
			insque((struct qelem *)m, (struct qelem *)linkbase.next);
			l = l->next;
		}
		rpt_mutex_unlock(&myrpt->lock);
		res = ast_streamfile(mychannel, "rpt/node", mychannel->language);
		if (!res)
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		ast_say_character_str(mychannel, myrpt->name, NULL, mychannel->language);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		if (myrpt->callmode) {
			hastx = 1;
			res = ast_streamfile(mychannel, "rpt/autopatch_on", mychannel->language);
			if (!res)
				res = ast_waitstream(mychannel, "");
			else
				 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
			ast_stopstream(mychannel);
		}
		l = linkbase.next;
		while (l != &linkbase) {
			hastx = 1;
			res = ast_streamfile(mychannel, "rpt/node", mychannel->language);
			if (!res) 
				res = ast_waitstream(mychannel, "");
			else
				ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
			ast_stopstream(mychannel);
			ast_say_character_str(mychannel, l->name, NULL, mychannel->language);
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
		if (!hastx) {
			res = ast_streamfile(mychannel, "rpt/repeat_only", mychannel->language);
			if (!res) 
				res = ast_waitstream(mychannel, "");
			else
				 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
			ast_stopstream(mychannel);
		}
		/* destroy our local link queue */
		l = linkbase.next;
		while (l != &linkbase) {
			m = l;
			l = l->next;
			remque((struct qelem *)m);
			ast_free(m);
		}			
		imdone = 1;
		break;

	case LASTNODEKEY: /* Identify last node which keyed us up */
		rpt_mutex_lock(&myrpt->lock);
		if (myrpt->lastnodewhichkeyedusup)
			p = ast_strdupa(myrpt->lastnodewhichkeyedusup); /* Make a local copy of the node name */
		else
			p = NULL;
		rpt_mutex_unlock(&myrpt->lock);
		if (!p) {
			imdone = 1; /* no node previously keyed us up, or the node which did has been disconnected */
			break;
		}
		wait_interval(myrpt, DLY_TELEM, mychannel);
		res = ast_streamfile(mychannel, "rpt/node", mychannel->language);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		ast_say_character_str(mychannel, p, NULL, mychannel->language);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		else
			ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		imdone = 1;
		break;		

	case TIMEOUT:
		res = ast_streamfile(mychannel, "rpt/node", mychannel->language);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		ast_say_character_str(mychannel, myrpt->name, NULL, mychannel->language);
		res = ast_streamfile(mychannel, "rpt/timeout", mychannel->language);
		break;
		
	case STATS_TIME:
		wait_interval(myrpt, DLY_TELEM, mychannel); /* Wait a little bit */
		tv = ast_tvnow();
		ast_localtime(&tv, &localtm, NULL);
		/* Say the phase of the day is before the time */
		if ((localtm.tm_hour >= 0) && (localtm.tm_hour < 12))
			p = "rpt/goodmorning";
		else if ((localtm.tm_hour >= 12) && (localtm.tm_hour < 18))
			p = "rpt/goodafternoon";
		else
			p = "rpt/goodevening";
		if (sayfile(mychannel, p) == -1) {
			imdone = 1;
			break;
		}
		/* Say the time is ... */		
		if (sayfile(mychannel, "rpt/thetimeis") == -1) {
			imdone = 1;
			break;
		}
		/* Say the time */				
		res = ast_say_time(mychannel, tv.tv_sec, "", mychannel->language);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);		
		imdone = 1;
	    	break;
	case STATS_VERSION:
		wait_interval(myrpt, DLY_TELEM, mychannel); /* Wait a little bit */
		/* Say "version" */
		if (sayfile(mychannel, "rpt/version") == -1) {
			imdone = 1;
			break;
		}
		if (!res) /* Say "X" */
			ast_say_number(mychannel, vmajor, "", mychannel->language, (char *) NULL);
		if (!res) 
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);	
		if (saycharstr(mychannel, ".") == -1) {
			imdone = 1;
			break;
		}
		if (!res) /* Say "Y" */
			ast_say_number(mychannel, vminor, "", mychannel->language, (char *) NULL);
		if (!res) {
			res = ast_waitstream(mychannel, "");
			ast_stopstream(mychannel);
		} else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		imdone = 1;
		break;
	case ARB_ALPHA:
		wait_interval(myrpt, DLY_TELEM, mychannel); /* Wait a little bit */
		if (mytele->param)
			saycharstr(mychannel, mytele->param);
		imdone = 1;
		break;
	case REV_PATCH:
		wait_interval(myrpt, DLY_TELEM, mychannel); /* Wait a little bit */
		if (mytele->param) {
			/* Parts of this section taken from app_parkandannounce */
			char *tpl_working, *tpl_current;
			char *tmp[100], *myparm;
			int looptemp=0, i = 0, dres = 0;

			tpl_working = ast_strdupa(mytele->param);
			myparm = strsep(&tpl_working, ",");
			tpl_current = strsep(&tpl_working, ":");

			while (tpl_current && looptemp < sizeof(tmp)) {
				tmp[looptemp] = tpl_current;
				looptemp++;
				tpl_current = strsep(&tpl_working, ":");
			}

			for (i = 0; i < looptemp; i++) {
				if (!strcmp(tmp[i], "PARKED")) {
					ast_say_digits(mychannel, atoi(myparm), "", mychannel->language);
				} else if (!strcmp(tmp[i], "NODE")) {
					ast_say_digits(mychannel, atoi(myrpt->name), "", mychannel->language);
				} else {
					dres = ast_streamfile(mychannel, tmp[i], mychannel->language);
					if (!dres) {
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
		while (mychannel->generatordata && (!myrpt->stopgen)) {
			if (ast_safe_sleep(mychannel, 1)) break;
			imdone = 1;
		}
		break;
	default:
		break;
	}

	myrpt->stopgen = 0;
	if (!imdone) {
		if (!res) 
			res = ast_waitstream(mychannel, "");
		else {
			ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
			res = 0;
		}
	}
	ast_stopstream(mychannel);
	rpt_mutex_lock(&myrpt->lock);
	if (mytele->mode == TAILMSG) {
		if (!res) {
			myrpt->tailmessagen++;
			if (myrpt->tailmessagen >= myrpt->p.tailmsg.argc)
				myrpt->tailmessagen = 0;
		} else {
			myrpt->tmsgtimer = myrpt->p.tailsquashedtime;
		}
	}
	remque((struct qelem *)mytele);
	rpt_mutex_unlock(&myrpt->lock);
	ast_free(mytele);		
	ast_hangup(mychannel);
#ifdef  APP_RPT_LOCK_DEBUG
	sleep(5);
	ast_mutex_lock(&locklock);
	t = get_lockthread(pthread_self());
	if (t)
		memset(t, 0, sizeof(struct lockthread));
	ast_mutex_unlock(&locklock);
#endif
	pthread_exit(NULL);
}

static void rpt_telemetry(struct rpt *myrpt, int mode, void *data)
{
	struct rpt_tele *tele;
	struct rpt_link *mylink = (struct rpt_link *) data;
	int res;

	tele = ast_calloc(1, sizeof(*tele));
	if (!tele) {
		ast_log(LOG_WARNING, "Unable to allocate memory\n");
		pthread_exit(NULL);
	}
	tele->rpt = myrpt;
	tele->mode = mode;
	rpt_mutex_lock(&myrpt->lock);
	if ((mode == CONNFAIL) || (mode == REMDISC) || (mode == CONNECTED)) {
		if (mylink) {
			memcpy(&tele->mylink, mylink, sizeof(struct rpt_link));
		}
	} else if ((mode == ARB_ALPHA) || (mode == REV_PATCH)) {
		ast_copy_string(tele->param, (char *) data, sizeof(tele->param));
	}
	insque((struct qelem *)tele, (struct qelem *)myrpt->tele.next);
	rpt_mutex_unlock(&myrpt->lock);
	res = ast_pthread_create_detached(&tele->threadid, NULL, rpt_tele_thread, (void *) tele);
	if (res != 0) {
		rpt_mutex_lock(&myrpt->lock);
		remque((struct qlem *) tele); /* We don't like stuck transmitters, remove it from the queue */
		rpt_mutex_unlock(&myrpt->lock);	
		ast_log(LOG_WARNING, "Could not create telemetry thread: %s\n", strerror(res));
	}
	return;
}

static void *rpt_call(void *this)
{
<<<<<<< .working
	DAHDI_CONFINFO ci;  /* conference info */
	struct rpt *myrpt = (struct rpt *)this;
	int	res;
	struct ast_frame wf;
	int stopped, congstarted, dialtimer, lastcidx, aborted;
	struct ast_channel *mychannel, *genchannel;
=======
struct dahdi_confinfo ci;  /* conference info */
struct	rpt *myrpt = (struct rpt *)this;
int	res;
int stopped,congstarted,dialtimer,lastcidx,aborted;
struct ast_channel *mychannel,*genchannel;
>>>>>>> .merge-right.r134260

	myrpt->mydtmf = 0;
	/* allocate a pseudo-channel thru asterisk */
	mychannel = ast_request("zap", AST_FORMAT_SLINEAR, "pseudo", NULL);
	if (!mychannel) {
		ast_log(LOG_ERROR, "rpt: unable to obtain pseudo channel\n");
		pthread_exit(NULL);
	}
	ci.chan = 0;
	ci.confno = myrpt->conf; /* use the pseudo conference */
	ci.confmode = DAHDI_CONF_REALANDPSEUDO | DAHDI_CONF_TALKER | DAHDI_CONF_LISTENER
		| DAHDI_CONF_PSEUDO_TALKER | DAHDI_CONF_PSEUDO_LISTENER; 
	/* first put the channel on the conference */
	if (ioctl(mychannel->fds[0], DAHDI_SETCONF, &ci) == -1) {
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		ast_hangup(mychannel);
		myrpt->callmode = 0;
		pthread_exit(NULL);
	}
	/* allocate a pseudo-channel thru asterisk */
	genchannel = ast_request("zap", AST_FORMAT_SLINEAR, "pseudo", NULL);
	if (!genchannel) {
		ast_log(LOG_ERROR, "rpt: unable to obtain pseudo channel\n");
		ast_hangup(mychannel);
		pthread_exit(NULL);
	}
	ci.chan = 0;
	ci.confno = myrpt->conf;
	ci.confmode = DAHDI_CONF_REALANDPSEUDO | DAHDI_CONF_TALKER | DAHDI_CONF_LISTENER
		| DAHDI_CONF_PSEUDO_TALKER | DAHDI_CONF_PSEUDO_LISTENER; 
	/* first put the channel on the conference */
	if (ioctl(genchannel->fds[0], DAHDI_SETCONF, &ci) == -1) {
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		myrpt->callmode = 0;
		pthread_exit(NULL);
	}
	if (myrpt->p.tonezone && (tone_zone_set_zone(mychannel->fds[0], myrpt->p.tonezone) == -1)) {
		ast_log(LOG_WARNING, "Unable to set tone zone %s\n", myrpt->p.tonezone);
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		myrpt->callmode = 0;
		pthread_exit(NULL);
	}
	if (myrpt->p.tonezone && (tone_zone_set_zone(genchannel->fds[0], myrpt->p.tonezone) == -1)) {
		ast_log(LOG_WARNING, "Unable to set tone zone %s\n", myrpt->p.tonezone);
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		myrpt->callmode = 0;
		pthread_exit(NULL);
	}
	/* start dialtone if patchquiet is 0. Special patch modes don't send dial tone */
	if ((!myrpt->patchquiet) && (tone_zone_play_tone(mychannel->fds[0], DAHDI_TONE_DIALTONE) < 0)) {
		ast_log(LOG_WARNING, "Cannot start dialtone\n");
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		myrpt->callmode = 0;
		pthread_exit(NULL);
	}
	stopped = 0;
	congstarted = 0;
	dialtimer = 0;
	lastcidx = 0;
	aborted = 0;

	while ((myrpt->callmode == 1) || (myrpt->callmode == 4)) {
		if ((myrpt->patchdialtime) && (myrpt->callmode == 1) && (myrpt->cidx != lastcidx)) {
			dialtimer = 0;
			lastcidx = myrpt->cidx;
		}		

		if ((myrpt->patchdialtime) && (dialtimer >= myrpt->patchdialtime)) { 
			rpt_mutex_lock(&myrpt->lock);
			aborted = 1;
			myrpt->callmode = 0;
			rpt_mutex_unlock(&myrpt->lock);
			break;
		}
	
		if ((!myrpt->patchquiet) && (!stopped) && (myrpt->callmode == 1) && (myrpt->cidx > 0)) {
			stopped = 1;
			/* stop dial tone */
			tone_zone_play_tone(mychannel->fds[0], -1);
		}
		if (myrpt->callmode == 4) {
			if (!congstarted) {
				congstarted = 1;
				/* start congestion tone */
				tone_zone_play_tone(mychannel->fds[0], DAHDI_TONE_CONGESTION);
			}
		}
		res = ast_safe_sleep(mychannel, MSWAIT);
		if (res < 0) {
			ast_hangup(mychannel);
			ast_hangup(genchannel);
			rpt_mutex_lock(&myrpt->lock);
			myrpt->callmode = 0;
			rpt_mutex_unlock(&myrpt->lock);
			pthread_exit(NULL);
		}
		dialtimer += MSWAIT;
	}
	/* stop any tone generation */
	tone_zone_play_tone(mychannel->fds[0], -1);
	/* end if done */
	if (!myrpt->callmode) {
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		rpt_mutex_lock(&myrpt->lock);
		myrpt->callmode = 0;
		rpt_mutex_unlock(&myrpt->lock);
		if ((!myrpt->patchquiet) && aborted)
			rpt_telemetry(myrpt, TERM, NULL);
		pthread_exit(NULL);			
	}

	if (myrpt->p.ourcallerid && *myrpt->p.ourcallerid) {
		char *name, *loc, *instr;
		instr = ast_strdup(myrpt->p.ourcallerid);
		if (instr) {
			ast_callerid_parse(instr, &name, &loc);
			if (loc) {
				if (mychannel->cid.cid_num)
					ast_free(mychannel->cid.cid_num);
				mychannel->cid.cid_num = ast_strdup(loc);
			}
			if (name) {
				if (mychannel->cid.cid_name)
					ast_free(mychannel->cid.cid_name);
				mychannel->cid.cid_name = ast_strdup(name);
			}
			ast_free(instr);
		}
	}

	ast_copy_string(mychannel->exten, myrpt->exten, sizeof(mychannel->exten));
	ast_copy_string(mychannel->context, myrpt->patchcontext, sizeof(mychannel->context));
	
	if (myrpt->p.acctcode)
		ast_string_field_set(mychannel, accountcode, myrpt->p.acctcode);
	mychannel->priority = 1;
	ast_channel_undefer_dtmf(mychannel);
	if (ast_pbx_start(mychannel) < 0) {
		ast_log(LOG_WARNING, "Unable to start PBX!!\n");
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		rpt_mutex_lock(&myrpt->lock);
	 	myrpt->callmode = 0;
		rpt_mutex_unlock(&myrpt->lock);
		pthread_exit(NULL);
	}
	usleep(10000);
	rpt_mutex_lock(&myrpt->lock);
	myrpt->callmode = 3;
	/* set appropriate conference for the pseudo */
	ci.chan = 0;
	ci.confno = myrpt->conf;
	ci.confmode = (myrpt->p.duplex == 2) ? DAHDI_CONF_CONFANNMON :
		(DAHDI_CONF_CONF | DAHDI_CONF_LISTENER | DAHDI_CONF_TALKER);
	/* first put the channel on the conference in announce mode */
	if (ioctl(myrpt->pchannel->fds[0], DAHDI_SETCONF, &ci) == -1) {
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		ast_hangup(mychannel);
		ast_hangup(genchannel);
		myrpt->callmode = 0;
		pthread_exit(NULL);
	}
	while (myrpt->callmode) {
		if ((!mychannel->pbx) && (myrpt->callmode != 4)) {
			if (myrpt->patchfarenddisconnect) { /* If patch is setup for far end disconnect */
				myrpt->callmode = 0;
				if (!myrpt->patchquiet) {
					rpt_mutex_unlock(&myrpt->lock);
					rpt_telemetry(myrpt, TERM, NULL);
					rpt_mutex_lock(&myrpt->lock);
				}
			} else { /* Send congestion until patch is downed by command */
				myrpt->callmode = 4;
				rpt_mutex_unlock(&myrpt->lock);
				/* start congestion tone */
				tone_zone_play_tone(genchannel->fds[0], DAHDI_TONE_CONGESTION);
				rpt_mutex_lock(&myrpt->lock);
			}
		}
		if (myrpt->mydtmf) {
			wf.frametype = AST_FRAME_DTMF;
			wf.subclass = myrpt->mydtmf;
			wf.offset = 0;
			wf.mallocd = 0;
			wf.data = NULL;
			wf.datalen = 0;
			wf.samples = 0;
			rpt_mutex_unlock(&myrpt->lock);
			ast_write(genchannel, &wf); 
			rpt_mutex_lock(&myrpt->lock);
			myrpt->mydtmf = 0;
		}
		rpt_mutex_unlock(&myrpt->lock);
		usleep(MSWAIT * 1000);
		rpt_mutex_lock(&myrpt->lock);
	}
	rpt_mutex_unlock(&myrpt->lock);
	tone_zone_play_tone(genchannel->fds[0], -1);
	if (mychannel->pbx)
		ast_softhangup(mychannel, AST_SOFTHANGUP_DEV);
	ast_hangup(genchannel);
	rpt_mutex_lock(&myrpt->lock);
	myrpt->callmode = 0;
	rpt_mutex_unlock(&myrpt->lock);
	/* set appropriate conference for the pseudo */
	ci.chan = 0;
	ci.confno = myrpt->conf;
	ci.confmode = ((myrpt->p.duplex == 2) || (myrpt->p.duplex == 4)) ? DAHDI_CONF_CONFANNMON :
		(DAHDI_CONF_CONF | DAHDI_CONF_LISTENER | DAHDI_CONF_TALKER);
	/* first put the channel on the conference in announce mode */
	if (ioctl(myrpt->pchannel->fds[0], DAHDI_SETCONF, &ci) == -1) {
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
	}
	pthread_exit(NULL);
}

static void send_link_dtmf(struct rpt *myrpt, char c)
{
	char str[300];
	struct ast_frame wf;
	struct rpt_link *l;

	snprintf(str, sizeof(str), "D %s %s %d %c", myrpt->cmdnode, myrpt->name, ++(myrpt->dtmfidx), c);
	wf.frametype = AST_FRAME_TEXT;
	wf.subclass = 0;
	wf.offset = 0;
	wf.mallocd = 1;
	wf.datalen = strlen(str) + 1;
	wf.samples = 0;
	l = myrpt->links.next;
	/* first, see if our dude is there */
	while (l != &myrpt->links) {
		if (l->name[0] == '0') {
			l = l->next;
			continue;
		}
		/* if we found it, write it and were done */
		if (!strcmp(l->name, myrpt->cmdnode)) {
			wf.data = ast_strdup(str);
			if (l->chan)
				ast_write(l->chan, &wf);
			return;
		}
		l = l->next;
	}
	l = myrpt->links.next;
	/* if not, give it to everyone */
	while (l != &myrpt->links) {
		wf.data = ast_strdup(str);
		if (l->chan)
			ast_write(l->chan, &wf);
		l = l->next;
	}
	return;
}

<<<<<<< .working
=======
static void send_link_keyquery(struct rpt *myrpt)
{
char	str[300];
struct	ast_frame wf;
struct	rpt_link *l;

	rpt_mutex_lock(&myrpt->lock);
	memset(myrpt->topkey,0,sizeof(myrpt->topkey));
	myrpt->topkeystate = 1;
	time(&myrpt->topkeytime);
	rpt_mutex_unlock(&myrpt->lock);
	snprintf(str, sizeof(str), "K? * %s 0 0", myrpt->name);
	wf.frametype = AST_FRAME_TEXT;
	wf.subclass = 0;
	wf.offset = 0;
	wf.mallocd = 0;
	wf.datalen = strlen(str) + 1;
	wf.samples = 0;
	l = myrpt->links.next;
	/* give it to everyone */
	while(l != &myrpt->links)
	{
		wf.data.ptr = str;
		if (l->chan) ast_write(l->chan,&wf);
		l = l->next;
	}
	return;
}

/* send newkey request */

static void send_newkey(struct ast_channel *chan)
{

	/* ast_safe_sleep(chan,10); */
	ast_sendtext(chan,newkeystr);
	return;
}


/* 
 * Connect a link 
 *
 * Return values:
 * -2: Attempt to connect to self 
 * -1: No such node
 *  0: Success
 *  1: No match yet
 *  2: Already connected to this node
 */

static int connect_link(struct rpt *myrpt, char* node, int mode, int perma)
{
	char *val, *s, *s1, *s2, *tele;
	char lstr[MAXLINKLIST],*strs[MAXLINKLIST];
	char tmp[300], deststr[300] = "",modechange = 0;
	char sx[320],*sy;
	struct rpt_link *l;
	int reconnects = 0;
	int i,n;
	struct dahdi_confinfo ci;  /* conference info */

	val = node_lookup(myrpt,node);
	if (!val){
		if(strlen(node) >= myrpt->longestnode)
			return -1; /* No such node */
		return 1; /* No match yet */
	}

	if(!strcmp(myrpt->name,node)) /* Do not allow connections to self */
		return -2;
		
	if(debug > 3){
		ast_log(LOG_NOTICE,"Connect attempt to node %s\n", node);
		ast_log(LOG_NOTICE,"Mode: %s\n",(mode)?"Transceive":"Monitor");
		ast_log(LOG_NOTICE,"Connection type: %s\n",(perma)?"Permalink":"Normal");
	}

	strncpy(tmp,val,sizeof(tmp) - 1);
	s = tmp;
	s1 = strsep(&s,",");
	if (!strchr(s1,':') && strchr(s1,'/') && strncasecmp(s1, "local/", 6))
	{
		sy = strchr(s1,'/');		
		*sy = 0;
		sprintf(sx,"%s:4569/%s",s1,sy + 1);
		s1 = sx;
	}
	s2 = strsep(&s,",");
	rpt_mutex_lock(&myrpt->lock);
	l = myrpt->links.next;
	/* try to find this one in queue */
	while(l != &myrpt->links){
		if (l->name[0] == '0') 
		{
			l = l->next;
			continue;
		}
	/* if found matching string */
		if (!strcmp(l->name, node))
			break;
		l = l->next;
	}
	/* if found */
	if (l != &myrpt->links){ 
	/* if already in this mode, just ignore */
		if ((l->mode) || (!l->chan)) {
			rpt_mutex_unlock(&myrpt->lock);
			return 2; /* Already linked */
		}
		reconnects = l->reconnects;
		rpt_mutex_unlock(&myrpt->lock);
		if (l->chan) ast_softhangup(l->chan, AST_SOFTHANGUP_DEV);
		l->retries = l->max_retries + 1;
		l->disced = 2;
		modechange = 1;
	} else
	{
		__mklinklist(myrpt,NULL,lstr);
		rpt_mutex_unlock(&myrpt->lock);
		n = finddelim(lstr,strs,MAXLINKLIST);
		for(i = 0; i < n; i++)
		{
			if ((*strs[i] < '0') || 
			    (*strs[i] > '9')) strs[i]++;
			if (!strcmp(strs[i],node))
			{
				return 2; /* Already linked */
			}
		}
	}
	strncpy(myrpt->lastlinknode,node,MAXNODESTR - 1);
	/* establish call */
	l = ast_malloc(sizeof(struct rpt_link));
	if (!l)
	{
		ast_log(LOG_WARNING, "Unable to malloc\n");
		return -1;
	}
	/* zero the silly thing */
	memset((char *)l,0,sizeof(struct rpt_link));
	l->mode = mode;
	l->outbound = 1;
	l->thisconnected = 0;
	voxinit_link(l,1);
	strncpy(l->name, node, MAXNODESTR - 1);
	l->isremote = (s && ast_true(s));
	if (modechange) l->connected = 1;
	l->hasconnected = l->perma = perma;
#ifdef ALLOW_LOCAL_CHANNELS
	if ((strncasecmp(s1,"iax2/", 5) == 0) || (strncasecmp(s1, "local/", 6) == 0))
        	strncpy(deststr, s1, sizeof(deststr));
	else
	        snprintf(deststr, sizeof(deststr), "IAX2/%s", s1);
#else
	snprintf(deststr, sizeof(deststr), "IAX2/%s", s1);
#endif
	tele = strchr(deststr, '/');
	if (!tele){
		ast_log(LOG_WARNING,"link3:Dial number (%s) must be in format tech/number\n",deststr);
		ast_free(l);
		return -1;
	}
	*tele++ = 0;
	l->chan = ast_request(deststr, AST_FORMAT_SLINEAR, tele,NULL);
	if (l->chan){
		ast_set_read_format(l->chan, AST_FORMAT_SLINEAR);
		ast_set_write_format(l->chan, AST_FORMAT_SLINEAR);
#ifdef	AST_CDR_FLAG_POST_DISABLED
		if (l->chan->cdr)
			ast_set_flag(l->chan->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
#ifndef	NEW_ASTERISK
		l->chan->whentohangup = 0;
#endif
		l->chan->appl = "Apprpt";
		l->chan->data = "(Remote Rx)";
		if (debug > 3)
			ast_log(LOG_NOTICE, "rpt (remote) initiating call to %s/%s on %s\n",
		deststr, tele, l->chan->name);
		if(l->chan->cid.cid_num)
			ast_free(l->chan->cid.cid_num);
		l->chan->cid.cid_num = ast_strdup(myrpt->name);
		ast_call(l->chan,tele,999);
	}
	else {
		if(debug > 3) 
			ast_log(LOG_NOTICE, "Unable to place call to %s/%s on %s\n",
		deststr,tele,l->chan->name);
		if (myrpt->p.archivedir)
		{
			char str[100];
			sprintf(str,"LINKFAIL,%s",l->name);
			donodelog(myrpt,str);
		}
		ast_free(l);
		return -1;
	}
	/* allocate a pseudo-channel thru asterisk */
	l->pchan = ast_request("DAHDI",AST_FORMAT_SLINEAR,"pseudo",NULL);
	if (!l->pchan){
		ast_log(LOG_WARNING,"rpt connect: Sorry unable to obtain pseudo channel\n");
		ast_hangup(l->chan);
		ast_free(l);
		return -1;
	}
	ast_set_read_format(l->pchan, AST_FORMAT_SLINEAR);
	ast_set_write_format(l->pchan, AST_FORMAT_SLINEAR);
#ifdef	AST_CDR_FLAG_POST_DISABLED
	if (l->pchan->cdr)
		ast_set_flag(l->pchan->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
	/* make a conference for the tx */
	ci.chan = 0;
	ci.confno = myrpt->conf;
	ci.confmode = DAHDI_CONF_CONF | DAHDI_CONF_LISTENER | DAHDI_CONF_TALKER;
	/* first put the channel on the conference in proper mode */
	if (ioctl(l->pchan->fds[0], DAHDI_SETCONF, &ci) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		ast_hangup(l->chan);
		ast_hangup(l->pchan);
		ast_free(l);
		return -1;
	}
	rpt_mutex_lock(&myrpt->lock);
	l->reconnects = reconnects;
	/* insert at end of queue */
	l->max_retries = MAX_RETRIES;
	if (perma)
		l->max_retries = MAX_RETRIES_PERM;
	if (l->isremote) l->retries = l->max_retries + 1;
	insque((struct qelem *)l,(struct qelem *)myrpt->links.next);
	__kickshort(myrpt);
	rpt_mutex_unlock(&myrpt->lock);
	if (!l->phonemode) send_newkey(l->chan);
	return 0;
}



>>>>>>> .merge-right.r134260
/*
* Internet linking function 
*/

static int function_ilink(struct rpt *myrpt, char *param, char *digits, int command_source, struct rpt_link *mylink)
{
	const char *val;
	char *s, *tele;
	char deststr[300] = "", modechange = 0;
	char digitbuf[MAXNODESTR];
	struct rpt_link *l;
	int reconnects = 0;
	DAHDI_CONFINFO ci;  /* conference info */
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(s1);
		AST_APP_ARG(s2); /* XXX Never used.  Scratch? XXX */
	);

	if (!param)
		return DC_ERROR;

	if (!myrpt->enable)
		return DC_ERROR;

	ast_copy_string(digitbuf, digits, sizeof(digitbuf));
	ast_debug(1, "@@@@ ilink param = %s, digitbuf = %s\n", S_OR(param, "(null)"), digitbuf);

	switch (myatoi(param)) {
	case 1: /* Link off */
		if ((digitbuf[0] == '0') && (myrpt->lastlinknode[0]))
			strcpy(digitbuf, myrpt->lastlinknode);
		val = ast_variable_retrieve(myrpt->cfg, myrpt->p.nodes, digitbuf);
		if (!val) {
			if (strlen(digitbuf) >= myrpt->longestnode)
				return DC_ERROR;
			break;
		}
		rpt_mutex_lock(&myrpt->lock);
		l = myrpt->links.next;
		/* try to find this one in queue */
		while (l != &myrpt->links) {
			if (l->name[0] == '0') {
				l = l->next;
				continue;
			}
			/* if found matching string */
			if (!strcmp(l->name, digitbuf))
				break;
			l = l->next;
		}
		if (l != &myrpt->links) { /* if found */
			struct ast_frame wf;
			ast_copy_string(myrpt->lastlinknode, digitbuf, MAXNODESTR);
			l->retries = MAX_RETRIES + 1;
			l->disced = 1;
			rpt_mutex_unlock(&myrpt->lock);
			wf.frametype = AST_FRAME_TEXT;
			wf.subclass = 0;
			wf.offset = 0;
			wf.mallocd = 1;
			wf.datalen = strlen(discstr) + 1;
			wf.samples = 0;
			wf.data = ast_strdup(discstr);
			if (l->chan) {
				ast_write(l->chan, &wf);
				if (ast_safe_sleep(l->chan, 250) == -1)
					return DC_ERROR;
				ast_softhangup(l->chan, AST_SOFTHANGUP_DEV);
			}
			rpt_telemetry(myrpt, COMPLETE, NULL);
			return DC_COMPLETE;
		}
		rpt_mutex_unlock(&myrpt->lock);	
		return DC_COMPLETE;
	case 2: /* Link Monitor */
		if ((digitbuf[0] == '0') && (myrpt->lastlinknode[0]))
			strcpy(digitbuf, myrpt->lastlinknode);
		val = ast_variable_retrieve(myrpt->cfg, myrpt->p.nodes, digitbuf);
		if (!val) {
			if (strlen(digitbuf) >= myrpt->longestnode)
				return DC_ERROR;
			break;
		}
		s = ast_strdupa(val);
		AST_STANDARD_APP_ARGS(args, s);
		rpt_mutex_lock(&myrpt->lock);
		l = myrpt->links.next;
		/* try to find this one in queue */
		while (l != &myrpt->links) {
			if (l->name[0] == '0') {
				l = l->next;
				continue;
			}
			/* if found matching string */
			if (!strcmp(l->name, digitbuf))
				break;
			l = l->next;
		}
		/* if found */
		if (l != &myrpt->links) {
			/* if already in this mode, just ignore */
			if ((!l->mode) || (!l->chan)) {
				rpt_mutex_unlock(&myrpt->lock);
				rpt_telemetry(myrpt, REMALREADY, NULL);
				return DC_COMPLETE;
			}
			reconnects = l->reconnects;
			rpt_mutex_unlock(&myrpt->lock);
			if (l->chan)
				ast_softhangup(l->chan, AST_SOFTHANGUP_DEV);
			l->retries = MAX_RETRIES + 1;
			l->disced = 2;
			modechange = 1;
		} else
			rpt_mutex_unlock(&myrpt->lock);
		ast_copy_string(myrpt->lastlinknode, digitbuf, MAXNODESTR);
		/* establish call in monitor mode */
		l = ast_calloc(1, sizeof(*l));
		if (!l) {
			ast_log(LOG_WARNING, "Unable to malloc\n");
			return DC_ERROR;
		}
		snprintf(deststr, sizeof(deststr), "IAX2/%s", args.s1);
		tele = strchr(deststr, '/');
		if (!tele) {
			ast_log(LOG_ERROR, "link2:Dial number (%s) must be in format tech/number\n", deststr);
			return DC_ERROR;
		}
		*tele++ = 0;
		l->isremote = (s && ast_true(s));
		ast_copy_string(l->name, digitbuf, MAXNODESTR);
		l->chan = ast_request(deststr, AST_FORMAT_SLINEAR, tele, NULL);
		if (modechange)
			l->connected = 1;
		if (l->chan) {
			ast_set_read_format(l->chan, AST_FORMAT_SLINEAR);
			ast_set_write_format(l->chan, AST_FORMAT_SLINEAR);
			l->chan->whentohangup = 0;
			l->chan->appl = "Apprpt";
			l->chan->data = "(Remote Rx)";
			ast_verb(3, "rpt (remote) initiating call to %s/%s on %s\n",
					deststr, tele, l->chan->name);
			if (l->chan->cid.cid_num)
				ast_free(l->chan->cid.cid_num);
			l->chan->cid.cid_num = ast_strdup(myrpt->name);
			ast_call(l->chan, tele, 0);
		} else {
			rpt_telemetry(myrpt, CONNFAIL, l);
			ast_free(l);
			ast_verb(3, "Unable to place call to %s/%s on %s\n",
					deststr, tele, l->chan->name);
			return DC_ERROR;
		}
		/* allocate a pseudo-channel thru asterisk */
		l->pchan = ast_request("zap", AST_FORMAT_SLINEAR, "pseudo", NULL);
		if (!l->pchan) {
			ast_log(LOG_ERROR, "rpt:Sorry unable to obtain pseudo channel\n");
			ast_hangup(l->chan);
			ast_free(l);
			return DC_ERROR;
		}
		ast_set_read_format(l->pchan, AST_FORMAT_SLINEAR);
		ast_set_write_format(l->pchan, AST_FORMAT_SLINEAR);
		/* make a conference for the pseudo-one */
		ci.chan = 0;
		ci.confno = myrpt->conf;
		ci.confmode = DAHDI_CONF_CONF | DAHDI_CONF_LISTENER | DAHDI_CONF_TALKER;
		/* first put the channel on the conference in proper mode */
		if (ioctl(l->pchan->fds[0], DAHDI_SETCONF, &ci) == -1) {
			ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
			ast_hangup(l->chan);
			ast_hangup(l->pchan);
			ast_free(l);
			return DC_ERROR;
		}
		rpt_mutex_lock(&myrpt->lock);
		l->reconnects = reconnects;
		/* insert at end of queue */
		insque((struct qelem *)l, (struct qelem *)myrpt->links.next);
		rpt_mutex_unlock(&myrpt->lock);
		rpt_telemetry(myrpt, COMPLETE, NULL);
		return DC_COMPLETE;
	case 3: /* Link transceive */
		if ((digitbuf[0] == '0') && (myrpt->lastlinknode[0]))
			strcpy(digitbuf, myrpt->lastlinknode);
		val = ast_variable_retrieve(myrpt->cfg, myrpt->p.nodes, digitbuf);
		if (!val) {
			if (strlen(digitbuf) >= myrpt->longestnode)
				return DC_ERROR;
			break;
		}
		s = ast_strdupa(val);
		AST_STANDARD_APP_ARGS(args, s);
		rpt_mutex_lock(&myrpt->lock);
		l = myrpt->links.next;
		/* try to find this one in queue */
		while (l != &myrpt->links) {
			if (l->name[0] == '0') {
				l = l->next;
				continue;
			}
			/* if found matching string */
			if (!strcmp(l->name, digitbuf))
				break;
			l = l->next;
		}
		/* if found */
		if (l != &myrpt->links) { 
			/* if already in this mode, just ignore */
			if ((l->mode) || (!l->chan)) {
				rpt_mutex_unlock(&myrpt->lock);
				rpt_telemetry(myrpt, REMALREADY, NULL);
				return DC_COMPLETE;
			}
			reconnects = l->reconnects;
			rpt_mutex_unlock(&myrpt->lock);
			if (l->chan)
				ast_softhangup(l->chan, AST_SOFTHANGUP_DEV);
			l->retries = MAX_RETRIES + 1;
			l->disced = 2;
			modechange = 1;
		} else
			rpt_mutex_unlock(&myrpt->lock);
		ast_copy_string(myrpt->lastlinknode, digitbuf, MAXNODESTR);
		/* establish call in tranceive mode */
		l = ast_calloc(1, sizeof(*l));
		if (!l) {
			ast_log(LOG_WARNING, "Unable to malloc\n");
			return DC_ERROR;
		}
		l->mode = 1;
		l->outbound = 1;
		ast_copy_string(l->name, digitbuf, MAXNODESTR);
		l->isremote = (s && ast_true(s));
		if (modechange)
			l->connected = 1;
		snprintf(deststr, sizeof(deststr), "IAX2/%s", args.s1);
		tele = strchr(deststr, '/');
		if (!tele) {
			ast_log(LOG_ERROR, "link3:Dial number (%s) must be in format tech/number\n", deststr);
			ast_free(l);
			return DC_ERROR;
		}
		*tele++ = 0;
		l->chan = ast_request(deststr, AST_FORMAT_SLINEAR, tele, NULL);
		if (l->chan) {
			ast_set_read_format(l->chan, AST_FORMAT_SLINEAR);
			ast_set_write_format(l->chan, AST_FORMAT_SLINEAR);
			l->chan->whentohangup = 0;
			l->chan->appl = "Apprpt";
			l->chan->data = "(Remote Rx)";
			ast_verb(3, "rpt (remote) initiating call to %s/%s on %s\n",
					deststr, tele, l->chan->name);
			if (l->chan->cid.cid_num)
				ast_free(l->chan->cid.cid_num);
			l->chan->cid.cid_num = ast_strdup(myrpt->name);
			ast_call(l->chan, tele, 999);
		} else {
			rpt_telemetry(myrpt, CONNFAIL, l);
			ast_free(l);
			ast_verb(3, "Unable to place call to %s/%s on %s\n",
					deststr, tele, l->chan->name);
			return DC_ERROR;
		}
		/* allocate a pseudo-channel thru asterisk */
		l->pchan = ast_request("zap", AST_FORMAT_SLINEAR, "pseudo", NULL);
		if (!l->pchan) {
			ast_log(LOG_ERROR, "rpt:Sorry unable to obtain pseudo channel\n");
			ast_hangup(l->chan);
			ast_free(l);
			return DC_ERROR;
		}
		ast_set_read_format(l->pchan, AST_FORMAT_SLINEAR);
		ast_set_write_format(l->pchan, AST_FORMAT_SLINEAR);
		/* make a conference for the tx */
		ci.chan = 0;
		ci.confno = myrpt->conf;
		ci.confmode = DAHDI_CONF_CONF | DAHDI_CONF_LISTENER | DAHDI_CONF_TALKER;
		/* first put the channel on the conference in proper mode */
		if (ioctl(l->pchan->fds[0], DAHDI_SETCONF, &ci) == -1) {
			ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
			ast_hangup(l->chan);
			ast_hangup(l->pchan);
			ast_free(l);
			return DC_ERROR;
		}
		rpt_mutex_lock(&myrpt->lock);
		l->reconnects = reconnects;
		/* insert at end of queue */
		insque((struct qelem *)l, (struct qelem *)myrpt->links.next);
		rpt_mutex_unlock(&myrpt->lock);
		rpt_telemetry(myrpt, COMPLETE, NULL);
		return DC_COMPLETE;
	case 4: /* Enter Command Mode */
		/* if doesnt allow link cmd, or no links active, return */
 		if (((command_source != SOURCE_RPT) &&
			 (command_source != SOURCE_PHONE) &&
			 (command_source != SOURCE_DPHONE)) ||
			(myrpt->links.next == &myrpt->links))
			return DC_COMPLETE;
		/* if already in cmd mode, or selected self, fughetabahtit */
		if ((myrpt->cmdnode[0]) || (!strcmp(myrpt->name, digitbuf))) {
			rpt_telemetry(myrpt, REMALREADY, NULL);
			return DC_COMPLETE;
		}
		if ((digitbuf[0] == '0') && (myrpt->lastlinknode[0]))
			strcpy(digitbuf, myrpt->lastlinknode);
		/* node must at least exist in list */
		val = ast_variable_retrieve(myrpt->cfg, myrpt->p.nodes, digitbuf);
		if (!val) {
			if (strlen(digitbuf) >= myrpt->longestnode)
				return DC_ERROR;
			break;
		}
		rpt_mutex_lock(&myrpt->lock);
		strcpy(myrpt->lastlinknode, digitbuf);
		ast_copy_string(myrpt->cmdnode, digitbuf, sizeof(myrpt->cmdnode));
		rpt_mutex_unlock(&myrpt->lock);
		rpt_telemetry(myrpt, REMGO, NULL);	
		return DC_COMPLETE;
	case 5: /* Status */
		rpt_telemetry(myrpt, STATUS, NULL);
		return DC_COMPLETE;
	case 6: /* All Links Off */
		l = myrpt->links.next;
		while (l != &myrpt->links) { /* This code is broke and needs to be changed to work with the reconnect kludge */
			if (l->chan)
				ast_softhangup(l->chan, AST_SOFTHANGUP_DEV); /* Hang 'em up */
			l = l->next;
		}
		rpt_telemetry(myrpt, COMPLETE, NULL);
		break;
	case 7: /* Identify last node which keyed us up */
		rpt_telemetry(myrpt, LASTNODEKEY, NULL);
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
	int i, index;
	char *value = NULL;
	AST_DECLARE_APP_ARGS(params,
		AST_APP_ARG(list)[20];
	);

	static char *keywords[] = {
	"context",
	"dialtime",
	"farenddisconnect",
	"noct",
	"quiet",
	NULL
	};
		
	if (!myrpt->enable)
		return DC_ERROR;

	ast_debug(1, "@@@@ Autopatch up\n");

	if (!myrpt->callmode) {
		/* Set defaults */
		myrpt->patchnoct = 0;
		myrpt->patchdialtime = 0;
		myrpt->patchfarenddisconnect = 0;
		myrpt->patchquiet = 0;
		ast_copy_string(myrpt->patchcontext, myrpt->p.ourcontext, sizeof(myrpt->patchcontext));

		if (param) {
			/* Process parameter list */
			char *tmp = ast_strdupa(param);
			AST_STANDARD_APP_ARGS(params, tmp);
			for (i = 0; i < params.argc; i++) {
				index = matchkeyword(params.list[i], &value, keywords);
				if (value)
					value = skipchars(value, "= ");
				switch (index) {
				case 1: /* context */
					ast_copy_string(myrpt->patchcontext, value, sizeof(myrpt->patchcontext)) ;
					break;
				case 2: /* dialtime */
					myrpt->patchdialtime = atoi(value);
					break;
				case 3: /* farenddisconnect */
					myrpt->patchfarenddisconnect = atoi(value);
					break;
				case 4:	/* noct */
					myrpt->patchnoct = atoi(value);
					break;
				case 5: /* quiet */
					myrpt->patchquiet = atoi(value);
					break;
				default:
					break;
				}
			}
		}
	}
					
	rpt_mutex_lock(&myrpt->lock);

	/* if on call, force * into current audio stream */
	
	if ((myrpt->callmode == 2) || (myrpt->callmode == 3)) {
		myrpt->mydtmf = myrpt->p.funcchar;
	}
	if (myrpt->callmode) {
		rpt_mutex_unlock(&myrpt->lock);
		return DC_COMPLETE;
	}
	myrpt->callmode = 1;
	myrpt->cidx = 0;
	myrpt->exten[myrpt->cidx] = 0;
	rpt_mutex_unlock(&myrpt->lock);
	ast_pthread_create_detached(&myrpt->rpt_call_thread, NULL, rpt_call, (void *) myrpt);
	return DC_COMPLETE;
}

/*
* Autopatch down
*/

static int function_autopatchdn(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{
	if (!myrpt->enable)
		return DC_ERROR;
	
	ast_debug(1, "@@@@ Autopatch down\n");
		
	rpt_mutex_lock(&myrpt->lock);
	
	if (!myrpt->callmode) {
		rpt_mutex_unlock(&myrpt->lock);
		return DC_COMPLETE;
	}
	
	myrpt->callmode = 0;
	rpt_mutex_unlock(&myrpt->lock);
	rpt_telemetry(myrpt, TERM, NULL);
	return DC_COMPLETE;
}

/*
* Status
*/

static int function_status(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{

	if (!param)
		return DC_ERROR;

	if (!myrpt->enable)
		return DC_ERROR;

	ast_debug(1, "@@@@ status param = %s, digitbuf = %s\n", (param)? param : "(null)", digitbuf);
	
	switch (myatoi(param)) {
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

	/* Never reached */
	return DC_INDETERMINATE;
}

/*
*  Macro-oni (without Salami)
*/

static int function_macro(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{

	const char *val;
	int	i;
	struct ast_channel *mychannel;

	if ((!myrpt->remote) && (!myrpt->enable))
		return DC_ERROR;

	ast_debug(1, "@@@@ macro-oni param = %s, digitbuf = %s\n", (param)? param : "(null)", digitbuf);
	
	mychannel = myrpt->remchannel;

	if (ast_strlen_zero(digitbuf)) /* needs 1 digit */
		return DC_INDETERMINATE;
			
	for (i = 0; i < digitbuf[i]; i++) {
		if ((digitbuf[i] < '0') || (digitbuf[i] > '9'))
			return DC_ERROR;
	}
   
	if (*digitbuf == '0')
		val = myrpt->p.startupmacro;
	else
		val = ast_variable_retrieve(myrpt->cfg, myrpt->p.macro, digitbuf);
	/* param was 1 for local buf */
	if (!val) {
		rpt_telemetry(myrpt, MACRO_NOTFOUND, NULL);
		return DC_COMPLETE;
	}			
	rpt_mutex_lock(&myrpt->lock);
	if ((sizeof(myrpt->macrobuf) - strlen(myrpt->macrobuf)) < strlen(val)) {
		rpt_mutex_unlock(&myrpt->lock);
		rpt_telemetry(myrpt, MACRO_BUSY, NULL);
		return DC_ERROR;
	}
	myrpt->macrotimer = MACROTIME;
	strncat(myrpt->macrobuf, val, sizeof(myrpt->macrobuf) - strlen(myrpt->macrobuf) - 1);
	rpt_mutex_unlock(&myrpt->lock);
	return DC_COMPLETE;	
}

/*
*  Gosub
*/

static int function_gosub(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{

	const char *val;
	int	i;
	struct ast_channel *mychannel;

	if ((!myrpt->remote) && (!myrpt->enable))
		return DC_ERROR;

	if (debug) 
		ast_log(LOG_DEBUG, "@@@@ gosub param = %s, digitbuf = %s\n", (param)? param : "(null)", digitbuf);
	
	mychannel = myrpt->remchannel;

	if (ast_strlen_zero(digitbuf)) /* needs 1 digit */
		return DC_INDETERMINATE;
			
	for (i = 0; i < digitbuf[i]; i++) {
		if ((digitbuf[i] < '0') || (digitbuf[i] > '9'))
			return DC_ERROR;
	}
   
	if (*digitbuf == '0')
		val = myrpt->p.startupgosub;
	else
		val = ast_variable_retrieve(myrpt->cfg, myrpt->p.gosub, digitbuf);
	/* param was 1 for local buf */
	if (!val) {
		rpt_telemetry(myrpt, GOSUB_NOTFOUND, NULL);
		return DC_COMPLETE;
	}			
	rpt_mutex_lock(&myrpt->lock);
	if ((sizeof(myrpt->gosubbuf) - strlen(myrpt->gosubbuf)) < strlen(val)) {
		rpt_mutex_unlock(&myrpt->lock);
		rpt_telemetry(myrpt, GOSUB_BUSY, NULL);
		return DC_ERROR;
	}
	myrpt->gosubtimer = GOSUBTIME;
	strncat(myrpt->gosubbuf, val, sizeof(myrpt->gosubbuf) - strlen(myrpt->gosubbuf) - 1);
	rpt_mutex_unlock(&myrpt->lock);
	return DC_COMPLETE;	
}

/*
* COP - Control operator
*/

static int function_cop(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{
	if (!param)
		return DC_ERROR;
	
	switch(myatoi(param)) {
	case 1: /* System reset */
		ast_cli_command(STDERR_FILENO, "restart now"); /* A little less drastic than what was previously here. */
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
		if (command_source != SOURCE_PHONE)
			return DC_INDETERMINATE;
		return DC_DOKEY;	
	}	
	return DC_INDETERMINATE;
}

/*
* Collect digits one by one until something matches
*/

static int collect_function_digits(struct rpt *myrpt, char *digits, int command_source, struct rpt_link *mylink)
{
	int i;
	char *stringp, *functiondigits;
	char function_table_name[30] = "";
	struct ast_variable *vp;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(action);
		AST_APP_ARG(param);
	);
	
	ast_debug(1, "@@@@ Digits collected: %s, source: %d\n", digits, command_source);
	
	if (command_source == SOURCE_DPHONE) {
		if (!myrpt->p.dphone_functions)
			return DC_INDETERMINATE;
		ast_copy_string(function_table_name, myrpt->p.dphone_functions, sizeof(function_table_name));
	} else if (command_source == SOURCE_PHONE) {
		if (!myrpt->p.phone_functions)
			return DC_INDETERMINATE;
		ast_copy_string(function_table_name, myrpt->p.phone_functions, sizeof(function_table_name));
	} else if (command_source == SOURCE_LNK)
		ast_copy_string(function_table_name, myrpt->p.link_functions, sizeof(function_table_name));
	else
		ast_copy_string(function_table_name, myrpt->p.functions, sizeof(function_table_name));

	for (vp = ast_variable_browse(myrpt->cfg, function_table_name); vp; vp = vp->next) {
		if (!strncasecmp(vp->name, digits, strlen(vp->name)))
			break;
	}	
	if (!vp) {
		int n;

		n = myrpt->longestfunc;
		if (command_source == SOURCE_LNK)
			n = myrpt->link_longestfunc;
		else if (command_source == SOURCE_PHONE)
			n = myrpt->phone_longestfunc;
		else if (command_source == SOURCE_DPHONE)
			n = myrpt->dphone_longestfunc;

		if (strlen(digits) >= n)
			return DC_ERROR;
		else
			return DC_INDETERMINATE;
	}

	/* Found a match, retrieve value part and parse */
	stringp = ast_strdupa(vp->value);
	AST_STANDARD_APP_ARGS(args, stringp);

	ast_debug(1, "@@@@ action: %s, param = %s\n", args.action, S_OR(args.param, "(null)"));
	/* Look up the action */
	for (i = 0; i < (sizeof(function_table) / sizeof(struct function_table_tag)); i++) {
		if (!strncasecmp(args.action, function_table[i].action, strlen(args.action)))
			break;
	}
	ast_debug(1, "@@@@ table index i = %d\n", i);
	if (i == (sizeof(function_table) / sizeof(struct function_table_tag))) {
		/* Error, action not in table */
		return DC_ERROR;
	}
	if (function_table[i].function == NULL) {
		/* Error, function undefined */
		ast_debug(1, "@@@@ NULL for action: %s\n", args.action);
		return DC_ERROR;
	}
	functiondigits = digits + strlen(vp->name);
	return (*function_table[i].function)(myrpt, args.param, functiondigits, command_source, mylink);
}


static void handle_link_data(struct rpt *myrpt, struct rpt_link *mylink, char *str)
{
	char cmd[300] = "", dest[300], src[300], c;
	int	seq, res;
	struct rpt_link *l;
	struct ast_frame wf;

	wf.frametype = AST_FRAME_TEXT;
	wf.subclass = 0;
	wf.offset = 0;
	wf.mallocd = 1;
	wf.datalen = strlen(str) + 1;
	wf.samples = 0;
	if (!strcmp(str, discstr)) {
		mylink->disced = 1;
		mylink->retries = MAX_RETRIES + 1;
		ast_softhangup(mylink->chan, AST_SOFTHANGUP_DEV);
		return;
	}
	if (sscanf(str, "%s %s %s %d %c", cmd, dest, src, &seq, &c) != 5) {
		ast_log(LOG_WARNING, "Unable to parse link string %s\n", str);
		return;
	}
	if (strcmp(cmd, "D")) {
		ast_log(LOG_WARNING, "Unable to parse link string %s\n", str);
		return;
	}

	if (dest[0] == '0') {
		strcpy(dest, myrpt->name);
	}

	/* if not for me, redistribute to all links */
	if (strcmp(dest, myrpt->name)) {
		l = myrpt->links.next;
		/* see if this is one in list */
		while (l != &myrpt->links) {
			if (l->name[0] == '0') {
				l = l->next;
				continue;
			}
			/* dont send back from where it came */
			if ((l == mylink) || (!strcmp(l->name, mylink->name))) {
				l = l->next;
				continue;
			}
			/* if it is, send it and we're done */
			if (!strcmp(l->name, dest)) {
				/* send, but not to src */
				if (strcmp(l->name, src)) {
					wf.data = ast_strdup(str);
					if (l->chan)
						ast_write(l->chan, &wf);
				}
				return;
			}
			l = l->next;
		}
		l = myrpt->links.next;
		/* otherwise, send it to all of em */
		while (l != &myrpt->links) {
			if (l->name[0] == '0') {
				l = l->next;
				continue;
			}
			/* dont send back from where it came */
			if ((l == mylink) || (!strcmp(l->name, mylink->name))) {
				l = l->next;
				continue;
			}
			/* send, but not to src */
			if (strcmp(l->name, src)) {
				wf.data = ast_strdup(str);
				if (l->chan)
					ast_write(l->chan, &wf);
			}
			l = l->next;
		}
		return;
	}
	rpt_mutex_lock(&myrpt->lock);
	if (c == myrpt->p.endchar)
		myrpt->stopgen = 1;
	if (myrpt->callmode == 1) {
		myrpt->exten[myrpt->cidx++] = c;
		myrpt->exten[myrpt->cidx] = 0;
		/* if this exists */
		if (ast_exists_extension(myrpt->pchannel, myrpt->patchcontext, myrpt->exten, 1, NULL)) {
			myrpt->callmode = 2;
			if (!myrpt->patchquiet) {
				rpt_mutex_unlock(&myrpt->lock);
				rpt_telemetry(myrpt, PROC, NULL); 
				rpt_mutex_lock(&myrpt->lock);
			}
		}
		/* if can continue, do so */
		if (!ast_canmatch_extension(myrpt->pchannel, myrpt->patchcontext, myrpt->exten, 1, NULL)) {
			/* call has failed, inform user */
			myrpt->callmode = 4;
		}
	}
	if ((myrpt->callmode == 2) || (myrpt->callmode == 3)) {
		myrpt->mydtmf = c;
	}
	if (c == myrpt->p.funcchar) {
		myrpt->rem_dtmfidx = 0;
		myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx] = 0;
		time(&myrpt->rem_dtmf_time);
		rpt_mutex_unlock(&myrpt->lock);
		return;
	} else if ((c != myrpt->p.endchar) && (myrpt->rem_dtmfidx >= 0)) {
		time(&myrpt->rem_dtmf_time);
		if (myrpt->rem_dtmfidx < MAXDTMF) {
			myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx++] = c;
			myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx] = 0;
			
			rpt_mutex_unlock(&myrpt->lock);
			ast_copy_string(cmd, myrpt->rem_dtmfbuf, sizeof(cmd));
			res = collect_function_digits(myrpt, cmd, SOURCE_LNK, mylink);
			rpt_mutex_lock(&myrpt->lock);
			
			switch (res) {
			case DC_INDETERMINATE:
				break;
			case DC_REQ_FLUSH:
				myrpt->rem_dtmfidx = 0;
				myrpt->rem_dtmfbuf[0] = 0;
				break;
			case DC_COMPLETE:
				myrpt->totalexecdcommands++;
				myrpt->dailyexecdcommands++;
				ast_copy_string(myrpt->lastdtmfcommand, cmd, MAXDTMF);
				myrpt->lastdtmfcommand[MAXDTMF-1] = '\0';
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
	rpt_mutex_unlock(&myrpt->lock);
	return;
}

static void handle_link_phone_dtmf(struct rpt *myrpt, struct rpt_link *mylink, char c)
{
	char cmd[300];
	int	res;

	rpt_mutex_lock(&myrpt->lock);
	if (c == myrpt->p.endchar) {
		if (mylink->lastrx) {
			mylink->lastrx = 0;
			rpt_mutex_unlock(&myrpt->lock);
			return;
		}
		myrpt->stopgen = 1;
		if (myrpt->cmdnode[0]) {
			myrpt->cmdnode[0] = 0;
			myrpt->dtmfidx = -1;
			myrpt->dtmfbuf[0] = 0;
			rpt_mutex_unlock(&myrpt->lock);
			rpt_telemetry(myrpt, COMPLETE, NULL);
			return;
		}
	}
	if (myrpt->cmdnode[0]) {
		rpt_mutex_unlock(&myrpt->lock);
		send_link_dtmf(myrpt, c);
		return;
	}
	if (myrpt->callmode == 1) {
		myrpt->exten[myrpt->cidx++] = c;
		myrpt->exten[myrpt->cidx] = 0;
		/* if this exists */
		if (ast_exists_extension(myrpt->pchannel, myrpt->patchcontext, myrpt->exten, 1, NULL)) {
			myrpt->callmode = 2;
			if (!myrpt->patchquiet) {
				rpt_mutex_unlock(&myrpt->lock);
				rpt_telemetry(myrpt, PROC, NULL); 
				rpt_mutex_lock(&myrpt->lock);
			}
		}
		/* if can continue, do so */
		if (!ast_canmatch_extension(myrpt->pchannel, myrpt->patchcontext, myrpt->exten, 1, NULL)) {
			/* call has failed, inform user */
			myrpt->callmode = 4;
		}
	}
	if ((myrpt->callmode == 2) || (myrpt->callmode == 3)) {
		myrpt->mydtmf = c;
	}
	if (c == myrpt->p.funcchar) {
		myrpt->rem_dtmfidx = 0;
		myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx] = 0;
		time(&myrpt->rem_dtmf_time);
		rpt_mutex_unlock(&myrpt->lock);
		return;
	} else if ((c != myrpt->p.endchar) && (myrpt->rem_dtmfidx >= 0)) {
		time(&myrpt->rem_dtmf_time);
		if (myrpt->rem_dtmfidx < MAXDTMF) {
			myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx++] = c;
			myrpt->rem_dtmfbuf[myrpt->rem_dtmfidx] = 0;
			
			rpt_mutex_unlock(&myrpt->lock);
			ast_copy_string(cmd, myrpt->rem_dtmfbuf, sizeof(cmd));
			switch(mylink->phonemode) {
		    case 1:
				res = collect_function_digits(myrpt, cmd, 
					SOURCE_PHONE, mylink);
				break;
		    case 2:
				res = collect_function_digits(myrpt, cmd, 
					SOURCE_DPHONE, mylink);
				break;
		    default:
				res = collect_function_digits(myrpt, cmd, 
					SOURCE_LNK, mylink);
				break;
			}

			rpt_mutex_lock(&myrpt->lock);

			switch(res) {
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
				myrpt->totalexecdcommands++;
				myrpt->dailyexecdcommands++;
				ast_copy_string(myrpt->lastdtmfcommand, cmd, MAXDTMF);
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
	rpt_mutex_unlock(&myrpt->lock);
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
	switch (i) {
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

	s = strchr(str, '.');
	i = 0;
	if (s)
		i = atoi(s + 1);
	i += atoi(str) * 10;
	switch(i) {
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

static void rbi_out_parallel(struct rpt *myrpt, unsigned char *data)
{
	int i, j;
	unsigned char od, d;

	for (i = 0; i < 5; i++) {
		od = *data++; 
		for (j = 0; j < 8; j++) {
			d = od & 1;
			outb(d, myrpt->p.iobase);
			usleep(15);
			od >>= 1;
			outb(d | 2, myrpt->p.iobase);
			usleep(30);
			outb(d, myrpt->p.iobase);
			usleep(10);
		}
	}
	/* >= 50 us */
	usleep(50);
}

static void rbi_out(struct rpt *myrpt, unsigned char *data)
{
	struct dahdi_radio_param r = { 0, };

	r.radpar = DAHDI_RADPAR_REMMODE;
	r.data = DAHDI_RADPAR_REM_RBI1;
	/* if setparam ioctl fails, its probably not a pciradio card */
	if (ioctl(myrpt->rxchannel->fds[0], DAHDI_RADIO_SETPARAM, &r) == -1) {
		rbi_out_parallel(myrpt, data);
		return;
	}
	r.radpar = DAHDI_RADPAR_REMCOMMAND;
	memcpy(&r.data, data, 5);
	if (ioctl(myrpt->rxchannel->fds[0], DAHDI_RADIO_SETPARAM, &r) == -1) {
		ast_log(LOG_WARNING, "Cannot send RBI command for channel %s\n", myrpt->rxchannel->name);
		return;
	}
}

static int serial_remote_io(struct rpt *myrpt, unsigned char *txbuf, int txbytes, char *rxbuf, int rxmaxbytes, int asciiflag)
{
	int i;
	struct dahdi_radio_param prm;

	char *buf = alloca(30 + txbytes * 3);
	int len;
	ast_copy_string(buf, "String output was: ", 30 + txbytes * 3);
	len = strlen(buf);
	for (i = 0; i < txbytes; i++)
		len += snprintf(buf + len, 30 + txbytes * 3 - len, "%02X ", (unsigned char) txbuf[i]);
	strcat(buf + len, "\n");
	ast_debug(1, "%s", buf);

	prm.radpar = DAHDI_RADPAR_REMMODE;
	if (asciiflag)
		prm.data = DAHDI_RADPAR_REM_SERIAL_ASCII;
	else
		prm.data = DAHDI_RADPAR_REM_SERIAL;
	if (ioctl(myrpt->rxchannel->fds[0], DAHDI_RADIO_SETPARAM, &prm) == -1)
		return -1;
	prm.radpar = DAHDI_RADPAR_REMCOMMAND;
	prm.data = rxmaxbytes;
	memcpy(prm.buf, txbuf, txbytes);
	prm.index = txbytes;
	if (ioctl(myrpt->rxchannel->fds[0], DAHDI_RADIO_SETPARAM, &prm) == -1)
		return -1;
	if (rxbuf) {
		*rxbuf = 0;
		memcpy(rxbuf, prm.buf, prm.index);
	}
	return(prm.index);
}

static int setrbi(struct rpt *myrpt)
{
	char tmp[MAXREMSTR] = "", *s;
	unsigned char rbicmd[5];
	int	band, txoffset = 0, txpower = 0, txpl;

	/* must be a remote system */
	if (!myrpt->remote)
		return(0);
	/* must have rbi hardware */
	if (strncmp(myrpt->remote, remote_rig_rbi, 3))
		return(0);
	ast_copy_string(tmp, myrpt->freq, sizeof(tmp));
	s = strchr(tmp, '.');
	/* if no decimal, is invalid */
	
	if (s == NULL) {
		ast_debug(1, "@@@@ Frequency needs a decimal\n");
		return -1;
	}
	
	*s++ = 0;
	if (strlen(tmp) < 2) {
		ast_debug(1, "@@@@ Bad MHz digits: %s\n", tmp);
	 	return -1;
	}
	 
	if (strlen(s) < 3) {
		ast_debug(1, "@@@@ Bad KHz digits: %s\n", s);
	 	return -1;
	}

	if ((s[2] != '0') && (s[2] != '5')) {
		ast_debug(1, "@@@@ KHz must end in 0 or 5: %c\n", s[2]);
	 	return -1;
	}
	 
	band = rbi_mhztoband(tmp);
	if (band == -1) {
		ast_debug(1, "@@@@ Bad Band: %s\n", tmp);
	 	return -1;
	}
	
	txpl = rbi_pltocode(myrpt->txpl);
	
	if (txpl == -1) {
		ast_debug(1, "@@@@ Bad TX PL: %s\n", myrpt->txpl);
	 	return -1;
	}

	
	switch (myrpt->offset) {
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
	switch(myrpt->powerlevel) {
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
	if (s[2] == '5')
		rbicmd[2] |= 0x40;
	rbicmd[3] = ((*s - '0') << 4) + (s[1] - '0');
	rbicmd[4] = txpl;
	if (myrpt->txplon)
		rbicmd[4] |= 0x40;
	if (myrpt->rxplon)
		rbicmd[4] |= 0x80;
	rbi_out(myrpt, rbicmd);
	return 0;
}


/* Check for valid rbi frequency */
/* Hard coded limits now, configurable later, maybe? */

static int check_freq_rbi(int m, int d, int *defmode)
{
	int dflmd = REM_MODE_FM;

	if (m == 50) { /* 6 meters */
		if (d < 10100)
			return -1;
	} else if ((m >= 51) && ( m < 54)) {
		/* nada */
	} else if (m == 144) { /* 2 meters */
		if (d < 10100)
			return -1;
	} else if ((m >= 145) && (m < 148)) {
		/* nada */
	} else if ((m >= 222) && (m < 225)) { /* 1.25 meters */
		/* nada */
	} else if ((m >= 430) && (m < 450)) { /* 70 centimeters */
		/* nada */
	} else if ((m >= 1240) && (m < 1300)) { /* 23 centimeters */
		/* nada */
	} else
		return -1;
	
	if (defmode)
		*defmode = dflmd;	

	return 0;
}

static int split_decimal(char *input, int *ints, int *decs, int places)
{
	double input2 = 0.0;
	long long modifier = (long long)pow(10.0, (double)places);
	if (sscanf(input, "%lf", &input2) == 1) {
		long long input3 = input2 * modifier;
		*ints = input3 / modifier;
		*decs = input3 % modifier;
		return 0;
	} else
		return -1;
}

/*
* Split frequency into mhz and decimals
*/
 
#define split_freq(mhz, decimal, freq)	split_decimal(freq, mhz, decimal, 5)

/*
* Split ctcss frequency into hertz and decimal
*/
 
#define split_ctcss_freq(hertz, decimal, freq)	split_decimal(freq, hertz, decimal, 1)

/*
* FT-897 I/O handlers
*/

/* Check to see that the frequency is valid */
/* Hard coded limits now, configurable later, maybe? */

static int check_freq_ft897(int m, int d, int *defmode)
{
	int dflmd = REM_MODE_FM;

	if (m == 1) { /* 160 meters */
		dflmd =	REM_MODE_LSB; 
		if (d < 80001)
			return -1;
	} else if (m == 3) { /* 80 meters */
		dflmd = REM_MODE_LSB;
		if (d < 75001)
			return -1;
	} else if (m == 7) { /* 40 meters */
		dflmd = REM_MODE_LSB;
		if ((d < 15001) || (d > 29999))
			return -1;
	} else if (m == 14) { /* 20 meters */
		dflmd = REM_MODE_USB;
		if ((d < 15001) || (d > 34999))
			return -1;
	} else if (m == 18) { /* 17 meters */
		dflmd = REM_MODE_USB;
		if ((d < 11001) || (d > 16797))
			return -1;
	} else if (m == 21) { /* 15 meters */
		dflmd = REM_MODE_USB;
		if ((d < 20001) || (d > 44999))
			return -1;
	} else if (m == 24) { /* 12 meters */
		dflmd = REM_MODE_USB;
		if ((d < 93001) || (d > 98999))
			return -1;
	} else if (m == 28) { /* 10 meters */
		dflmd = REM_MODE_USB;
		if (d < 30001)
			return -1;
	} else if (m == 29) { 
		if (d >= 51000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
		if (d > 69999)
			return -1;
	} else if (m == 50) { /* 6 meters */
		if (d < 10100)
			return -1;
		if (d >= 30000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
	} else if ((m >= 51) && ( m < 54)) {
		dflmd = REM_MODE_FM;
	} else if (m == 144) { /* 2 meters */
		if (d < 10100)
			return -1;
		if (d >= 30000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
	} else if ((m >= 145) && (m < 148)) {
		dflmd = REM_MODE_FM;
	} else if ((m >= 430) && (m < 450)) { /* 70 centimeters */
		if (m  < 438)
			dflmd = REM_MODE_USB;
		else
			dflmd = REM_MODE_FM;
		;
	} else
		return -1;

	if (defmode)
		*defmode = dflmd;

	return 0;
}

/*
* Set a new frequency for the FT897
*/

static int set_freq_ft897(struct rpt *myrpt, char *newfreq)
{
	unsigned char cmdstr[5];
	int fd, m, d;

	fd = 0;
	ast_debug(1, "New frequency: %s\n", newfreq);

	if (split_freq(&m, &d, newfreq))
		return -1; 

	/* The FT-897 likes packed BCD frequencies */

	cmdstr[0] = ((m / 100) << 4) + ((m % 100) / 10);              /* 100MHz 10Mhz */
	cmdstr[1] = ((m % 10) << 4) + (d / 10000);                    /* 1MHz 100KHz */
	cmdstr[2] = (((d % 10000) / 1000) << 4) + ((d % 1000) / 100); /* 10KHz 1KHz */
	cmdstr[3] = (((d % 100) / 10) << 4) + (d % 10);               /* 100Hz 10Hz */
	cmdstr[4] = 0x01;                                             /* command */

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);
}

/* ft-897 simple commands */

static int simple_command_ft897(struct rpt *myrpt, char command)
{
	unsigned char cmdstr[5] = { 0, 0, 0, 0, command };

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);
}

/* ft-897 offset */

static int set_offset_ft897(struct rpt *myrpt, char offset)
{
	unsigned char cmdstr[5] = "";

	switch (offset) {
	case REM_SIMPLEX:
		cmdstr[0] = 0x89;
		break;
	case REM_MINUS:
		cmdstr[0] = 0x09;
		break;
	case REM_PLUS:
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
	unsigned char cmdstr[5] = { 0, 0, 0, 0, 0x07 };

	switch (newmode) {
	case REM_MODE_FM:
		cmdstr[0] = 0x08;
		break;
	case REM_MODE_USB:
		cmdstr[0] = 0x01;
		break;
	case REM_MODE_LSB:
		cmdstr[0] = 0x00;
		break;
	case REM_MODE_AM:
		cmdstr[0] = 0x04;
		break;
	default:
		return -1;
	}

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);
}

/* Set tone encode and decode modes */

static int set_ctcss_mode_ft897(struct rpt *myrpt, char txplon, char rxplon)
{
	unsigned char cmdstr[5] = { 0, 0, 0, 0, 0x0A };

	if (rxplon && txplon)
		cmdstr[0] = 0x2A; /* Encode and Decode */
	else if (!rxplon && txplon)
		cmdstr[0] = 0x4A; /* Encode only */
	else if (rxplon && !txplon)
		cmdstr[0] = 0x3A; /* Encode only */
	else
		cmdstr[0] = 0x8A; /* OFF */

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);
}


/* Set transmit and receive ctcss tone frequencies */

static int set_ctcss_freq_ft897(struct rpt *myrpt, char *txtone, char *rxtone)
{
	unsigned char cmdstr[5] = { 0, 0, 0, 0, 0x0B };
	int hertz, decimal;

	if (split_ctcss_freq(&hertz, &decimal, txtone))
		return -1; 

	cmdstr[0] = ((hertz / 100) << 4) + (hertz % 100) / 10;
	cmdstr[1] = ((hertz % 10) << 4) + (decimal % 10);
	
	if (rxtone) {
		if (split_ctcss_freq(&hertz, &decimal, rxtone))
			return -1; 

		cmdstr[2] = ((hertz / 100) << 4) + (hertz % 100)/ 10;
		cmdstr[3] = ((hertz % 10) << 4) + (decimal % 10);
	}

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);
}	



static int set_ft897(struct rpt *myrpt)
{
	int res;
	
	ast_debug(1, "@@@@ lock on\n");

	res = simple_command_ft897(myrpt, 0x00);				/* LOCK on */	

	ast_debug(1, "@@@@ ptt off\n");

	if (!res)
		res = simple_command_ft897(myrpt, 0x88);		/* PTT off */

	ast_debug(1, "Modulation mode\n");

	if (!res)
		res = set_mode_ft897(myrpt, myrpt->remmode);		/* Modulation mode */

	ast_debug(1, "Split off\n");

	if (!res)
		simple_command_ft897(myrpt, 0x82);			/* Split off */

	ast_debug(1, "Frequency\n");

	if (!res)
		res = set_freq_ft897(myrpt, myrpt->freq);		/* Frequency */
	if ((myrpt->remmode == REM_MODE_FM)) {
		ast_debug(1, "Offset\n");
		if (!res)
			res = set_offset_ft897(myrpt, myrpt->offset);	/* Offset if FM */
		if ((!res)&&(myrpt->rxplon || myrpt->txplon)) {
			ast_debug(1, "CTCSS tone freqs.\n");
			res = set_ctcss_freq_ft897(myrpt, myrpt->txpl, myrpt->rxpl); /* CTCSS freqs if CTCSS is enabled */
		}
		if (!res) {
			ast_debug(1, "CTCSS mode\n");
			res = set_ctcss_mode_ft897(myrpt, myrpt->txplon, myrpt->rxplon); /* CTCSS mode */
		}
	}
	if ((myrpt->remmode == REM_MODE_USB)||(myrpt->remmode == REM_MODE_LSB)) {
		ast_debug(1, "Clarifier off\n");
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
	int m, d;

	ast_debug(1, "Before bump: %s\n", myrpt->freq);

	if (split_freq(&m, &d, myrpt->freq))
		return -1;
	
	d += (interval / 10); /* 10Hz resolution */
	if (d < 0) {
		m--;
		d += 100000;
	} else if (d >= 100000) {
		m++;
		d -= 100000;
	}

	if (check_freq_ft897(m, d, NULL)) {
		ast_debug(1, "Bump freq invalid\n");
		return -1;
	}

	snprintf(myrpt->freq, MAXREMSTR, "%d.%05d", m, d);
	ast_debug(1, "After bump: %s\n", myrpt->freq);

	return set_freq_ft897(myrpt, myrpt->freq);	
}



/*
* Dispatch to correct I/O handler 
*/

static int setrem(struct rpt *myrpt)
{
	return 0; /* XXX BROKEN!! */
	if (!strcmp(myrpt->remote, remote_rig_ft897))
		return set_ft897(myrpt);
	else if (!strcmp(myrpt->remote, remote_rig_rbi))
		return setrbi(myrpt);
	else
		return -1;
}

static int closerem(struct rpt *myrpt)
{
	return 0; /* XXX BROKEN!! */
	if (!strcmp(myrpt->remote, remote_rig_ft897))
		return closerem_ft897(myrpt);
	else
		return 0;
}

/*
* Dispatch to correct frequency checker
*/

static int check_freq(struct rpt *myrpt, int m, int d, int *defmode)
{
	if (!strcmp(myrpt->remote, remote_rig_ft897))
		return check_freq_ft897(m, d, defmode);
	else if (!strcmp(myrpt->remote, remote_rig_rbi))
		return check_freq_rbi(m, d, defmode);
	else
		return -1;
}

/*
* Return 1 if rig is multimode capable
*/

static int multimode_capable(struct rpt *myrpt)
{
	if (!strcmp(myrpt->remote, remote_rig_ft897))
		return 1;
	return 0;
}	

/*
* Dispatch to correct frequency bumping function
*/

static int multimode_bump_freq(struct rpt *myrpt, int interval)
{
	if (!strcmp(myrpt->remote, remote_rig_ft897))
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
	int res, interval, mhz, decimals;
	char k10=0, k100=0;

	switch (myrpt->hfscanmode) {

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

	res = split_freq(&mhz, &decimals, myrpt->freq);
		
	if (!res) {
		k100 = decimals / 10000;
		k10 = (decimals / 1000) % 10;
		res = multimode_bump_freq(myrpt, interval);
	}

	if (!res)
		res = split_freq(&mhz, &decimals, myrpt->freq);

	if (res) {
		stop_scan(myrpt, 1);
		return -1;
	}

	/* Announce 10KHz boundaries */
	if (k10 != (decimals / 1000) % 10) {
		int myhund = (interval < 0) ? k100 : decimals / 10000;
		int myten = (interval < 0) ? k10 : (decimals / 1000) % 10;
		myrpt->hfscanstatus = (myten == 0) ? (myhund) * 100 : (myten) * 10;
	}
	return res;

}


static int rmt_telem_start(struct rpt *myrpt, struct ast_channel *chan, int delay)
{
	myrpt->remotetx = 0;
	ast_indicate(myrpt->txchannel, AST_CONTROL_RADIO_UNKEY);
	if (!myrpt->remoterx)
		ast_indicate(chan, AST_CONTROL_RADIO_KEY);
	if (ast_safe_sleep(chan, delay) == -1)
			return -1;
	return 0;
}


static int rmt_telem_finish(struct rpt *myrpt, struct ast_channel *chan)
{
	struct dahdi_params par;

	if (ioctl(myrpt->txchannel->fds[0], DAHDI_GET_PARAMS, &par) == -1) {
		return -1;

	}
	if (!par.rxisoffhook) {
		ast_indicate(myrpt->remchannel, AST_CONTROL_RADIO_UNKEY);
		myrpt->remoterx = 0;
	} else {
		myrpt->remoterx = 1;
	}
	return 0;
}


static int rmt_sayfile(struct rpt *myrpt, struct ast_channel *chan, int delay, char *filename)
{
	int res;

	res = rmt_telem_start(myrpt, chan, delay);

	if (!res)
		res = sayfile(chan, filename);
	
	if (!res)
		res = rmt_telem_finish(myrpt, chan);
	return res;
}

static int rmt_saycharstr(struct rpt *myrpt, struct ast_channel *chan, int delay, char *charstr)
{
	int res;

	res = rmt_telem_start(myrpt, chan, delay);

	if (!res)
		res = saycharstr(chan, charstr);
	
	if (!res)
		res = rmt_telem_finish(myrpt, chan);
	return res;
}

/*
* Remote base function
*/

static int function_remote(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{
	char *s, *modestr;
	const char *val;
	int i, j, ht, k, l, ls2, res, offset, offsave, modesave, defmode = 0;
	char multimode = 0;
	char oc;
	char tmp[20], freq[20] = "", savestr[20] = "";
	int mhz = 0, decimals = 0;
	struct ast_channel *mychannel;
	AST_DECLARE_APP_ARGS(args1,
		AST_APP_ARG(freq);
		AST_APP_ARG(xpl);
		AST_APP_ARG(mode);
	);
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(s1);
		AST_APP_ARG(s2);
	);

	if ((!param) || (command_source == SOURCE_RPT) || (command_source == SOURCE_LNK))
		return DC_ERROR;
		
	multimode = multimode_capable(myrpt);
	mychannel = myrpt->remchannel;

	switch (myatoi(param)) {
	case 1:  /* retrieve memory */
		if (strlen(digitbuf) < 2) /* needs 2 digits */
			break;
			
		for (i = 0 ; i < 2 ; i++) {
			if ((digitbuf[i] < '0') || (digitbuf[i] > '9'))
				return DC_ERROR;
		}

		val = ast_variable_retrieve(myrpt->cfg, myrpt->p.memory, digitbuf);
		if (!val) {
			if (ast_safe_sleep(mychannel, 1000) == -1)
				return DC_ERROR;
			sayfile(mychannel, "rpt/memory_notfound");
			return DC_COMPLETE;
		}
		s = ast_strdupa(val);
		AST_STANDARD_APP_ARGS(args1, s);
		if (args1.argc < 3)
			return DC_ERROR;
		ast_copy_string(myrpt->freq, args1.freq, sizeof(myrpt->freq));
		ast_copy_string(myrpt->rxpl, args1.xpl, sizeof(myrpt->rxpl));
		ast_copy_string(myrpt->txpl, args1.xpl, sizeof(myrpt->rxpl));
		myrpt->remmode = REM_MODE_FM;
		myrpt->offset = REM_SIMPLEX;
		myrpt->powerlevel = REM_MEDPWR;
		myrpt->txplon = myrpt->rxplon = 0;
		modestr = args1.mode;
		while (*modestr) {
			switch (*modestr++) {
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
		for (i = 0, j = 0, k = 0, l = 0 ; digitbuf[i] ; i++) { /* look for M+*K+*O or M+*H+* depending on mode */
			if (digitbuf[i] == '*') {
				j++;
				continue;
			}
			if ((digitbuf[i] < '0') || (digitbuf[i] > '9'))
				goto invalid_freq;
			else {
				if (j == 0)
					l++; /* # of digits before first * */
				if (j == 1)
					k++; /* # of digits after first * */
			}
		}

		i = strlen(digitbuf) - 1;
		if (multimode) {
			if ((j > 2) || (l > 3) || (k > 6))
				goto invalid_freq; /* &^@#! */
 		} else {
			if ((j > 2) || (l > 4) || (k > 3))
				goto invalid_freq; /* &^@#! */
		}

		/* Wait for M+*K+* */

		if (j < 2)
			break; /* Not yet */

		/* We have a frequency */

		s = ast_strdupa(digitbuf);
		AST_NONSTANDARD_APP_ARGS(args, s, '*');
		ls2 = strlen(args.s2);	

		switch (ls2) { /* Allow partial entry of khz and hz digits for laziness support */
		case 1:
			ht = 0;
			k = 100 * atoi(args.s2);
			break;
		case 2:
			ht = 0;
			k = 10 * atoi(args.s2);
			break;
		case 3:
			if (!multimode) {
				if ((args.s2[2] != '0') && (args.s2[2] != '5'))
					goto invalid_freq;
			}
			ht = 0;
			k = atoi(args.s2);
				break;
		case 4:
			k = atoi(args.s2) / 10;
			ht = 10 * (atoi(args.s2 + (ls2 - 1)));
			break;
		case 5:
			k = atoi(args.s2) / 100;
			ht = (atoi(args.s2 + (ls2 - 2)));
			break;
		default:
			goto invalid_freq;
		}

		/* Check frequency for validity and establish a default mode */
			
		snprintf(freq, sizeof(freq), "%s.%03d%02d", args.s1, k, ht);
		ast_debug(1, "New frequency: %s\n", freq);		
	
		split_freq(&mhz, &decimals, freq);

		if (check_freq(myrpt, mhz, decimals, &defmode)) /* Check to see if frequency entered is legit */
			goto invalid_freq;

		if ((defmode == REM_MODE_FM) && (digitbuf[i] == '*')) /* If FM, user must enter and additional offset digit */
			break; /* Not yet */

		offset = REM_SIMPLEX; /* Assume simplex */

		if (defmode == REM_MODE_FM) {
			oc = *s; /* Pick off offset */
			if (oc) {
				switch (oc) {
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
		ast_copy_string(savestr, myrpt->freq, sizeof(savestr));
		ast_copy_string(myrpt->freq, freq, sizeof(myrpt->freq));
		myrpt->offset = offset;
		myrpt->remmode = defmode;

		if (setrem(myrpt) == -1) {
			myrpt->offset = offsave;
			myrpt->remmode = modesave;
			ast_copy_string(myrpt->freq, savestr, sizeof(myrpt->freq));
			goto invalid_freq;
		}

		return DC_COMPLETE;

invalid_freq:
		rmt_sayfile(myrpt, mychannel, 1000, "rpt/invalid-freq");

		return DC_ERROR; 
		
	case 3: /* set rx PL tone */
			
		for (i = 0, j = 0, k = 0, l = 0 ; digitbuf[i] ; i++) { /* look for N+*N */
			if (digitbuf[i] == '*') {
				j++;
				continue;
			}
			if ((digitbuf[i] < '0') || (digitbuf[i] > '9'))
				return DC_ERROR;
			else {
				if (j)
					l++;
				else
					k++;
			}
		}
		if ((j > 1) || (k > 3) || (l > 1))
			return DC_ERROR; /* &$@^! */
		i = strlen(digitbuf) - 1;
		if ((j != 1) || (k < 2)|| (l != 1))
			break; /* Not yet */
		ast_debug(1, "PL digits entered %s\n", digitbuf);
 		
		ast_copy_string(tmp, digitbuf, sizeof(tmp));
		/* see if we have at least 1 */
		s = strchr(tmp, '*');
		if (s)
			*s = '.';
		ast_copy_string(savestr, myrpt->rxpl, sizeof(savestr));
		ast_copy_string(myrpt->rxpl, tmp, sizeof(myrpt->rxpl));

		if (setrem(myrpt) == -1) {
			ast_copy_string(myrpt->rxpl, savestr, sizeof(myrpt->rxpl));
			return DC_ERROR;
		}

		return DC_COMPLETE;
	case 4: /* set tx PL tone */
		for (i = 0, j = 0, k = 0, l = 0 ; digitbuf[i] ; i++) { /* look for N+*N */
			if (digitbuf[i] == '*') {
				j++;
				continue;
			}
			if ((digitbuf[i] < '0') || (digitbuf[i] > '9'))
				return DC_ERROR;
			else {
				if (j)
					l++;
				else
					k++;
			}
		}
		if ((j > 1) || (k > 3) || (l > 1))
			return DC_ERROR; /* &$@^! */
		i = strlen(digitbuf) - 1;
		if ((j != 1) || (k < 2)|| (l != 1))
			break; /* Not yet */
		ast_debug(1, "PL digits entered %s\n", digitbuf);

		ast_copy_string(tmp, digitbuf, sizeof(tmp));
		/* see if we have at least 1 */
		s = strchr(tmp, '*');
		if (s)
			*s = '.';
		ast_copy_string(savestr, myrpt->txpl, sizeof(savestr));
		ast_copy_string(myrpt->txpl, tmp, sizeof(myrpt->txpl));
			
		if (setrem(myrpt) == -1) {
			ast_copy_string(myrpt->txpl, savestr, sizeof(myrpt->txpl));
			return DC_ERROR;
		}

		return DC_COMPLETE;

	case 6: /* MODE (FM,USB,LSB,AM) */
		if (strlen(digitbuf) < 1)
			break;

		if (!multimode)
			return DC_ERROR; /* Multimode radios only */

		switch (*digitbuf) {
		case '1':
			split_freq(&mhz, &decimals, myrpt->freq); 
			if (mhz < 29) /* No FM allowed below 29MHz! */
				return DC_ERROR;
			myrpt->remmode = REM_MODE_FM;
			res = rmt_saycharstr(myrpt, mychannel, 1000, "FM");
			break;

		case '2':
			myrpt->remmode = REM_MODE_USB;
			res = rmt_saycharstr(myrpt, mychannel, 1000, "USB");
			break;	

		case '3':
			myrpt->remmode = REM_MODE_LSB;
			res = rmt_saycharstr(myrpt, mychannel, 1000, "LSB");
			break;

		case '4':
			myrpt->remmode = REM_MODE_AM;
			res = rmt_saycharstr(myrpt, mychannel, 1000, "AM");
			break;

		default:
			return DC_ERROR;
		}
		if (res)
			return DC_ERROR;

		if (setrem(myrpt))
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
		switch (myatoi(param)) { /* Quick commands requiring a setrem call */
		case 100: /* RX PL Off */
			myrpt->rxplon = 0;
			if (!res)
				res = sayfile(mychannel, "rpt/rxpl");
			if (!res)
				sayfile(mychannel, "rpt/off");
			break;

		case 101: /* RX PL On */
			myrpt->rxplon = 1;
			if (!res)
				res = sayfile(mychannel, "rpt/rxpl");
			if (!res)
				sayfile(mychannel, "rpt/on");
			break;

		case 102: /* TX PL Off */
			myrpt->txplon = 0;
			if (!res)
				res = sayfile(mychannel, "rpt/txpl");
			if (!res)
				sayfile(mychannel, "rpt/off");
			break;

		case 103: /* TX PL On */
			myrpt->txplon = 1;
			if (!res)
				res = sayfile(mychannel, "rpt/txpl");
			if (!res)
				sayfile(mychannel, "rpt/on");
			break;

		case 104: /* Low Power */
			myrpt->powerlevel = REM_LOWPWR;
			if (!res)
				res = sayfile(mychannel, "rpt/lopwr");
			break;

		case 105: /* Medium Power */
			myrpt->powerlevel = REM_MEDPWR;
			if (!res)
				res = sayfile(mychannel, "rpt/medpwr");
			break;

		case 106: /* Hi Power */
			myrpt->powerlevel = REM_HIPWR;
			if (!res)
				res = sayfile(mychannel, "rpt/hipwr");
			break;

		default:
			if (!res)
				rmt_telem_finish(myrpt, mychannel);
			return DC_ERROR;
		}
		if (!res)
			res = rmt_telem_finish(myrpt, mychannel);
		if (res)
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
		ast_indicate(myrpt->txchannel, AST_CONTROL_RADIO_UNKEY);
		if (!myrpt->remoterx)
			ast_indicate(mychannel, AST_CONTROL_RADIO_KEY);
		if (ast_safe_sleep(mychannel, 1000) == -1)
				return DC_ERROR;

		switch (myatoi(param)) {
		case 113: /* Scan down slow */
			res = sayfile(mychannel, "rpt/down");
			if (!res)
				res = sayfile(mychannel, "rpt/slow");
			if (!res) {
				myrpt->scantimer = REM_SCANTIME;
				myrpt->hfscanmode = HF_SCAN_DOWN_SLOW;
			}
			break;

		case 114: /* Scan down quick */
			res = sayfile(mychannel, "rpt/down");
			if (!res)
				res = sayfile(mychannel, "rpt/quick");
			if (!res) {
				myrpt->scantimer = REM_SCANTIME;
				myrpt->hfscanmode = HF_SCAN_DOWN_QUICK;
			}
			break;

		case 115: /* Scan down fast */
			res = sayfile(mychannel, "rpt/down");
			if (!res)
				res = sayfile(mychannel, "rpt/fast");
			if (!res) {
				myrpt->scantimer = REM_SCANTIME;
				myrpt->hfscanmode = HF_SCAN_DOWN_FAST;
			}
			break;

		case 116: /* Scan up slow */
			res = sayfile(mychannel, "rpt/up");
			if (!res)
				res = sayfile(mychannel, "rpt/slow");
			if (!res) {
				myrpt->scantimer = REM_SCANTIME;
				myrpt->hfscanmode = HF_SCAN_UP_SLOW;
			}
			break;

		case 117: /* Scan up quick */
			res = sayfile(mychannel, "rpt/up");
			if (!res)
				res = sayfile(mychannel, "rpt/quick");
			if (!res) {
				myrpt->scantimer = REM_SCANTIME;
				myrpt->hfscanmode = HF_SCAN_UP_QUICK;
			}
			break;

		case 118: /* Scan up fast */
			res = sayfile(mychannel, "rpt/up");
			if (!res)
				res = sayfile(mychannel, "rpt/fast");
			if (!res) {
				myrpt->scantimer = REM_SCANTIME;
				myrpt->hfscanmode = HF_SCAN_UP_FAST;
			}
			break;
		}
		rmt_telem_finish(myrpt, mychannel);
		return DC_COMPLETE;


	case 119: /* Tune Request */
		myrpt->tunerequest = 1;
		return DC_COMPLETE;

	case 5: /* Long Status */
	case 140: /* Short Status */
		res = rmt_telem_start(myrpt, mychannel, 1000);

		res = sayfile(mychannel, "rpt/node");
		if (!res)
			res = saycharstr(mychannel, myrpt->name);
		if (!res)
			res = sayfile(mychannel, "rpt/frequency");
		if (!res)
			res = split_freq(&mhz, &decimals, myrpt->freq);
		if (!res) {
			if (mhz < 100)
				res = saynum(mychannel, mhz);
			else
				res = saydigits(mychannel, mhz);
		}
		if (!res)
			res = sayfile(mychannel, "letters/dot");
		if (!res)
			res = saydigits(mychannel, decimals);

		if (res) {	
			rmt_telem_finish(myrpt, mychannel);
			return DC_ERROR;
		}
		if (myrpt->remmode == REM_MODE_FM) { /* Mode FM? */
			switch (myrpt->offset) {
			case REM_MINUS:
				res = sayfile(mychannel, "rpt/minus");
				break;

			case REM_SIMPLEX:
				res = sayfile(mychannel, "rpt/simplex");
				break;

			case REM_PLUS:
				res = sayfile(mychannel, "rpt/plus");
				break;

			default:
				return DC_ERROR;

			}
		} else { /* Must be USB, LSB, or AM */
			switch (myrpt->remmode) {
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

		if (res == -1) {
			rmt_telem_finish(myrpt, mychannel);
			return DC_ERROR;
		}

		if (myatoi(param) == 140) { /* Short status? */
			if (!res)
				res = rmt_telem_finish(myrpt, mychannel);
			if (res)
				return DC_ERROR;
			return DC_COMPLETE;
		}

		switch (myrpt->powerlevel) {
		case REM_LOWPWR:
			res = sayfile(mychannel, "rpt/lopwr") ;
			break;
		case REM_MEDPWR:
			res = sayfile(mychannel, "rpt/medpwr");
			break;
		case REM_HIPWR:
			res = sayfile(mychannel, "rpt/hipwr"); 
			break;
		}
		if (res || (sayfile(mychannel, "rpt/rxpl") == -1) ||
			(sayfile(mychannel, "rpt/frequency") == -1) ||
			(saycharstr(mychannel, myrpt->rxpl) == -1) ||
			(sayfile(mychannel, "rpt/txpl") == -1) ||
			(sayfile(mychannel, "rpt/frequency") == -1) ||
			(saycharstr(mychannel, myrpt->txpl) == -1) ||
			(sayfile(mychannel, "rpt/txpl") == -1) ||
			(sayfile(mychannel, ((myrpt->txplon) ? "rpt/on" : "rpt/off")) == -1) ||
			(sayfile(mychannel, "rpt/rxpl") == -1) ||
			(sayfile(mychannel, ((myrpt->rxplon) ? "rpt/on" : "rpt/off")) == -1))
			{
			rmt_telem_finish(myrpt, mychannel);
			return DC_ERROR;
		}
		if (!res)
			res = rmt_telem_finish(myrpt, mychannel);
		if (res)
			return DC_ERROR;

		return DC_COMPLETE;
	default:
		return DC_ERROR;
	}

	return DC_INDETERMINATE;
}

static int handle_remote_dtmf_digit(struct rpt *myrpt, char c, char *keyed, int phonemode)
{
	time_t now;
	int	ret, res = 0, src;

	/* Stop scan mode if in scan mode */
	if (myrpt->hfscanmode) {
		stop_scan(myrpt, 0);
		return 0;
	}

	time(&now);
	/* if timed-out */
	if ((myrpt->dtmf_time_rem + DTMF_TIMEOUT) < now) {
		myrpt->dtmfidx = -1;
		myrpt->dtmfbuf[0] = 0;
		myrpt->dtmf_time_rem = 0;
	}
	/* if decode not active */
	if (myrpt->dtmfidx == -1) {
		/* if not lead-in digit, dont worry */
		if (c != myrpt->p.funcchar)
			return 0;
		myrpt->dtmfidx = 0;
		myrpt->dtmfbuf[0] = 0;
		myrpt->dtmf_time_rem = now;
		return 0;
	}
	/* if too many in buffer, start over */
	if (myrpt->dtmfidx >= MAXDTMF) {
		myrpt->dtmfidx = 0;
		myrpt->dtmfbuf[0] = 0;
		myrpt->dtmf_time_rem = now;
	}
	if (c == myrpt->p.funcchar) {
		/* if star at beginning, or 2 together, erase buffer */
		if ((myrpt->dtmfidx < 1) || (myrpt->dtmfbuf[myrpt->dtmfidx - 1] == myrpt->p.funcchar)) {
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
	if (phonemode > 1)
		src = SOURCE_DPHONE;
	else if (phonemode)
		src = SOURCE_PHONE;
	ret = collect_function_digits(myrpt, myrpt->dtmfbuf, src, NULL);
	
	switch(ret) {
	case DC_INDETERMINATE:
		res = 0;
		break;
	case DC_DOKEY:
		if (keyed)
			*keyed = 1;
		res = 0;
		break;
	case DC_REQ_FLUSH:
		myrpt->dtmfidx = 0;
		myrpt->dtmfbuf[0] = 0;
		res = 0;
		break;
	case DC_COMPLETE:
		myrpt->totalexecdcommands++;
		myrpt->dailyexecdcommands++;
		ast_copy_string(myrpt->lastdtmfcommand, myrpt->dtmfbuf, MAXDTMF);
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
	}

	return res;
}

static int handle_remote_data(struct rpt *myrpt, char *str)
{
	char cmd[300], dest[300], src[300], c;
	int	seq, res;

	if (!strcmp(str, discstr))
		return 0;
	if (sscanf(str, "%s %s %s %d %c", cmd, dest, src, &seq, &c) != 5) {
		ast_log(LOG_WARNING, "Unable to parse link string %s\n", str);
		return 0;
	}
	if (strcmp(cmd, "D")) {
		ast_log(LOG_WARNING, "Unable to parse link string %s\n", str);
		return 0;
	}
	/* if not for me, ignore */
	if (strcmp(dest, myrpt->name))
		return 0;
	res = handle_remote_dtmf_digit(myrpt, c, NULL, 0);
	if (res != 1)
		return res;
	myrpt->remotetx = 0;
	ast_indicate(myrpt->txchannel, AST_CONTROL_RADIO_UNKEY);
	if (!myrpt->remoterx) {
		ast_indicate(myrpt->remchannel, AST_CONTROL_RADIO_KEY);
	}
	if (ast_safe_sleep(myrpt->remchannel, 1000) == -1)
		return -1;
	res = telem_lookup(myrpt, myrpt->remchannel, myrpt->name, "functcomplete");
	rmt_telem_finish(myrpt, myrpt->remchannel);
	return res;
}

static int handle_remote_phone_dtmf(struct rpt *myrpt, char c, char *keyed, int phonemode)
{
	int	res;

	if (keyed && *keyed && (c == myrpt->p.endchar)) {
		*keyed = 0;
		return DC_INDETERMINATE;
	}

	res = handle_remote_dtmf_digit(myrpt, c, keyed, phonemode);
	if (res != 1)
		return res;
	myrpt->remotetx = 0;
	ast_indicate(myrpt->txchannel, AST_CONTROL_RADIO_UNKEY);
	if (!myrpt->remoterx) {
		ast_indicate(myrpt->remchannel, AST_CONTROL_RADIO_KEY);
	}
	if (ast_safe_sleep(myrpt->remchannel, 1000) == -1)
		return -1;
	res = telem_lookup(myrpt, myrpt->remchannel, myrpt->name, "functcomplete");
	rmt_telem_finish(myrpt, myrpt->remchannel);
	return res;
}

static int attempt_reconnect(struct rpt *myrpt, struct rpt_link *l)
{
	const char *val;
	char *s, *tele;
	char deststr[300] = "";
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(channel);
		AST_APP_ARG(extra);
	);

	val = ast_variable_retrieve(myrpt->cfg, myrpt->p.nodes, l->name);
	if (!val) {
		ast_log(LOG_ERROR, "attempt_reconnect: cannot find node %s\n", l->name);
		return -1;
	}

	rpt_mutex_lock(&myrpt->lock);
	/* remove from queue */
	remque((struct qelem *) l);
	rpt_mutex_unlock(&myrpt->lock);
	s = ast_strdupa(val);
	AST_STANDARD_APP_ARGS(args, s);

	/* XXX This section doesn't make any sense.  Why not just use args.channel? XXX */
	snprintf(deststr, sizeof(deststr), "IAX2/%s", args.channel);
	tele = strchr(deststr, '/');
	if (!tele) {
		ast_log(LOG_ERROR, "attempt_reconnect:Dial number (%s) must be in format tech/number\n", deststr);
		return -1;
	}
	*tele++ = 0;
	l->elaptime = 0;
	l->connecttime = 0;
	l->chan = ast_request(deststr, AST_FORMAT_SLINEAR, tele, NULL);
	if (l->chan) {
		ast_set_read_format(l->chan, AST_FORMAT_SLINEAR);
		ast_set_write_format(l->chan, AST_FORMAT_SLINEAR);
		l->chan->whentohangup = 0;
		l->chan->appl = "Apprpt";
		l->chan->data = "(Remote Rx)";
		ast_verb(3, "rpt (attempt_reconnect) initiating call to %s/%s on %s\n",
				deststr, tele, l->chan->name);
		if (l->chan->cid.cid_num)
			ast_free(l->chan->cid.cid_num);
		l->chan->cid.cid_num = ast_strdup(myrpt->name);
		ast_call(l->chan, tele, 999); 

	} else {
		ast_verb(3, "Unable to place call to %s/%s on %s\n",
				deststr, tele, l->chan->name);
		return -1;
	}
	rpt_mutex_lock(&myrpt->lock);
	/* put back in queue */
	insque((struct qelem *)l, (struct qelem *)myrpt->links.next);
	rpt_mutex_unlock(&myrpt->lock);
	ast_log(LOG_NOTICE, "Reconnect Attempt to %s in process\n", l->name);
	return 0;
}

/* 0 return=continue, 1 return = break, -1 return = error */
static void local_dtmf_helper(struct rpt *myrpt, char c)
{
	int	res;
	char cmd[MAXDTMF+1] = "";

	if (c == myrpt->p.endchar) {
	/* if in simple mode, kill autopatch */
		if (myrpt->p.simple && myrpt->callmode) {
			rpt_mutex_lock(&myrpt->lock);
			myrpt->callmode = 0;
			rpt_mutex_unlock(&myrpt->lock);
			rpt_telemetry(myrpt, TERM, NULL);
			return;
		}
		rpt_mutex_lock(&myrpt->lock);
		myrpt->stopgen = 1;
		if (myrpt->cmdnode[0]) {
			myrpt->cmdnode[0] = 0;
			myrpt->dtmfidx = -1;
			myrpt->dtmfbuf[0] = 0;
			rpt_mutex_unlock(&myrpt->lock);
			rpt_telemetry(myrpt, COMPLETE, NULL);
		} else
			rpt_mutex_unlock(&myrpt->lock);
		return;
	}
	rpt_mutex_lock(&myrpt->lock);
	if (myrpt->cmdnode[0]) {
		rpt_mutex_unlock(&myrpt->lock);
		send_link_dtmf(myrpt, c);
		return;
	}
	if (!myrpt->p.simple) {
		if (c == myrpt->p.funcchar) {
			myrpt->dtmfidx = 0;
			myrpt->dtmfbuf[myrpt->dtmfidx] = 0;
			rpt_mutex_unlock(&myrpt->lock);
			time(&myrpt->dtmf_time);
			return;
		} else if ((c != myrpt->p.endchar) && (myrpt->dtmfidx >= 0)) {
			time(&myrpt->dtmf_time);

			if (myrpt->dtmfidx < MAXDTMF) {
				myrpt->dtmfbuf[myrpt->dtmfidx++] = c;
				myrpt->dtmfbuf[myrpt->dtmfidx] = 0;

				ast_copy_string(cmd, myrpt->dtmfbuf, sizeof(cmd));

				rpt_mutex_unlock(&myrpt->lock);
				res = collect_function_digits(myrpt, cmd, SOURCE_RPT, NULL);
				rpt_mutex_lock(&myrpt->lock);
				switch(res) {
				case DC_INDETERMINATE:
					break;
				case DC_REQ_FLUSH:
					myrpt->dtmfidx = 0;
					myrpt->dtmfbuf[0] = 0;
					break;
				case DC_COMPLETE:
					myrpt->totalexecdcommands++;
					myrpt->dailyexecdcommands++;
					ast_copy_string(myrpt->lastdtmfcommand, cmd, MAXDTMF);
					myrpt->dtmfbuf[0] = 0;
					myrpt->dtmfidx = -1;
					myrpt->dtmf_time = 0;
					break;

				case DC_ERROR:
				default:
					myrpt->dtmfbuf[0] = 0;
					myrpt->dtmfidx = -1;
					myrpt->dtmf_time = 0;
					break;
				}
				if (res != DC_INDETERMINATE) {
					rpt_mutex_unlock(&myrpt->lock);
					return;
				}
			} 
		}
	} else /* if simple */ {
		if ((!myrpt->callmode) && (c == myrpt->p.funcchar)) {
			myrpt->callmode = 1;
			myrpt->patchnoct = 0;
			myrpt->patchquiet = 0;
			myrpt->patchfarenddisconnect = 0;
			myrpt->patchdialtime = 0;
			ast_copy_string(myrpt->patchcontext, myrpt->p.ourcontext, sizeof(myrpt->patchcontext));
			myrpt->cidx = 0;
			myrpt->exten[myrpt->cidx] = 0;
			rpt_mutex_unlock(&myrpt->lock);
			ast_pthread_create_detached(&myrpt->rpt_call_thread, NULL, rpt_call, (void *)myrpt);
			return;
		}
	}
	if (myrpt->callmode == 1) {
		myrpt->exten[myrpt->cidx++] = c;
		myrpt->exten[myrpt->cidx] = 0;
		/* if this exists */
		if (ast_exists_extension(myrpt->pchannel, myrpt->patchcontext, myrpt->exten, 1, NULL)) {
			myrpt->callmode = 2;
			rpt_mutex_unlock(&myrpt->lock);
			if (!myrpt->patchquiet)
				rpt_telemetry(myrpt, PROC, NULL); 
			return;
		}
		/* if can continue, do so */
		if (!ast_canmatch_extension(myrpt->pchannel, myrpt->patchcontext, myrpt->exten, 1, NULL)) {
			/* call has failed, inform user */
			myrpt->callmode = 4;
		}
		rpt_mutex_unlock(&myrpt->lock);
		return;
	}
	if ((myrpt->callmode == 2) || (myrpt->callmode == 3)) {
		myrpt->mydtmf = c;
	}
	rpt_mutex_unlock(&myrpt->lock);
	return;
}


/* place an ID event in the telemetry queue */

static void queue_id(struct rpt *myrpt)
{
	myrpt->mustid = myrpt->tailid = 0;
	myrpt->idtimer = myrpt->p.idtime; /* Reset our ID timer */
	rpt_mutex_unlock(&myrpt->lock);
	rpt_telemetry(myrpt, ID, NULL);
	rpt_mutex_lock(&myrpt->lock);
}

/* Scheduler */

static void do_scheduler(struct rpt *myrpt)
{
	int res;
	struct ast_tm tmnow;

	memcpy(&myrpt->lasttv, &myrpt->curtv, sizeof(struct timeval));
	
	if ( (res = gettimeofday(&myrpt->curtv, NULL)) < 0)
		ast_log(LOG_NOTICE, "Scheduler gettime of day returned: %s\n", strerror(res));

	/* Try to get close to a 1 second resolution */
	
	if (myrpt->lasttv.tv_sec == myrpt->curtv.tv_sec)
		return;

	ast_localtime(&myrpt->curtv, &tmnow, NULL);

	/* If midnight, then reset all daily statistics */
	
	if ((tmnow.tm_hour == 0) && (tmnow.tm_min == 0) && (tmnow.tm_sec == 0)) {
		myrpt->dailykeyups = 0;
		myrpt->dailytxtime = 0;
		myrpt->dailykerchunks = 0;
		myrpt->dailyexecdcommands = 0;
	}
}


/* single thread with one file (request) to dial */
static void *rpt(void *this)
{
<<<<<<< .working
	struct rpt *myrpt = (struct rpt *)this;
	char *tele, c;
	const char *idtalkover;
	int ms = MSWAIT, i, lasttx=0, val, remrx=0, identqueued, othertelemqueued, tailmessagequeued, ctqueued;
	struct ast_channel *who;
	DAHDI_CONFINFO ci;  /* conference info */
	time_t t;
	struct rpt_link *l, *m;
	struct rpt_tele *telem;
	char tmpstr[300];
=======
struct	rpt *myrpt = (struct rpt *)this;
char *tele,*idtalkover,c,myfirst,*p;
int ms = MSWAIT,i,lasttx=0,val,remrx=0,identqueued,othertelemqueued;
int tailmessagequeued,ctqueued,dtmfed,lastmyrx,localmsgqueued;
struct ast_channel *who;
struct dahdi_confinfo ci;  /* conference info */
time_t	t;
struct rpt_link *l,*m;
struct rpt_tele *telem;
char tmpstr[300],lstr[MAXLINKLIST];
>>>>>>> .merge-right.r134260

	rpt_mutex_lock(&myrpt->lock);

	telem = myrpt->tele.next;
	while (telem != &myrpt->tele) {
		ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV);
		telem = telem->next;
	}
	rpt_mutex_unlock(&myrpt->lock);
	/* find our index, and load the vars initially */
	for (i = 0; i < nrpts; i++) {
		if (&rpt_vars[i] == myrpt) {
			load_rpt_vars(i, 0);
			break;
		}
	}
	rpt_mutex_lock(&myrpt->lock);
	ast_copy_string(tmpstr, myrpt->rxchanname, sizeof(tmpstr));
	tele = strchr(tmpstr, '/');
	if (!tele) {
		ast_log(LOG_ERROR, "rpt:Rxchannel Dial number (%s) must be in format tech/number\n", myrpt->rxchanname);
		rpt_mutex_unlock(&myrpt->lock);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		pthread_exit(NULL);
	}
	*tele++ = 0;
	myrpt->rxchannel = ast_request(tmpstr, AST_FORMAT_SLINEAR, tele, NULL);
	if (myrpt->rxchannel) {
		if (myrpt->rxchannel->_state == AST_STATE_BUSY) {
			ast_log(LOG_ERROR, "rpt:Sorry unable to obtain Rx channel\n");
			rpt_mutex_unlock(&myrpt->lock);
			ast_hangup(myrpt->rxchannel);
			myrpt->rpt_thread = AST_PTHREADT_STOP;
			pthread_exit(NULL);
		}
		ast_set_read_format(myrpt->rxchannel, AST_FORMAT_SLINEAR);
		ast_set_write_format(myrpt->rxchannel, AST_FORMAT_SLINEAR);
		myrpt->rxchannel->whentohangup = 0;
		myrpt->rxchannel->appl = "Apprpt";
		myrpt->rxchannel->data = "(Repeater Rx)";
		ast_verb(3, "rpt (Rx) initiating call to %s/%s on %s\n",
				tmpstr, tele, myrpt->rxchannel->name);
		ast_call(myrpt->rxchannel, tele, 999);
		if (myrpt->rxchannel->_state != AST_STATE_UP) {
			rpt_mutex_unlock(&myrpt->lock);
			ast_hangup(myrpt->rxchannel);
			myrpt->rpt_thread = AST_PTHREADT_STOP;
			pthread_exit(NULL);
		}
	} else {
		ast_log(LOG_ERROR, "rpt:Sorry unable to obtain Rx channel\n");
		rpt_mutex_unlock(&myrpt->lock);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		pthread_exit(NULL);
	}
	if (myrpt->txchanname) {
		ast_copy_string(tmpstr, myrpt->txchanname, sizeof(tmpstr));
		tele = strchr(tmpstr, '/');
		if (!tele) {
			ast_log(LOG_ERROR, "rpt:Txchannel Dial number (%s) must be in format tech/number\n", myrpt->txchanname);
			rpt_mutex_unlock(&myrpt->lock);
			ast_hangup(myrpt->rxchannel);
			myrpt->rpt_thread = AST_PTHREADT_STOP;
			pthread_exit(NULL);
		}
		*tele++ = 0;
		myrpt->txchannel = ast_request(tmpstr, AST_FORMAT_SLINEAR, tele, NULL);
		if (myrpt->txchannel) {
			if (myrpt->txchannel->_state == AST_STATE_BUSY) {
				ast_log(LOG_ERROR, "rpt:Sorry unable to obtain Tx channel\n");
				rpt_mutex_unlock(&myrpt->lock);
				ast_hangup(myrpt->txchannel);
				ast_hangup(myrpt->rxchannel);
				myrpt->rpt_thread = AST_PTHREADT_STOP;
				pthread_exit(NULL);
			}			
			ast_set_read_format(myrpt->txchannel, AST_FORMAT_SLINEAR);
			ast_set_write_format(myrpt->txchannel, AST_FORMAT_SLINEAR);
			myrpt->txchannel->whentohangup = 0;
			myrpt->txchannel->appl = "Apprpt";
			myrpt->txchannel->data = "(Repeater Tx)";
			ast_verb(3, "rpt (Tx) initiating call to %s/%s on %s\n",
					tmpstr, tele, myrpt->txchannel->name);
			ast_call(myrpt->txchannel, tele, 999);
			if (myrpt->rxchannel->_state != AST_STATE_UP) {
				rpt_mutex_unlock(&myrpt->lock);
				ast_hangup(myrpt->rxchannel);
				ast_hangup(myrpt->txchannel);
				myrpt->rpt_thread = AST_PTHREADT_STOP;
				pthread_exit(NULL);
			}
		} else {
			ast_log(LOG_ERROR, "rpt:Sorry unable to obtain Tx channel\n");
			rpt_mutex_unlock(&myrpt->lock);
			ast_hangup(myrpt->rxchannel);
			myrpt->rpt_thread = AST_PTHREADT_STOP;
			pthread_exit(NULL);
		}
	} else {
		myrpt->txchannel = myrpt->rxchannel;
	}
	ast_indicate(myrpt->txchannel, AST_CONTROL_RADIO_KEY);
	ast_indicate(myrpt->txchannel, AST_CONTROL_RADIO_UNKEY);
	/* allocate a pseudo-channel thru asterisk */
	myrpt->pchannel = ast_request("zap", AST_FORMAT_SLINEAR, "pseudo", NULL);
	if (!myrpt->pchannel) {
		ast_log(LOG_ERROR, "rpt:Sorry unable to obtain pseudo channel\n");
		rpt_mutex_unlock(&myrpt->lock);
		if (myrpt->txchannel != myrpt->rxchannel) 
			ast_hangup(myrpt->txchannel);
		ast_hangup(myrpt->rxchannel);
		myrpt->rpt_thread = AST_PTHREADT_STOP;
		pthread_exit(NULL);
	}
	/* make a conference for the tx */
	ci.chan = 0;
	ci.confno = -1; /* make a new conf */
	ci.confmode = DAHDI_CONF_CONF | DAHDI_CONF_LISTENER;
	/* first put the channel on the conference in proper mode */
	if (ioctl(myrpt->txchannel->fds[0], DAHDI_SETCONF, &ci) == -1) {
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		rpt_mutex_unlock(&myrpt->lock);
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
	ci.confmode = ((myrpt->p.duplex == 2) || (myrpt->p.duplex == 4)) ? DAHDI_CONF_CONFANNMON :
		(DAHDI_CONF_CONF | DAHDI_CONF_LISTENER | DAHDI_CONF_TALKER);
	/* first put the channel on the conference in announce mode */
	if (ioctl(myrpt->pchannel->fds[0], DAHDI_SETCONF, &ci) == -1) {
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		rpt_mutex_unlock(&myrpt->lock);
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
	myrpt->txpchannel = ast_request("zap", AST_FORMAT_SLINEAR, "pseudo", NULL);
	if (!myrpt->txpchannel) {
		ast_log(LOG_ERROR, "rpt:Sorry unable to obtain pseudo channel\n");
		rpt_mutex_unlock(&myrpt->lock);
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
	ci.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER ;
 	/* first put the channel on the conference in proper mode */
	if (ioctl(myrpt->txpchannel->fds[0], DAHDI_SETCONF, &ci) == -1) {
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		rpt_mutex_unlock(&myrpt->lock);
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
	myrpt->tmsgtimer = myrpt->p.tailmessagetime;
	myrpt->idtimer = myrpt->p.politeid;
	myrpt->mustid = myrpt->tailid = 0;
	myrpt->callmode = 0;
	myrpt->tounkeyed = 0;
	myrpt->tonotify = 0;
	myrpt->retxtimer = 0;
	myrpt->skedtimer = 0;
	myrpt->tailevent = 0;
	lasttx = 0;
	myrpt->keyed = 0;
	idtalkover = ast_variable_retrieve(myrpt->cfg, myrpt->name, "idtalkover");
	myrpt->dtmfidx = -1;
	myrpt->dtmfbuf[0] = 0;
	myrpt->rem_dtmfidx = -1;
	myrpt->rem_dtmfbuf[0] = 0;
	myrpt->dtmf_time = 0;
	myrpt->rem_dtmf_time = 0;
	myrpt->enable = 1;
	myrpt->disgorgetime = 0;
	myrpt->lastnodewhichkeyedusup[0] = '\0';
	myrpt->dailytxtime = 0;
	myrpt->totaltxtime = 0;
	myrpt->dailykeyups = 0;
	myrpt->totalkeyups = 0;
	myrpt->dailykerchunks = 0;
	myrpt->totalkerchunks = 0;
	myrpt->dailyexecdcommands = 0;
	myrpt->totalexecdcommands = 0;
	myrpt->timeouts = 0;
	myrpt->exten[0] = '\0';
	myrpt->lastdtmfcommand[0] = '\0';
	if (myrpt->p.startupmacro) {
		snprintf(myrpt->macrobuf, sizeof(myrpt->macrobuf), "PPPP%s", myrpt->p.startupmacro);
	}
	if (myrpt->p.startupgosub) {
		snprintf(myrpt->gosubbuf, sizeof(myrpt->gosubbuf), "PPPP%s", myrpt->p.startupgosub);
	}
	rpt_mutex_unlock(&myrpt->lock);
	val = 0;
	ast_channel_setoption(myrpt->rxchannel, AST_OPTION_TONE_VERIFY, &val, sizeof(char), 0);
	val = 1;
	ast_channel_setoption(myrpt->rxchannel, AST_OPTION_RELAXDTMF, &val, sizeof(char), 0);
	while (ms >= 0) {
		struct ast_frame *f;
		struct ast_channel *cs[300];
		int totx=0, elap=0, n, toexit = 0;

		/* DEBUG Dump */
		if ((myrpt->disgorgetime) && (time(NULL) >= myrpt->disgorgetime)) {
			struct rpt_link *zl;
			struct rpt_tele *zt;

			myrpt->disgorgetime = 0;
			ast_log(LOG_NOTICE, "********** Variable Dump Start (app_rpt) **********\n");
			ast_log(LOG_NOTICE, "totx = %d\n", totx);
			ast_log(LOG_NOTICE, "remrx = %d\n", remrx);
			ast_log(LOG_NOTICE, "lasttx = %d\n", lasttx);
			ast_log(LOG_NOTICE, "elap = %d\n", elap);
			ast_log(LOG_NOTICE, "toexit = %d\n", toexit);

			ast_log(LOG_NOTICE, "myrpt->keyed = %d\n", myrpt->keyed);
			ast_log(LOG_NOTICE, "myrpt->localtx = %d\n", myrpt->localtx);
			ast_log(LOG_NOTICE, "myrpt->callmode = %d\n", myrpt->callmode);
			ast_log(LOG_NOTICE, "myrpt->enable = %d\n", myrpt->enable);
			ast_log(LOG_NOTICE, "myrpt->mustid = %d\n", myrpt->mustid);
			ast_log(LOG_NOTICE, "myrpt->tounkeyed = %d\n", myrpt->tounkeyed);
			ast_log(LOG_NOTICE, "myrpt->tonotify = %d\n", myrpt->tonotify);
			ast_log(LOG_NOTICE, "myrpt->retxtimer = %ld\n", myrpt->retxtimer);
			ast_log(LOG_NOTICE, "myrpt->totimer = %d\n", myrpt->totimer);
			ast_log(LOG_NOTICE, "myrpt->tailtimer = %d\n", myrpt->tailtimer);
			ast_log(LOG_NOTICE, "myrpt->tailevent = %d\n", myrpt->tailevent);

			zl = myrpt->links.next;
			while (zl != &myrpt->links) {
				ast_log(LOG_NOTICE, "*** Link Name: %s ***\n", zl->name);
				ast_log(LOG_NOTICE, "        link->lasttx %d\n", zl->lasttx);
				ast_log(LOG_NOTICE, "        link->lastrx %d\n", zl->lastrx);
				ast_log(LOG_NOTICE, "        link->connected %d\n", zl->connected);
				ast_log(LOG_NOTICE, "        link->hasconnected %d\n", zl->hasconnected);
				ast_log(LOG_NOTICE, "        link->outbound %d\n", zl->outbound);
				ast_log(LOG_NOTICE, "        link->disced %d\n", zl->disced);
				ast_log(LOG_NOTICE, "        link->killme %d\n", zl->killme);
				ast_log(LOG_NOTICE, "        link->disctime %ld\n", zl->disctime);
				ast_log(LOG_NOTICE, "        link->retrytimer %ld\n", zl->retrytimer);
				ast_log(LOG_NOTICE, "        link->retries = %d\n", zl->retries);
				ast_log(LOG_NOTICE, "        link->reconnects = %d\n", zl->reconnects);
				zl = zl->next;
			}

			zt = myrpt->tele.next;
			if (zt != &myrpt->tele)
				ast_log(LOG_NOTICE, "*** Telemetry Queue ***\n");
			while (zt != &myrpt->tele) {
				ast_log(LOG_NOTICE, "        Telemetry mode: %d\n", zt->mode);
				zt = zt->next;
			}
			ast_log(LOG_NOTICE, "******* Variable Dump End (app_rpt) *******\n");
		}	

		if (myrpt->reload) {
			struct rpt_tele *telem;

			rpt_mutex_lock(&myrpt->lock);
			telem = myrpt->tele.next;
			while (telem != &myrpt->tele) {
				ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV);
				telem = telem->next;
			}
			myrpt->reload = 0;
			rpt_mutex_unlock(&myrpt->lock);
			usleep(10000);
			/* find our index, and load the vars */
			for (i = 0; i < nrpts; i++) {
				if (&rpt_vars[i] == myrpt) {
					load_rpt_vars(i, 0);
					break;
				}
			}
		}

		rpt_mutex_lock(&myrpt->lock);
		if (ast_check_hangup(myrpt->rxchannel)) break;
		if (ast_check_hangup(myrpt->txchannel)) break;
		if (ast_check_hangup(myrpt->pchannel)) break;
		if (ast_check_hangup(myrpt->txpchannel)) break;

		/* Update local tx with keyed if not parsing a command */
		myrpt->localtx = myrpt->keyed && (myrpt->dtmfidx == -1) && (!myrpt->cmdnode[0]);
		/* If someone's connected, and they're transmitting from their end to us, set remrx true */
		l = myrpt->links.next;
		remrx = 0;
		while (l != &myrpt->links) {
			if (l->lastrx) {
				remrx = 1;
				if (l->name[0] != '0') /* Ignore '0' nodes */
					strcpy(myrpt->lastnodewhichkeyedusup, l->name); /* Note the node which is doing the key up */
			}
			l = l->next;
		}
		/* Create a "must_id" flag for the cleanup ID */		
		myrpt->mustid |= (myrpt->idtimer) && (myrpt->keyed || remrx) ;
		/* Build a fresh totx from myrpt->keyed and autopatch activated */
		totx = myrpt->callmode;
		/* If full duplex, add local tx to totx */
		if (myrpt->p.duplex > 1) totx = totx || myrpt->localtx;
		/* Traverse the telemetry list to see what's queued */
		identqueued = 0;
		othertelemqueued = 0;
		tailmessagequeued = 0;
		ctqueued = 0;
		telem = myrpt->tele.next;
		while (telem != &myrpt->tele) {
			if ((telem->mode == ID) || (telem->mode == IDTALKOVER)) {
				identqueued = 1; /* Identification telemetry */
			} else if (telem->mode == TAILMSG) {
				tailmessagequeued = 1; /* Tail message telemetry */
			} else {
				if (telem->mode != UNKEY)
					othertelemqueued = 1;  /* Other telemetry */
				else
					ctqueued = 1; /* Courtesy tone telemetry */
			}
			telem = telem->next;
		}
	
		/* Add in any "other" telemetry, if 3/4 or full duplex */
		if (myrpt->p.duplex > 0)
			totx = totx || othertelemqueued;
		/* Update external (to links) transmitter PTT state with everything but ID, CT, and tailmessage telemetry */
		myrpt->exttx = totx;
		/* If half or 3/4 duplex, add localtx to external link tx */
		if (myrpt->p.duplex < 2)
			myrpt->exttx = myrpt->exttx || myrpt->localtx;
		/* Add in ID telemetry to local transmitter */
		totx = totx || remrx;
		/* If 3/4 or full duplex, add in ident and CT telemetry */
		if (myrpt->p.duplex > 0)
			totx = totx || identqueued || ctqueued;
		/* Reset time out timer variables if there is no activity */
		if (!totx) {
			myrpt->totimer = myrpt->p.totime;
			myrpt->tounkeyed = 0;
			myrpt->tonotify = 0;
		} else
			myrpt->tailtimer = myrpt->p.hangtime; /* Initialize tail timer */
		/* Disable the local transmitter if we are timed out */
		totx = totx && myrpt->totimer;
		/* if timed-out and not said already, say it */
		if ((!myrpt->totimer) && (!myrpt->tonotify)) {
			myrpt->tonotify = 1;
			myrpt->timeouts++;
			rpt_mutex_unlock(&myrpt->lock);
			rpt_telemetry(myrpt, TIMEOUT, NULL);
			rpt_mutex_lock(&myrpt->lock);
		}

		/* If unkey and re-key, reset time out timer */
		if ((!totx) && (!myrpt->totimer) && (!myrpt->tounkeyed) && (!myrpt->keyed)) {
			myrpt->tounkeyed = 1;
		}
		if ((!totx) && (!myrpt->totimer) && myrpt->tounkeyed && myrpt->keyed) {
			myrpt->totimer = myrpt->p.totime;
			myrpt->tounkeyed = 0;
			myrpt->tonotify = 0;
			rpt_mutex_unlock(&myrpt->lock);
			continue;
		}
		/* if timed-out and in circuit busy after call */
		if ((!totx) && (!myrpt->totimer) && (myrpt->callmode == 4)) {
			myrpt->callmode = 0;
		}
		/* get rid of tail if timed out */
		if (!myrpt->totimer)
			myrpt->tailtimer = 0;
		/* if not timed-out, add in tail */
		if (myrpt->totimer)
			totx = totx || myrpt->tailtimer;
		/* If user or links key up or are keyed up over standard ID, switch to talkover ID, if one is defined */
		/* If tail message, kill the message if someone keys up over it */ 
		if ((myrpt->keyed || remrx) && ((identqueued && idtalkover) || (tailmessagequeued))) {
			int hasid = 0, hastalkover = 0;

			telem = myrpt->tele.next;
			while (telem != &myrpt->tele) {
				if (telem->mode == ID) {
					if (telem->chan)
						ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV); /* Whoosh! */
					hasid = 1;
				}
				if (telem->mode == TAILMSG) {
					if (telem->chan)
						ast_softhangup(telem->chan, AST_SOFTHANGUP_DEV); /* Whoosh! */
				}
				if (telem->mode == IDTALKOVER)
					hastalkover = 1;
				telem = telem->next;
			}
			rpt_mutex_unlock(&myrpt->lock);
			if (hasid && (!hastalkover))
				rpt_telemetry(myrpt, IDTALKOVER, NULL); /* Start Talkover ID */
			rpt_mutex_lock(&myrpt->lock);
		}
		/* Try to be polite */
		/* If the repeater has been inactive for longer than the ID time, do an initial ID in the tail*/
		/* If within 30 seconds of the time to ID, try do it in the tail */
		/* else if at ID time limit, do it right over the top of them */
		/* Lastly, if the repeater has been keyed, and the ID timer is expired, do a clean up ID */
		if (myrpt->mustid && (!myrpt->idtimer))
			queue_id(myrpt);

		if ((totx && (!myrpt->exttx) &&
			 (myrpt->idtimer <= myrpt->p.politeid) && myrpt->tailtimer)) {
			myrpt->tailid = 1;
		}

		/* If tail timer expires, then check for tail messages */

		if (myrpt->tailevent) {
			myrpt->tailevent = 0;
			if (myrpt->tailid) {
				totx = 1;
				queue_id(myrpt);
			}
			else if ((myrpt->p.tailmsg.msgs[0]) &&
				(myrpt->p.tailmessagetime) && (myrpt->tmsgtimer == 0)) {
					totx = 1;
					myrpt->tmsgtimer = myrpt->p.tailmessagetime;	
					rpt_mutex_unlock(&myrpt->lock);
					rpt_telemetry(myrpt, TAILMSG, NULL);
					rpt_mutex_lock(&myrpt->lock);
			}	
		}

		/* Main TX control */

		/* let telemetry transmit anyway (regardless of timeout) */
		if (myrpt->p.duplex > 0)
			totx = totx || (myrpt->tele.next != &myrpt->tele);
		if (totx && (!lasttx)) {
			lasttx = 1;
			myrpt->dailykeyups++;
			myrpt->totalkeyups++;
			rpt_mutex_unlock(&myrpt->lock);
			ast_indicate(myrpt->txchannel, AST_CONTROL_RADIO_KEY);
			rpt_mutex_lock(&myrpt->lock);
		}
		totx = totx && myrpt->enable;
		if ((!totx) && lasttx) {
			lasttx = 0;
			rpt_mutex_unlock(&myrpt->lock);
			ast_indicate(myrpt->txchannel, AST_CONTROL_RADIO_UNKEY);
			rpt_mutex_lock(&myrpt->lock);
		}
		time(&t);
		/* if DTMF timeout */
		if ((!myrpt->cmdnode[0]) && (myrpt->dtmfidx >= 0) && ((myrpt->dtmf_time + DTMF_TIMEOUT) < t)) {
			myrpt->dtmfidx = -1;
			myrpt->dtmfbuf[0] = 0;
		}			
		/* if remote DTMF timeout */
		if ((myrpt->rem_dtmfidx >= 0) && ((myrpt->rem_dtmf_time + DTMF_TIMEOUT) < t)) {
			myrpt->rem_dtmfidx = -1;
			myrpt->rem_dtmfbuf[0] = 0;
		}	

		/* Reconnect */
	
		l = myrpt->links.next;
		while (l != &myrpt->links) {
			if (l->killme) {
				/* remove from queue */
				remque((struct qelem *) l);
				if (!strcmp(myrpt->cmdnode, l->name))
					myrpt->cmdnode[0] = 0;
				rpt_mutex_unlock(&myrpt->lock);
				/* hang-up on call to device */
				if (l->chan)
					ast_hangup(l->chan);
				ast_hangup(l->pchan);
				ast_free(l);
				rpt_mutex_lock(&myrpt->lock);
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
		if (myrpt->txchannel != myrpt->rxchannel)
			cs[n++] = myrpt->txchannel;
		l = myrpt->links.next;
		while (l != &myrpt->links) {
			if ((!l->killme) && (!l->disctime) && l->chan) {
				cs[n++] = l->chan;
				cs[n++] = l->pchan;
			}
			l = l->next;
		}
		rpt_mutex_unlock(&myrpt->lock);
		ms = MSWAIT;
		who = ast_waitfor_n(cs, n, &ms);
		if (who == NULL)
			ms = 0;
		elap = MSWAIT - ms;
		rpt_mutex_lock(&myrpt->lock);
		l = myrpt->links.next;
		while (l != &myrpt->links) {
			if (!l->lasttx) {
				if ((l->retxtimer += elap) >= REDUNDANT_TX_TIME) {
					l->retxtimer = 0;
					if (l->chan)
						ast_indicate(l->chan, AST_CONTROL_RADIO_UNKEY);
				}
			} else
				l->retxtimer = 0;
			if (l->disctime) { /* Disconnect timer active on a channel ? */
				l->disctime -= elap;
				if (l->disctime <= 0) /* Disconnect timer expired on inbound channel ? */
					l->disctime = 0; /* Yep */
			}

			if (l->retrytimer) {
				l->retrytimer -= elap;
				if (l->retrytimer < 0)
					l->retrytimer = 0;
			}

			/* Tally connect time */
			l->connecttime += elap;

			/* ignore non-timing channels */
			if (l->elaptime < 0) {
				l = l->next;
				continue;
			}
			l->elaptime += elap;
			/* if connection has taken too long */
			if ((l->elaptime > MAXCONNECTTIME) && 
			   ((!l->chan) || (l->chan->_state != AST_STATE_UP))) {
				l->elaptime = 0;
				rpt_mutex_unlock(&myrpt->lock);
				if (l->chan)
					ast_softhangup(l->chan, AST_SOFTHANGUP_DEV);
				rpt_mutex_lock(&myrpt->lock);
				break;
			}
			if ((!l->chan) && (!l->retrytimer) && l->outbound && 
				(l->retries++ < MAX_RETRIES) && (l->hasconnected)) {
				if (l->chan)
					ast_hangup(l->chan);
				rpt_mutex_unlock(&myrpt->lock);
				if ((l->name[0] != '0') && (!l->isremote)) {
					l->retrytimer = MAX_RETRIES + 1;
				} else {
					if (attempt_reconnect(myrpt, l) == -1) {
						l->retrytimer = RETRY_TIMER_MS;
					}
				}
				rpt_mutex_lock(&myrpt->lock);
				break;
			}
			if ((!l->chan) && (!l->retrytimer) && l->outbound && (l->retries >= MAX_RETRIES)) {
				/* remove from queue */
				remque((struct qelem *) l);
				if (!strcmp(myrpt->cmdnode, l->name))
					myrpt->cmdnode[0] = 0;
				rpt_mutex_unlock(&myrpt->lock);
				if (l->name[0] != '0') {
					if (!l->hasconnected)
						rpt_telemetry(myrpt, CONNFAIL, l);
					else
						rpt_telemetry(myrpt, REMDISC, l);
				}
				/* hang-up on call to device */
				ast_hangup(l->pchan);
				ast_free(l);
				rpt_mutex_lock(&myrpt->lock);
				break;
			}
			if ((!l->chan) && (!l->disctime) && (!l->outbound)) {
				/* remove from queue */
				remque((struct qelem *) l);
				if (!strcmp(myrpt->cmdnode, l->name))
					myrpt->cmdnode[0] = 0;
				rpt_mutex_unlock(&myrpt->lock);
				if (l->name[0] != '0') {
					rpt_telemetry(myrpt, REMDISC, l);
				}
				/* hang-up on call to device */
				ast_hangup(l->pchan);
				ast_free(l);
				rpt_mutex_lock(&myrpt->lock);
				break;
			}
			l = l->next;
		}
		if (totx) {
			myrpt->dailytxtime += elap;
			myrpt->totaltxtime += elap;
		}
		i = myrpt->tailtimer;
		if (myrpt->tailtimer)
			myrpt->tailtimer -= elap;
		if (myrpt->tailtimer < 0)
			myrpt->tailtimer = 0;
		if ((i) && (myrpt->tailtimer == 0))
			myrpt->tailevent = 1;
		if (myrpt->totimer)
			myrpt->totimer -= elap;
		if (myrpt->totimer < 0)
			myrpt->totimer = 0;
		if (myrpt->idtimer)
			myrpt->idtimer -= elap;
		if (myrpt->idtimer < 0)
			myrpt->idtimer = 0;
		if (myrpt->tmsgtimer)
			myrpt->tmsgtimer -= elap;
		if (myrpt->tmsgtimer < 0)
			myrpt->tmsgtimer = 0;
		/* do macro timers */
		if (myrpt->macrotimer)
			myrpt->macrotimer -= elap;
		if (myrpt->macrotimer < 0)
			myrpt->macrotimer = 0;
		/* do gosub timers */
		if (myrpt->gosubtimer)
			myrpt->gosubtimer -= elap;
		if (myrpt->gosubtimer < 0)
			myrpt->gosubtimer = 0;
		/* Execute scheduler appx. every 2 tenths of a second */
		if (myrpt->skedtimer <= 0) {
			myrpt->skedtimer = 200;
			do_scheduler(myrpt);
		} else
			myrpt->skedtimer -= elap;
		if (!ms) {
			rpt_mutex_unlock(&myrpt->lock);
			continue;
		}
		c = myrpt->macrobuf[0];
		if (c && (!myrpt->macrotimer)) {
			myrpt->macrotimer = MACROTIME;
			memmove(myrpt->macrobuf, myrpt->macrobuf + 1, sizeof(myrpt->macrobuf) - 1);
			if ((c == 'p') || (c == 'P'))
				myrpt->macrotimer = MACROPTIME;
			rpt_mutex_unlock(&myrpt->lock);
			local_dtmf_helper(myrpt, c);
		} else
			rpt_mutex_unlock(&myrpt->lock);
		c = myrpt->gosubbuf[0];
		if (c && (!myrpt->gosubtimer)) {
			myrpt->gosubtimer = GOSUBTIME;
			memmove(myrpt->gosubbuf, myrpt->gosubbuf + 1, sizeof(myrpt->gosubbuf) - 1);
			if ((c == 'p') || (c == 'P'))
				myrpt->gosubtimer = GOSUBPTIME;
			rpt_mutex_unlock(&myrpt->lock);
			local_dtmf_helper(myrpt, c);
		} else
			rpt_mutex_unlock(&myrpt->lock);
		if (who == myrpt->rxchannel) { /* if it was a read from rx */
			f = ast_read(myrpt->rxchannel);
			if (!f) {
				ast_debug(1, "@@@@ rpt:Hung Up\n");
				break;
			}
			if (f->frametype == AST_FRAME_VOICE) {
#ifdef	_MDC_DECODE_H_
				unsigned char ubuf[2560];
				short *sp;
				int n;
#endif

				if (!myrpt->localtx) {
					memset(f->data, 0, f->datalen);
				}

#ifdef	_MDC_DECODE_H_
				sp = (short *) f->data;
				/* convert block to unsigned char */
				for (n = 0; n < f->datalen / 2; n++) {
					ubuf[n] = (*sp++ >> 8) + 128;
				}
				n = mdc_decoder_process_samples(myrpt->mdc, ubuf, f->datalen / 2);
				if (n == 1) {
					unsigned char op, arg;
					unsigned short unitID;

					mdc_decoder_get_packet(myrpt->mdc, &op, &arg, &unitID);
					if (debug > 2) {
						ast_log(LOG_NOTICE, "Got (single-length) packet:\n");
						ast_log(LOG_NOTICE, "op: %02x, arg: %02x, UnitID: %04x\n", op & 255, arg & 255, unitID);
					}
					if ((op == 1) && (arg == 0)) {
						myrpt->lastunit = unitID;
					}
				}
				if ((debug > 2) && (i == 2)) {
					unsigned char op, arg, ex1, ex2, ex3, ex4;
					unsigned short unitID;

					mdc_decoder_get_double_packet(myrpt->mdc, &op, &arg, &unitID, &ex1, &ex2, &ex3, &ex4);
					ast_log(LOG_NOTICE, "Got (double-length) packet:\n");
					ast_log(LOG_NOTICE, "op: %02x, arg: %02x, UnitID: %04x\n", op & 255, arg & 255, unitID);
					ast_log(LOG_NOTICE, "ex1: %02x, ex2: %02x, ex3: %02x, ex4: %02x\n",
						ex1 & 255, ex2 & 255, ex3 & 255, ex4 & 255);
				}
#endif
#ifdef	__RPT_NOTCH
				/* apply inbound filters, if any */
				rpt_filter(myrpt, f->data, f->datalen / 2);
#endif
				ast_write(myrpt->pchannel, f);
			} else if (f->frametype == AST_FRAME_DTMF) {
				c = (char) f->subclass; /* get DTMF char */
				ast_frfree(f);
				if (!myrpt->keyed)
					continue;
				local_dtmf_helper(myrpt, c);
				continue;
			} else if (f->frametype == AST_FRAME_CONTROL) {
				if (f->subclass == AST_CONTROL_HANGUP) {
					ast_debug(1, "@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
				/* if RX key */
				if (f->subclass == AST_CONTROL_RADIO_KEY) {
					if ((!lasttx) || (myrpt->p.duplex > 1)) {
						ast_debug(8, "@@@@ rx key\n");
						myrpt->keyed = 1;
					}
				}
				/* if RX un-key */
				if (f->subclass == AST_CONTROL_RADIO_UNKEY) {
					if ((!lasttx) || (myrpt->p.duplex > 1)) {
						ast_debug(8, "@@@@ rx un-key\n");
						if (myrpt->keyed) {
							rpt_telemetry(myrpt, UNKEY, NULL);
						}
						myrpt->keyed = 0;
					}
				}
			}
			ast_frfree(f);
			continue;
		}
		if (who == myrpt->pchannel) { /* if it was a read from pseudo */
			f = ast_read(myrpt->pchannel);
			if (!f) {
				ast_debug(1, "@@@@ rpt:Hung Up\n");
				break;
			}
			if (f->frametype == AST_FRAME_VOICE) {
				ast_write(myrpt->txpchannel, f);
			}
			if (f->frametype == AST_FRAME_CONTROL) {
				if (f->subclass == AST_CONTROL_HANGUP) {
					ast_debug(1, "@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
			}
			ast_frfree(f);
			continue;
		}
		if (who == myrpt->txchannel) { /* if it was a read from tx */
			f = ast_read(myrpt->txchannel);
			if (!f) {
				ast_debug(1, "@@@@ rpt:Hung Up\n");
				break;
			}
			if (f->frametype == AST_FRAME_CONTROL) {
				if (f->subclass == AST_CONTROL_HANGUP) {
					ast_debug(1, "@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
			}
			ast_frfree(f);
			continue;
		}
		toexit = 0;
		rpt_mutex_lock(&myrpt->lock);
		l = myrpt->links.next;
		while (l != &myrpt->links) {
			if (l->disctime) {
				l = l->next;
				continue;
			}
			if (who == l->chan) { /* if it was a read from rx */
				remrx = 0;
				/* see if any other links are receiving */
				m = myrpt->links.next;
				while (m != &myrpt->links) {
					/* if not us, count it */
					if ((m != l) && (m->lastrx))
						remrx = 1;
					m = m->next;
				}
				rpt_mutex_unlock(&myrpt->lock);
				totx = (((l->isremote) ? myrpt->localtx : 
					myrpt->exttx) || remrx) && l->mode;
				if (l->chan && (l->lasttx != totx)) {
					if (totx) {
						ast_indicate(l->chan, AST_CONTROL_RADIO_KEY);
					} else {
						ast_indicate(l->chan, AST_CONTROL_RADIO_UNKEY);
					}
				}
				l->lasttx = totx;
				f = ast_read(l->chan);
				if (!f) {
					if ((!l->disced) && (!l->outbound)) {
						if ((l->name[0] == '0') || l->isremote)
							l->disctime = 1;
						else
							l->disctime = DISC_TIME;
						rpt_mutex_lock(&myrpt->lock);
						ast_hangup(l->chan);
						l->chan = 0;
						break;
					}

					if (l->retrytimer) {
						rpt_mutex_lock(&myrpt->lock);
						break; 
					}
					if (l->outbound && (l->retries++ < MAX_RETRIES) && (l->hasconnected)) {
						rpt_mutex_lock(&myrpt->lock);
						ast_hangup(l->chan);
						l->chan = 0;
						rpt_mutex_unlock(&myrpt->lock);
						if (attempt_reconnect(myrpt, l) == -1) {
							l->retrytimer = RETRY_TIMER_MS;
						}
						rpt_mutex_lock(&myrpt->lock);
						break;
					}
					rpt_mutex_lock(&myrpt->lock);
					/* remove from queue */
					remque((struct qelem *) l);
					if (!strcmp(myrpt->cmdnode, l->name))
						myrpt->cmdnode[0] = 0;
					rpt_mutex_unlock(&myrpt->lock);
					if (!l->hasconnected)
						rpt_telemetry(myrpt, CONNFAIL, l);
					else if (l->disced != 2)
						rpt_telemetry(myrpt, REMDISC, l);
					/* hang-up on call to device */
					ast_hangup(l->chan);
					ast_hangup(l->pchan);
					ast_free(l);
					rpt_mutex_lock(&myrpt->lock);
					break;
				}
				if (f->frametype == AST_FRAME_VOICE) {
					if (!l->lastrx) {
						memset(f->data, 0, f->datalen);
					}
					ast_write(l->pchan, f);
				}
				if (f->frametype == AST_FRAME_TEXT) {
					handle_link_data(myrpt, l, f->data);
				}
				if (f->frametype == AST_FRAME_DTMF) {
					handle_link_phone_dtmf(myrpt, l, f->subclass);
				}
				if (f->frametype == AST_FRAME_CONTROL) {
					if (f->subclass == AST_CONTROL_ANSWER) {
						char lconnected = l->connected;
						l->connected = 1;
						l->hasconnected = 1;
						l->elaptime = -1;
						l->retries = 0;
						if (!lconnected) 
							rpt_telemetry(myrpt, CONNECTED, l);
						else
							l->reconnects++;
					}
					/* if RX key */
					if (f->subclass == AST_CONTROL_RADIO_KEY) {
						ast_debug(8, "@@@@ rx key\n");
						l->lastrx = 1;
					}
					/* if RX un-key */
					if (f->subclass == AST_CONTROL_RADIO_UNKEY) {
						ast_debug(8, "@@@@ rx un-key\n");
						l->lastrx = 0;
					}
					if (f->subclass == AST_CONTROL_HANGUP) {
						ast_frfree(f);
						if ((!l->outbound) && (!l->disced)) {
							if ((l->name[0] == '0') || l->isremote)
								l->disctime = 1;
							else
								l->disctime = DISC_TIME;
							rpt_mutex_lock(&myrpt->lock);
							ast_hangup(l->chan);
							l->chan = 0;
							break;
						}
						if (l->retrytimer) {
							rpt_mutex_lock(&myrpt->lock);
							break;
						}
						if (l->outbound && (l->retries++ < MAX_RETRIES) && (l->hasconnected)) {
							rpt_mutex_lock(&myrpt->lock);
							ast_hangup(l->chan);
							l->chan = 0;
							rpt_mutex_unlock(&myrpt->lock);
							if (attempt_reconnect(myrpt, l) == -1) {
								l->retrytimer = RETRY_TIMER_MS;
							}
							rpt_mutex_lock(&myrpt->lock);
							break;
						}
						rpt_mutex_lock(&myrpt->lock);
						/* remove from queue */
						remque((struct qelem *) l);
						if (!strcmp(myrpt->cmdnode, l->name))
							myrpt->cmdnode[0] = 0;
						rpt_mutex_unlock(&myrpt->lock);
						if (!l->hasconnected)
							rpt_telemetry(myrpt, CONNFAIL, l);
						else if (l->disced != 2)
							rpt_telemetry(myrpt, REMDISC, l);
						/* hang-up on call to device */
						ast_hangup(l->chan);
						ast_hangup(l->pchan);
						ast_free(l);
						rpt_mutex_lock(&myrpt->lock);
						break;
					}
				}
				ast_frfree(f);
				rpt_mutex_lock(&myrpt->lock);
				break;
			}
			if (who == l->pchan) {
				rpt_mutex_unlock(&myrpt->lock);
				f = ast_read(l->pchan);
				if (!f) {
					ast_debug(1, "@@@@ rpt:Hung Up\n");
					toexit = 1;
					rpt_mutex_lock(&myrpt->lock);
					break;
				}
				if (f->frametype == AST_FRAME_VOICE) {
					if (l->chan)
						ast_write(l->chan, f);
				}
				if (f->frametype == AST_FRAME_CONTROL) {
					if (f->subclass == AST_CONTROL_HANGUP) {
						ast_debug(1, "@@@@ rpt:Hung Up\n");
						ast_frfree(f);
						toexit = 1;
						rpt_mutex_lock(&myrpt->lock);
						break;
					}
				}
				ast_frfree(f);
				rpt_mutex_lock(&myrpt->lock);
				break;
			}
			l = l->next;
		}
		rpt_mutex_unlock(&myrpt->lock);
		if (toexit)
			break;
		if (who == myrpt->txpchannel) { /* if it was a read from remote tx */
			f = ast_read(myrpt->txpchannel);
			if (!f) {
				ast_debug(1, "@@@@ rpt:Hung Up\n");
				break;
			}
			if (f->frametype == AST_FRAME_CONTROL) {
				if (f->subclass == AST_CONTROL_HANGUP) {
					ast_debug(1, "@@@@ rpt:Hung Up\n");
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
	if (myrpt->txchannel != myrpt->rxchannel)
		ast_hangup(myrpt->txchannel);
	ast_hangup(myrpt->rxchannel);
	rpt_mutex_lock(&myrpt->lock);
	l = myrpt->links.next;
	while (l != &myrpt->links) {
		struct rpt_link *ll = l;
		/* remove from queue */
		remque((struct qelem *) l);
		/* hang-up on call to device */
		if (l->chan)
			ast_hangup(l->chan);
		ast_hangup(l->pchan);
		l = l->next;
		ast_free(ll);
	}
	rpt_mutex_unlock(&myrpt->lock);
	ast_debug(1, "@@@@ rpt:Hung up channel\n");
	myrpt->rpt_thread = AST_PTHREADT_STOP;
	pthread_exit(NULL); 
	return NULL;
}

	
static void *rpt_master(void *config)
{
	int	i, n;
	struct ast_config *cfg;
	char *this;
	const char *val;

	/* go thru all the specified repeaters */
	this = NULL;
	n = 0;
	rpt_vars[n].cfg = config;
	cfg = rpt_vars[n].cfg;
	if (!cfg) {
		ast_log(LOG_NOTICE, "Unable to open radio repeater configuration rpt.conf.  Radio Repeater disabled.\n");
		pthread_exit(NULL);
	}
	while ((this = ast_category_browse(cfg, this)) != NULL) {
		for (i = 0; i < strlen(this); i++) {
			if ((this[i] < '0') || (this[i] > '9'))
				break;
		}
		if (i != strlen(this))
			continue; /* Not a node defn */
		memset(&rpt_vars[n], 0, sizeof(rpt_vars[n]));
		rpt_vars[n].name = ast_strdup(this);
		val = ast_variable_retrieve(cfg, this, "rxchannel");
		if (val)
			rpt_vars[n].rxchanname = ast_strdup(val);
		val = ast_variable_retrieve(cfg, this, "txchannel");
		if (val)
			rpt_vars[n].txchanname = ast_strdup(val);
		val = ast_variable_retrieve(cfg, this, "remote");
		if (val)
			rpt_vars[n].remote = ast_strdup(val);
		ast_mutex_init(&rpt_vars[n].lock);
		rpt_vars[n].tele.next = &rpt_vars[n].tele;
		rpt_vars[n].tele.prev = &rpt_vars[n].tele;
		rpt_vars[n].rpt_thread = AST_PTHREADT_NULL;
		rpt_vars[n].tailmessagen = 0;
#ifdef	_MDC_DECODE_H_
		rpt_vars[n].mdc = mdc_decoder_new(8000);
#endif
		n++;
	}
	nrpts = n;
	ast_config_destroy(cfg);

	/* start em all */
	for (i = 0; i < n; i++) {
		load_rpt_vars(i, 1);

		/* if is a remote, dont start one for it */
		if (rpt_vars[i].remote) {
			ast_copy_string(rpt_vars[i].freq, "146.580", sizeof(rpt_vars[i].freq));
			ast_copy_string(rpt_vars[i].rxpl, "100.0", sizeof(rpt_vars[i].rxpl));
			ast_copy_string(rpt_vars[i].txpl, "100.0", sizeof(rpt_vars[i].txpl));
			rpt_vars[i].remmode = REM_MODE_FM;
			rpt_vars[i].offset = REM_SIMPLEX;
			rpt_vars[i].powerlevel = REM_MEDPWR;
			continue;
		}
		if (!rpt_vars[i].p.ident) {
			ast_log(LOG_WARNING, "Did not specify ident for node %s\n", rpt_vars[i].name);
			ast_config_destroy(cfg);
			pthread_exit(NULL);
		}
		ast_pthread_create_detached(&rpt_vars[i].rpt_thread, NULL, rpt, (void *) &rpt_vars[i]);
	}
	usleep(500000);
	for (;;) {
		/* Now monitor each thread, and restart it if necessary */
		for (i = 0; i < n; i++) { 
			int rv;
			if (rpt_vars[i].remote)
				continue;
			if (rpt_vars[i].rpt_thread == AST_PTHREADT_STOP) 
				rv = -1;
			else
				rv = pthread_kill(rpt_vars[i].rpt_thread, 0);
			if (rv) {
				if (time(NULL) - rpt_vars[i].lastthreadrestarttime <= 15) {
					if (rpt_vars[i].threadrestarts >= 5) {
						ast_log(LOG_ERROR, "Continual RPT thread restarts, killing Asterisk\n");
						ast_cli_command(STDERR_FILENO, "restart now");
					} else {
						ast_log(LOG_NOTICE, "RPT thread restarted on %s\n", rpt_vars[i].name);
						rpt_vars[i].threadrestarts++;
					}
				} else
					rpt_vars[i].threadrestarts = 0;

				rpt_vars[i].lastthreadrestarttime = time(NULL);
				ast_pthread_create_detached(&rpt_vars[i].rpt_thread, NULL, rpt, (void *) &rpt_vars[i]);
				ast_log(LOG_WARNING, "rpt_thread restarted on node %s\n", rpt_vars[i].name);
			}

		}
		sleep(2);
	}
	ast_config_destroy(cfg);
	pthread_exit(NULL);
}

static int rpt_exec(struct ast_channel *chan, void *data)
{
	int res = -1, i, rem_totx, n, phone_mode = 0;
	char *tmp, keyed = 0;
	char *options = NULL, *tele, c;
	struct rpt *myrpt;
	struct ast_frame *f;
	struct ast_channel *who;
	struct ast_channel *cs[20];
	struct rpt_link *l;
	struct dahdi_confinfo ci;  /* conference info */
	struct dahdi_params par;
	int ms, elap;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(node);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Rpt requires an argument (system node)\n");
		return -1;
	}
	tmp = ast_strdupa((char *)data);
	AST_STANDARD_APP_ARGS(args, tmp);
	myrpt = NULL;
	/* see if we can find our specified one */
	for (i = 0; i < nrpts; i++) {
		/* if name matches, assign it and exit loop */
		if (!strcmp(args.node, rpt_vars[i].name)) {
			myrpt = &rpt_vars[i];
			break;
		}
	}
	if (myrpt == NULL) {
		ast_log(LOG_WARNING, "Cannot find specified system node %s\n", args.node);
		return -1;
	}

	/* if not phone access, must be an IAX connection */
	if (options && ((*options == 'P') || (*options == 'D') || (*options == 'R'))) {
		phone_mode = 1;
		if (*options == 'D')
			phone_mode = 2;
		ast_set_callerid(chan, "0", "app_rpt user", "0");
	} else {
		if (strncmp(chan->name, "IAX2", 4)) {
			ast_log(LOG_WARNING, "We only accept links via IAX2!!\n");
			return -1;
		}
	}
	if (*args.options == 'R') {
		/* Parts of this section taken from app_parkandannounce */
		int m, lot, timeout = 0;
		char tmp[256];
		char *s;
		AST_DECLARE_APP_ARGS(optionarg,
			AST_APP_ARG(template);
			AST_APP_ARG(timeout);
			AST_APP_ARG(return_context);
		);

		rpt_mutex_lock(&myrpt->lock);
		m = myrpt->callmode;
		rpt_mutex_unlock(&myrpt->lock);

		if ((!myrpt->p.nobusyout) && m) {
			if (chan->_state != AST_STATE_UP) {
				ast_indicate(chan, AST_CONTROL_BUSY);
			}
			while (ast_safe_sleep(chan, 10000) != -1) {
				/* This used to be a busy loop.  It's probably better to yield the processor here. */
				usleep(1);
			}
			return -1;
		}

		if (chan->_state != AST_STATE_UP) {
			ast_answer(chan);
		}

		s = ast_strdupa(options);
		AST_STANDARD_APP_ARGS(optionarg, s);
		if (optionarg.argc == 0 || ast_strlen_zero(optionarg.template)) {
			ast_log(LOG_WARNING, "An announce template must be defined\n");
			return -1;
		} 
  
		if (optionarg.argc >= 2) {
			timeout = atoi(optionarg.timeout) * 1000;
		}
	
		if (!ast_strlen_zero(optionarg.return_context)) {
			if (ast_parseable_goto(chan, optionarg.return_context)) {
				ast_verb(3, "Warning: Return Context Invalid, call will return to default|s\n");
			}
		}

		/* we are using masq_park here to protect * from touching the channel once we park it.  If the channel comes out of timeout
		before we are done announcing and the channel is messed with, Kablooeee.  So we use Masq to prevent this.  */

		ast_masq_park_call(chan, NULL, timeout, &lot);
		ast_verb(3, "Call Parking Called, lot: %d, timeout: %d, context: %s\n", lot, timeout, optionarg.return_context);

		snprintf(tmp, sizeof(tmp), "%d,%s", lot, optionarg.template + 1);
		rpt_telemetry(myrpt, REV_PATCH, tmp);

		return 0;
	}

	if (!options) {
		struct ast_hostent ahp;
		struct hostent *hp;
		struct in_addr ia;
		char hisip[100], nodeip[100];
		const char *val;
		char *s, *s1, *s2;

		/* look at callerid to see what node this comes from */
		if (!chan->cid.cid_num) { /* if doesn't have caller id */
			ast_log(LOG_WARNING, "Doesn't have callerid on %s\n", args.node);
			return -1;
		}

		/* get his IP from IAX2 module */
		pbx_substitute_variables_helper(chan, "${IAXPEER(CURRENTCHANNEL)}", hisip, sizeof(hisip) - 1);
		if (ast_strlen_zero(hisip)) {
			ast_log(LOG_WARNING, "Link IP address cannot be determined!!\n");
			return -1;
		}
		
		if (!strcmp(myrpt->name, chan->cid.cid_num)) {
			ast_log(LOG_WARNING, "Trying to link to self!!\n");
			return -1;
		}

		if (*(chan->cid.cid_num) < '1') {
			ast_log(LOG_WARNING, "Node %s Invalid for connection here!!\n", chan->cid.cid_num);
			return -1;
		}

		/* look for his reported node string */
		val = ast_variable_retrieve(myrpt->cfg, myrpt->p.nodes, chan->cid.cid_num);
		if (!val) {
			ast_log(LOG_WARNING, "Reported node %s cannot be found!!\n", chan->cid.cid_num);
			return -1;
		}
		ast_copy_string(tmp, val, sizeof(tmp));
		s = tmp;
		s1 = strsep(&s, ",");
		s2 = strsep(&s, ",");
		if (!s2) {
			ast_log(LOG_WARNING, "Reported node %s not in correct format!!\n", chan->cid.cid_num);
			return -1;
		}
		if (strcmp(s2, "NONE")) {
			hp = ast_gethostbyname(s2, &ahp);
			if (!hp) {
				ast_log(LOG_WARNING, "Reported node %s, name %s cannot be found!!\n", chan->cid.cid_num, s2);
				return -1;
			}
			memcpy(&ia, hp->h_addr, sizeof(in_addr_t));
			ast_copy_string(nodeip, ast_inet_ntoa(ia), sizeof(nodeip));
			if (strcmp(hisip, nodeip)) {
				char *s3 = strchr(s1, '@');
				if (s3)
					s1 = s3 + 1;
				s3 = strchr(s1, '/');
				if (s3)
					*s3 = 0;
				hp = ast_gethostbyname(s1, &ahp);
				if (!hp) {
					ast_log(LOG_WARNING, "Reported node %s, name %s cannot be found!!\n", chan->cid.cid_num, s1);
					return -1;
				}
				memcpy(&ia, hp->h_addr, sizeof(in_addr_t));
				ast_copy_string(nodeip, ast_inet_ntoa(ia), sizeof(nodeip));
				if (strcmp(hisip, nodeip)) {
					ast_log(LOG_WARNING, "Node %s IP %s does not match link IP %s!!\n", chan->cid.cid_num, nodeip, hisip);
					return -1;
				}
			}
		}
	}

	/* if is not a remote */
	if (!myrpt->remote) {
		int reconnects = 0;

		/* look at callerid to see what node this comes from */
		if (!chan->cid.cid_num) { /* if doesn't have caller id */
			ast_log(LOG_WARNING, "Doesnt have callerid on %s\n", args.node);
			return -1;
		}

		if (!strcmp(myrpt->name, chan->cid.cid_num)) {
			ast_log(LOG_WARNING, "Trying to link to self!!\n");
			return -1;
		}
		rpt_mutex_lock(&myrpt->lock);
		l = myrpt->links.next;
		/* try to find this one in queue */
		while (l != &myrpt->links) {
			if (l->name[0] == '0') {
				l = l->next;
				continue;
			}
			/* if found matching string */
			if (!strcmp(l->name, chan->cid.cid_num))
				break;
			l = l->next;
		}
		/* if found */
		if (l != &myrpt->links) {
			l->killme = 1;
			l->retries = MAX_RETRIES + 1;
			l->disced = 2;
			reconnects = l->reconnects;
			reconnects++;
			rpt_mutex_unlock(&myrpt->lock);
			usleep(500000);	
		} else 
			rpt_mutex_unlock(&myrpt->lock);
		/* establish call in tranceive mode */
		l = ast_calloc(1, sizeof(*l));
		if (!l) {
			ast_log(LOG_WARNING, "Unable to malloc\n");
			pthread_exit(NULL);
		}
		l->mode = 1;
		ast_copy_string(l->name, chan->cid.cid_num, sizeof(l->name));
		l->isremote = 0;
		l->chan = chan;
		l->connected = 1;
		l->hasconnected = 1;
		l->reconnects = reconnects;
		l->phonemode = phone_mode;
		ast_set_read_format(l->chan, AST_FORMAT_SLINEAR);
		ast_set_write_format(l->chan, AST_FORMAT_SLINEAR);
		/* allocate a pseudo-channel thru asterisk */
		l->pchan = ast_request("zap", AST_FORMAT_SLINEAR, "pseudo", NULL);
		if (!l->pchan) {
			ast_log(LOG_ERROR, "rpt:Sorry unable to obtain pseudo channel\n");
			pthread_exit(NULL);
		}
		ast_set_read_format(l->pchan, AST_FORMAT_SLINEAR);
		ast_set_write_format(l->pchan, AST_FORMAT_SLINEAR);
		/* make a conference for the tx */
		ci.chan = 0;
		ci.confno = myrpt->conf;
		ci.confmode = DAHDI_CONF_CONF | DAHDI_CONF_LISTENER | DAHDI_CONF_TALKER;
		/* first put the channel on the conference in proper mode */
		if (ioctl(l->pchan->fds[0], DAHDI_SETCONF, &ci) == -1) {
			ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
			pthread_exit(NULL);
		}
		rpt_mutex_lock(&myrpt->lock);
		if (phone_mode > 1)
			l->lastrx = 1;
		/* insert at end of queue */
		insque((struct qelem *)l, (struct qelem *)myrpt->links.next);
		rpt_mutex_unlock(&myrpt->lock);
		if (chan->_state != AST_STATE_UP) {
			ast_answer(chan);
		}
		return AST_PBX_KEEPALIVE;
	}
	rpt_mutex_lock(&myrpt->lock);
	/* if remote, error if anyone else already linked */
	if (myrpt->remoteon) {
		rpt_mutex_unlock(&myrpt->lock);
		usleep(500000);
		if (myrpt->remoteon) {
			ast_log(LOG_WARNING, "Trying to use busy link on %s\n", args.node);
			return -1;
		}		
		rpt_mutex_lock(&myrpt->lock);
	}
	myrpt->remoteon = 1;
	if (ioperm(myrpt->p.iobase, 1, 1) == -1) {
		rpt_mutex_unlock(&myrpt->lock);
		ast_log(LOG_WARNING, "Cant get io permission on IO port %x hex\n", myrpt->p.iobase);
		return -1;
	}
	rpt_mutex_unlock(&myrpt->lock);
	/* find our index, and load the vars initially */
	for (i = 0; i < nrpts; i++) {
		if (&rpt_vars[i] == myrpt) {
			load_rpt_vars(i, 0);
			break;
		}
	}
	rpt_mutex_lock(&myrpt->lock);
	tele = strchr(myrpt->rxchanname, '/');
	if (!tele) {
		ast_log(LOG_ERROR, "rpt:Dial number must be in format tech/number\n");
		rpt_mutex_unlock(&myrpt->lock);
		pthread_exit(NULL);
	}
	*tele++ = 0;
	myrpt->rxchannel = ast_request(myrpt->rxchanname, AST_FORMAT_SLINEAR, tele, NULL);
	if (myrpt->rxchannel) {
		ast_set_read_format(myrpt->rxchannel, AST_FORMAT_SLINEAR);
		ast_set_write_format(myrpt->rxchannel, AST_FORMAT_SLINEAR);
		myrpt->rxchannel->whentohangup = 0;
		myrpt->rxchannel->appl = "Apprpt";
		myrpt->rxchannel->data = "(Link Rx)";
		ast_verb(3, "rpt (Rx) initiating call to %s/%s on %s\n",
				myrpt->rxchanname, tele, myrpt->rxchannel->name);
		rpt_mutex_unlock(&myrpt->lock);
		ast_call(myrpt->rxchannel, tele, 999);
		rpt_mutex_lock(&myrpt->lock);
	} else {
		ast_log(LOG_ERROR, "rpt:Sorry unable to obtain Rx channel\n");
		rpt_mutex_unlock(&myrpt->lock);
		pthread_exit(NULL);
	}
	*--tele = '/';
	if (myrpt->txchanname) {
		tele = strchr(myrpt->txchanname, '/');
		if (!tele) {
			ast_log(LOG_ERROR, "rpt:Dial number must be in format tech/number\n");
			rpt_mutex_unlock(&myrpt->lock);
			ast_hangup(myrpt->rxchannel);
			pthread_exit(NULL);
		}
		*tele++ = 0;
		myrpt->txchannel = ast_request(myrpt->txchanname, AST_FORMAT_SLINEAR, tele, NULL);
		if (myrpt->txchannel) {
			ast_set_read_format(myrpt->txchannel, AST_FORMAT_SLINEAR);
			ast_set_write_format(myrpt->txchannel, AST_FORMAT_SLINEAR);
			myrpt->txchannel->whentohangup = 0;
			myrpt->txchannel->appl = "Apprpt";
			myrpt->txchannel->data = "(Link Tx)";
			ast_verb(3, "rpt (Tx) initiating call to %s/%s on %s\n",
					myrpt->txchanname, tele, myrpt->txchannel->name);
			rpt_mutex_unlock(&myrpt->lock);
			ast_call(myrpt->txchannel, tele, 999);
			rpt_mutex_lock(&myrpt->lock);
		} else {
			ast_log(LOG_ERROR, "rpt:Sorry unable to obtain Tx channel\n");
			rpt_mutex_unlock(&myrpt->lock);
			ast_hangup(myrpt->rxchannel);
			pthread_exit(NULL);
		}
		*--tele = '/';
	} else {
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
	if (myrpt->p.startupmacro) {
		myrpt->remchannel = chan; /* Save copy of channel */
		snprintf(myrpt->macrobuf, sizeof(myrpt->macrobuf), "PPPP%s", myrpt->p.startupmacro);
	}
	if (myrpt->p.startupgosub) {
		myrpt->remchannel = chan; /* Save copy of channel */
		snprintf(myrpt->gosubbuf, sizeof(myrpt->gosubbuf), "PPPP%s", myrpt->p.startupgosub);
	}
	myrpt->reload = 0;
	rpt_mutex_unlock(&myrpt->lock);
	setrem(myrpt); 
	ast_set_write_format(chan, AST_FORMAT_SLINEAR);
	ast_set_read_format(chan, AST_FORMAT_SLINEAR);
	/* if we are on 2w loop and are a remote, turn EC on */
	if (myrpt->remote && (myrpt->rxchannel == myrpt->txchannel)) {
		i = 128;
		ioctl(myrpt->rxchannel->fds[0], DAHDI_ECHOCANCEL, &i);
	}
	if (chan->_state != AST_STATE_UP) {
		ast_answer(chan);
	}

	if (ioctl(myrpt->txchannel->fds[0], DAHDI_GET_PARAMS, &par) != -1) {
		if (par.rxisoffhook) {
			ast_indicate(chan, AST_CONTROL_RADIO_KEY);
			myrpt->remoterx = 1;
		}
	}
	n = 0;
	cs[n++] = chan;
	cs[n++] = myrpt->rxchannel;
	if (myrpt->rxchannel != myrpt->txchannel)
		cs[n++] = myrpt->txchannel;
	for (;;) {
		if (ast_check_hangup(chan))
			break;
		if (ast_check_hangup(myrpt->rxchannel))
			break;
		if (myrpt->reload) {
			myrpt->reload = 0;
			rpt_mutex_unlock(&myrpt->lock);
			/* find our index, and load the vars */
			for (i = 0; i < nrpts; i++) {
				if (&rpt_vars[i] == myrpt) {
					load_rpt_vars(i, 0);
					break;
				}
			}
			rpt_mutex_lock(&myrpt->lock);
		}
		ms = MSWAIT;
		who = ast_waitfor_n(cs, n, &ms);
		if (who == NULL)
			ms = 0;
		elap = MSWAIT - ms;
		if (myrpt->macrotimer)
			myrpt->macrotimer -= elap;
		if (myrpt->macrotimer < 0)
			myrpt->macrotimer = 0;
		if (myrpt->gosubtimer)
			myrpt->gosubtimer -= elap;
		if (myrpt->gosubtimer < 0)
			myrpt->gosubtimer = 0;
		rpt_mutex_unlock(&myrpt->lock);
		if (!ms)
			continue;
		rem_totx = keyed;
		
		
		if ((!myrpt->remoterx) && (!myrpt->remotetx)) {
			if ((myrpt->retxtimer += elap) >= REDUNDANT_TX_TIME) {
				myrpt->retxtimer = 0;
				ast_indicate(chan, AST_CONTROL_RADIO_UNKEY);
			}
		} else
			myrpt->retxtimer = 0;
		if (rem_totx && (!myrpt->remotetx)) { /* Remote base radio TX key */
			myrpt->remotetx = 1;
			ast_indicate(myrpt->txchannel, AST_CONTROL_RADIO_KEY);
		}
		if ((!rem_totx) && myrpt->remotetx) { /* Remote base radio TX unkey */
			myrpt->remotetx = 0;
			ast_indicate(myrpt->txchannel, AST_CONTROL_RADIO_UNKEY);
		}

		if (myrpt->tunerequest && (!strcmp(myrpt->remote, remote_rig_ft897))) { /* ft-897 specific for now... */
			myrpt->tunerequest = 0;
			set_mode_ft897(myrpt, REM_MODE_AM);
			simple_command_ft897(myrpt, 8);
			myrpt->remotetx = 0;
			ast_indicate(myrpt->txchannel, AST_CONTROL_RADIO_UNKEY);
			if (!myrpt->remoterx)
				ast_indicate(chan, AST_CONTROL_RADIO_KEY);
			if (play_tone(chan, 800, 6000, 8192) == -1)
				break;

			rmt_telem_finish(myrpt, chan);
			set_mode_ft897(myrpt, 0x88);
			setrem(myrpt);
		}
	
		if (myrpt->hfscanmode) {
			myrpt->scantimer -= elap;
			if (myrpt->scantimer <= 0) {
				myrpt->scantimer = REM_SCANTIME;
				service_scan(myrpt);
			}
		}
		if (who == chan) { /* if it was a read from incoming */
			f = ast_read(chan);
			if (!f) {
				ast_debug(1, "@@@@ link:Hung Up\n");
				break;
			}
			if (f->frametype == AST_FRAME_VOICE) {
				/* if not transmitting, zero-out audio */
				if (!myrpt->remotetx)
					memset(f->data, 0, f->datalen);
				ast_write(myrpt->txchannel, f);
			}
			if (f->frametype == AST_FRAME_DTMF) {
				myrpt->remchannel = chan; /* Save copy of channel */
				if (handle_remote_phone_dtmf(myrpt, f->subclass, &keyed, phone_mode) == -1) {
					ast_debug(1, "@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
			}
			if (f->frametype == AST_FRAME_TEXT) {
				myrpt->remchannel = chan; /* Save copy of channel */
				if (handle_remote_data(myrpt, f->data) == -1) {
					ast_debug(1, "@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
			}
			if (f->frametype == AST_FRAME_CONTROL) {
				if (f->subclass == AST_CONTROL_HANGUP) {
					ast_debug(1, "@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
				/* if RX key */
				if (f->subclass == AST_CONTROL_RADIO_KEY) {
					ast_debug(8, "@@@@ rx key\n");
					keyed = 1;
				}
				/* if RX un-key */
				if (f->subclass == AST_CONTROL_RADIO_UNKEY) {
					ast_debug(8, "@@@@ rx un-key\n");
					keyed = 0;
				}
			}
			if (myrpt->hfscanstatus) {
				myrpt->remchannel = chan; /* Save copy of channel */
				myrpt->remotetx = 0;
				ast_indicate(myrpt->txchannel, AST_CONTROL_RADIO_UNKEY);
				if (!myrpt->remoterx) {
					ast_indicate(myrpt->remchannel, AST_CONTROL_RADIO_KEY);
				}
				if (myrpt->hfscanstatus < 0) {
					if (myrpt->hfscanstatus == -1) {
						if (ast_safe_sleep(myrpt->remchannel, 1000) == -1)
							break;
					}
					sayfile(myrpt->remchannel, "rpt/stop");
				} else {
					saynum(myrpt->remchannel, myrpt->hfscanstatus );
				}	
				rmt_telem_finish(myrpt, myrpt->remchannel);
				myrpt->hfscanstatus = 0;
			}
			ast_frfree(f);
			rpt_mutex_lock(&myrpt->lock);
			c = myrpt->macrobuf[0];
			if (c && (!myrpt->macrotimer)) {
				myrpt->macrotimer = MACROTIME;
				memmove(myrpt->macrobuf, myrpt->macrobuf + 1, sizeof(myrpt->macrobuf) - 1);
				if ((c == 'p') || (c == 'P'))
					myrpt->macrotimer = MACROPTIME;
				rpt_mutex_unlock(&myrpt->lock);
				if (handle_remote_dtmf_digit(myrpt, c, &keyed, 0) == -1)
					break;
				continue;
			}
			c = myrpt->gosubbuf[0];
			if (c && (!myrpt->gosubtimer)) {
				myrpt->gosubtimer = GOSUBTIME;
				memmove(myrpt->gosubbuf, myrpt->gosubbuf + 1, sizeof(myrpt->gosubbuf) - 1);
				if ((c == 'p') || (c == 'P'))
					myrpt->gosubtimer = GOSUBPTIME;
				rpt_mutex_unlock(&myrpt->lock);
				if (handle_remote_dtmf_digit(myrpt, c, &keyed, 0) == -1)
					break;
				continue;
			} 
			rpt_mutex_unlock(&myrpt->lock);
			continue;
		}
		if (who == myrpt->rxchannel) { /* if it was a read from radio */
			f = ast_read(myrpt->rxchannel);
			if (!f) {
				ast_debug(1, "@@@@ link:Hung Up\n");
				break;
			}
			if (f->frametype == AST_FRAME_VOICE) {
				if ((myrpt->remote) && (myrpt->remotetx))
					memset(f->data, 0, f->datalen);
				 ast_write(chan, f);
			} else if (f->frametype == AST_FRAME_CONTROL) {
				if (f->subclass == AST_CONTROL_HANGUP) {
					ast_debug(1, "@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
				/* if RX key */
				if (f->subclass == AST_CONTROL_RADIO_KEY) {
					ast_debug(8, "@@@@ remote rx key\n");
					if (!myrpt->remotetx) {
						ast_indicate(chan, AST_CONTROL_RADIO_KEY);
						myrpt->remoterx = 1;
					}
				}
				/* if RX un-key */
				if (f->subclass == AST_CONTROL_RADIO_UNKEY) {
					ast_debug(8, "@@@@ remote rx un-key\n");
					if (!myrpt->remotetx) {
						ast_indicate(chan, AST_CONTROL_RADIO_UNKEY);
						myrpt->remoterx = 0;
					}
				}
			}
			ast_frfree(f);
			continue;
		}
		if ((myrpt->rxchannel != myrpt->txchannel) && (who == myrpt->txchannel)) {
			/* do this cuz you have to */
			f = ast_read(myrpt->txchannel);
			if (!f) {
				ast_debug(1, "@@@@ link:Hung Up\n");
				break;
			}
			if (f->frametype == AST_FRAME_CONTROL) {
				if (f->subclass == AST_CONTROL_HANGUP) {
					ast_debug(1, "@@@@ rpt:Hung Up\n");
					ast_frfree(f);
					break;
				}
			}
			ast_frfree(f);
			continue;
		}

	}
	rpt_mutex_lock(&myrpt->lock);
	if (myrpt->rxchannel != myrpt->txchannel)
		ast_hangup(myrpt->txchannel);
	ast_hangup(myrpt->rxchannel);
	myrpt->hfscanmode = 0;
	myrpt->hfscanstatus = 0;
	myrpt->remoteon = 0;
	rpt_mutex_unlock(&myrpt->lock);
	closerem(myrpt);
	return res;
}

static int unload_module(void)
{
	int i;

	for (i = 0; i < nrpts; i++) {
		if (!strcmp(rpt_vars[i].name, rpt_vars[i].p.nodes))
			continue;
		ast_mutex_destroy(&rpt_vars[i].lock);
	}
	i = ast_unregister_application(app);

	/* Unregister cli extensions */
	ast_cli_unregister_multiple(cli_rpt, sizeof(cli_rpt) / sizeof(struct ast_cli_entry));

	return i;
}

static int load_module(void)
{
	struct ast_flags config_flags = { CONFIG_FLAG_NOCACHE };
	struct ast_config *cfg = ast_config_load("rpt.conf", config_flags);
	if (!cfg) {
		ast_log(LOG_WARNING, "No such configuration file rpt.conf\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_pthread_create(&rpt_master_thread, NULL, rpt_master, cfg);

	/* Register cli extensions */
	ast_cli_register_multiple(cli_rpt, sizeof(cli_rpt) / sizeof(struct ast_cli_entry));

	return ast_register_application(app, rpt_exec, synopsis, descrip);
}

static int reload(void)
{
	int n;

	for (n = 0; n < nrpts; n++)
		rpt_vars[n].reload = 1;
	return(0);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Radio Repeater / Remote Base",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	);
