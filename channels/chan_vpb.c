/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * VoiceTronix Interface driver
 * 
 * Copyright (C) 2003, Paul Bagyenda
 *
 * Paul Bagyenda <bagyenda@dsmagic.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <asterisk/lock.h>
#include <asterisk/channel.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/config.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <vpbapi.h>


#define DEFAULT_GAIN 1.0
#define VPB_SAMPLES 240 
#define VPB_MAX_BUF VPB_SAMPLES*4 + AST_FRIENDLY_OFFSET

#define VPB_NULL_EVENT 200

#define VPB_DIALTONE_WAIT 2000
#define VPB_RINGWAIT 2000
#define VPB_WAIT_TIMEOUT 40

#if defined(__cplusplus) || defined(c_plusplus)
 extern "C" {
#endif

static char *desc = "VoiceTronix V6PCI/V12PCI  API Support";
static char *type = "vpb";
static char *tdesc = "Standard VoiceTronix API Driver";
static char *config = "vpb.conf";

/* Default context for dialtone mode */
static char context[AST_MAX_EXTENSION] = "default";

/* Default language */
static char language[MAX_LANGUAGE] = "";
static int usecnt =0;

static int echocancel = 1; 
static int setrxgain = 0, settxgain = 0;

static int tcounter  = 0;

static int gruntdetect_timeout = 5000; /* Grunt detect timeout is 5 seconds. */

static int silencesupression = 0;

static const int prefformat = AST_FORMAT_ALAW | AST_FORMAT_SLINEAR | AST_FORMAT_ULAW | AST_FORMAT_ADPCM;

static ast_mutex_t usecnt_lock = AST_MUTEX_INITIALIZER;

/* Protect the interface list (of vpb_pvt's) */
static ast_mutex_t iflock = AST_MUTEX_INITIALIZER;

/* Protect the monitoring thread, so only one process can kill or start it, and not
   when it's doing something critical. */
static ast_mutex_t monlock = AST_MUTEX_INITIALIZER;

/* This is the thread for the monitor which checks for input on the channels
   which are not currently in use.  */
static pthread_t monitor_thread;

static int mthreadactive = -1; /* Flag for monitoring monitorthread.*/


static int restart_monitor(void);

/* The private structures of the VPB channels are linked for
   selecting outgoing channels */
   
#define MODE_DIALTONE 	1
#define MODE_IMMEDIATE	2
#define MODE_FXO	3


static VPB_TONE Dialtone     = {440, 440, 440, 0,  0, 0, 5000, 0   };
static VPB_TONE Busytone     = {440,   0,   0, 0,  -100, -100,   500, 500};
static VPB_TONE Ringbacktone = {440,   0,   0, 0,  -100, -100,  100, 100};


#define VPB_MAX_BRIDGES 128 
static struct vpb_bridge_t {
     int inuse;
     struct ast_channel *c0, *c1, **rc;
     struct ast_frame **fo;
     int flags;
     
     ast_mutex_t lock;
     pthread_cond_t cond;
} bridges[VPB_MAX_BRIDGES]; /* Bridges...*/

static ast_mutex_t bridge_lock = AST_MUTEX_INITIALIZER;

static struct vpb_pvt {

     struct ast_channel *owner;		/* Channel we belong to, possibly NULL */
     int mode;						/* Is this in the  */
     int handle;  /* Handle for vpb interface */

     char dev[256];

     struct ast_frame f, fr;
     char buf[VPB_MAX_BUF];			/* Static buffer for reading frames */
     char obuf[VPB_MAX_BUF];
     
     int dialtone;
     float txgain, rxgain;             /* gain control for playing, recording  */
     
     int wantdtmf; /* Waiting for DTMF. */
     int silencesupression;
     char context[AST_MAX_EXTENSION];

     char ext[AST_MAX_EXTENSION];
     char language[MAX_LANGUAGE];
     char callerid[AST_MAX_EXTENSION];

     int lastinput;
     int lastoutput;

     struct vpb_bridge_t *bridge;
     void *timer; /* For call timeout. */
     int calling;
    
     int lastgrunt;

     ast_mutex_t lock;

    int stopreads; /* Stop reading...*/
     pthread_t readthread;    /* For monitoring read channel. One per owned channel. */

     struct vpb_pvt *next;			/* Next channel in list */
} *iflist = NULL;

static char callerid[AST_MAX_EXTENSION];

static int vpb_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc)
{
     struct vpb_pvt *p0 = (struct vpb_pvt *)c0->pvt->pvt;
     struct vpb_pvt *p1 = (struct vpb_pvt *)c1->pvt->pvt;
     int i, len = sizeof bridges/sizeof bridges[0], res;
     
     /* Bridge channels, check if we can.  I believe we always can, so find a slot.*/
     
     ast_mutex_lock(&bridge_lock); {
	  for (i = 0; i < len; i++) 
	       if (!bridges[i].inuse)
		    break;
	  if (i < len) {
	       bridges[i].inuse = 1;
	       bridges[i].flags = flags;
	       bridges[i].rc = rc;
	       bridges[i].fo = fo;
	       bridges[i].c0 = c0;
	       bridges[i].c1 = c1;
	       ast_mutex_init(&bridges[i].lock);
	       pthread_cond_init(&bridges[i].cond, NULL);
	  } 	       
     } ast_mutex_unlock(&bridge_lock); 

     if (i == len) {
	  ast_log(LOG_WARNING, "Failed to bridge %s and %s!\n", c0->name, c1->name);
	  return -2;
     } else {

	  /* Set bridge pointers. You don't want to take these locks while holding bridge lock.*/
	  ast_mutex_lock(&p0->lock); {
	       p0->bridge = &bridges[i];
	  } ast_mutex_unlock(&p0->lock);

	  ast_mutex_lock(&p1->lock); {
	       p1->bridge = &bridges[i];
	  } ast_mutex_unlock(&p1->lock);

	  if (option_verbose > 4) 
	       ast_verbose(VERBOSE_PREFIX_3 
			   " Bridging call entered with [%s, %s]\n", c0->name, c1->name);
     }
     res = vpb_bridge(p0->handle, p1->handle, VPB_BRIDGE_ON, 0);

     if (res != VPB_OK) 
	  goto done;

     res = pthread_cond_wait(&bridges[i].cond, &bridges[i].lock); /* Wait for condition signal. */
     
     
 done: /* Out of wait. */

     vpb_bridge(p0->handle, p1->handle, VPB_BRIDGE_OFF, 0); 

     
     ast_mutex_lock(&bridge_lock); {
	  bridges[i].inuse = 0;
	  ast_mutex_destroy(&bridges[i].lock);
	  pthread_cond_destroy(&bridges[i].cond);	
     } ast_mutex_unlock(&bridge_lock); 
     
     ast_mutex_lock(&p0->lock); {
	  p0->bridge = NULL;
     } ast_mutex_unlock(&p0->lock);
     
     ast_mutex_lock(&p1->lock); {
	  p1->bridge = NULL;
     } ast_mutex_unlock(&p1->lock);

     
     if (option_verbose > 4) 
	  ast_verbose(VERBOSE_PREFIX_3 
		      " Bridging call done with [%s, %s] => %d\n", c0->name, c1->name, res);
    
     if (res != 0 && res != VPB_OK) /* Don't assume VPB_OK is zero! */
	  return -1;
     else 
	  return 0;
}

static inline int monitor_handle_owned(struct vpb_pvt *p, VPB_EVENT *e)
{
     struct ast_frame f = {AST_FRAME_CONTROL}; /* default is control, Clear rest. */
     int endbridge = 0;

     if (option_verbose > 4) 
	  ast_verbose(VERBOSE_PREFIX_3 " %s handle_owned got event: [%d=>%d]\n",
		      p->dev, e->type, e->data);
     
     f.src = type;
     switch (e->type) {
     case VPB_RING:
	  if (p->mode == MODE_FXO) 
	       f.subclass = AST_CONTROL_RING;
	  else
	       f.frametype = -1; /* ignore ring on station port. */
	  break;
     case VPB_TIMEREXP:
	  if (p->calling) { /* This means time to stop calling. */
	       f.subclass = AST_CONTROL_BUSY;
	       vpb_timer_close(p->timer);
	  } else
	       f.frametype = -1; /* Ignore. */
	  break;
     case VPB_DTMF:
	  if (p->owner->_state == AST_STATE_UP) {
	       f.frametype = AST_FRAME_DTMF;
	       f.subclass = e->data;
	  } else
	       f.frametype = -1;
	  break;

     case VPB_TONEDETECT:
	  if (e->data == VPB_BUSY || e->data == VPB_BUSY_308)
	       f.subclass = AST_CONTROL_BUSY;
	  else if (e->data == VPB_GRUNT) {
	       p->lastgrunt = tcounter;
	       f.frametype = -1;
	  } else
	       f.frametype = -1;
	  break;

     case VPB_CALLEND:
	  if (e->data == VPB_CALL_CONNECTED)
	       f.subclass = AST_CONTROL_ANSWER;
	  else if (e->data == VPB_CALL_NO_DIAL_TONE ||
		   e->data == VPB_CALL_NO_RING_BACK)
	       f.subclass =  AST_CONTROL_CONGESTION;
	  else if (e->data == VPB_CALL_NO_ANSWER ||
		   e->data == VPB_CALL_BUSY)
	       f.subclass = AST_CONTROL_BUSY;
	  else if (e->data  == VPB_CALL_DISCONNECTED) 
	       f.subclass = AST_CONTROL_HANGUP;
	  break;

     case VPB_STATION_OFFHOOK:
	   f.subclass = AST_CONTROL_ANSWER;
	  break;
	  
     case VPB_STATION_ONHOOK:
	  f.subclass = AST_CONTROL_HANGUP;
	  break;
	  
     case VPB_STATION_FLASH:
	  f.subclass = AST_CONTROL_FLASH;
	  break;
	
     case VPB_DIALEND:
	  f.subclass = AST_CONTROL_ANSWER;
	  break;
	  
     default:
	  f.frametype = -1;
	  break;
     }

     if (option_verbose > 4) 
	  ast_verbose(VERBOSE_PREFIX_3 " handle_owned: putting frame: [%d=>%d], bridge=%p\n",
		      f.frametype, f.subclass, (void *)p->bridge);

     ast_mutex_lock(&p->lock); {
	  if (p->bridge) { /* Check what happened, see if we need to report it. */
	       switch (f.frametype) { 
	       case AST_FRAME_DTMF:
		    if (!(p->bridge->c0 == p->owner && 
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
			 pthread_cond_signal(&p->bridge->cond);
		    } ast_mutex_unlock(&p->bridge->lock); 	       		   
	       }	  
	  }
     } ast_mutex_unlock(&p->lock);
     
     if (endbridge) return 0;
     
     if (f.frametype >= 0 && f.frametype != AST_FRAME_NULL) 
	  ast_queue_frame(p->owner, &f, 0);
     return 0;
}

static struct ast_channel *vpb_new(struct vpb_pvt *i, int state, char *context);

static inline int monitor_handle_notowned(struct vpb_pvt *p, VPB_EVENT *e)
{
     char s[2] = {0};

     if (option_verbose > 4) 
	  ast_verbose(VERBOSE_PREFIX_3 " %s: In not owned, mode=%d, [%d=>%d]\n",
		      p->dev, p->mode, e->type, e->data);
          
     switch(e->type) {
     case VPB_RING:
	  if (p->mode == MODE_FXO) /* FXO port ring, start * */
	       vpb_new(p, AST_STATE_RING, p->context);
	  break;
     case VPB_STATION_OFFHOOK:
	  if (p->mode == MODE_IMMEDIATE) 
	       vpb_new(p,AST_STATE_RING, p->context);
	  else {
	       vpb_playtone_async(p->handle, &Dialtone);
	       p->wantdtmf = 1;
	       p->ext[0] = 0; /* Just to be sure & paranoid.*/
	  }
	  break;

     case VPB_STATION_ONHOOK: /*, clear ext */
           while (vpb_playtone_state(p->handle) != 0){
                 vpb_tone_terminate(p->handle);
                 vpb_sleep(10);
             }
	  p->wantdtmf = 1;
	  p->ext[0] = 0;
	  break;

     case VPB_DTMF:
	  if (p->wantdtmf == 1) {
           while (vpb_playtone_state(p->handle) != 0){
                 vpb_tone_terminate(p->handle);
                 vpb_sleep(10);
             }

	       p->wantdtmf = 0;
	  }
	  s[0] = e->data;
	  strcat(p->ext, s);
	  
	  if (ast_exists_extension(NULL, p->context, p->ext, 1, p->callerid)) 
	       vpb_new(p,AST_STATE_RING, p->context);
	  else if (!ast_canmatch_extension(NULL, p->context, p->ext, 1, p->callerid)) 
	       if (ast_exists_extension(NULL, "default", p->ext, 1, p->callerid)) 
		    vpb_new(p,AST_STATE_RING, "default");	      
	       else if (!ast_canmatch_extension(NULL, "default", p->ext, 1, p->callerid)) {
		    if (option_debug)
			 ast_log(LOG_DEBUG, 
				 "%s can't match anything in %s or default\n", 
				 p->ext, p->context);
		    vpb_playtone_async(p->handle, &Busytone);
	       }
	  
	  break;
	  
     default:
	  /* Ignore.*/
	  break;
     }
     
     if (option_verbose > 4) 
	 ast_verbose(VERBOSE_PREFIX_3 " %s: Done not owned, mode=%d, [%d=>%d]\n",
		      p->dev, p->mode, e->type, e->data);
     
     return 0;
 }

static void *do_monitor(void *unused)
{
    
     /* Monitor thread, doesn't die until explicitly killed. */
     
     if (option_verbose > 4) 
	  ast_verbose(VERBOSE_PREFIX_3 "Starting vpb monitor thread[%ld]\n",
		      pthread_self());
     pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
     
     do {
	  VPB_EVENT e;
	  char str[VPB_MAX_STR];
	  
	  int res = vpb_get_event_sync(&e, VPB_WAIT_TIMEOUT);

	  if (res != VPB_OK)
	       goto end_loop;
	  
	  str[0] = 0;
	  ast_mutex_lock(&monlock),
	       ast_mutex_lock(&iflock); {
	       struct vpb_pvt *p = iflist; /* Find the pvt structure */	      
		
	       vpb_translate_event(&e, str);
	       
	       if (e.type == VPB_NULL_EVENT) 
		    goto done; /* Nothing to do, just a wakeup call.*/
	       while (p && p->handle != e.handle)
		    p = p->next;
	       
	       if (option_verbose > 2)
		    ast_verbose(VERBOSE_PREFIX_3 " Event [%d=>%s] on %s\n", 
				e.type, str, p ? p->dev : "null");
	       
	       if (!p) {
			   ast_log(LOG_WARNING, 
				   "Got event %s, no matching iface!\n", str);    
			   goto done;
	       } 
	       
	       
	       /* Two scenarios: Are you owned or not. */
	       
	       if (p->owner) 
			monitor_handle_owned(p, &e);
	       else 
		    monitor_handle_notowned(p, &e);
	       
	       done: (void)0;
	  } ast_mutex_unlock(&iflock);
	  ast_mutex_unlock(&monlock);
	  
     end_loop:
	  tcounter += VPB_WAIT_TIMEOUT; /* Ok, not quite but will suffice. */
	  
     } while(1);
     
     
     return NULL;
}

static int restart_monitor(void)
{
     int error = 0;
     
     /* If we're supposed to be stopped -- stay stopped */
     if (mthreadactive == -2)
	  return 0;
     ast_mutex_lock(&monlock); {
	  if (monitor_thread == pthread_self()) {
	       ast_log(LOG_WARNING, "Cannot kill myself\n");
	       error = -1;
	       goto done;
	  }
	  if (mthreadactive != -1) {
	       /* Why do other drivers kill the thread? No need says I, simply awake thread with event. */
	       VPB_EVENT e;
	       e.handle = 0;
	       e.type = VPB_NULL_EVENT;
	       e.data = 0;
	       
	       vpb_put_event(&e);
	  } else {
	       /* Start a new monitor */
	       int pid = pthread_create(&monitor_thread, NULL, do_monitor, NULL); 
	       if (pid < 0) {
		    ast_log(LOG_ERROR, "Unable to start monitor thread.\n");
		    error = -1;
		    goto done;
	       } else
		    mthreadactive = 0; /* Started the thread!*/

#if 0	       
	       if (option_verbose > 4) 
		    ast_verbose(VERBOSE_PREFIX_3 
				"Starting vpb restast: thread[%d]\n",
				pid);
#endif
	  }
     done: (void)0;
     } ast_mutex_unlock(&monlock);
     
     return error;
}

struct vpb_pvt *mkif(int board, int channel, int mode, float txgain, float rxgain)
{
     struct vpb_pvt *tmp;


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
          
     if (echocancel) {
	  if (option_verbose > 4)
	       ast_verbose(VERBOSE_PREFIX_3 " vpb turned on echo cancel.\n");
	  vpb_echo_canc_enable();
	  vpb_echo_canc_force_adapt_on();
	  echocancel = 0; /* So we do not initialise twice! */
     }
     
     if (option_verbose > 4) 
	  ast_verbose(VERBOSE_PREFIX_3 " vpb created channel: [%d:%d]\n",
		      board, channel);

     sprintf(tmp->dev, "vpb/%d-%d", board, channel);

     tmp->mode = mode;

     strcpy(tmp->language, language);
     strcpy(tmp->context, context);
     
     strcpy(tmp->callerid, callerid);
     tmp->txgain = txgain;

     tmp->rxgain = rxgain;

     tmp->lastinput = -1;
     tmp->lastoutput = -1;

     tmp->bridge = NULL;

     tmp->readthread = 0;

     ast_mutex_init(&tmp->lock);

     if (setrxgain)      
	  vpb_record_set_gain(tmp->handle, rxgain);
     if (settxgain)  
	  vpb_play_set_gain(tmp->handle, txgain);
     
     return tmp;
}


static int vpb_indicate(struct ast_channel *ast, int condition)
{
    struct vpb_pvt *p = (struct vpb_pvt *)ast->pvt->pvt;
    int res = 0;

    if (option_verbose > 4)
	ast_verbose(VERBOSE_PREFIX_3 " vpb indicate on %s with %d\n",
		    p->dev, condition);

    switch(condition) {
	case AST_CONTROL_BUSY:
	case AST_CONTROL_CONGESTION:
          while (vpb_playtone_state(p->handle) != 0){
                res = vpb_tone_terminate(p->handle);
                vpb_sleep(10);
            }
	    res = vpb_playtone_async(p->handle, &Busytone);
	    break;
	case AST_CONTROL_RINGING:
          while (vpb_playtone_state(p->handle) != 0){
                res = vpb_tone_terminate(p->handle);
                vpb_sleep(10);
            }
	    res = vpb_playtone_async(p->handle, &Ringbacktone);
	    break;	    
	case AST_CONTROL_ANSWER:
	case -1: /* -1 means stop playing? */
           while (vpb_playtone_state(p->handle) != 0){
                 res = vpb_tone_terminate(p->handle);
                 vpb_sleep(10);
	   }

	    break;
	case AST_CONTROL_HANGUP:
          while (vpb_playtone_state(p->handle) != 0){
                res = vpb_tone_terminate(p->handle);
                vpb_sleep(10);
            }
	    res = vpb_playtone_async(p->handle, &Busytone);
	    break;
	    
	default:
	    res = 0;
	    break;
    }
    return res;
}

static int vpb_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct vpb_pvt *p = (struct vpb_pvt *)newchan->pvt->pvt;

	ast_log(LOG_DEBUG, 
		"New owner for channel %s is %s\n", p->dev, newchan->name);
	if (p->owner == oldchan)
		p->owner = newchan;

	if (newchan->_state == AST_STATE_RINGING) 
		vpb_indicate(newchan, AST_CONTROL_RINGING);

	return 0;
}

static int vpb_digit(struct ast_channel *ast, char digit)
{
    char s[2];

    s[0] = digit;
    s[1] = '\0';
   
    return vpb_dial_sync(((struct vpb_pvt *)ast->pvt->pvt)->handle, s);
}


static int vpb_call(struct ast_channel *ast, char *dest, int timeout)
{
    struct vpb_pvt *p = (struct vpb_pvt *)ast->pvt->pvt;
    int res = 0;
    char *s = strrchr(dest, '/');

    if (s)
	 s = s + 1;
    else
	 s = dest;

    if (ast->_state != AST_STATE_DOWN && ast->_state != AST_STATE_RESERVED) {
	ast_log(LOG_WARNING, "vpb_call on %s neither down nor reserved!\n", 
		ast->name);
	return -1;
    }
    if (p->mode != MODE_FXO)  /* Station port, ring it. */
	res = vpb_ring_station_async(p->handle, VPB_RING_STATION_ON,'1');       
     else {
	  VPB_CALL call;

	  vpb_get_call(p->handle, &call);
	  
	  call.dialtone_timeout = VPB_DIALTONE_WAIT;
	  call.answer_timeout = timeout;
	  call.ringback_timeout = VPB_RINGWAIT;

	  vpb_set_call(p->handle, &call);

	  if (option_verbose > 2)
	       ast_verbose(VERBOSE_PREFIX_3 " Calling %s on %s \n", 
			   dest, ast->name); 

	  vpb_sethook_sync(p->handle,VPB_OFFHOOK);
	  
	  res = vpb_dial_async(p->handle, s);

	  if (res != VPB_OK) {
	    ast_log(LOG_DEBUG, "Call on %s to %s failed: %s\n", 
		    ast->name, dest, vpb_strerror(res));	      
	    res = -1;
	  } else 
	    res = 0;
    }

    if (option_verbose > 2)
	ast_verbose(VERBOSE_PREFIX_3 
		    " VPB Calling %s [t=%d] on %s returned %d\n", 
		    dest, timeout, ast->name, res); 

    if (res == 0) {
	 if (timeout) {
	      vpb_timer_open(&p->timer, p->handle, 0, 100*timeout);
	      vpb_timer_start(p->timer);
	 }
	 p->calling = 1;
	ast_setstate(ast, AST_STATE_RINGING);
	ast_queue_control(ast,AST_CONTROL_RINGING, 0);		
    }

    return res;
}

static int vpb_hangup(struct ast_channel *ast)
{
    struct vpb_pvt *p;

    if (option_verbose > 2) 
	ast_verbose(VERBOSE_PREFIX_3 " hangup on vpb (%s)\n", ast->name);
    
    if (!ast->pvt || !ast->pvt->pvt) {
	ast_log(LOG_WARNING, "channel (%s) not connected?\n", ast->name);
	return 0;
    }

    p = (struct vpb_pvt *)ast->pvt->pvt;

    vpb_play_terminate(p->handle);
    vpb_record_terminate(p->handle);
    
    if (p->mode != MODE_FXO) { /* station port. */
	vpb_ring_station_async(p->handle, VPB_RING_STATION_OFF,'1');	
	vpb_playtone_async(p->handle, &Busytone);

    } else
	vpb_sethook_sync(p->handle, VPB_ONHOOK);

    ast_setstate(ast,AST_STATE_DOWN);
    
    ast_mutex_lock(&p->lock); {    
	 p->lastinput = p->lastoutput  = -1;
	 p->ext[0]  = 0;
	 p->owner = NULL;
	 p->dialtone = 0;
	 p->calling = 0;
	 ast->pvt->pvt = NULL; 	 
    } ast_mutex_unlock(&p->lock);
	 
    ast_mutex_lock(&usecnt_lock); {
	usecnt--;
    } ast_mutex_unlock(&usecnt_lock);
    ast_update_use_count();

    /* Stop thread doing reads. */
    p->stopreads = 1;
    pthread_join(p->readthread, NULL); 

    if (option_verbose > 2)
	ast_verbose(VERBOSE_PREFIX_3 " Hungup on %s complete\n", ast->name);
    
    restart_monitor();
    return 0;
}

static int vpb_answer(struct ast_channel *ast)
{
    struct vpb_pvt *p = (struct vpb_pvt *)ast->pvt->pvt;

    if (p->mode == MODE_FXO)
	vpb_sethook_sync(p->handle, VPB_OFFHOOK);
    
    if (option_debug)
	ast_log(LOG_DEBUG, "vpb answer on %s\n", ast->name);
    ast->rings = 0;
    ast_setstate(ast, AST_STATE_UP);
    
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

static int vpb_write(struct ast_channel *ast, struct ast_frame *frame)
{
    struct vpb_pvt *p = (struct vpb_pvt *)ast->pvt->pvt; 
    int res = 0, fmt = 0;

    if (frame->frametype != AST_FRAME_VOICE) {
	ast_log(LOG_WARNING, "Don't know how to handle from type %d\n",
		frame->frametype);
	return 0;
    } else if (ast->_state != AST_STATE_UP) {
	if (option_verbose  > 4)
	    ast_log(LOG_WARNING, "Writing frame type [%d,%d] on chan %s not up\n",
		    frame->frametype, frame->subclass, ast->name);
	return 0;
	
    }
    

    fmt = ast2vpbformat(frame->subclass);

    if (option_verbose > 4)
	ast_verbose(VERBOSE_PREFIX_3 
		    " Write chan %s: got frame type = %d\n",
		    p->dev, frame->subclass);
       
    if (fmt < 0) {
	ast_log(LOG_WARNING, "vpb_write Cannot handle frames of %d format!\n", 
		frame->subclass);
	return -1;
    }
    

    if (p->lastoutput != fmt) {
	 vpb_play_buf_start(p->handle, fmt);
	 p->lastoutput = fmt;
    }
    
    memcpy(p->obuf, frame->data, 
	   (frame->datalen > (int)sizeof p->obuf) ? sizeof p->obuf : frame->datalen);
    res = vpb_play_buf_sync(p->handle, p->obuf, frame->datalen);
    if (res != VPB_OK)
	 return -1;
    else 
	 return 0;
}

/* Read monitor thread function. */
static void *do_chanreads(void *pvt)
{
     struct vpb_pvt *p = (struct vpb_pvt *)pvt;
     struct ast_frame *fr = &p->fr;
     char *readbuf = ((char *)p->buf) + AST_FRIENDLY_OFFSET;
     int bridgerec = 0;
 
     fr->frametype = AST_FRAME_VOICE;
     fr->src = type;
     fr->mallocd = 0;
     
     memset(p->buf, 0, sizeof p->buf);

     while (!p->stopreads && p->owner) {	 
	 int res = -1, fmt;
	 struct ast_channel *owner = p->owner;
	 int afmt = (owner) ? owner->pvt->rawreadformat : AST_FORMAT_ALAW;
	 int state = (owner) ? owner->_state : AST_STATE_DOWN;
	 int readlen;	
	 
	 fmt = ast2vpbformat(afmt);
	 
	 if (fmt < 0) {
	     p->stopreads = 1;
	     goto done;
	 }

	 readlen = VPB_SAMPLES * astformatbits(afmt) / 8;

	 if (p->lastinput != fmt) {
	     if (option_verbose > 2) 
		 ast_verbose(" Read_channel ##  %s: Setting record mode, bridge = %d\n", 
			     p->dev, p->bridge ? 1 : 0);     	     
	     vpb_record_buf_start(p->handle, fmt);
	     p->lastinput = fmt;
	 } 

	 ast_mutex_lock(&p->lock); {
	      if (p->bridge) 
		   if (p->bridge->c0 == p->owner && 
		       (p->bridge->flags & AST_BRIDGE_REC_CHANNEL_0))
			bridgerec = 1;
		   else if (p->bridge->c1 == p->owner && 
		       (p->bridge->flags & AST_BRIDGE_REC_CHANNEL_1))
			bridgerec = 1;
		   else 
			bridgerec = 0;
	      else
		   bridgerec = 1;
	 } ast_mutex_unlock(&p->lock);
	 
	 if (state == AST_STATE_UP && bridgerec) {  
             /* Read only if up and not bridged, or a bridge for which we can read. */
	      res = vpb_record_buf_sync(p->handle, readbuf, readlen);	      
	}  else {
	      res = 0;
	      vpb_sleep(10);
	 }
	 if (res == VPB_OK) {
	      fr->subclass = afmt;
	      fr->samples = VPB_SAMPLES;
	      fr->data = readbuf;
	      fr->datalen = readlen;
	      fr->offset = AST_FRIENDLY_OFFSET;

	      ast_mutex_lock(&p->lock); {    
		   if (p->owner) ast_queue_frame(p->owner, fr, 0);
	      } ast_mutex_unlock(&p->lock);
	 } else 
	      p->stopreads = 1;
	 	 
     done: (void)0;
	 if (option_verbose > 4)
	     ast_verbose(" Read_channel  %s (state=%d), res=%d, bridge=%d\n", 
			 p->dev, state, res, bridgerec);     
     }
     
     /* When stopreads seen, go away! */
     vpb_record_buf_finish(p->handle);

     if (option_verbose > 4)
	 ast_verbose(" Read_channel  %s terminating, stopreads=%d, owner=%s\n", 
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
		strncpy(tmp->name, i->dev, sizeof(tmp->name));
		tmp->type = type;
	       
		tmp->nativeformats = prefformat;
		tmp->pvt->rawreadformat = AST_FORMAT_ALAW;
		tmp->pvt->rawwriteformat =  AST_FORMAT_ALAW;
		ast_setstate(tmp, state);
		if (state == AST_STATE_RING)
			tmp->rings = 1;
		tmp->pvt->pvt = i;
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
		if (strlen(i->callerid))
			tmp->callerid = strdup(i->callerid);
		i->owner = tmp;

		
		i->lastinput = i->lastoutput = -1;
		i->lastgrunt  = tcounter; /* Assume at least one grunt tone seen now. */

		ast_mutex_lock(&usecnt_lock);
		usecnt++;
		ast_mutex_unlock(&usecnt_lock);
		ast_update_use_count();
		if (state != AST_STATE_DOWN) 
		     if (ast_pbx_start(tmp)) {
			  ast_log(LOG_WARNING, "Unable to start PBX on %s\n", tmp->name);
			  ast_hangup(tmp);
		     }

		i->stopreads = 0; /* So read thread runs. */	
		/* Finally start read monitoring thread. */
		pthread_create(&i->readthread, NULL, do_chanreads, (void *)i);
	} else
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
	return tmp;
}

static struct ast_channel *vpb_request(char *type, int format, void *data) 
{
     int oldformat;
     struct vpb_pvt *p;
     struct ast_channel *tmp = NULL;
     char *name = strdup(data ? (char *)data : "");
     char *s, *sepstr;
     
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
     /* Search for an unowned channel */
     ast_mutex_lock(&iflock); {
	  p = iflist;
	  while(p) {
	       if (strncmp(s, p->dev + 4, sizeof p->dev) == 0) 
		    if (!p->owner) {
			 tmp = vpb_new(p, AST_STATE_DOWN, p->context);
			 break;
		    }
	       p = p->next;
	  }
     } ast_mutex_unlock(&iflock);


     if (option_verbose > 2) 
	  ast_verbose(VERBOSE_PREFIX_3 " %s requested, got: [%s]\n",
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
		ast_log(LOG_ERROR, "Invalid %s value '%s' in '%s' config\n",
			value, gain_type, config);
		return DEFAULT_GAIN;
	}


	/* percentage? */
	if (value[strlen(value) - 1] == '%')
		return gain / (float)100;

	return gain;
}

static int __unload_module(void)
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
		     p->readthread = 0;
		     
		     iflist = iflist->next;
		     
		     free(p);
		}
		iflist = NULL;
	} ast_mutex_unlock(&iflock);
	
	ast_mutex_lock(&bridge_lock); {
	     memset(bridges, 0, sizeof bridges);	     
	} ast_mutex_unlock(&bridge_lock);
	ast_mutex_destroy(&bridge_lock);

	tcounter = 0;
	
	return 0;
}

int unload_module(void)
{
	return __unload_module();
}

int load_module()
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct vpb_pvt *tmp;
	int board = 0, group = 0;
	int mode = MODE_IMMEDIATE;
        float txgain = DEFAULT_GAIN, rxgain = DEFAULT_GAIN; 
	
	int error = 0; /* Error flag */


	setrxgain = settxgain = 0;

	cfg = ast_load(config);
	
	/* We *must* have a config file otherwise stop immediately */
	if (!cfg) {
	     ast_log(LOG_ERROR, "Unable to load config %s\n", config);
	     return -1;
	}  
	
	vpb_seterrormode(VPB_DEVELOPMENT);
	
	ast_mutex_lock(&iflock); {
	     v = ast_variable_browse(cfg, "interfaces");
	     while(v) {
		  /* Create the interface list */
		  if (strcasecmp(v->name, "board") == 0) 
		       board = atoi(v->value);
		  else  if (strcasecmp(v->name, "group") == 0)
		       group = atoi(v->value);
		  else if (strcasecmp(v->name, "channel") == 0) {
		       int channel = atoi(v->value);
		       tmp = mkif(board, channel, mode, txgain, rxgain);
		       if (tmp) {
			    tmp->next = iflist;
			    iflist = tmp;
			    
		       } else {
			    ast_log(LOG_ERROR, 
				    "Unable to register channel '%s'\n", v->value);
			    error = -1;
			    goto done;
			    
		       }
		  } else if (strcasecmp(v->name, "silencesupression") == 0) 
		       silencesupression = ast_true(v->value);
		  else if (strcasecmp(v->name, "language") == 0) 
		       strncpy(language, v->value, sizeof(language)-1);
		  else if (strcasecmp(v->name, "callerid") == 0) 
		       strncpy(callerid, v->value, sizeof(callerid)-1);
		  else if (strcasecmp(v->name, "mode") == 0) {
		       if (strncasecmp(v->value, "di", 2) == 0) 
			    mode = MODE_DIALTONE;
		       else if (strncasecmp(v->value, "im", 2) == 0)
			    mode = MODE_IMMEDIATE;
		       else if (strncasecmp(v->value, "fx", 2) == 0)
			    mode = MODE_FXO;
		       else
			    ast_log(LOG_WARNING, "Unknown mode: %s\n", v->value);
		  } else if (!strcasecmp(v->name, "context")) 
		       strncpy(context, v->value, sizeof(context)-1);
		  else if (!strcasecmp(v->name, "echocancel")) {
		       if (!strcasecmp(v->value, "off")) 
			    echocancel = 0;
		       else 
			    echocancel = 1;
		  } else if (strcasecmp(v->name, "txgain") == 0) {
		       settxgain = 1;
		       txgain = parse_gain_value(v->name, v->value);
		  } else if (strcasecmp(v->name, "rxgain") == 0) {
		       setrxgain = 1;
		       rxgain = parse_gain_value(v->name, v->value);
		  } 
#if 0
		  else if (strcasecmp(v->name, "grunttimeout") == 0) 
		       gruntdetect_timeout = 1000*atoi(v->value);
#endif	  
		  v = v->next;
	     }
	     
	     if (gruntdetect_timeout < 1000)
		  gruntdetect_timeout = 1000;

	done: (void)0;
	} ast_mutex_unlock(&iflock);

	
	ast_destroy(cfg);
	
	if (!error && 
	    ast_channel_register(type, tdesc, 
				prefformat, vpb_request) != 0) {
	     ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
	     
	     error = -1;
	}


	if (error)
	     __unload_module();
	else	     
	     restart_monitor(); /* And start the monitor for the first time */
	
	return error;
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
