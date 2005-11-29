/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * VoiceTronix Interface driver
 * 
 * Copyright (C) 2003, Paul Bagyenda
 * Paul Bagyenda <bagyenda@dsmagic.com>
 * Copyright (C) 2004, Ben Kramer
 * Ben Kramer <ben@voicetronix.com.au>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */


#include <stdio.h>
#include <string.h>
#include <asterisk/lock.h>
#include <asterisk/channel.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/config.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/utils.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <ctype.h>

#include <vpbapi.h>
#include <assert.h>

#define DEFAULT_GAIN 0
#define DEFAULT_ECHO_CANCEL 1
  
#define VPB_SAMPLES 160 
#define VPB_MAX_BUF VPB_SAMPLES*4 + AST_FRIENDLY_OFFSET

#define VPB_NULL_EVENT 200

#define VPB_WAIT_TIMEOUT 4000

#define MAX_VPB_GAIN 12.0

#if defined(__cplusplus) || defined(c_plusplus)
 extern "C" {
#endif

static char *desc = "VoiceTronix V6PCI/V12PCI/V4PCI  API Support";
static char *type = "vpb";
static char *tdesc = "Standard VoiceTronix API Driver";
static char *config = "vpb.conf";

/* Default context for dialtone mode */
static char context[AST_MAX_EXTENSION] = "default";

/* Default language */
static char language[MAX_LANGUAGE] = "";
static int usecnt =0;

static int gruntdetect_timeout = 3600000; /* Grunt detect timeout is 1hr. */

static const int prefformat = AST_FORMAT_SLINEAR;

AST_MUTEX_DEFINE_STATIC(usecnt_lock);

/* Protect the interface list (of vpb_pvt's) */
AST_MUTEX_DEFINE_STATIC(iflock);

/* Protect the monitoring thread, so only one process can kill or start it, and not
   when it's doing something critical. */
AST_MUTEX_DEFINE_STATIC(monlock);

/* This is the thread for the monitor which checks for input on the channels
   which are not currently in use.  */
static pthread_t monitor_thread;

static int mthreadactive = -1; /* Flag for monitoring monitorthread.*/


static int restart_monitor(void);

/* The private structures of the VPB channels are 
   linked for selecting outgoing channels */
   
#define MODE_DIALTONE 	1
#define MODE_IMMEDIATE	2
#define MODE_FXO	3

/* Pick a country or add your own! */
#define TONES_AU
#ifdef TONES_AU
static VPB_TONE Dialtone     = {440,   	440, 	440, 	-10,  	-10, 	-10, 	5000,	0   };
static VPB_TONE Busytone     = {470,   	0,   	0, 	-10,  	-100, 	-100,   5000, 	0 };
static VPB_TONE Ringbacktone = {400,   	50,   	440, 	-10,  	-10, 	-10,  	1400, 	800 };
#endif
/*
#define TONES_USA
#ifdef TONES_USA
static VPB_TONE Dialtone     = {425,   0,   0, -16,  -100, -100, 10000,    0};
static VPB_TONE Busytone     = {425,   0,   0, -10,  -100, -100,   500,  500};
static VPB_TONE Ringbacktone = {400,   425,   450, -20,  -20, -20,  1000, 1000};
#endif
*/

/* grunt tone defn's */
static VPB_DETECT toned_grunt = { 3, VPB_GRUNT, 1, 2000, 3000, 0, 0, -40, 0, 0, 0, 40, { { VPB_DELAY, 1000, 0, 0 }, { VPB_RISING, 0, 40, 0 }, { 0, 100, 0, 0 } } };
static VPB_DETECT toned_ungrunt = { 2, VPB_GRUNT, 1, 2000, 1, 0, 0, -40, 0, 0, 30, 40, { { 0, 0, 0, 0 } } };

/* Use loop drop detection */
static int UseLoopDrop=1;

#define TIMER_PERIOD_RINGBACK 2000
#define TIMER_PERIOD_BUSY 700
	  
#define VPB_EVENTS_ALL (VPB_MRING|VPB_MDIGIT|VPB_MDTMF|VPB_MTONEDETECT|VPB_MTIMEREXP|VPB_MPLAY_UNDERFLOW \
			|VPB_MRECORD_OVERFLOW|VPB_MSTATION_OFFHOOK|VPB_MSTATION_ONHOOK \
			|VPB_MRING_OFF|VPB_MDROP|VPB_MSTATION_FLASH)
#define VPB_EVENTS_NODTMF (VPB_MRING|VPB_MDIGIT|VPB_MTONEDETECT|VPB_MTIMEREXP|VPB_MPLAY_UNDERFLOW \
			|VPB_MRECORD_OVERFLOW|VPB_MSTATION_OFFHOOK|VPB_MSTATION_ONHOOK \
			|VPB_MRING_OFF|VPB_MDROP|VPB_MSTATION_FLASH)
#define VPB_EVENTS_STAT (VPB_MRING|VPB_MDIGIT|VPB_MDTMF|VPB_MTONEDETECT|VPB_MTIMEREXP|VPB_MPLAY_UNDERFLOW \
			|VPB_MRECORD_OVERFLOW|VPB_MSTATION_OFFHOOK|VPB_MSTATION_ONHOOK \
			|VPB_MRING_OFF|VPB_MSTATION_FLASH)


// Dialing parameters for Australia
//#define DIAL_WITH_CALL_PROGRESS
VPB_TONE_MAP DialToneMap[] = { 	{ VPB_BUSY_AUST, VPB_CALL_DISCONNECT, 0 },
  				{ VPB_DIAL, VPB_CALL_DIALTONE, 0 },
				{ VPB_RINGBACK_308, VPB_CALL_RINGBACK, 0 },
				{ VPB_BUSY_AUST, VPB_CALL_BUSY, 0 },
				{ VPB_GRUNT, VPB_CALL_GRUNT, 0 },
				{ 0, 0, 1 } };
#define VPB_DIALTONE_WAIT 2000 /* Wait up to 2s for a dialtone */
#define VPB_RINGWAIT 4000 /* Wait up to 4s for ring tone after dialing */
#define VPB_CONNECTED_WAIT 4000 /* If no ring tone detected for 4s then consider call connected */
#define TIMER_PERIOD_NOANSWER 120000 /* Let it ring for 120s before deciding theres noone there */

#define MAX_BRIDGES_V4PCI 2
#define MAX_BRIDGES_V12PCI 128

/* port states */
#define VPB_STATE_ONHOOK	0
#define VPB_STATE_OFFHOOK	1
#define VPB_STATE_DIALLING	2
#define VPB_STATE_JOINED	3
#define VPB_STATE_GETDTMF	4
#define VPB_STATE_PLAYDIAL	5
#define VPB_STATE_PLAYBUSY	6
#define VPB_STATE_PLAYRING	7


typedef struct  {
	int inuse;
	struct ast_channel *c0, *c1, **rc;
	struct ast_frame **fo;
	int flags;
	ast_mutex_t lock;
	pthread_cond_t cond;
	int endbridge;
} vpb_bridge_t;

static vpb_bridge_t * bridges;
static int max_bridges = MAX_BRIDGES_V4PCI;

AST_MUTEX_DEFINE_STATIC(bridge_lock);

typedef enum {
	vpb_model_unknown = 0, 
	vpb_model_v4pci,
	vpb_model_v12pci
} vpb_model_t;

static struct vpb_pvt {

	ast_mutex_t owner_lock;			/* Protect blocks that expect ownership to remain the same */
	struct ast_channel *owner;		/* Channel who owns us, possibly NULL */

	int golock;				/* Got owner lock ? */

	int mode;				/* fxo/imediate/dialtone*/
	int handle;				/* Handle for vpb interface */

	int state;				/* used to keep port state (internal to driver) */

	int group;				/* Which group this port belongs to */

	char dev[256];				/* Device name, eg vpb/1-1 */
	vpb_model_t vpb_model;			/* card model */

	struct ast_frame f, fr;			/* Asterisk frame interface */
	char buf[VPB_MAX_BUF];			/* Static buffer for reading frames */

	int dialtone;				/* NOT USED */
	float txgain, rxgain;			/* Hardware gain control */
	float txswgain, rxswgain;		/* Software gain control */

	int wantdtmf;				/* Waiting for DTMF. */
	char context[AST_MAX_EXTENSION];	/* The context for this channel */

	char ext[AST_MAX_EXTENSION];		/* DTMF buffer for the ext[ens] */
	char language[MAX_LANGUAGE];		/* language being used */
	char callerid[AST_MAX_EXTENSION];	/* CallerId used for directly connected phone */

	int lastoutput;				/* Holds the last Audio format output'ed */
	int lastinput;				/* Holds the last Audio format input'ed */
	int last_ignore_dtmf;

	void *busy_timer;			/* Void pointer for busy vpb_timer */
	int busy_timer_id;			/* unique timer ID for busy timer */

	void *ringback_timer; 			/* Void pointer for ringback vpb_timer */
	int ringback_timer_id;			/* unique timer ID for ringback timer */

	double lastgrunt;			/* time stamp (secs since epoc) of last grunt event */

	ast_mutex_t lock;			/* This one just protects bridge ptr below */
	vpb_bridge_t *bridge;

	int stopreads; 				/* Stop reading...*/

	pthread_t readthread;			/* For monitoring read channel. One per owned channel. */

	ast_mutex_t record_lock;		/* This one prevents reentering a record_buf block */
	ast_mutex_t play_lock;			/* This one prevents reentering a play_buf block */

	ast_mutex_t play_dtmf_lock;
	char play_dtmf[16];

	struct vpb_pvt *next;			/* Next channel in list */

} *iflist = NULL;

static struct ast_channel *vpb_new(struct vpb_pvt *i, int state, char *context);
static void *do_chanreads(void *pvt);

// Can't get vpb_bridge() working on v4pci without either a horrible
// high pitched feedback noise or bad hiss noise depending on gain settings
// Get asterisk to do the bridging
#define BAD_V4PCI_BRIDGE

// This one enables a half duplex bridge which may be required to prevent high pitched
// feedback when getting asterisk to do the bridging and when using certain gain settings.
//#define HALF_DUPLEX_BRIDGE

static int vpb_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc)
{
	struct vpb_pvt *p0 = (struct vpb_pvt *)c0->pvt->pvt;
	struct vpb_pvt *p1 = (struct vpb_pvt *)c1->pvt->pvt;
	int i, res;

	#ifdef BAD_V4PCI_BRIDGE
	if(p0->vpb_model==vpb_model_v4pci)
		return -2;
	#endif

	ast_mutex_lock(&p0->lock);
	ast_mutex_lock(&p1->lock);

	/* Bridge channels, check if we can.  I believe we always can, so find a slot.*/

	ast_mutex_lock(&bridge_lock); {
		for (i = 0; i < max_bridges; i++) 
			if (!bridges[i].inuse)
				break;
		if (i < max_bridges) {
			bridges[i].inuse = 1;
			bridges[i].endbridge = 0;
			bridges[i].flags = flags;
			bridges[i].rc = rc;
			bridges[i].fo = fo;
			bridges[i].c0 = c0;
			bridges[i].c1 = c1;
		} 	       
	} ast_mutex_unlock(&bridge_lock); 

	if (i == max_bridges) {
		ast_log(LOG_WARNING, "Failed to bridge %s and %s!\n", c0->name, c1->name);
		ast_mutex_unlock(&p0->lock);
		ast_mutex_unlock(&p1->lock);
		return -2;
	} else {
		/* Set bridge pointers. You don't want to take these locks while holding bridge lock.*/
		ast_mutex_lock(&p0->lock); {
			p0->bridge = &bridges[i];
		} ast_mutex_unlock(&p0->lock);

		ast_mutex_lock(&p1->lock); {
			p1->bridge = &bridges[i];
		} ast_mutex_unlock(&p1->lock);

		if (option_verbose>1) 
			ast_verbose(VERBOSE_PREFIX_2 "Bridging call entered with [%s, %s]\n", c0->name, c1->name);
	}

	#ifdef HALF_DUPLEX_BRIDGE

	if (option_verbose>1) 
		ast_verbose(VERBOSE_PREFIX_2 "Starting half-duplex bridge [%s, %s]\n", c0->name, c1->name);

	int dir = 0;

	memset(p0->buf, 0, sizeof p0->buf);
	memset(p1->buf, 0, sizeof p1->buf);

	vpb_record_buf_start(p0->handle, VPB_ALAW);
	vpb_record_buf_start(p1->handle, VPB_ALAW);

	vpb_play_buf_start(p0->handle, VPB_ALAW);
	vpb_play_buf_start(p1->handle, VPB_ALAW);

	while( !bridges[i].endbridge ) {
		struct vpb_pvt *from, *to;
		if(++dir%2) {
			from = p0;
			to = p1;
		} else {
			from = p1;
			to = p0;
		}
		vpb_record_buf_sync(from->handle, from->buf, VPB_SAMPLES);
		vpb_play_buf_sync(to->handle, from->buf, VPB_SAMPLES);
	}

	vpb_record_buf_finish(p0->handle);
	vpb_record_buf_finish(p1->handle);

	vpb_play_buf_finish(p0->handle);
	vpb_play_buf_finish(p1->handle);

	if (option_verbose>1) 
		ast_verbose(VERBOSE_PREFIX_2 "Finished half-duplex bridge [%s, %s]\n", c0->name, c1->name);

	res = VPB_OK;

	#else

	res = vpb_bridge(p0->handle, p1->handle, VPB_BRIDGE_ON, i+1 /* resource 1 & 2 only for V4PCI*/ );
	if (res == VPB_OK) {
		//pthread_cond_wait(&bridges[i].cond, &bridges[i].lock); /* Wait for condition signal. */
		while( !bridges[i].endbridge ) {};
		vpb_bridge(p0->handle, p1->handle, VPB_BRIDGE_OFF, i+1 /* resource 1 & 2 only for V4PCI*/ ); 
	}

	#endif

	ast_mutex_lock(&bridge_lock); {
		bridges[i].inuse = 0;
	} ast_mutex_unlock(&bridge_lock); 

	p0->bridge = NULL;
	p1->bridge = NULL;


	if (option_verbose>1) 
		ast_verbose(VERBOSE_PREFIX_2 "Bridging call done with [%s, %s] => %d\n", c0->name, c1->name, res);

	ast_mutex_unlock(&p0->lock);
	ast_mutex_unlock(&p1->lock);
	return (res==VPB_OK)?0:-1;
}

static double get_time_in_ms()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return ((double)tv.tv_sec*1000)+((double)tv.tv_usec/1000);
}

// Caller ID can be located in different positions between the rings depending on your Telco
// Australian (Telstra) callerid starts 700ms after 1st ring and finishes 1.5s after first ring
// Use ANALYSE_CID to record rings and determine location of callerid
//#define ANALYSE_CID
#define RING_SKIP 600
#define CID_MSECS 1700

static void get_callerid(struct vpb_pvt *p)
{
	short buf[CID_MSECS*8]; // 8kHz sampling rate
	double cid_record_time;
	int rc;

	if(strcasecmp(p->callerid, "on")) {
		p->owner->callerid = strdup("unknown");
		return;
	}

	if( ast_mutex_trylock(&p->record_lock) == 0 ) {

		char callerid[AST_MAX_EXTENSION] = ""; 

		cid_record_time = get_time_in_ms();
		if (option_verbose>3) 
			ast_verbose(VERBOSE_PREFIX_4 "CID record - start\n");

		#ifdef ANALYSE_CID
		VPB_RECORD record_parms = { "", 8 * 1000 }; // record a couple of rings
		vpb_record_set(p->handle,&record_parms);
		ast_verbose(VERBOSE_PREFIX_4 "Recording debug CID sample to /tmp/cid.raw\n");
		vpb_record_file_sync(p->handle,"/tmp/cid.raw",VPB_LINEAR);
		rc = 1; // don't try to decode_cid
		#else	
		// Skip any trailing ringtone
		vpb_sleep(RING_SKIP);

		if (option_verbose>3) 
			ast_verbose(VERBOSE_PREFIX_4 "CID record - skipped %fms trailing ring\n",
				 get_time_in_ms() - cid_record_time);
		cid_record_time = get_time_in_ms();

		// Record bit between the rings which contains the callerid
		vpb_record_buf_start(p->handle, VPB_LINEAR);
		rc = vpb_record_buf_sync(p->handle, (char*)buf, sizeof(buf));
		vpb_record_buf_finish(p->handle);
		#endif

		if (option_verbose>3) 
			ast_verbose(VERBOSE_PREFIX_4 "CID record - recorded %fms between rings\n", 
				get_time_in_ms() - cid_record_time);

		ast_mutex_unlock(&p->record_lock);

		if( rc != VPB_OK ) {
			ast_log(LOG_ERROR, "Failed to record caller id sample on %s\n", p->dev );
			return;
		}

		// This decodes FSK 1200baud type callerid
		if ((rc=vpb_cid_decode(callerid, buf, CID_MSECS*8)) == VPB_OK ) {
			if(!*callerid) 
				strncpy(callerid,"undisclosed", sizeof(callerid) - 1); // blocked CID (eg caller used 1831)
		} else {
			ast_log(LOG_ERROR, "Failed to decode caller id on %s - %s\n", p->dev, vpb_strerror(rc) );
			strncpy(callerid,"unknown", sizeof(callerid) - 1);
		}
		p->owner->callerid = strdup(callerid);

	} else 
		ast_log(LOG_ERROR, "Failed to set record mode for caller id on %s\n", p->dev );
}

// Terminate any tones we are presently playing
static void stoptone( int handle)
{
	int ret;
	VPB_EVENT je;
	while(vpb_playtone_state(handle)!=VPB_OK){
		vpb_tone_terminate(handle);
		ret = vpb_get_event_ch_async(handle,&je);
		if ((ret == VPB_OK)&&(je.type != VPB_DIALEND)){
			if (option_verbose > 3){
					ast_verbose(VERBOSE_PREFIX_4 "Stop tone collected a wrong event!!\n");
			}
			vpb_put_event(&je);
		}
		vpb_sleep(10);
	}
}

/* Safe vpb_playtone_async */
static int playtone( int handle, VPB_TONE *tone)
{
	int ret=VPB_OK;
	stoptone(handle);
	if (option_verbose > 3) 
		ast_verbose(VERBOSE_PREFIX_4 "[%02d]: Playing tone\n", handle);
	ret = vpb_playtone_async(handle, tone);
	return ret;
}

static inline int monitor_handle_owned(struct vpb_pvt *p, VPB_EVENT *e)
{
	struct ast_frame f = {AST_FRAME_CONTROL}; /* default is control, Clear rest. */
	int endbridge = 0;
	int res=0;

	if (option_verbose > 3) 
		ast_verbose(VERBOSE_PREFIX_4 "%s: handle_owned: got event: [%d=>%d]\n",
			p->dev, e->type, e->data);

	f.src = type;
	switch (e->type) {
		case VPB_RING:
			if (p->mode == MODE_FXO) {
				f.subclass = AST_CONTROL_RING;
			} else
				f.frametype = -1; /* ignore ring on station port. */
			break;

		case VPB_RING_OFF:
			f.frametype = -1;
			break;

		case VPB_TIMEREXP:
			if (e->data == p->busy_timer_id) {
				playtone(p->handle,&Busytone);
				p->state = VPB_STATE_PLAYBUSY;
				vpb_timer_stop(p->busy_timer);
				vpb_timer_start(p->busy_timer);
				f.frametype = -1;
			} else if (e->data == p->ringback_timer_id) {
				playtone(p->handle, &Ringbacktone);
				vpb_timer_stop(p->ringback_timer);
				vpb_timer_start(p->ringback_timer);
				f.frametype = -1;
			} else {
				f.frametype = -1; /* Ignore. */
			}
			break;

		case VPB_DTMF:
			if (p->owner->_state == AST_STATE_UP) {
				f.frametype = AST_FRAME_DTMF;
				f.subclass = e->data;
			} else
				f.frametype = -1;
			break;

		case VPB_TONEDETECT:
			if (e->data == VPB_BUSY || e->data == VPB_BUSY_308 || e->data == VPB_BUSY_AUST ) {
				if (p->owner->_state == AST_STATE_UP) {
					f.subclass = AST_CONTROL_HANGUP;
				}
				else {
					f.subclass = AST_CONTROL_BUSY;
				}
			} else if (e->data == VPB_GRUNT) {
				if( ( get_time_in_ms() - p->lastgrunt ) > gruntdetect_timeout ) {
					// Nothing heard on line for a very long time
					// Timeout connection
					if (option_verbose > 2) 
						ast_verbose(VERBOSE_PREFIX_3 "grunt timeout\n");
					ast_log(LOG_NOTICE,"%s: Line hangup due of lack of conversation\n",p->dev); 
					f.subclass = AST_CONTROL_HANGUP;
				} else {
					p->lastgrunt = get_time_in_ms();
					f.frametype = -1;
				}
			} else
				f.frametype = -1;
			break;

		case VPB_CALLEND:
			#ifdef DIAL_WITH_CALL_PROGRESS
			if (e->data == VPB_CALL_CONNECTED) 
				f.subclass = AST_CONTROL_ANSWER;
			else if (e->data == VPB_CALL_NO_DIAL_TONE || e->data == VPB_CALL_NO_RING_BACK)
				f.subclass =  AST_CONTROL_CONGESTION;
			else if (e->data == VPB_CALL_NO_ANSWER || e->data == VPB_CALL_BUSY)
				f.subclass = AST_CONTROL_BUSY;
			else if (e->data  == VPB_CALL_DISCONNECTED) 
				f.subclass = AST_CONTROL_HANGUP;
			#else
			ast_log(LOG_NOTICE,"%s: Got call progress callback but blind dialing \n", p->dev); 
			f.frametype = -1;
			#endif
			break;

		case VPB_STATION_OFFHOOK:
			f.subclass = AST_CONTROL_ANSWER;
			break;

		case VPB_DROP:
			if ((p->mode == MODE_FXO)&&(UseLoopDrop)){ /* ignore loop drop on stations */
				if (p->owner->_state == AST_STATE_UP) 
					f.subclass = AST_CONTROL_HANGUP;
				else
					f.frametype = -1;
			}
			break;
		case VPB_STATION_ONHOOK:
			f.subclass = AST_CONTROL_HANGUP;
			break;

		case VPB_STATION_FLASH:
			f.subclass = AST_CONTROL_FLASH;
			break;

		// Called when dialing has finished and ringing starts
		// No indication that call has really been answered when using blind dialing
		case VPB_DIALEND:
			if (p->state < 5){
				f.subclass = AST_CONTROL_ANSWER;
				if (option_verbose > 1) 
					ast_verbose(VERBOSE_PREFIX_2 "%s: Dialend\n", p->dev);
			} else {
				f.frametype = -1;
			}
			break;

		case VPB_PLAY_UNDERFLOW:
			f.frametype = -1;
			vpb_reset_play_fifo_alarm(p->handle);
			break;

		case VPB_RECORD_OVERFLOW:
			f.frametype = -1;
			vpb_reset_record_fifo_alarm(p->handle);
			break;

		default:
			f.frametype = -1;
			break;
	}

	if (option_verbose > 3) 
		ast_verbose(VERBOSE_PREFIX_4 "%s: handle_owned: putting frame type[%d]subclass[%d], bridge=%p\n",
			p->dev, f.frametype, f.subclass, (void *)p->bridge);

	res = ast_mutex_lock(&p->lock); 
	if (option_verbose > 3) ast_verbose("%s: LOCKING in handle_owned [%d]\n", p->dev,res);
	{
		if (p->bridge) { /* Check what happened, see if we need to report it. */
			switch (f.frametype) {
				case AST_FRAME_DTMF:
					if (	!(p->bridge->c0 == p->owner && 
							(p->bridge->flags & AST_BRIDGE_DTMF_CHANNEL_0) ) &&
						!(p->bridge->c1 == p->owner && 
							(p->bridge->flags & AST_BRIDGE_DTMF_CHANNEL_1) )) 
						/* Kill bridge, this is interesting. */
						endbridge = 1;
					break;

				case AST_FRAME_CONTROL:
					if (!(p->bridge->flags & AST_BRIDGE_IGNORE_SIGS)) 
					#if 0
					if (f.subclass == AST_CONTROL_BUSY ||
					f.subclass == AST_CONTROL_CONGESTION ||
					f.subclass == AST_CONTROL_HANGUP ||
					f.subclass == AST_CONTROL_FLASH)
					#endif
						endbridge = 1;
					break;

				default:
					break;
			}
			if (endbridge) {
				if (p->bridge->fo)
					*p->bridge->fo = ast_frisolate(&f);
				if (p->bridge->rc)
					*p->bridge->rc = p->owner;

				ast_mutex_lock(&p->bridge->lock); {
					p->bridge->endbridge = 1;
					pthread_cond_signal(&p->bridge->cond);
				} ast_mutex_unlock(&p->bridge->lock); 	       		   
			}	  
		}
	} 

	if (endbridge){
		res = ast_mutex_unlock(&p->lock);
		if (option_verbose > 3) ast_verbose("%s: unLOCKING in handle_owned [%d]\n", p->dev,res);
		return 0;
	}

	// Trylock used here to avoid deadlock that can occur if we
	// happen to be in here handling an event when hangup is called
	// Problem is that hangup holds p->owner->lock
	if ((f.frametype >= 0)&& (f.frametype != AST_FRAME_NULL)&&(p->owner)) {
		if (ast_mutex_trylock(&p->owner->lock)==0)  {
			ast_queue_frame(p->owner, &f);
			ast_mutex_unlock(&p->owner->lock);
		} else {
			ast_verbose("%s: handled_owned: Missed event %d/%d \n",
				p->dev,f.frametype, f.subclass);
		}
	}
	res = ast_mutex_unlock(&p->lock);
	if (option_verbose > 3) ast_verbose("%s: unLOCKING in handle_owned [%d]\n", p->dev,res);

	return 0;
}

static inline int monitor_handle_notowned(struct vpb_pvt *p, VPB_EVENT *e)
{
	char s[2] = {0};

	if (option_verbose > 3) {
		char str[VPB_MAX_STR];
		vpb_translate_event(e, str);
		ast_verbose(VERBOSE_PREFIX_4 "%s: handle_notowned: mode=%d, event[%d][%s]=[%d]\n",
			p->dev, p->mode, e->type,str, e->data);
	}

	switch(e->type) {
		case VPB_RING:
			if (p->mode == MODE_FXO) /* FXO port ring, start * */ {
				vpb_new(p, AST_STATE_RING, p->context);
				get_callerid(p);	/* Australian Caller ID only between 1st and 2nd ring */
			}
			break;

		case VPB_RING_OFF:
			break;

		case VPB_STATION_OFFHOOK:
			if (p->mode == MODE_IMMEDIATE) 
				vpb_new(p,AST_STATE_RING, p->context);
			else {
				ast_verbose(VERBOSE_PREFIX_4 "%s: handle_notowned: playing dialtone\n",p->dev);
				playtone(p->handle, &Dialtone);
				p->wantdtmf = 1;
				p->ext[0] = 0;	/* Just to be sure & paranoid.*/
				p->state=VPB_STATE_PLAYDIAL;
			}
			break;

		case VPB_DIALEND:
			if (p->mode == MODE_DIALTONE){
				if (p->state == VPB_STATE_PLAYDIAL) {
					playtone(p->handle, &Dialtone);
					p->wantdtmf = 1;
					p->ext[0] = 0;	// Just to be sure & paranoid.
				}
				/* These are not needed as they have timers to restart them
				else if (p->state == VPB_STATE_PLAYBUSY) {
					playtone(p->handle, &Busytone);
					p->wantdtmf = 1;
					p->ext[0] = 0;	// Just to be sure & paranoid.
				}
				else if (p->state == VPB_STATE_PLAYRING) {
					playtone(p->handle, &Ringbacktone);
					p->wantdtmf = 1;
					p->ext[0] = 0;	// Just to be sure & paranoid.
				}
				*/
			} else {
				ast_verbose(VERBOSE_PREFIX_4 "%s: handle_notowned: Got a DIALEND when not really expected\n",p->dev);
			}
			break;

		case VPB_STATION_ONHOOK:	/* clear ext */
			stoptone(p->handle);
			p->wantdtmf = 1 ;
			p->ext[0] = 0;
			p->state=VPB_STATE_ONHOOK;
			break;

		case VPB_DTMF:
			if (p->state == VPB_STATE_ONHOOK){
				/* DTMF's being passed while on-hook maybe Caller ID */
				break;
			}
			if (p->wantdtmf == 1) {
				stoptone(p->handle);
				p->wantdtmf = 0;
			}
			p->state=VPB_STATE_GETDTMF;
			s[0] = e->data;
			strncat(p->ext, s, sizeof(p->ext) - strlen(p->ext) - 1);
			if (ast_exists_extension(NULL, p->context, p->ext, 1, p->callerid)){
				vpb_new(p,AST_STATE_RING, p->context);
			} else if (!ast_canmatch_extension(NULL, p->context, p->ext, 1, p->callerid)){
				if (ast_exists_extension(NULL, "default", p->ext, 1, p->callerid)) {
					vpb_new(p,AST_STATE_RING, "default");	      
				} else if (!ast_canmatch_extension(NULL, "default", p->ext, 1, p->callerid)) {
					if (option_verbose > 3) {
						ast_verbose(VERBOSE_PREFIX_4 "%s: handle_notowned: can't match anything in %s or default\n",
							 p->dev, p->context);
					}
					playtone(p->handle, &Busytone);
					vpb_timer_stop(p->busy_timer);
					vpb_timer_start(p->busy_timer);
					p->state = VPB_STATE_PLAYBUSY;
				}
			}
			break;

		default:
			/* Ignore.*/
			break;
	}

	if (option_verbose > 3) 
		ast_verbose(VERBOSE_PREFIX_4 "%s: handle_notowned: mode=%d, [%d=>%d]\n",
			p->dev, p->mode, e->type, e->data);

	return 0;
}

static void *do_monitor(void *unused)
{

	/* Monitor thread, doesn't die until explicitly killed. */

	if (option_verbose > 1) 
		ast_verbose(VERBOSE_PREFIX_2 "Starting vpb monitor thread[%ld]\n",
	pthread_self());

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	for(;;) {
		VPB_EVENT e;
		VPB_EVENT je;
		char str[VPB_MAX_STR];
		struct vpb_pvt *p;

		/*
		if (option_verbose > 3)
		     ast_verbose(VERBOSE_PREFIX_4 "Monitor waiting for event\n");
		*/

		int res = vpb_get_event_sync(&e, VPB_WAIT_TIMEOUT);
		if( (res==VPB_NO_EVENTS) || (res==VPB_TIME_OUT) ){
			/*
			if (option_verbose > 3){
				if (res ==  VPB_NO_EVENTS){
					ast_verbose(VERBOSE_PREFIX_4 "No events....\n");
				} else {
					ast_verbose(VERBOSE_PREFIX_4 "No events, timed out....\n");
				}
			}
			*/
			continue;
		}

		if (res != VPB_OK) {
			ast_log(LOG_ERROR,"Monitor get event error %s\n", vpb_strerror(res) );
			ast_verbose("Monitor get event error %s\n", vpb_strerror(res) );
			continue;
		}

		str[0] = 0;

		p = NULL;

		ast_mutex_lock(&monlock); {
			vpb_translate_event(&e, str);

			if (e.type == VPB_NULL_EVENT) {
				if (option_verbose > 3)
					ast_verbose(VERBOSE_PREFIX_4 "Monitor got null event\n");
				goto done; /* Nothing to do, just a wakeup call.*/
			}
			if (strlen(str)>1){
				str[(strlen(str)-1)]='\0';
			}

			ast_mutex_lock(&iflock); {
				p = iflist;
				while (p && p->handle != e.handle)
					p = p->next;
			} ast_mutex_unlock(&iflock);

			if (p && (option_verbose > 3))
				ast_verbose(VERBOSE_PREFIX_4 "%s: Event [%d=>%s] \n", 
					p ? p->dev : "null", e.type, str );
			done: (void)0;

		} ast_mutex_unlock(&monlock); 

		if (!p) {
			if (e.type != VPB_NULL_EVENT){
				ast_log(LOG_WARNING, "Got event [%s][%d], no matching iface!\n", str,e.type);    
				if (option_verbose > 3){
					ast_verbose(VERBOSE_PREFIX_4 "vpb/ERR: No interface for Event [%d=>%s] \n",e.type,str );
				}
			}
			continue;
		} 

		/* flush the event from the channel event Q */
		vpb_get_event_ch_async(e.handle,&je);
		if (option_verbose > 3){
			vpb_translate_event(&je, str);
			ast_verbose( VERBOSE_PREFIX_4 "%s: Flushing event [%d]=>%s\n",p->dev,je.type,str);
		}

		/* Check for ownership and locks */
		if ((p->owner)&&(!p->golock)){
			/* Need to get owner lock */
			/* Safely grab both p->lock and p->owner->lock so that there
			cannot be a race with something from the other side */
			/*
			ast_mutex_lock(&p->lock);
			while(ast_mutex_trylock(&p->owner->lock)) {
				ast_mutex_unlock(&p->lock);
				usleep(1);
				ast_mutex_lock(&p->lock);
				if (!p->owner)
					break;
			}
			if (p->owner)
				p->golock=1;
			*/
		}
		/* Two scenarios: Are you owned or not. */
		if (p->owner) {
			monitor_handle_owned(p, &e);
		} else {
			monitor_handle_notowned(p, &e);
		}
		/* if ((!p->owner)&&(p->golock)){
			ast_mutex_unlock(&p->owner->lock);
			ast_mutex_unlock(&p->lock);
		}
		*/

	}

	return NULL;
}

static int restart_monitor(void)
{
	int error = 0;

	/* If we're supposed to be stopped -- stay stopped */
	if (mthreadactive == -2)
		return 0;

	if (option_verbose > 3)
		ast_verbose(VERBOSE_PREFIX_4 "Restarting monitor\n");

	ast_mutex_lock(&monlock); {
		if (monitor_thread == pthread_self()) {
			ast_log(LOG_WARNING, "Cannot kill myself\n");
			error = -1;
			if (option_verbose > 3)
				ast_verbose(VERBOSE_PREFIX_4 "Monitor trying to kill monitor\n");
			goto done;
		}
		if (mthreadactive != -1) {
			/* Why do other drivers kill the thread? No need says I, simply awake thread with event. */
			VPB_EVENT e;
			e.handle = 0;
			e.type = VPB_NULL_EVENT;
			e.data = 0;

			if (option_verbose > 3)
				ast_verbose(VERBOSE_PREFIX_4 "Trying to reawake monitor\n");

			vpb_put_event(&e);
		} else {
			/* Start a new monitor */
			int pid = ast_pthread_create(&monitor_thread, NULL, do_monitor, NULL); 
			if (option_verbose > 3)
				ast_verbose(VERBOSE_PREFIX_4 "Created new monitor thread %d\n",pid);
			if (pid < 0) {
				ast_log(LOG_ERROR, "Unable to start monitor thread.\n");
				error = -1;
				goto done;
			} else
				mthreadactive = 0; /* Started the thread!*/
		}
		done: (void)0;
	} ast_mutex_unlock(&monlock);

	if (option_verbose > 3)
		ast_verbose(VERBOSE_PREFIX_4 "Monitor restarted\n");

	return error;
}

// Per board config that must be called after vpb_open()
static void mkbrd(vpb_model_t model, int echo_cancel)
{
	if(!bridges) {
		if(model==vpb_model_v4pci) 
			max_bridges = MAX_BRIDGES_V4PCI;
		bridges = (vpb_bridge_t *)malloc(max_bridges * sizeof(vpb_bridge_t) );
		if(!bridges) 
			ast_log(LOG_ERROR, "Failed to initialize bridges\n");
		else {
			memset(bridges,0,max_bridges * sizeof(vpb_bridge_t));
			for(int i = 0; i < max_bridges; i++ ) {
				ast_mutex_init(&bridges[i].lock);
				pthread_cond_init(&bridges[i].cond, NULL);
			}
		}
	}
	if(!echo_cancel) {
		if (model==vpb_model_v4pci) {
			vpb_echo_canc_disable();
			ast_log(LOG_NOTICE, "Voicetronix echo cancellation OFF\n");
		} 
		else {
		/* need to it port by port for OpenSwitch*/
		}
	} else {
		if (model==vpb_model_v4pci) {
			vpb_echo_canc_enable();
			ast_log(LOG_NOTICE, "Voicetronix echo cancellation ON\n");
		}
		else {
		/* need to it port by port for OpenSwitch*/
		}
	}
}

struct vpb_pvt *mkif(int board, int channel, int mode, float txgain, float rxgain,
			 float txswgain, float rxswgain, int bal1, int bal2, int bal3,
			 char * callerid, int echo_cancel, int group )
{
	struct vpb_pvt *tmp;
	char buf[64];

	tmp = (struct vpb_pvt *)calloc(1, sizeof *tmp);

	if (!tmp)
		return NULL;

	tmp->handle = vpb_open(board, channel);

	if (tmp->handle < 0) {	  
		ast_log(LOG_WARNING, "Unable to create channel vpb/%d-%d: %s\n", 
					board, channel, strerror(errno));
		free(tmp);
		return NULL;
	}
	       
	snprintf(tmp->dev, sizeof(tmp->dev), "vpb/%d-%d", board, channel);

	tmp->mode = mode;

	tmp->group = group;

	strncpy(tmp->language, language, sizeof(tmp->language) - 1);
	strncpy(tmp->context, context, sizeof(tmp->context) - 1);

	if(callerid) { 
		strncpy(tmp->callerid, callerid, sizeof(tmp->callerid) - 1);
		free(callerid);
	} else {
		strncpy(tmp->callerid, "unknown", sizeof(tmp->callerid) - 1);
	}

	/* check if codec balances have been set in the config file */
	if (bal3>=0) {
		if ((bal1>=0) && !(bal1 & 32)) bal1 |= 32;
			vpb_set_codec_reg(tmp->handle, 0x42, bal3);
	}
	if(bal1>=0) vpb_set_codec_reg(tmp->handle, 0x32, bal1);
	if(bal2>=0) vpb_set_codec_reg(tmp->handle, 0x3a, bal2);

	tmp->txgain = txgain;
	vpb_play_set_hw_gain(tmp->handle, (txgain > MAX_VPB_GAIN) ? MAX_VPB_GAIN : txgain);

	tmp->rxgain = rxgain;
	vpb_record_set_hw_gain(tmp->handle, (rxgain > MAX_VPB_GAIN) ? MAX_VPB_GAIN : rxgain);

	tmp->txswgain = txswgain;
	vpb_play_set_gain(tmp->handle, (txswgain > MAX_VPB_GAIN) ? MAX_VPB_GAIN : txswgain);

	tmp->rxswgain = rxswgain;
	vpb_record_set_gain(tmp->handle, (rxswgain > MAX_VPB_GAIN) ? MAX_VPB_GAIN : rxswgain);

	tmp->vpb_model = vpb_model_unknown;
	if( vpb_get_model(buf) == VPB_OK ) {
		if(strcmp(buf,"V12PCI")==0) 
			tmp->vpb_model = vpb_model_v12pci;
		else if(strcmp(buf,"VPB4")==0) 
			tmp->vpb_model = vpb_model_v4pci;
	}

	ast_mutex_init(&tmp->owner_lock);
	ast_mutex_init(&tmp->lock);
	ast_mutex_init(&tmp->record_lock);
	ast_mutex_init(&tmp->play_lock);
	ast_mutex_init(&tmp->play_dtmf_lock);
	
	tmp->golock=0;

	tmp->busy_timer_id = vpb_timer_get_unique_timer_id();
	vpb_timer_open(&tmp->busy_timer, tmp->handle, tmp->busy_timer_id, TIMER_PERIOD_BUSY);

	tmp->ringback_timer_id = vpb_timer_get_unique_timer_id();
	vpb_timer_open(&tmp->ringback_timer, tmp->handle, tmp->ringback_timer_id, TIMER_PERIOD_RINGBACK);
	      
	if (mode == MODE_FXO){
		vpb_set_event_mask(tmp->handle, VPB_EVENTS_NODTMF );
	}
	else {
		vpb_set_event_mask(tmp->handle, VPB_EVENTS_STAT );
	}

	if ((tmp->vpb_model == vpb_model_v12pci) && (echo_cancel)){
		vpb_hostecho_on(tmp->handle);
	}

	/* define grunt tone */
	vpb_settonedet(tmp->handle,&toned_ungrunt);

	ast_log(LOG_NOTICE,"Voicetronix %s channel %s initialized (rxgain=%f txgain=%f) (%f/%f/0x%X/0x%X/0x%X)\n",
		(tmp->vpb_model==vpb_model_v4pci)?"V4PCI":
		(tmp->vpb_model==vpb_model_v12pci)?"V12PCI":"[Unknown model]",
		tmp->dev, tmp->rxswgain, tmp->txswgain, tmp->rxgain, tmp->txgain, bal1, bal2, bal3 );

	return tmp;
}

static int vpb_indicate(struct ast_channel *ast, int condition)
{
	struct vpb_pvt *p = (struct vpb_pvt *)ast->pvt->pvt;
	int res = 0;
	int tmp = 0;

	if (option_verbose > 3)
		ast_verbose(VERBOSE_PREFIX_4 "%s: vpb indicate [%d] state[%d]\n", p->dev, condition,ast->_state);
	if (ast->_state != AST_STATE_UP) {
		return res;
	}

	if (option_verbose > 3) ast_verbose("%s: LOCKING in indicate \n", p->dev);
	ast_mutex_lock(&p->lock);
	switch(condition) {
		case AST_CONTROL_BUSY:
		case AST_CONTROL_CONGESTION:
			playtone(p->handle, &Busytone);
			p->state = VPB_STATE_PLAYBUSY;
			vpb_timer_stop(p->busy_timer); 
			vpb_timer_start(p->busy_timer); 
			break;
		case AST_CONTROL_RINGING:
			playtone(p->handle, &Ringbacktone);
			p->state = VPB_STATE_PLAYRING;
			if (option_verbose > 3)
				ast_verbose(VERBOSE_PREFIX_4 "%s: vpb indicate: setting ringback timer [%d]\n", p->dev,p->ringback_timer_id);
			
			vpb_timer_stop(p->ringback_timer);
			vpb_timer_start(p->ringback_timer);
			break;	    
		case AST_CONTROL_ANSWER:
		case -1: /* -1 means stop playing? */
			vpb_timer_stop(p->ringback_timer);
			vpb_timer_stop(p->busy_timer);
			stoptone(p->handle);
			break;
		case AST_CONTROL_HANGUP:
			playtone(p->handle, &Busytone);
			p->state = VPB_STATE_PLAYBUSY;
			vpb_timer_stop(p->busy_timer);
			vpb_timer_start(p->busy_timer);
			break;

		default:
			res = 0;
			break;
	}
	tmp = ast_mutex_unlock(&p->lock);
	if (option_verbose > 3) ast_verbose("%s: unLOCKING in indicate [%d]\n", p->dev,tmp);
	return res;
}

static int vpb_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct vpb_pvt *p = (struct vpb_pvt *)newchan->pvt->pvt;
	int res = 0;

	if (option_verbose > 3) ast_verbose("%s: LOCKING in fixup \n", p->dev);
	ast_mutex_lock(&p->lock);
	ast_log(LOG_DEBUG, "New owner for channel %s is %s\n", p->dev, newchan->name);

	if (p->owner == oldchan) {
		p->owner = newchan;
	}

	if (newchan->_state == AST_STATE_RINGING) 
		vpb_indicate(newchan, AST_CONTROL_RINGING);

	res= ast_mutex_unlock(&p->lock);
	if (option_verbose > 3) ast_verbose("%s: unLOCKING in fixup [%d]\n", p->dev,res);
	return 0;
}

static int vpb_digit(struct ast_channel *ast, char digit)
{
	struct vpb_pvt *p = (struct vpb_pvt *)ast->pvt->pvt;
	char s[2];
	int res = 0;

	if (option_verbose > 3) ast_verbose("%s: LOCKING in digit \n", p->dev);
	ast_mutex_lock(&p->lock);


	s[0] = digit;
	s[1] = '\0';

	if (option_verbose > 3)
		ast_verbose(VERBOSE_PREFIX_4 "%s: play digit[%s]\n", p->dev, s);

	ast_mutex_lock(&p->play_dtmf_lock);
	strncat(p->play_dtmf,s,sizeof(*p->play_dtmf));
	ast_mutex_unlock(&p->play_dtmf_lock);

	res = ast_mutex_unlock(&p->lock);
	if (option_verbose > 3) ast_verbose("%s: unLOCKING in digit [%d]\n", p->dev,res);
	return 0;
}

static int vpb_call(struct ast_channel *ast, char *dest, int timeout)
{
	struct vpb_pvt *p = (struct vpb_pvt *)ast->pvt->pvt;
	int res = 0,i;
	char *s = strrchr(dest, '/');
	char dialstring[254] = "";
	int tmp = 0;

	if (option_verbose > 3) ast_verbose("%s: LOCKING in call \n", p->dev);
	ast_mutex_lock(&p->lock);

	if (s)
		s = s + 1;
	else
		s = dest;
	strncpy(dialstring, s, sizeof(dialstring) - 1);
	for (i=0; dialstring[i] != '\0' ; i++) {
		if ((dialstring[i] == 'w') || (dialstring[i] == 'W'))
			dialstring[i] = ',';
		else if ((dialstring[i] == 'f') || (dialstring[i] == 'F'))
			dialstring[i] = '&';
	}	
	if (option_verbose > 3)
		ast_verbose(VERBOSE_PREFIX_4 "%s: starting call\n", p->dev);

	if (ast->_state != AST_STATE_DOWN && ast->_state != AST_STATE_RESERVED) {
		ast_log(LOG_WARNING, "vpb_call on %s neither down nor reserved!\n", ast->name);
		tmp = ast_mutex_unlock(&p->lock);
		if (option_verbose > 3) ast_verbose("%s: unLOCKING in call [%d]\n", p->dev,tmp);
		return -1;
	}
	if (p->mode != MODE_FXO)  /* Station port, ring it. */
		res = vpb_ring_station_async(p->handle, VPB_RING_STATION_ON,0);       
	else {
		VPB_CALL call;

		// Dial must timeout or it can leave channels unuseable
		if( timeout == 0 )
			timeout = TIMER_PERIOD_NOANSWER;
		else 
			timeout = timeout * 1000; //convert from secs to ms.

		// These timeouts are only used with call progress dialing
		call.dialtones = 1; // Number of dialtones to get outside line
		call.dialtone_timeout = VPB_DIALTONE_WAIT; // Wait this long for dialtone (ms)
		call.ringback_timeout = VPB_RINGWAIT; // Wait this long for ringing after dialing (ms)
		call.inter_ringback_timeout = VPB_CONNECTED_WAIT; // If ringing stops for this long consider it connected (ms)
		call.answer_timeout = timeout; // Time to wait for answer after ringing starts (ms)
		memcpy( &call.tone_map,  DialToneMap, sizeof(DialToneMap) );
		vpb_set_call(p->handle, &call);

		if (option_verbose > 1)
			ast_verbose(VERBOSE_PREFIX_2 "%s: Calling %s on %s \n",p->dev, dialstring, ast->name); 

		if (option_verbose > 2) {
			int j;
			ast_verbose(VERBOSE_PREFIX_2 "%s: Dial parms for %s %d/%dms/%dms/%dms/%dms\n", p->dev
				, ast->name, call.dialtones, call.dialtone_timeout
				, call.ringback_timeout, call.inter_ringback_timeout
				, call.answer_timeout );
			for( j=0; !call.tone_map[j].terminate; j++ )
				ast_verbose(VERBOSE_PREFIX_2 "%s: Dial parms for %s tone %d->%d\n", p->dev,
					ast->name, call.tone_map[j].tone_id, call.tone_map[j].call_id); 
		}

		vpb_sethook_sync(p->handle,VPB_OFFHOOK);
		p->state=VPB_STATE_OFFHOOK;

		#ifndef DIAL_WITH_CALL_PROGRESS
		vpb_sleep(300);
		res = vpb_dial_async(p->handle, dialstring);
		#else
		res = vpb_call_async(p->handle, dialstring);
		#endif

		if (res != VPB_OK) {
			ast_log(LOG_DEBUG, "Call on %s to %s failed: %s\n", ast->name, s, vpb_strerror(res));	      
			res = -1;
		} else 
			res = 0;
	}

	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "%s: VPB Calling %s [t=%d] on %s returned %d\n",p->dev , s, timeout, ast->name, res); 
	if (res == 0) {
		ast_setstate(ast, AST_STATE_RINGING);
		ast_queue_control(ast,AST_CONTROL_RINGING);		
	}

	if (!p->readthread){
		ast_pthread_create(&p->readthread, NULL, do_chanreads, (void *)p);
	}

	tmp = ast_mutex_unlock(&p->lock);
	if (option_verbose > 3) ast_verbose("%s: unLOCKING in call [%d]\n", p->dev,tmp);
	return res;
}

static int vpb_hangup(struct ast_channel *ast)
{
	struct vpb_pvt *p = (struct vpb_pvt *)ast->pvt->pvt;
	VPB_EVENT je;
	char str[VPB_MAX_STR];
	int res =0 ;

	if (option_verbose > 3) ast_verbose("%s: LOCKING in hangup \n", p->dev);
	ast_mutex_lock(&p->lock);
	if (option_verbose > 1) 
		ast_verbose(VERBOSE_PREFIX_2 "%s: Hangup requested\n", ast->name);

	if (!ast->pvt || !ast->pvt->pvt) {
		ast_log(LOG_WARNING, "%s: channel not connected?\n", ast->name);
		res = ast_mutex_unlock(&p->lock);
		if (option_verbose > 3) ast_verbose("%s: unLOCKING in hangup [%d]\n", p->dev,res);
		return 0;
	}

	if(option_verbose>3) 
		ast_verbose( VERBOSE_PREFIX_4 "%s: Setting state down\n",ast->name);
	ast_setstate(ast,AST_STATE_DOWN);


	/* Stop record */
	p->stopreads = 1;
	if( p->readthread ){
		pthread_join(p->readthread, NULL); 
		if(option_verbose>3) 
			ast_verbose( VERBOSE_PREFIX_4 "%s: stopped record thread on %s\n",ast->name,p->dev);
	}

	/* Stop play */
	if (p->lastoutput != -1) {
		if(option_verbose>1) 
			ast_verbose( VERBOSE_PREFIX_2 "%s: Ending play mode on %s\n",ast->name,p->dev);
		vpb_play_terminate(p->handle);
		ast_mutex_lock(&p->play_lock); {
			vpb_play_buf_finish(p->handle);
		} ast_mutex_unlock(&p->play_lock);
	}

	if (p->mode != MODE_FXO) { 
		/* station port. */
		vpb_ring_station_async(p->handle, VPB_RING_STATION_OFF,0);	
		if(p->state!=VPB_STATE_ONHOOK){
			/* This is causing a "dial end" "play tone" loop
			playtone(p->handle, &Busytone); 
			p->state = VPB_STATE_PLAYBUSY;
			if(option_verbose>4) 
				ast_verbose( VERBOSE_PREFIX_4 "%s: Station offhook[%d], playing busy tone\n",
								ast->name,p->state);
			*/
		}
		else {
			stoptone(p->handle);
		}
	} else {
		stoptone(p->handle); // Terminates any dialing
		vpb_sethook_sync(p->handle, VPB_ONHOOK);
		p->state=VPB_STATE_ONHOOK;
	}
	while (VPB_OK==vpb_get_event_ch_async(p->handle,&je)){
		if(option_verbose>3) {
			vpb_translate_event(&je, str);
			ast_verbose( VERBOSE_PREFIX_4 "%s: Flushing event [%d]=>%s\n",ast->name,je.type,str);
		}
	}

	p->readthread = 0;
	p->lastoutput = -1;
	p->lastinput = -1;
	p->last_ignore_dtmf = 1;
	p->ext[0]  = 0;
	p->dialtone = 0;

	p->owner = NULL;
	ast->pvt->pvt=NULL;

	ast_mutex_lock(&usecnt_lock); {
		usecnt--;
	} ast_mutex_unlock(&usecnt_lock);
	ast_update_use_count();

	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "%s: Hangup complete\n", ast->name);

	restart_monitor();
	res = ast_mutex_unlock(&p->lock);
	if (option_verbose > 3) ast_verbose("%s: unLOCKING in hangup [%d]\n", p->dev,res);
	return 0;
}

static int vpb_answer(struct ast_channel *ast)
{
	struct vpb_pvt *p = (struct vpb_pvt *)ast->pvt->pvt;
	VPB_EVENT je;
	int ret;
	int res = 0;
	if (option_verbose > 3) ast_verbose("%s: LOCKING in answer \n", p->dev);
	ast_mutex_lock(&p->lock);

	if (option_verbose > 3)
			ast_verbose(VERBOSE_PREFIX_4 "%s: Answering channel\n",p->dev);

	if (ast->_state != AST_STATE_UP) {
		if (p->mode == MODE_FXO){
			vpb_sethook_sync(p->handle, VPB_OFFHOOK);
			p->state=VPB_STATE_OFFHOOK;
			vpb_sleep(500);
			ret = vpb_get_event_ch_async(p->handle,&je);
			if ((ret == VPB_OK)&&(je.type != VPB_DROP)){
				if (option_verbose > 3){
						ast_verbose(VERBOSE_PREFIX_4 "%s: Answer collected a wrong event!!\n",p->dev);
				}
				vpb_put_event(&je);
			}
		}
		ast_setstate(ast, AST_STATE_UP);

		if(option_verbose>1) 
			ast_verbose( VERBOSE_PREFIX_2 "%s: Answered call from %s on %s [%s]\n", p->dev,
					p->owner->callerid, ast->name,(p->mode == MODE_FXO)?"FXO":"FXS");

		ast->rings = 0;
		if( !p->readthread ){
	//		res = ast_mutex_unlock(&p->lock);
	//		ast_verbose("%s: unLOCKING in answer [%d]\n", p->dev,res);
			ast_pthread_create(&p->readthread, NULL, do_chanreads, (void *)p);
		} else {
			if(option_verbose>3) 
				ast_verbose(VERBOSE_PREFIX_4 "%s: Record thread already running!!\n",p->dev);
		}
	} else {
		if(option_verbose>3) {
			ast_verbose(VERBOSE_PREFIX_4 "%s: Answered state is up\n",p->dev);
		}
	//	res = ast_mutex_unlock(&p->lock);
	//	ast_verbose("%s: unLOCKING in answer [%d]\n", p->dev,res);
	}
	res = ast_mutex_unlock(&p->lock);
	if(option_verbose>3) ast_verbose("%s: unLOCKING in answer [%d]\n", p->dev,res);
	return 0;
}

static struct ast_frame  *vpb_read(struct ast_channel *ast)
{
	struct vpb_pvt *p = (struct vpb_pvt *)ast->pvt->pvt; 
	static struct ast_frame f = {AST_FRAME_NULL}; 

	f.src = type;
	ast_log(LOG_NOTICE, "vpb_read should never be called (chan=%s)!\n", p->dev);

	return &f;
}

static inline int ast2vpbformat(int ast_format)
{
	switch(ast_format) {
		case AST_FORMAT_ALAW:
			return VPB_ALAW;
		case AST_FORMAT_SLINEAR:
			return VPB_LINEAR;
		case AST_FORMAT_ULAW:
			return VPB_MULAW;
		case AST_FORMAT_ADPCM:
			return VPB_OKIADPCM;
		default:
			return -1;
	}
}

static inline int astformatbits(int ast_format)
{
	switch(ast_format) {
		case AST_FORMAT_ALAW:
		case AST_FORMAT_ULAW:
			return 8;
		case AST_FORMAT_SLINEAR:
			return 16;
		case AST_FORMAT_ADPCM:
			return 4;
		default:
			return 8;
	}   
}

int gain_vector(float g, short *v, int n) 
{
	int i;
	float tmp;
	for ( i = 0; i<n; i++) {
		tmp = g*v[i];
		if (tmp > 32767.0)
			tmp = 32767.0;
		if (tmp < -32768.0)
			tmp = -32768.0;
		v[i] = (short)tmp;	
	}  
	return(i);
}

static int vpb_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct vpb_pvt *p = (struct vpb_pvt *)ast->pvt->pvt; 
	int res = 0, fmt = 0;
//	ast_mutex_lock(&p->lock);
	if(option_verbose>5) 
		ast_verbose( VERBOSE_PREFIX_4 "%s: Writing to channel\n", p->dev);

	if (frame->frametype != AST_FRAME_VOICE) {
		if(option_verbose>3) 
			ast_verbose( VERBOSE_PREFIX_4 "%s: Don't know how to handle from type %d\n", ast->name, frame->frametype);
//		ast_mutex_unlock(&p->lock);
		return 0;
	} else if (ast->_state != AST_STATE_UP) {
		if(option_verbose>3) 
			ast_verbose( VERBOSE_PREFIX_4 "%s: Attempt to Write frame type[%d]subclass[%d] on not up chan\n",ast->name, frame->frametype, frame->subclass);
		p->lastoutput = -1;
//		ast_mutex_unlock(&p->lock);
		return 0;
	}

	fmt = ast2vpbformat(frame->subclass);
	if (fmt < 0) {
		ast_log(LOG_WARNING, "%s: vpb_write Cannot handle frames of %d format!\n",ast->name, frame->subclass);
		return -1;
	}

	ast_mutex_lock(&p->play_lock);

	if (p->lastoutput == -1) {
		vpb_play_buf_start(p->handle, fmt);
		if(option_verbose>1) 
			ast_verbose( VERBOSE_PREFIX_2 "%s: Starting play mode (codec=%d)\n",p->dev,fmt);
	} else if (p->lastoutput != fmt) {
		vpb_play_buf_finish(p->handle);
		vpb_play_buf_start(p->handle, fmt);
		if(option_verbose>1) 
			ast_verbose( VERBOSE_PREFIX_2 "%s: Changed play format (%d=>%d)\n",p->dev,p->lastoutput,fmt);
	}

	p->lastoutput = fmt;

	// Apply extra gain !
	if( p->txswgain > MAX_VPB_GAIN )
		gain_vector(p->txswgain - MAX_VPB_GAIN , (short*)frame->data, frame->datalen/sizeof(short));

	res = vpb_play_buf_sync(p->handle, (char*)frame->data, frame->datalen);
	if( (res == VPB_OK) && (option_verbose > 5) ) {
		short * data = (short*)frame->data;
		ast_verbose("%s: Write chan (codec=%d) %d %d\n", p->dev, fmt, data[0],data[1]);
	}

	ast_mutex_unlock(&p->play_lock);
//	ast_mutex_unlock(&p->lock);
	return 0;
}

/* Read monitor thread function. */
static void *do_chanreads(void *pvt)
{
	struct vpb_pvt *p = (struct vpb_pvt *)pvt;
	struct ast_frame *fr = &p->fr;
	char *readbuf = ((char *)p->buf) + AST_FRIENDLY_OFFSET;
	int bridgerec = 0;
	int afmt, readlen, res, fmt;
	int ignore_dtmf;
	char * getdtmf_var = NULL;

	fr->frametype = AST_FRAME_VOICE;
	fr->src = type;
	fr->mallocd = 0;
	fr->delivery.tv_sec = 0;
	fr->delivery.tv_usec = 0;
	fr->samples = VPB_SAMPLES;
	fr->offset = AST_FRIENDLY_OFFSET;
	memset(p->buf, 0, sizeof p->buf);

	if (option_verbose > 4) {
		ast_verbose("%s: chanreads: starting thread\n", p->dev);
	}  
	ast_mutex_lock(&p->record_lock);

	p->stopreads = 0; 
	while (!p->stopreads && p->owner) {

		if (option_verbose > 4)
			ast_verbose("%s: chanreads: Starting cycle ...\n", p->dev);
		res = ast_mutex_trylock(&p->lock);
		if (res !=0){
			if (res == EINVAL )
				if (option_verbose > 4) ast_verbose("%s: chanreads: try lock gave me EINVAL[%d]\n", p->dev,res);
			else if (res == EBUSY )
				if (option_verbose > 4) ast_verbose("%s: chanreads: try lock gave me EBUSY[%d]\n", p->dev,res);
			res = ast_mutex_unlock(&p->lock);
			if (res == EINVAL )
				if (option_verbose > 4) ast_verbose("%s: chanreads: Releasing it gave me EINVAL[%d]\n", p->dev,res);
			else if (res == EPERM )
				if (option_verbose > 4) ast_verbose("%s: chanreads: Releasing it gave me EPERM[%d]\n", p->dev,res);
			res = ast_mutex_trylock(&p->lock);
			if (res == EINVAL )
				if (option_verbose > 4) ast_verbose("%s: chanreads: trying again it gave me EINVAL[%d]\n", p->dev,res);
			else if (res == EBUSY )
				if (option_verbose > 4) ast_verbose("%s: chanreads: trying again it gave me EBUSY[%d]\n", p->dev,res);
		}
		if (res==0)  {
			if (option_verbose > 4)
				ast_verbose("%s: chanreads: Checking bridge \n", p->dev);
			if (p->bridge) {
				if (p->bridge->c0 == p->owner && (p->bridge->flags & AST_BRIDGE_REC_CHANNEL_0))
					bridgerec = 1;
				else if (p->bridge->c1 == p->owner && (p->bridge->flags & AST_BRIDGE_REC_CHANNEL_1))
					bridgerec = 1;
				else 
					bridgerec = 0;
			} else {
				bridgerec = 1;
			}
			ast_mutex_unlock(&p->lock);
		} else {
			if (option_verbose > 4)
				ast_verbose("%s: chanreads: Couldnt get channel lock to check bridges!!\n", p->dev);
		}

		if ( (p->owner->_state != AST_STATE_UP) || !bridgerec) {
			if (option_verbose > 4) {
				ast_verbose("%s: chanreads: owner not up[%d] or no bridgerec[%d]\n", p->dev,p->owner->_state,bridgerec);
			}  
			vpb_sleep(10);
			continue;
		}

		// Voicetronix DTMF detection can be triggered off ordinary speech
		// This leads to annoying beeps during the conversation
		// Avoid this problem by just setting VPB_GETDTMF when you want to listen for DTMF
		//ignore_dtmf = 1;
		ignore_dtmf = 0; /* set this to 1 to turn this feature on */
		getdtmf_var = pbx_builtin_getvar_helper(p->owner,"VPB_GETDTMF");
		if( getdtmf_var && ( strcasecmp( getdtmf_var, "yes" ) == 0 ) )
			ignore_dtmf = 0;

		if( ignore_dtmf != p->last_ignore_dtmf ) {
			if(option_verbose>1) 
				ast_verbose( VERBOSE_PREFIX_2 "%s:Now %s DTMF \n",
					p->dev, ignore_dtmf ? "ignoring" : "listening for");
			vpb_set_event_mask(p->handle, ignore_dtmf ? VPB_EVENTS_NODTMF : VPB_EVENTS_ALL );
		}
		p->last_ignore_dtmf = ignore_dtmf;

		// Play DTMF digits here to avoid problem you get if playing a digit during
		// a record operation
		if (option_verbose > 5) {
			ast_verbose("%s: chanreads: Checking dtmf's \n", p->dev);
		}  
		ast_mutex_lock(&p->play_dtmf_lock);
		if( p->play_dtmf[0] ) {
			// Try to ignore DTMF event we get after playing digit
			// This DTMF is played by asterisk and leads to an annoying trailing beep on CISCO phones
			if( !ignore_dtmf) 
				vpb_set_event_mask(p->handle, VPB_EVENTS_NODTMF );
			vpb_dial_sync(p->handle,p->play_dtmf);
			if(option_verbose>1) 
				ast_verbose( VERBOSE_PREFIX_2 "%s: Played DTMF %s\n",p->dev,p->play_dtmf);
			p->play_dtmf[0] = '\0';
			ast_mutex_unlock(&p->play_dtmf_lock);
			vpb_sleep(700); // Long enough to miss echo and DTMF event
			if( !ignore_dtmf) 
				vpb_set_event_mask(p->handle, VPB_EVENTS_ALL );
			continue;
		}
		ast_mutex_unlock(&p->play_dtmf_lock);

		afmt = (p->owner) ? p->owner->pvt->rawreadformat : AST_FORMAT_SLINEAR;
		fmt = ast2vpbformat(afmt);
		if (fmt < 0) {
			ast_log(LOG_WARNING,"%s: Record failure (unsupported format %d)\n", p->dev, afmt);
			return NULL;
		}
		readlen = VPB_SAMPLES * astformatbits(afmt) / 8;

		if (p->lastinput == -1) {
			vpb_record_buf_start(p->handle, fmt);
			vpb_reset_record_fifo_alarm(p->handle);
			if(option_verbose>1) 
				ast_verbose( VERBOSE_PREFIX_2 "%s: Starting record mode (codec=%d)\n",p->dev,fmt);
		} else if (p->lastinput != fmt) {
			vpb_record_buf_finish(p->handle);
			vpb_record_buf_start(p->handle, fmt);
			if(option_verbose>1) 
				ast_verbose( VERBOSE_PREFIX_2 "%s: Changed record format (%d=>%d)\n",p->dev,p->lastinput,fmt);
		}
		p->lastinput = fmt;

		/* Read only if up and not bridged, or a bridge for which we can read. */
		if (option_verbose > 5) {
			ast_verbose("%s: chanreads: getting buffer!\n", p->dev);
		}  
		if( (res = vpb_record_buf_sync(p->handle, readbuf, readlen) ) == VPB_OK ) {
			if (option_verbose > 5) {
				ast_verbose("%s: chanreads: got buffer!\n", p->dev);
			}  
			// Apply extra gain !
			if( p->rxswgain > MAX_VPB_GAIN )
				gain_vector(p->rxswgain - MAX_VPB_GAIN , (short*)readbuf, readlen/sizeof(short));
			if (option_verbose > 5) {
				ast_verbose("%s: chanreads: applied gain\n", p->dev);
			}  

			fr->subclass = afmt;
			fr->data = readbuf;
			fr->datalen = readlen;

			// Using trylock here to prevent deadlock when channel is hungup
			// (ast_hangup() immediately gets lock)
			if (p->owner && !p->stopreads ) {
				if (option_verbose > 5) {
					ast_verbose("%s: chanreads: queueing buffer on frame (state[%d])\n", p->dev,p->owner->_state);
				}  
				
				res = ast_mutex_trylock(&p->owner->lock);
				if (res==0)  {
					ast_queue_frame(p->owner, fr);
					ast_mutex_unlock(&p->owner->lock);
				} else {
					if (res == EINVAL )
						if (option_verbose > 4) ast_verbose("%s: chanreads: try owner->lock gave me EINVAL[%d]\n", p->dev,res);
					else if (res == EBUSY )
						if (option_verbose > 4) ast_verbose("%s: chanreads: try owner->lock gave me EBUSY[%d]\n", p->dev,res);
					res = ast_mutex_unlock(&p->lock);
					if (res == EINVAL )
						if (option_verbose > 4) ast_verbose("%s: chanreads: Releasing pvt->lock it gave me EINVAL[%d]\n", p->dev,res);
					else if (res == EPERM )
						if (option_verbose > 4) ast_verbose("%s: chanreads: Releasing pvt->lock it gave me EPERM[%d]\n", p->dev,res);
					res = ast_mutex_trylock(&p->lock);
					if (res == EINVAL )
						if (option_verbose > 4) ast_verbose("%s: chanreads: trying pvt->lock again it gave me EINVAL[%d]\n", p->dev,res);
					else if (res == EBUSY )
						if (option_verbose > 4) ast_verbose("%s: chanreads: trying pvt->lock again it gave me EBUSY[%d]\n", p->dev,res);
					res = ast_mutex_trylock(&p->owner->lock);
					if (res==0)  {
						ast_queue_frame(p->owner, fr);
						ast_mutex_unlock(&p->owner->lock);
					}
					else {
						if (res == EINVAL )
							if (option_verbose > 4) ast_verbose("%s: chanreads: try owner->lock gave me EINVAL[%d]\n", p->dev,res);
						else if (res == EBUSY )
							if (option_verbose > 4) ast_verbose("%s: chanreads: try owner->lock gave me EBUSY[%d]\n", p->dev,res);
						if (option_verbose > 3) 
							if (option_verbose > 4) ast_verbose("%s: chanreads: Couldnt get lock on owner channel to send frame!\n", p->dev);
						//assert(p->dev!=p->dev);
					}
				}
				if (option_verbose > 6) {
					short * data = (short*)readbuf;
					ast_verbose("%s: Read channel (codec=%d) %d %d\n", p->dev, fmt, data[0], data[1] );
				}  
			}
			else {
				if (option_verbose > 4) {
					ast_verbose("%s: p->stopreads[%d] p->owner[%p]\n", p->dev, p->stopreads,(void *)p->owner);
				}  
			}
		} else {
			ast_log(LOG_WARNING,"%s: Record failure (%s)\n", p->dev, vpb_strerror(res));
			vpb_record_buf_finish(p->handle);
			vpb_record_buf_start(p->handle, fmt);
		}
		if (option_verbose > 4)
			ast_verbose("%s: chanreads: Finished cycle...\n", p->dev);
	}

	/* When stopreads seen, go away! */
	vpb_record_buf_finish(p->handle);
	ast_mutex_unlock(&p->record_lock);

	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "%s: Ending record mode (%d/%s)\n",
			 p->dev, p->stopreads, p->owner? "yes" : "no");     
	return NULL;
}

static struct ast_channel *vpb_new(struct vpb_pvt *i, int state, char *context)
{
	struct ast_channel *tmp; 

	if (i->owner) {
	    ast_log(LOG_WARNING, "Called vpb_new on owned channel (%s) ?!\n", i->dev);
	    return NULL;
	}
	    
	tmp = ast_channel_alloc(1);
	if (tmp) {
		strncpy(tmp->name, i->dev, sizeof(tmp->name) - 1);
		tmp->type = type;
	       
		// Linear is the preferred format. Although Voicetronix supports other formats
		// they are all converted to/from linear in the vpb code. Best for us to use
		// linear since we can then adjust volume in this modules.
		tmp->nativeformats = prefformat;
		tmp->pvt->rawreadformat = AST_FORMAT_SLINEAR;
		tmp->pvt->rawwriteformat =  AST_FORMAT_SLINEAR;
		ast_setstate(tmp, state);
		if (state == AST_STATE_RING)
			tmp->rings = 1;
		tmp->pvt->pvt = i;
		/* set call backs */
		tmp->pvt->send_digit = vpb_digit;
		tmp->pvt->call = vpb_call;
		tmp->pvt->hangup = vpb_hangup;
		tmp->pvt->answer = vpb_answer;
		tmp->pvt->read = vpb_read;
		tmp->pvt->write = vpb_write;
		tmp->pvt->bridge = vpb_bridge;
		tmp->pvt->indicate = vpb_indicate;
		tmp->pvt->fixup = vpb_fixup;
		
		strncpy(tmp->context, context, sizeof(tmp->context)-1);
		if (strlen(i->ext))
			strncpy(tmp->exten, i->ext, sizeof(tmp->exten)-1);
		else
			strncpy(tmp->exten, "s",  sizeof(tmp->exten) - 1);
		if (strlen(i->language))
			strncpy(tmp->language, i->language, sizeof(tmp->language)-1);

		i->owner = tmp;
     
     		i->bridge = NULL;
		i->lastoutput = -1;
		i->lastinput = -1;
		i->last_ignore_dtmf = 1;
		i->readthread = 0;
		i->play_dtmf[0] = '\0';
		
		i->lastgrunt  = get_time_in_ms(); /* Assume at least one grunt tone seen now. */

		ast_mutex_lock(&usecnt_lock);
		usecnt++;
		ast_mutex_unlock(&usecnt_lock);
		ast_update_use_count();
		if (state != AST_STATE_DOWN) {
			if (ast_pbx_start(tmp)) {
				ast_log(LOG_WARNING, "Unable to start PBX on %s\n", tmp->name);
				ast_hangup(tmp);
		   	}
		}
	} else {
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
	}
	return tmp;
}

static struct ast_channel *vpb_request(char *type, int format, void *data) 
{
	int oldformat;
	struct vpb_pvt *p;
	struct ast_channel *tmp = NULL;
	char *name = strdup(data ? (char *)data : "");
	char *s, *sepstr;
	int group=-1;

	oldformat = format;
	format &= prefformat;
	if (!format) {
		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%d'\n", oldformat);
		return NULL;
	}

	sepstr = name;
	s = strsep(&sepstr, "/"); /* Handle / issues */
	if (!s) 
		s = "";
	/* Check if we are looking for a group */
	if (toupper(name[0]) == 'G' || toupper(name[0])=='R') {
		group=atoi(name+1);	
	}
	/* Search for an unowned channel */
	ast_mutex_lock(&iflock); {
		p = iflist;
		while(p) {
			if (group == -1){
				if (strncmp(s, p->dev + 4, sizeof p->dev) == 0) {
					if (!p->owner) {
						tmp = vpb_new(p, AST_STATE_DOWN, p->context);
						break;
					}
				}
			}
			else {
				if ((p->group == group) && (!p->owner)) {
					tmp = vpb_new(p, AST_STATE_DOWN, p->context);
					break;
				}
			}
			p = p->next;
		}
	} ast_mutex_unlock(&iflock);


	if (option_verbose > 1) 
		ast_verbose(VERBOSE_PREFIX_2 " %s requested, got: [%s]\n",
		name, tmp ? tmp->name : "None");

	free(name);

	restart_monitor();
	return tmp;
}

static float parse_gain_value(char *gain_type, char *value)
{
	float gain;

	/* try to scan number */
	if (sscanf(value, "%f", &gain) != 1)
	{
		ast_log(LOG_ERROR, "Invalid %s value '%s' in '%s' config\n", value, gain_type, config);
		return DEFAULT_GAIN;
	}


	/* percentage? */
	//if (value[strlen(value) - 1] == '%')
	//	return gain / (float)100;

	return gain;
}

int load_module()
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct vpb_pvt *tmp;
	int board = 0, group = 0;
	int mode = MODE_IMMEDIATE;
	float txgain = DEFAULT_GAIN, rxgain = DEFAULT_GAIN; 
	float txswgain = 0, rxswgain = 0; 
	int first_channel = 1;
	int echo_cancel = DEFAULT_ECHO_CANCEL;
	int error = 0; /* Error flag */
	int bal1 = -1; // Special value - means do not set
	int bal2 = -1; 
	int bal3 = -1;
	char * callerid = NULL;

	cfg = ast_load(config);

	/* We *must* have a config file otherwise stop immediately */
	if (!cfg) {
		ast_log(LOG_ERROR, "Unable to load config %s\n", config);
		return -1;
	}  

	vpb_seterrormode(VPB_ERROR_CODE);

	ast_mutex_lock(&iflock); {
		v = ast_variable_browse(cfg, "interfaces");
		while(v) {
			/* Create the interface list */
			if (strcasecmp(v->name, "board") == 0) {
				board = atoi(v->value);
			} else  if (strcasecmp(v->name, "group") == 0){
				group = atoi(v->value);
			} else  if (strcasecmp(v->name, "useloopdrop") == 0){
				UseLoopDrop = atoi(v->value);
			} else if (strcasecmp(v->name, "channel") == 0) {
				int channel = atoi(v->value);
				tmp = mkif(board, channel, mode, txgain, rxgain, txswgain, rxswgain, bal1, bal2, bal3, callerid, echo_cancel,group);
				if (tmp) {
					if(first_channel) {
						mkbrd( tmp->vpb_model, echo_cancel );
						first_channel = 0;
					}
					tmp->next = iflist;
					iflist = tmp;
				} else {
					ast_log(LOG_ERROR, 
					"Unable to register channel '%s'\n", v->value);
					error = -1;
					goto done;
				}
				callerid = NULL;
			} else if (strcasecmp(v->name, "language") == 0) {
				strncpy(language, v->value, sizeof(language)-1);
			} else if (strcasecmp(v->name, "callerid") == 0) {
				callerid = strdup(v->value);
			} else if (strcasecmp(v->name, "mode") == 0) {
				if (strncasecmp(v->value, "di", 2) == 0) 
					mode = MODE_DIALTONE;
				else if (strncasecmp(v->value, "im", 2) == 0)
					mode = MODE_IMMEDIATE;
				else if (strncasecmp(v->value, "fx", 2) == 0)
					mode = MODE_FXO;
				else
					ast_log(LOG_WARNING, "Unknown mode: %s\n", v->value);
			} else if (!strcasecmp(v->name, "context")) {
				strncpy(context, v->value, sizeof(context)-1);
			} else if (!strcasecmp(v->name, "echocancel")) {
				if (!strcasecmp(v->value, "off")) 
					echo_cancel = 0;
			} else if (strcasecmp(v->name, "txgain") == 0) {
				txswgain = parse_gain_value(v->name, v->value);
			} else if (strcasecmp(v->name, "rxgain") == 0) {
				rxswgain = parse_gain_value(v->name, v->value);
			} else if (strcasecmp(v->name, "txhwgain") == 0) {
				txgain = parse_gain_value(v->name, v->value);
			} else if (strcasecmp(v->name, "rxhwgain") == 0) {
				rxgain = parse_gain_value(v->name, v->value);
			} else if (strcasecmp(v->name, "bal1") == 0) {
				bal1 = strtol(v->value, NULL, 16);
				if(bal1<0 || bal1>255) {
					ast_log(LOG_WARNING, "Bad bal1 value: %d\n", bal1);
					bal1 = -1;
				}
			} else if (strcasecmp(v->name, "bal2") == 0) {
				bal2 = strtol(v->value, NULL, 16);
				if(bal2<0 || bal2>255) {
					ast_log(LOG_WARNING, "Bad bal2 value: %d\n", bal2);
					bal2 = -1;
				}
			} else if (strcasecmp(v->name, "bal3") == 0) {
				bal3 = strtol(v->value, NULL, 16);
				if(bal3<0 || bal3>255) {
					ast_log(LOG_WARNING, "Bad bal3 value: %d\n", bal3);
					bal3 = -1;
				}
			} else if (strcasecmp(v->name, "grunttimeout") == 0) {
				gruntdetect_timeout = 1000*atoi(v->value);
			}
			v = v->next;
		}

		if (gruntdetect_timeout < 1000)
			gruntdetect_timeout = 1000;

		done: (void)0;
	} ast_mutex_unlock(&iflock);

	ast_destroy(cfg);

	if (!error && ast_channel_register(type, tdesc, prefformat, vpb_request) != 0) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		error = -1;
	}


	if (error)
		unload_module();
	else 
		restart_monitor(); /* And start the monitor for the first time */

	return error;
}


int unload_module()
{
	struct vpb_pvt *p;
	/* First, take us out of the channel loop */
	ast_channel_unregister(type);

	ast_mutex_lock(&iflock); {
		/* Hangup all interfaces if they have an owner */
		p = iflist;
		while(p) {
			if (p->owner)
				ast_softhangup(p->owner, AST_SOFTHANGUP_APPUNLOAD);
			p = p->next;
		}
		iflist = NULL;
	} ast_mutex_unlock(&iflock);

	ast_mutex_lock(&monlock); {
		if (mthreadactive > -1) {
			pthread_cancel(monitor_thread);
			pthread_join(monitor_thread, NULL);
		}
		mthreadactive = -2;
	} ast_mutex_unlock(&monlock);

	ast_mutex_lock(&iflock); {
		/* Destroy all the interfaces and free their memory */

		while(iflist) {
			p = iflist;		    
			ast_mutex_destroy(&p->lock);
			pthread_cancel(p->readthread);
			ast_mutex_destroy(&p->owner_lock);
			ast_mutex_destroy(&p->record_lock);
			ast_mutex_destroy(&p->play_lock);
			ast_mutex_destroy(&p->play_dtmf_lock);
			p->readthread = 0;

			vpb_close(p->handle);

			iflist = iflist->next;

			free(p);
		}
		iflist = NULL;
	} ast_mutex_unlock(&iflock);

	ast_mutex_lock(&bridge_lock); {
		memset(bridges, 0, sizeof bridges);	     
	} ast_mutex_unlock(&bridge_lock);
	ast_mutex_destroy(&bridge_lock);
	for(int i = 0; i < max_bridges; i++ ) {
		ast_mutex_destroy(&bridges[i].lock);
		pthread_cond_destroy(&bridges[i].cond);
	}
	free(bridges);

	return 0;
}

int usecount()
{
	int res;
	ast_mutex_lock(&usecnt_lock);
	res = usecnt;
	ast_mutex_unlock(&usecnt_lock);
	return res;
}

char *description()
{
	return desc;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}

#if defined(__cplusplus) || defined(c_plusplus)
 }
#endif
