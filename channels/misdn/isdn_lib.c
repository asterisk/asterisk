/*
 * Chan_Misdn -- Channel Driver for Asterisk
 *
 * Interface to mISDN
 *
 * Copyright (C) 2004, Christian Richter
 *
 * Christian Richter <crich@beronet.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include "isdn_lib_intern.h"


void misdn_free_ibuffer(void *ibuf)
{
	free_ibuffer((ibuffer_t*)ibuf);
}


void misdn_clear_ibuffer(void *ibuf)
{
	clear_ibuffer( (ibuffer_t*)ibuf);
}

void *misdn_init_ibuffer(int len)
{
	return init_ibuffer(len);
}

int misdn_ibuf_freecount(void *buf)
{
	return ibuf_freecount( (ibuffer_t*)buf);
}

int misdn_ibuf_usedcount(void *buf)
{
	return ibuf_usedcount( (ibuffer_t*)buf);
}

void misdn_ibuf_memcpy_r(char *to, void *buf, int len)
{
	ibuf_memcpy_r( to, (ibuffer_t*)buf, len);
}

void misdn_ibuf_memcpy_w(void *buf, char *from,  int len)
{
	ibuf_memcpy_w((ibuffer_t*)buf, from, len);
}

struct misdn_stack* get_misdn_stack( void );


int misdn_lib_is_ptp(int port)
{
	struct misdn_stack *stack=get_misdn_stack();
	for ( ; stack; stack=stack->next) {
		if (stack->port == port) return stack->ptp;
	}
	return -1;
}


struct misdn_stack* get_stack_by_bc(struct misdn_bchannel *bc)
{
	struct misdn_stack *stack=get_misdn_stack();

	if (!bc) return NULL;
	
	for ( ; stack; stack=stack->next) {
		int i;
		for (i=0; i <stack->b_num; i++) {
			if ( bc->port == stack->port) return stack;
		}
	}

	return NULL;
}


void get_show_stack_details(int port, char *buf)
{
	struct misdn_stack *stack=get_misdn_stack();
	
	for ( ; stack; stack=stack->next) {
		if (stack->port == port) break;
	}
	
	if (stack) {
		sprintf(buf, "* Stack Addr:%x Port %d Type %s Prot. %s L2Link %s L1Link:%s", stack->upper_id, stack->port, stack->nt?"NT":"TE", stack->ptp?"PTP":"PMP", stack->l2link?"UP":"DOWN", stack->l1link?"UP":"DOWN");
		
	} else {
		buf[0]=0;
	}
	
}


static int nt_err_cnt =0 ;

enum global_states {
	MISDN_INITIALIZING,
	MISDN_INITIALIZED
} ;

static enum global_states  global_state=MISDN_INITIALIZING;


#include <mISDNuser/net_l2.h>
#include <mISDNuser/tone.h>
#include <unistd.h>
#include <semaphore.h>
#include <pthread.h>
#include <signal.h>

#include "isdn_lib.h"


struct misdn_lib {
	int midev;
	int midev_nt;

	pthread_t event_thread;
	pthread_t event_handler_thread;
  
	void *user_data;

	msg_queue_t upqueue;
	msg_queue_t activatequeue; 
  
	sem_t new_msg;
  
	struct misdn_stack *stack_list;
} ;

#ifndef ECHOCAN_ON
#define ECHOCAN_ON 123
#define ECHOCAN_OFF 124
#endif

#define MISDN_DEBUG 0

struct misdn_bchannel *find_bc_by_l3id(struct misdn_stack *stack, unsigned long l3id);

int setup_bc(struct misdn_bchannel *bc);

int manager_isdn_handler(iframe_t *frm ,msg_t *msg);

int misdn_lib_port_restart(int port);

extern struct isdn_msg msgs_g[]; 

#define ISDN_PID_L3_B_USER 0x430000ff
#define ISDN_PID_L4_B_USER 0x440000ff

/* #define MISDN_IBUF_SIZE 1024 */
#define MISDN_IBUF_SIZE 512

/*  Fine Tuning of Inband  Signalling time */
#define TONE_ALERT_CNT 41 /*  1 Sec  */
#define TONE_ALERT_SILENCE_CNT 200 /*  4 Sec */

#define TONE_BUSY_CNT 20 /*  ? */
#define TONE_BUSY_SILENCE_CNT 48 /*  ? */

static int  entity;

static struct misdn_lib *glob_mgr;

unsigned char tone_425_flip[TONE_425_SIZE];
unsigned char tone_silence_flip[TONE_SILENCE_SIZE];

static void misdn_lib_isdn_event_catcher(void *arg);
static int handle_event_nt(void *dat, void *arg);


void stack_holder_add(struct misdn_stack *stack, struct misdn_bchannel *holder);
void stack_holder_remove(struct misdn_stack *stack, struct misdn_bchannel *holder);
struct misdn_bchannel *stack_holder_find(struct misdn_stack *stack, unsigned long l3id);

/* from isdn_lib.h */
int init_bc(struct misdn_stack * stack,  struct misdn_bchannel *bc, int midev, int port, int bidx, char *msn, int firsttime);
struct misdn_stack* stack_init(int midev,  int port, int ptp);
void stack_te_destroy(struct misdn_stack* stack);
	/* user iface */
int te_lib_init( void ) ; /* returns midev */
void te_lib_destroy(int midev) ;
struct misdn_bchannel *manager_find_bc_by_pid(int pid);
struct misdn_bchannel *manager_find_bc_holded(struct misdn_bchannel* bc);
unsigned char * manager_flip_buf_bits ( unsigned char * buf , int len);
void manager_ph_control_block(struct misdn_bchannel *bc, int c1, void *c2, int c2_len);
void manager_clean_bc(struct misdn_bchannel *bc );
void manager_bchannel_setup (struct misdn_bchannel *bc);
void manager_bchannel_cleanup (struct misdn_bchannel *bc);

int isdn_msg_get_index(struct isdn_msg msgs[], msg_t *frm, int nt);
enum event_e isdn_msg_get_event(struct isdn_msg msgs[], msg_t *frm, int nt);
int isdn_msg_parse_event(struct isdn_msg msgs[], msg_t *frm, struct misdn_bchannel *bc, int nt);
char * isdn_get_info(struct isdn_msg msgs[], enum event_e event, int nt);
msg_t * isdn_msg_build_event(struct isdn_msg msgs[], struct misdn_bchannel *bc, enum event_e event, int nt);
void ec_chunk( struct misdn_bchannel *bc, unsigned char *rxchunk, unsigned char *txchunk, int chunk_size);
	/* end */
int bchdev_echocancel_activate(struct misdn_bchannel* dev);
void bchdev_echocancel_deactivate(struct misdn_bchannel* dev);
/* end */


static char *bearer2str(int cap) {
	static char *bearers[]={
		"Speech",
		"Audio 3.1k",
		"Unres Digital",
		"Res Digital",
		"Unknown Bearer"
	};
	
	switch (cap) {
	case INFO_CAPABILITY_SPEECH:
		return bearers[0];
		break;
	case INFO_CAPABILITY_AUDIO_3_1K:
		return bearers[1];
		break;
	case INFO_CAPABILITY_DIGITAL_UNRESTRICTED:
		return bearers[2];
		break;
	case INFO_CAPABILITY_DIGITAL_RESTRICTED:
		return bearers[3];
		break;
	default:
		return bearers[4];
		break;
	}
}


static char flip_table[256];

void init_flip_bits(void)
{
	int i,k;
	
	for (i = 0 ; i < 256 ; i++) {
		unsigned char sample = 0 ;
		for (k = 0; k<8; k++) {
			if ( i & 1 << k ) sample |= 0x80 >>  k;
		}
		flip_table[i] = sample;
	}
}

unsigned char * flip_buf_bits ( unsigned char * buf , int len)
{
	int i;
	char * start = buf;
	
	for (i = 0 ; i < len; i++) {
		buf[i] = flip_table[buf[i]];
	}
	
	return start;
}




msg_t *create_l2msg(int prim, int dinfo, int size) /* NT only */
{
	int i = 0;
	msg_t *dmsg;
  
	while(i < 10)
	{
		dmsg = prep_l3data_msg(prim, dinfo, size, 256, NULL);
		if (dmsg)
			return(dmsg);
      
		if (!i)
			printf("cannot allocate memory, trying again...\n");
		i++;
		usleep(300000);
	}
	printf("cannot allocate memory, system overloaded.\n");
	exit(-1);
}



msg_t *create_l3msg(int prim, int mt, int dinfo, int size, int ntmode)
{
	int i = 0;
	msg_t *dmsg;
	Q931_info_t *qi;
	iframe_t *frm;
  
	if (!ntmode)
		size = sizeof(Q931_info_t)+2;
  
	while(i < 10) {
		if (ntmode) {
			dmsg = prep_l3data_msg(prim, dinfo, size, 256, NULL);
			if (dmsg) {
				return(dmsg);
			}
		} else {
			dmsg = alloc_msg(size+256+mISDN_HEADER_LEN+DEFAULT_HEADROOM);
			if (dmsg)
			{
				memset(msg_put(dmsg,size+mISDN_HEADER_LEN), 0, size+mISDN_HEADER_LEN);
				frm = (iframe_t *)dmsg->data;
				frm->prim = prim;
				frm->dinfo = dinfo;
				qi = (Q931_info_t *)(dmsg->data + mISDN_HEADER_LEN);
				qi->type = mt;
				return(dmsg);
			}
		}
    
		if (!i) printf("cannot allocate memory, trying again...\n");
		i++;
		usleep(300000);
	}
	printf("cannot allocate memory, system overloaded.\n");
	exit(-1);
}


int send_msg (int midev, struct misdn_bchannel *bc, msg_t *dmsg)
{
	iframe_t *frm;
	frm = (iframe_t *)dmsg->data;
	struct misdn_stack *stack=get_stack_by_bc(bc);


	if (!stack) {
		cb_log(0,0,"send_msg: IEK!! no stack\n ");
		return -1;
	}
	
	frm->addr = (stack->upper_id | FLG_MSG_DOWN);

	
	frm->dinfo = bc->l3_id;
  
	frm->len = (dmsg->len) - mISDN_HEADER_LEN;
	
	mISDN_write(midev, dmsg->data, dmsg->len, TIMEOUT_1SEC);
  
	free_msg(dmsg);

	return 0;
}


static int mypid=0;


int misdn_cap_is_speech(int cap)
/** Poor mans version **/
{
	if ( (cap != INFO_CAPABILITY_DIGITAL_UNRESTRICTED) &&
	     (cap != INFO_CAPABILITY_DIGITAL_RESTRICTED) ) return 1;
	return 0;
}

int misdn_inband_avail(struct misdn_bchannel *bc)
{

	/*if ! early_bconnect we have never inband available*/
	if ( ! bc->early_bconnect ) return 0;
	
	switch (bc->progress_indicator) {
	case INFO_PI_INBAND_AVAILABLE:
	case INFO_PI_CALL_NOT_E2E_ISDN:
	case INFO_PI_CALLED_NOT_ISDN:
		return 1;
	default:
		return 0;
	}
	return 0;
}


void dump_chan_list(struct misdn_stack *stack)
{
	int i;

	for (i=0; i <stack->b_num; i++) {
		cb_log(3, stack->port, "Idx:%d stack->cchan:%d Chan:%d\n",i,stack->channels[i], i+1);
	}
}




static int find_free_chan_in_stack(struct misdn_stack *stack, int channel)
{
	int i;

	if (channel < 0 || channel > MAX_BCHANS) {
		cb_log(4, stack->port, " !! out of bound call to find_free_chan_in_stack! (ch:%d)\n", channel);
		return 0;
	}
	
	channel--;
  
	for (i = 0; i < stack->b_num; i++) {
		if (i != 15 && (channel < 0 || i == channel)) { /* skip E1 Dchannel ;) and work with chan preselection */
			if (!stack->channels[i]) {
				cb_log (4, stack->port, " --> found chan%s: %d\n", channel>=0?" (preselected)":"", i+1);
				stack->channels[i] = 1;
				return i+1;
			}
		}
	}

	cb_log (4, stack->port, " !! NO FREE CHAN IN STACK\n");
	dump_chan_list(stack);
  
	return 0;
}

int empty_chan_in_stack(struct misdn_stack *stack, int channel)
{
	cb_log (4, stack?stack->port:0, " --> empty chan %d\n",channel); 
	stack->channels[channel-1] = 0;
	dump_chan_list(stack);
	return 0;
}


void empty_bc(struct misdn_bchannel *bc)
{
	bc->state=STATE_NOTHING;
	
	bc->channel = 0;
	bc->in_use = 0;

	bc->send_dtmf=0;
	bc->nodsp=0;
	bc->nojitter=0;

	bc->time_usec=0;
	
	bc->rxgain=0;
	bc->txgain=0;

	bc->crypt=0;
	bc->curptx=0; bc->curprx=0;
	
	bc->crypt_key[0] = 0;
	
	bc->tone=TONE_NONE;
	bc->tone_cnt2 = bc->tone_cnt=0;
  
	bc->dnumplan=NUMPLAN_UNKNOWN;
	bc->onumplan=NUMPLAN_UNKNOWN;
	bc->rnumplan=NUMPLAN_UNKNOWN;
	

	bc->active = 0;

	bc->early_bconnect = 1;
	
	bc->ec_enable = 0;
	bc->ec_deftaps = 128;
	bc->ec_whenbridged = 0;
	bc->ec_training = 1;
	
	
	bc->orig=0;
  
	bc->cause=16;
	bc->out_cause=16;
	bc->pres=0 ; /* screened */
	
	bc->evq=EVENT_NOTHING;

	bc->progress_coding=0;
	bc->progress_location=0;
	bc->progress_indicator=0;
	
/** Set Default Bearer Caps **/
	bc->capability=INFO_CAPABILITY_SPEECH;
	bc->law=INFO_CODEC_ALAW;
	bc->mode=0;
	bc->rate=0;
	bc->user1=0;
	bc->async=0;
	bc->urate=0;
	
	
	
	bc->info_dad[0] = 0;
	bc->display[0] = 0;
	bc->infos_pending[0] = 0;
	bc->oad[0] = 0;
	bc->dad[0] = 0;
	bc->orig_dad[0] = 0;
	
	bc->fac_type=FACILITY_NONE;
	
	bc->te_choose_channel = 0;
}


int clean_up_bc(struct misdn_bchannel *bc)
{
	int ret=0;
	unsigned char buff[32];
	struct misdn_stack * stack;
	
	if (!bc  ) return -1;
	stack=get_stack_by_bc(bc);
	if (!stack) return -1;
	
	if (!bc->upset) {
		cb_log(5, stack->port, "$$$ Already cleaned up bc with stid :%x\n", bc->b_stid);
		return -1;
	}

	if (bc->active) {
		cb_log(2, stack->port, "$$$ bc still active, deactivatiing .. stid :%x\n", bc->b_stid);
		manager_bchannel_deactivate(bc);
	}
	
	cb_log(5, stack->port, "$$$ Cleaning up bc with stid :%x\n", bc->b_stid);
	
	
	if ( misdn_cap_is_speech(bc->capability) && bc->ec_enable) {
		manager_ec_disable(bc);
	}

	mISDN_write_frame(stack->midev, buff, bc->addr|FLG_MSG_DOWN, MGR_DELLAYER | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);
	
	
	bc->b_stid = 0;
	
	bc->upset=0;
	
	
	return ret;
}



void clear_l3(struct misdn_stack *stack)
{
	int i;
	for (i=0; i<stack->b_num; i++) {
		if (global_state == MISDN_INITIALIZED)  {
			cb_event(EVENT_CLEANUP, &stack->bc[i], glob_mgr->user_data);
			empty_chan_in_stack(stack,i+1);
			empty_bc(&stack->bc[i]);
			clean_up_bc(&stack->bc[i]);
			
		}
		
	} 
}

int set_chan_in_stack(struct misdn_stack *stack, int channel)
{
	stack->channels[channel-1] = 1;
  
	return 0;
}

int chan_in_stack_free(struct misdn_stack *stack, int channel)
{
	if (stack->channels[channel-1])
		return 0;
  
	return 1;
}



static int newteid=0;

#ifdef MISDNUSER_JOLLY
#define MAXPROCS 0x100
#else
#define MAXPROCS 0x10
#endif


int misdn_lib_get_l1_up(struct misdn_stack *stack)
{
	/* Pull Up L1 if we have JOLLY */ 
	iframe_t act;
	act.prim = PH_ACTIVATE | REQUEST; 
	act.addr = (stack->upper_id | FLG_MSG_DOWN)  ;

	
	act.dinfo = 0;
	act.len = 0;

	return mISDN_write(stack->midev, &act, mISDN_HEADER_LEN+act.len, TIMEOUT_1SEC);

}

int misdn_lib_get_l2_up(struct misdn_stack *stack)
{
	
	if (stack->ptp && (stack->nt) ) {
		msg_t *dmsg;
		/* L2 */
		dmsg = create_l2msg(DL_ESTABLISH | REQUEST, 0, 0);
		
		if (stack->nst.manager_l3(&stack->nst, dmsg))
			free_msg(dmsg);
		
	} else {
		iframe_t act;
		
		act.prim = DL_ESTABLISH | REQUEST;
		act.addr = (stack->upper_id |FLG_MSG_DOWN)  ;
		
		act.dinfo = 0;
		act.len = 0;
		return mISDN_write(stack->midev, &act, mISDN_HEADER_LEN+act.len, TIMEOUT_1SEC);
	}
	
	return 0;
}


int misdn_lib_get_l2_status(struct misdn_stack *stack)
{
	iframe_t act;
	
	act.prim = DL_ESTABLISH | REQUEST;

	act.addr = (stack->upper_id | FLG_MSG_DOWN)  ;

	act.dinfo = 0;
	act.len = 0;
	return mISDN_write(stack->midev, &act, mISDN_HEADER_LEN+act.len, TIMEOUT_1SEC);
}

int misdn_lib_get_short_status(struct misdn_stack *stack)
{
	iframe_t act;
	
	
	act.prim = MGR_SHORTSTATUS | REQUEST; 
	
	act.addr = (stack->upper_id | FLG_MSG_DOWN)  ;

	act.dinfo = SSTATUS_L1;
	
	act.len = 0;
	return mISDN_write(stack->midev, &act, mISDN_HEADER_LEN+act.len, TIMEOUT_1SEC);
}



static int create_process (int midev, struct misdn_bchannel *bc) {
	iframe_t ncr;
	int l3_id;
	int i;
	struct misdn_stack *stack=get_stack_by_bc(bc);
	int free_chan;
  
	if (stack->nt) {
		free_chan = find_free_chan_in_stack(stack, bc->channel_preselected?bc->channel:0);
		if (!free_chan) return -1;
		bc->channel=free_chan;
    
		for (i=0; i <= MAXPROCS; i++)
			if (stack->procids[i]==0) break;
    
		if (i== MAXPROCS) {
			cb_log(-1, stack->port, "Couldnt Create New ProcId.\n");
			return -1;
		}
		stack->procids[i]=1;

#ifdef MISDNUSER_JOLLY
		l3_id = 0xff00 | i;
#else
		l3_id = 0xfff0 | i;
#endif
    
		ncr.prim = CC_NEW_CR | REQUEST; 

		ncr.addr = (stack->upper_id | FLG_MSG_DOWN)  ;

		ncr.dinfo = l3_id;
		ncr.len = 0;

		bc->l3_id = l3_id;
		if (mypid>5000) mypid=0;
		bc->pid=mypid++;
      
		cb_log(3, stack->port, " --> new_l3id %x\n",l3_id);
    
	} else { 
		if (stack->ptp || bc->te_choose_channel) {
			/* we know exactly which channels are in use */
			free_chan = find_free_chan_in_stack(stack, bc->channel_preselected?bc->channel:0);
			if (!free_chan) return -1;
			bc->channel=free_chan;
		} else {
			/* other phones could have made a call also on this port (ptmp) */
			bc->channel=0xff;
		}
    
    
		/* if we are in te-mode, we need to create a process first */
		if (newteid++ > 0xffff)
			newteid = 0x0001;
    
		l3_id = (entity<<16) | newteid;
		/* preparing message */
		ncr.prim = CC_NEW_CR | REQUEST; 

		ncr.addr = (stack->upper_id | FLG_MSG_DOWN)  ;

		ncr.dinfo =l3_id;
		ncr.len = 0;
		/* send message */

		bc->l3_id = l3_id;
		if (mypid>5000) mypid=0;
		bc->pid=mypid++;
    
		cb_log(3, stack->port, "--> new_l3id %x\n",l3_id);
    
		mISDN_write(midev, &ncr, mISDN_HEADER_LEN+ncr.len, TIMEOUT_1SEC);
	}
  
	return l3_id;
}


void misdn_lib_setup_bc(struct misdn_bchannel *bc)
{
	setup_bc(bc);
}


int setup_bc(struct misdn_bchannel *bc)
{
	unsigned char buff[1025];
  
	mISDN_pid_t pid;
	int ret;
	

	struct misdn_stack *stack=get_stack_by_bc(bc);
	
	int midev=stack->midev;
	int channel=bc->channel-1-(bc->channel>16);
	int b_stid=stack->b_stids[channel>=0?channel:0];
	
	
	if ( !misdn_cap_is_speech(bc->capability))
		clean_up_bc(bc);
	
	
	if (bc->upset) {
		cb_log(4, stack->port, "$$$ bc already upsetted stid :%x\n", b_stid);
		return -1;
	}
	
	cb_log(5, stack->port, "$$$ Setting up bc with stid :%x\n", b_stid);
	
	if (b_stid <= 0) {
		cb_log(-1, stack->port," -- Stid <=0 at the moment in channel:%d\n",channel);

		return 1;
	}
	
	
	bc->b_stid = b_stid;
	
	{
		layer_info_t li;
		memset(&li, 0, sizeof(li));
    
		li.object_id = -1;
		li.extentions = 0;
		
		li.st = bc->b_stid; /*  given idx */


#define MISDN_DSP
#ifndef MISDN_DSP
		bc->nodsp=1;
#endif
		if ( misdn_cap_is_speech(bc->capability) && !bc->nodsp && (bc->async != 1) ) {
			cb_log(4, stack->port,"setup_bc: with dsp\n");
			{ 
				int l = sizeof(li.name);
				strncpy(li.name, "B L4", l);
				li.name[l-1] = 0;
			}
			li.pid.layermask = ISDN_LAYER((4));
			li.pid.protocol[4] = ISDN_PID_L4_B_USER;

			bc->layer=4;
			
		} else {
			cb_log(4, stack->port,"setup_bc: without dsp\n");
			{ 
				int l = sizeof(li.name);
				strncpy(li.name, "B L3", l);
				li.name[l-1] = 0;
			}
			li.pid.layermask = ISDN_LAYER((3));
			li.pid.protocol[3] = ISDN_PID_L3_B_USER;
			
			bc->layer=3;
		}
		
		ret = mISDN_new_layer(midev, &li);
		if (ret ) {
			cb_log(-1, stack->port,"New Layer Err: %d %s\n",ret,strerror(errno));
			
			return(-EINVAL);
		}
		
		bc->layer_id = li.id;
	}
	
	memset(&pid, 0, sizeof(pid));
	
	
	
	cb_log(4, stack->port," --> Channel is %d\n", bc->channel);
	
	
	if (bc->async == 1 || bc->nodsp) {
		cb_log(4, stack->port," --> TRANSPARENT Mode (no DSP, no HDLC)\n");
		pid.protocol[1] = ISDN_PID_L1_B_64TRANS;
		pid.protocol[2] = ISDN_PID_L2_B_TRANS;
		pid.protocol[3] = ISDN_PID_L3_B_USER;
		pid.layermask = ISDN_LAYER((1)) | ISDN_LAYER((2)) | ISDN_LAYER((3));
		
	} else if ( misdn_cap_is_speech(bc->capability)) {
		cb_log(4, stack->port," --> TRANSPARENT Mode\n");
		pid.protocol[1] = ISDN_PID_L1_B_64TRANS;
		pid.protocol[2] = ISDN_PID_L2_B_TRANS;
		pid.protocol[3] = ISDN_PID_L3_B_DSP;
		pid.protocol[4] = ISDN_PID_L4_B_USER;
		pid.layermask = ISDN_LAYER((1)) | ISDN_LAYER((2)) | ISDN_LAYER((3)) | ISDN_LAYER((4));
		
	} else {
		cb_log(4, stack->port," --> HDLC Mode\n");
		pid.protocol[1] = ISDN_PID_L1_B_64HDLC ;
		pid.protocol[2] = ISDN_PID_L2_B_TRANS  ;
		pid.protocol[3] = ISDN_PID_L3_B_USER;
		pid.layermask = ISDN_LAYER((1)) | ISDN_LAYER((2)) | ISDN_LAYER((3)) ;
	}
	
	ret = mISDN_set_stack(midev, bc->b_stid, &pid);
	
	if (ret){
		cb_log(5, stack->port,"$$$ Set Stack Err: %d %s\n",ret,strerror(errno));
		
		mISDN_write_frame(midev, buff, bc->layer_id, MGR_DELLAYER | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);
		return(-EINVAL);
	}

#if 0
	ret = mISDN_get_setstack_ind(midev, bc->layer_id );
	
	if (ret){
		cb_log(5, stack->port,"$$$ Get Set Stack Err: %d %s\n",ret,strerror(errno));
		
		mISDN_write_frame(midev, buff, bc->layer_id, MGR_DELLAYER | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);
		return(-EINVAL);
	}

#endif

	
	bc->upset=1;
	
	
	manager_bchannel_deactivate(bc);
	
	return 0;
}



/** IFACE **/
int init_bc(struct misdn_stack *stack,  struct misdn_bchannel *bc, int midev, int port, int bidx,  char *msn, int firsttime)
{
	unsigned char buff[1025];
	iframe_t *frm = (iframe_t *)buff;
	int ret;
  
	if (!bc) return -1;
  
	cb_log(4, port, "Init.BC %d.\n",bidx);
	
	memset(bc, 0,sizeof(struct misdn_bchannel));
	
	if (msn) {
		int l = sizeof(bc->msn);
		strncpy(bc->msn,msn, l);
		bc->msn[l-1] = 0;
	}
	
	
	empty_bc(bc);
	bc->upset=0;
	bc->port=stack->port;
	bc->nt=stack->nt?1:0;
	
	{
		ibuffer_t* ibuf= init_ibuffer(MISDN_IBUF_SIZE);

		if (!ibuf) return -1;
		
		clear_ibuffer( ibuf);
		
		ibuf->rsem=malloc(sizeof(sem_t));
		
		bc->astbuf=ibuf;

		if (sem_init(ibuf->rsem,1,0)<0)
			sem_init(ibuf->rsem,0,0);
		
	}
	
	
	
	
	{
		stack_info_t *stinf;
		ret = mISDN_get_stack_info(midev, stack->port, buff, sizeof(buff));
		if (ret < 0) {
			cb_log(-1, port, "%s: Cannot get stack info for this port. (ret=%d)\n", __FUNCTION__, ret);
			return -1;
		}
    
		stinf = (stack_info_t *)&frm->data.p;
    
		cb_log(4, port, " --> Child %x\n",stinf->child[bidx]);
	}
  
	return 0;
}



struct misdn_stack* stack_init( int midev, int port, int ptp )
{
	int ret;
	unsigned char buff[1025];
	iframe_t *frm = (iframe_t *)buff;
	stack_info_t *stinf;
	int i; 
	layer_info_t li;

	struct misdn_stack *stack = malloc(sizeof(struct misdn_stack));
	if (!stack ) return NULL;


	//cb_log(2, "Init. Stack on port:%d\n",port);
	cb_log(4, port, "Init. Stack.\n");
  
	memset(stack,0,sizeof(struct misdn_stack));
  
	for (i=0; i<MAX_BCHANS + 1; i++ ) stack->channels[i]=0;
	
	stack->port=port;
	stack->midev=midev;
	stack->ptp=ptp;
  
	stack->holding=NULL;
	stack->pri=0;
  
	msg_queue_init(&stack->downqueue);
  
	/* query port's requirements */
	ret = mISDN_get_stack_info(midev, port, buff, sizeof(buff));
	if (ret < 0) {
		cb_log(-1, port, "%s: Cannot get stack info for this port. (ret=%d)\n", __FUNCTION__, ret);
		return(NULL);
	}
  
	stinf = (stack_info_t *)&frm->data.p;

	stack->d_stid = stinf->id;
	stack->b_num = stinf->childcnt;

	for (i=0; i<stinf->childcnt; i++)
		stack->b_stids[i] = stinf->child[i];
  
	switch(stinf->pid.protocol[0] & ~ISDN_PID_FEATURE_MASK) {
	case ISDN_PID_L0_TE_S0:
		//cb_log(2, "TE Stack\n");
		stack->nt=0;
		break;
	case ISDN_PID_L0_NT_S0:
		cb_log(4, port, "NT Stack\n");

		stack->nt=1;
		break;

	case ISDN_PID_L0_TE_U:
		break;
	case ISDN_PID_L0_NT_U:
		break;
	case ISDN_PID_L0_TE_UP2:
		break;
	case ISDN_PID_L0_NT_UP2:
		break;
	case ISDN_PID_L0_TE_E1:
		cb_log(4, port, "TE S2M Stack\n");
		stack->nt=0;
		stack->pri=1;
		break;
	case ISDN_PID_L0_NT_E1:
		cb_log(4, port, "TE S2M Stack\n");
		stack->nt=1;
		stack->pri=1;
		
		break;
	default:
		cb_log(-1, port, "this is a unknown port type 0x%08x\n", stinf->pid.protocol[0]);

	}

	if (!stack->nt) {
		if (stinf->pid.protocol[2] & ISDN_PID_L2_DF_PTP ) { 
			stack->ptp = 1;
		} else {
			stack->ptp = 0;
		}
	}
	
	

	{
		int ret;
		int nt=stack->nt;

		cb_log(4, port, "Init. Stack.\n");
		
		
		
		memset(&li, 0, sizeof(li));
		{
			int l = sizeof(li.name);
			strncpy(li.name,nt?"net l2":"user l4", l);
			li.name[l-1] = 0;
		}
		li.object_id = -1;
		li.extentions = 0;
		li.pid.protocol[nt?2:4] = nt?ISDN_PID_L2_LAPD_NET:ISDN_PID_L4_CAPI20;
		li.pid.layermask = ISDN_LAYER((nt?2:4));
		li.st = stack->d_stid;
		
		
		ret = mISDN_new_layer(midev, &li);
		if (ret) {
			cb_log(-1, port, "%s: Cannot add layer %d to this port.\n", __FUNCTION__, nt?2:4);
			return(NULL);
		}
		
		
		stack->upper_id = li.id;
		ret = mISDN_register_layer(midev, stack->d_stid, stack->upper_id);
		if (ret)
		{
			cb_log(-1,port,"Cannot register layer %d of this port.\n", nt?2:4);
			return(NULL);
		}
		
		
		stack->lower_id = mISDN_get_layerid(midev, stack->d_stid, nt?1:3); 
		if (stack->lower_id < 0) {
			cb_log(-1, port, "%s: Cannot get layer(%d) id of this port.\n", __FUNCTION__, nt?1:3);
			return(NULL);
		}
		
		stack->upper_id = mISDN_get_layerid(midev, stack->d_stid, nt?2:4);
		if (stack->upper_id < 0) {
			cb_log(-1, port, "%s: Cannot get layer(%d) id of this port.\n", __FUNCTION__, 2);
			return(NULL);
		}
		
		cb_log(4, port, "NT Stacks upper_id %x\n",stack->upper_id);
		
		
		/* create nst (nt-mode only) */
		if (nt) {
			
			memset(&stack->nst, 0, sizeof(net_stack_t));
			memset(&stack->mgr, 0, sizeof(manager_t));
    
			stack->mgr.nst = &stack->nst;
			stack->nst.manager = &stack->mgr;
    
			stack->nst.l3_manager = handle_event_nt;
			stack->nst.device = midev;
			stack->nst.cardnr = port;
			stack->nst.d_stid = stack->d_stid;
    
			stack->nst.feature = FEATURE_NET_HOLD;
			if (stack->ptp)
				stack->nst.feature |= FEATURE_NET_PTP;
			if (stack->pri)
				stack->nst.feature |= FEATURE_NET_CRLEN2 | FEATURE_NET_EXTCID;
			
			stack->nst.l1_id = stack->lower_id;
			stack->nst.l2_id = stack->upper_id;
			
			msg_queue_init(&stack->nst.down_queue);
			
			Isdnl2Init(&stack->nst);
			Isdnl3Init(&stack->nst);
			
			//misdn_lib_get_l1_up(stack);
		} 
		
		
		
		if ( stack->nt && stack->ptp) {
			//misdn_lib_get_l2_up(stack);
			//stack->l2link=0;
		}
		
		if (!stack->nt) {
			/*assume L1 is up, we'll get DEACTIVATES soon, for non
			 * up L1s*/
			stack->l1link=1;
			//misdn_lib_get_l2_up(stack);
			//misdn_lib_get_l1_up(stack);
		}

		//misdn_lib_get_short_status(stack);
		
		
		misdn_lib_get_l1_up(stack);
		misdn_lib_get_l2_up(stack);
		
	}

	cb_log(1,0,"stack_init: port:%d lowerId:%x  upperId:%x\n",stack->port,stack->lower_id, stack->upper_id);
	
	return stack;
}


void stack_te_destroy(struct misdn_stack* stack)
{
	char buf[1024];
	if (!stack) return;
  
	if (stack->lower_id) 
		mISDN_write_frame(stack->midev, buf, stack->lower_id, MGR_DELLAYER | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);

	if (stack->upper_id) 
		mISDN_write_frame(stack->midev, buf, stack->upper_id, MGR_DELLAYER | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);
}


struct misdn_stack * find_stack_by_addr(int  addr)
{
	struct misdn_stack *stack;
	
	for (stack=glob_mgr->stack_list;
	     stack;
	     stack=stack->next) {
		//cb_log(2,0,"stack->addr:%x addr:%x masked: st->a:%x a:%x\n",stack->upper_id, addr, stack->upper_id&STACK_ID_MASK, addr&STACK_ID_MASK);
		
		if ( (stack->upper_id&STACK_ID_MASK) == (addr&STACK_ID_MASK)) return stack;

	}
  
	return NULL;
}


struct misdn_stack * find_stack_by_port(int port)
{
	struct misdn_stack *stack;
  
	for (stack=glob_mgr->stack_list;
	     stack;
	     stack=stack->next) 
		if (stack->port == port) return stack;
  
	return NULL;
}

struct misdn_stack * find_stack_by_mgr(manager_t* mgr_nt)
{
	struct misdn_stack *stack;
  
	for (stack=glob_mgr->stack_list;
	     stack;
	     stack=stack->next) 
		if ( &stack->mgr == mgr_nt) return stack;
  
	return NULL;
}

struct misdn_bchannel *find_bc_by_masked_l3id(struct misdn_stack *stack, unsigned long l3id, unsigned long mask)
{
	int i;
	for (i=0; i<stack->b_num; i++) {
		if ( (stack->bc[i].l3_id & mask)  ==  (l3id & mask)) return &stack->bc[i] ;
	}
	return stack_holder_find(stack,l3id);
}


struct misdn_bchannel *find_bc_by_l3id(struct misdn_stack *stack, unsigned long l3id)
{
	int i;
	for (i=0; i<stack->b_num; i++) {
		if (stack->bc[i].l3_id == l3id) return &stack->bc[i] ;
	}
	return stack_holder_find(stack,l3id);
}

struct misdn_bchannel *find_bc_holded(struct misdn_stack *stack)
{
	int i;
	for (i=0; i<stack->b_num; i++) {
		if (stack->bc[i].holded ) return &stack->bc[i] ;
	}
	return NULL;
}


struct misdn_bchannel *find_bc_by_addr(unsigned long addr)
{
	struct misdn_stack* stack;
	int i;

	
	for (stack=glob_mgr->stack_list;
	     stack;
	     stack=stack->next) {
		
		for (i=0; i< stack->b_num; i++) {

			if ( (stack->bc[i].addr&STACK_ID_MASK)==(addr&STACK_ID_MASK) ||  stack->bc[i].layer_id== addr ) {
				return &stack->bc[i];
			}
		}
		
	}

	
	return NULL;
}





int handle_event ( struct misdn_bchannel *bc, enum event_e event, iframe_t *frm)
{
	struct misdn_stack *stack=get_stack_by_bc(bc);
	
	if (!stack->nt) {
		
		switch (event) {

		case EVENT_CONNECT_ACKNOWLEDGE:
			manager_bchannel_activate(bc);
			break;
		case EVENT_CONNECT:
			
			if ( *bc->crypt_key ) {
				cb_log(4, stack->port, "ENABLING BLOWFISH channel:%d oad%d:%s dad%d:%s\n", bc->channel, bc->onumplan,bc->oad, bc->dnumplan,bc->dad);
				
				manager_ph_control_block(bc,  BF_ENABLE_KEY, bc->crypt_key, strlen(bc->crypt_key) );
			}
		case EVENT_SETUP:
			if (bc->channel>0 && bc->channel<255)
				set_chan_in_stack(stack, bc->channel);
			break;
		case EVENT_ALERTING:
		case EVENT_PROGRESS:
		case EVENT_PROCEEDING:
		case EVENT_SETUP_ACKNOWLEDGE:
			
		{
			if (bc->channel == 0xff) {
				bc->channel=find_free_chan_in_stack(stack, 0);
				if (!bc->channel) {
					cb_log(-1, stack->port, "Any Channel Requested, but we have no more!!\n");

					break;
				}
			} 
			
			if (!stack->nt) {
				int ret=setup_bc(bc);
				if (ret == -EINVAL){

					cb_log(-1,bc->port,"handle_event: setup_bc failed\n");
					misdn_lib_send_event(bc,EVENT_RELEASE_COMPLETE);
				}
			}
		}
		
		default:
			break;
		}
	} else {    /** NT MODE **/
		
	}
	return 0;
}

int handle_new_process(struct misdn_stack *stack, iframe_t *frm)
{
  
	struct misdn_bchannel* bc=misdn_lib_get_free_bc(stack->port, 0);
	
	
	if (!bc) {
		cb_log(-1, 0, " --> !! lib: No free channel!\n");
		return -1;
	}
  
	cb_log(4, stack->port, " --> new_process: New L3Id: %x\n",frm->dinfo);
	bc->l3_id=frm->dinfo;
	
	if (mypid>5000) mypid=0;
	bc->pid=mypid++;
	return 0;
}

int handle_cr ( struct misdn_stack *stack, iframe_t *frm)
{
	
	if (!stack) return -1;
  
	switch (frm->prim) {
	case CC_NEW_CR|INDICATION:
		cb_log(4, stack->port, " --> lib: NEW_CR Ind with l3id:%x on this port.\n",frm->dinfo);
		handle_new_process(stack, frm); 
		return 1;
	case CC_NEW_CR|CONFIRM:
		return 1;
	case CC_NEW_CR|REQUEST:
		return 1;
	case CC_RELEASE_CR|REQUEST:
		return 1;
	case CC_RELEASE_CR|CONFIRM:
		break;
	case CC_RELEASE_CR|INDICATION:
		cb_log(4, stack->port, " --> lib: RELEASE_CR Ind with l3id:%x\n",frm->dinfo);
		{
			struct misdn_bchannel *bc=find_bc_by_l3id(stack, frm->dinfo);
			struct misdn_bchannel dummybc;
      
			if (!bc) {
				cb_log(4, stack->port, " --> Didn't found BC so temporarly creating dummy BC (l3id:%x) on this port.\n", frm->dinfo);
				memset (&dummybc,0,sizeof(dummybc));
				dummybc.port=stack->port;
				dummybc.l3_id=frm->dinfo;
				bc=&dummybc; 
			}
      
			if (bc) {
				cb_log(4, stack->port, " --> lib: CLEANING UP l3id: %x\n",frm->dinfo);
				empty_chan_in_stack(stack,bc->channel);
				empty_bc(bc);
				clean_up_bc(bc);
				dump_chan_list(stack);
				bc->pid = 0;
				cb_event(EVENT_CLEANUP, bc, glob_mgr->user_data);
				
				if (bc->stack_holder) {
					cb_log(4,stack->port, "REMOVEING Holder\n");
					stack_holder_remove( stack, bc);
					free(bc);
				}
			}
			else {
				if (stack->nt) 
					cb_log(4, stack->port, "BC with dinfo: %x  not found.. (prim was %x and addr %x)\n",frm->dinfo, frm->prim, frm->addr);
			}
      
			return 1;
		}
		break;
	}
  
	return 0;
}


/*Emptys bc if it's reserved (no SETUP out yet)*/
void misdn_lib_release(struct misdn_bchannel *bc)
{
	struct misdn_stack *stack=get_stack_by_bc(bc);

	if (!stack) {
		cb_log(1,0,"misdn_release: No Stack found\n");
		return;
	}
	
	if (bc->channel>=0) {
		empty_chan_in_stack(stack,bc->channel);
		empty_bc(bc);
	}
	clean_up_bc(bc);
}




int misdn_lib_get_port_up (int port) 
{ /* Pull Up L1 if we have JOLLY */ 
	struct misdn_stack *stack;
	
	for (stack=glob_mgr->stack_list;
	     stack;
	     stack=stack->next) {
		
		if (stack->port == port) {

			if (!stack->l1link)
				misdn_lib_get_l1_up(stack);
			if (!stack->l2link)
				misdn_lib_get_l2_up(stack);
			
			return 0;
		}
	}
	return 0;
}


int misdn_lib_send_facility(struct misdn_bchannel *bc, enum facility_type fac, void *data)
{
	switch (fac) {
	case FACILITY_CALLDEFLECT:
		strcpy(bc->out_fac.calldeflect_nr,(char*)data);
		break;
	default:
		cb_log(1,bc?bc->port:0,"We don't handle this facility yet: %d\n",fac);
		return 0;
	}
	
	bc->out_fac_type=fac;
	
	misdn_lib_send_event(bc,EVENT_FACILITY);
	return 0;
}


int misdn_lib_port_up(int port)
{
	struct misdn_stack *stack;
	
	for (stack=glob_mgr->stack_list;
	     stack;
	     stack=stack->next) {
		
		if (stack->port == port) {
			if (stack->nt) {
				if (stack->l1link)
					return 1;
				else
					return 0;
			} else {
				if (stack->l1link)
					return 1;
				else
					return 0;
			}

		}
	}
  
	return -1;
}


int
handle_event_nt(void *dat, void *arg)
{
	manager_t *mgr = (manager_t *)dat;
	msg_t *msg = (msg_t *)arg;
	mISDNuser_head_t *hh;

	struct misdn_stack *stack=find_stack_by_mgr(mgr);
	int port;

	if (!msg || !mgr)
		return(-EINVAL);

	hh=(mISDNuser_head_t*)msg->data;


	port=stack->port;
	
	
	cb_log(4, stack->port, " --> lib: prim %x dinfo %x\n",hh->prim, hh->dinfo);
  
	{
		switch(hh->prim){
			
		case CC_RETRIEVE|INDICATION:
		{
			iframe_t frm; /* fake te frm to add callref to global callreflist */
			frm.dinfo = hh->dinfo;

			frm.addr=stack->upper_id | FLG_MSG_DOWN;

			frm.prim = CC_NEW_CR|INDICATION;
			
			if (handle_cr( stack, &frm)< 0) {
				msg_t *dmsg;
				cb_log(4, stack->port, "Patch from MEIDANIS:Sending RELEASE_COMPLETE %x (No free Chan for you..)\n", hh->dinfo);
				dmsg = create_l3msg(CC_RELEASE_COMPLETE | REQUEST,MT_RELEASE_COMPLETE, hh->dinfo,sizeof(RELEASE_COMPLETE_t), 1);
				stack->nst.manager_l3(&stack->nst, dmsg);
				free_msg(msg);
				return 0;
			}
			
			struct misdn_bchannel *bc=find_bc_by_l3id(stack, hh->dinfo);
			cb_event(EVENT_NEW_BC, bc, glob_mgr->user_data);
			struct misdn_bchannel *hold_bc=stack_holder_find(stack,bc->l3_id);
			if (hold_bc) {
				cb_log(4, stack->port, "REMOVEING Holder\n");
				stack_holder_remove(stack, hold_bc);
				free(hold_bc);
			}
			
		}
			
			break;
			
			
		case CC_SETUP|CONFIRM:
		{
			struct misdn_bchannel *bc=find_bc_by_l3id(stack, hh->dinfo);
			int l3id = *((int *)(((u_char *)msg->data)+ mISDNUSER_HEAD_SIZE));
			
			cb_log(4, bc?stack->port:0, " --> lib: Event_ind:SETUP CONFIRM [NT] : new L3ID  is %x\n",l3id );
	
			if (!bc) { cb_log(4, 0, "Bc Not found (after SETUP CONFIRM)\n"); return 0; }
			cb_log (2,bc->port,"I IND :CC_SETUP|CONFIRM: old l3id:%x new l3id:%x\n", bc->l3_id, l3id);
			bc->l3_id=l3id;
			cb_event(EVENT_NEW_L3ID, bc, glob_mgr->user_data);
		}
		free_msg(msg);
		return 0;
      
		case CC_SETUP|INDICATION:
		{
			iframe_t frm; /* fake te frm to add callref to global callreflist */
			frm.dinfo = hh->dinfo;
			frm.addr=stack->upper_id;
			frm.prim = CC_NEW_CR|INDICATION;
			
			if (handle_cr(stack, &frm)< 0) {
				msg_t *dmsg;
				cb_log(4, stack->port, "Patch from MEIDANIS:Sending RELEASE_COMPLETE %x (No free Chan for you..)\n", hh->dinfo);
				dmsg = create_l3msg(CC_RELEASE_COMPLETE | REQUEST,MT_RELEASE_COMPLETE, hh->dinfo,sizeof(RELEASE_COMPLETE_t), 1);
				stack->nst.manager_l3(&stack->nst, dmsg);
				free_msg(msg);
				return 0;
			}
		}
		break;


		case CC_CONNECT|INDICATION:
		case CC_ALERTING|INDICATION:
		case CC_PROCEEDING|INDICATION:

		{
			struct misdn_bchannel *bc=find_bc_by_l3id(stack, hh->dinfo);
			
			if (!bc) {
				msg_t *dmsg;
				cb_log(-1, stack->port,"!!!! We didn't found our bc, dinfo:%x on this port.\n",hh->dinfo);
				
				cb_log(-1, stack->port, "Releaseing call %x (No free Chan for you..)\n", hh->dinfo);
				dmsg = create_l3msg(CC_RELEASE_COMPLETE | REQUEST,MT_RELEASE_COMPLETE, hh->dinfo,sizeof(RELEASE_COMPLETE_t), 1);
				stack->nst.manager_l3(&stack->nst, dmsg);
				free_msg(msg);
				return 0;
				
			}
			int ret=setup_bc(bc);
			if (ret == -EINVAL){
				cb_log(-1,bc->port,"handle_event_nt: setup_bc failed\n");
				misdn_lib_send_event(bc,EVENT_RELEASE_COMPLETE);
			}
		}
		break;
		case CC_DISCONNECT|INDICATION:
		{
			struct misdn_bchannel *bc=find_bc_by_l3id(stack, hh->dinfo);
			if (!bc) {
				bc=find_bc_by_masked_l3id(stack, hh->dinfo, 0xffff0000);
				if (bc) { //repair reject bug
					int myprocid=bc->l3_id&0x0000ffff;
					hh->dinfo=(hh->dinfo&0xffff0000)|myprocid;
					cb_log(4,stack->port,"Repaired reject Bug, new dinfo: %x\n",hh->dinfo);
				}
			}
		}
		break;
		
		case CC_FACILITY|INDICATION:
		{
			struct misdn_bchannel *bc=find_bc_by_l3id(stack, hh->dinfo);
			if (!bc) {
				bc=find_bc_by_masked_l3id(stack, hh->dinfo, 0xffff0000);
				if (bc) { //repair reject bug
					int myprocid=bc->l3_id&0x0000ffff;
					hh->dinfo=(hh->dinfo&0xffff0000)|myprocid;
					cb_log(4,bc->port,"Repaired reject Bug, new dinfo: %x\n",hh->dinfo);
				}
			}
		}
		break;
		

		case CC_RELEASE_COMPLETE|INDICATION:
			break;

		case CC_SUSPEND|INDICATION:
		{
			msg_t *dmsg;
			cb_log(4, stack->port, " --> Got Suspend, sending Reject for now\n");
			dmsg = create_l3msg(CC_SUSPEND_REJECT | REQUEST,MT_SUSPEND_REJECT, hh->dinfo,sizeof(RELEASE_COMPLETE_t), 1);
			stack->nst.manager_l3(&stack->nst, dmsg);
			free_msg(msg);
			return 0;
		}
		break;
		case CC_RESUME|INDICATION:
			break;

		case CC_RELEASE|CONFIRM:
		{
			struct misdn_bchannel *bc=find_bc_by_l3id(stack, hh->dinfo);
			cb_log(4, stack->port, " --> RELEASE CONFIRM, sending RELEASE_COMPLETE\n");
			if (bc) misdn_lib_send_event(bc,EVENT_RELEASE_COMPLETE);
		}
		hh->prim=CC_RELEASE|INDICATION;
		break;  
		case CC_RELEASE|INDICATION:
			break;

		case CC_RELEASE_CR|INDICATION:
		{
			struct misdn_bchannel *bc=find_bc_by_l3id(stack, hh->dinfo);
			struct misdn_bchannel dummybc;
			iframe_t frm; /* fake te frm to remove callref from global callreflist */
			frm.dinfo = hh->dinfo;

			frm.addr=stack->upper_id | FLG_MSG_DOWN;


			frm.prim = CC_RELEASE_CR|INDICATION;
			cb_log(4, stack->port, " --> Faking Realease_cr for %x\n",frm.addr);
			/** removing procid **/

			if (!bc) {
				cb_log(4, stack->port, " --> Didn't found BC so temporarly creating dummy BC (l3id:%x) on this port.\n", hh->dinfo);
				memset (&dummybc,0,sizeof(dummybc));
				dummybc.port=stack->port;
				dummybc.l3_id=hh->dinfo;
				bc=&dummybc; 
			}
	
			if (bc) {
#ifdef MISDNUSER_JOLLY
				if ( (bc->l3_id & 0xff00) == 0xff00) {
					cb_log(4, stack->port, " --> Removing Process Id:%x on this port.\n", bc->l3_id&0xff);
					stack->procids[bc->l3_id&0xff] = 0 ;
				}
#else
				if ( (bc->l3_id & 0xfff0) == 0xfff0) {
					cb_log(4, stack->port, " --> Removing Process Id:%x on this port.\n", bc->l3_id&0xf);
					stack->procids[bc->l3_id&0xf] = 0 ;
	    
				}
	  
#endif

			}
			else cb_log(-1, stack->port, "Couldnt find BC so I couldnt remove the Process!!!! this is a bad port.\n");
	
			handle_cr(stack, &frm);
			free_msg(msg);
			return 0 ;
		}
      
		break;
      
		case CC_NEW_CR|INDICATION:
			/*  Got New CR for bchan, for now I handle this one in */
			/*  connect_ack, Need to be changed */
		{
			struct misdn_bchannel *bc=find_bc_by_l3id(stack, hh->dinfo);
			int l3id = *((int *)(((u_char *)msg->data)+ mISDNUSER_HEAD_SIZE));
			if (!bc) { cb_log(-1, 0, " --> In NEW_CR: didn't found bc ??\n"); return -1;};
#ifdef MISDNUSER_JOLLY
			if (((l3id&0xff00)!=0xff00) && ((bc->l3_id&0xff00)==0xff00)) {
				cb_log(4, stack->port, " --> Removing Process Id:%x on this port.\n", 0xff&bc->l3_id);
				stack->procids[bc->l3_id&0xff] = 0 ;
			}
#else
			if (((l3id&0xfff0)!=0xfff0) && ((bc->l3_id&0xfff0)==0xfff0)) {
				cb_log(4, stack->port, "Removing Process Id:%x on this port.\n", 0xf&bc->l3_id);
				stack->procids[bc->l3_id&0xf] = 0 ;
			}

#endif
			cb_log(4, stack->port, "lib: Event_ind:CC_NEW_CR : very new L3ID  is %x\n",l3id );
	
			bc->l3_id =l3id;
			cb_event(EVENT_NEW_L3ID, bc, glob_mgr->user_data);
	
	
			free_msg(msg);
			return 0;
		}
      
		case DL_ESTABLISH | INDICATION:
		case DL_ESTABLISH | CONFIRM:
		{
			cb_log(4, stack->port, "%% GOT L2 Activate Info.\n");
			stack->l2link = 1;
			
			free_msg(msg);
			return 0;
		}
		break;

		case DL_RELEASE | INDICATION:
		case DL_RELEASE | CONFIRM:
		{
			cb_log(4, stack->port, "%% GOT L2 DeActivate Info.\n");
			stack->l2link = 0;
			
			/** Clean the L3 here **/
			if (cb_clearl3_true())
				clear_l3(stack);
			
			free_msg(msg);
			return 0;
		}
		break;
		}
	}
	
  

	{
		/*  Parse Events and fire_up to App. */
		struct misdn_bchannel *bc;
		struct misdn_bchannel dummybc;
		
		enum event_e event = isdn_msg_get_event(msgs_g, msg, 1);
    
		bc=find_bc_by_l3id(stack, hh->dinfo);
    
		if (!bc) {
      
			cb_log(4, stack->port, " --> Didn't found BC so temporarly creating dummy BC (l3id:%x).\n", hh->dinfo);
			memset (&dummybc,0,sizeof(dummybc));
			dummybc.port=stack->port;
			dummybc.l3_id=hh->dinfo;
			bc=&dummybc; 
		}
		if (bc ) {
			isdn_msg_parse_event(msgs_g,msg,bc, 1);
			
			if(!isdn_get_info(msgs_g,event,1)) {
				cb_log(4, stack->port, "Unknown Event Ind: prim %x dinfo %x\n",hh->prim, hh->dinfo);
			} else {
				cb_event(event, bc, glob_mgr->user_data);
			}

      
		} else {
			cb_log(4, stack->port, "No BC found with l3id: prim %x dinfo %x\n",hh->prim, hh->dinfo);
		}

		free_msg(msg);
	}


	return 0;
}


int handle_timers(msg_t* msg)
{
	iframe_t *frm= (iframe_t*)msg->data;
	struct misdn_stack *stack; 
  
	/* Timer Stuff */
	switch (frm->prim) {
	case MGR_INITTIMER | CONFIRM:
	case MGR_ADDTIMER | CONFIRM:
	case MGR_DELTIMER | CONFIRM:
	case MGR_REMOVETIMER | CONFIRM:
		free_msg(msg);
		return(1);
	}
  
  
  
	if (frm->prim==(MGR_TIMER | INDICATION) ) {
		for (stack = glob_mgr->stack_list;
		     stack;
		     stack = stack->next) {
			itimer_t *it;
      
			if (!stack->nt) continue;
      
			it = stack->nst.tlist;
			/* find timer */
			for(it=stack->nst.tlist;
			    it;
			    it=it->next) {
				if (it->id == (int)frm->addr)
					break;
			}
			if (it) {
				int ret;
				ret = mISDN_write_frame(stack->midev, msg->data, frm->addr,
							MGR_TIMER | RESPONSE, 0, 0, NULL, TIMEOUT_1SEC);
				test_and_clear_bit(FLG_TIMER_RUNING, (long unsigned int *)&it->Flags);
				ret = it->function(it->data);
				free_msg(msg);
				return 1;
			}
		}
    
		cb_log(-1, 0, "Timer Msg without Timer ??\n");
		free_msg(msg);
		return 1;
	}
  
	return 0;
}




static int do_tone(struct misdn_bchannel *bc, int len)
{
	char buf[4096 + mISDN_HEADER_LEN];
	iframe_t *frm= (iframe_t*)buf;
	int  r;
	struct misdn_stack *stack=get_stack_by_bc(bc);
	
	if (bc->tone == TONE_NONE) return 0;

	frm->prim = DL_DATA|REQUEST;
	frm->dinfo = 0;

	frm->addr = bc->addr | FLG_MSG_DOWN ;

	
  
	bc->tone_cnt+=len;

	if (bc->tone_cnt < TONE_425_SIZE) return 1;
	
	switch(bc->tone) {
	case TONE_DIAL:
	{
		frm->len = TONE_425_SIZE;
		memcpy(&buf[mISDN_HEADER_LEN], tone_425_flip,TONE_425_SIZE);
      
		r=mISDN_write(stack->midev, buf, frm->len + mISDN_HEADER_LEN, TIMEOUT_1SEC);
		if (r<frm->len) {
			perror("Error written less than told bytes :(\n");
		}
	}
	break;

	case TONE_ALERTING:
		bc->tone_cnt2++;
    
		if (bc->tone_cnt2 <= TONE_ALERT_CNT) {
			frm->len = TONE_425_SIZE;
			memcpy(&buf[mISDN_HEADER_LEN], tone_425_flip,TONE_425_SIZE);
			r=mISDN_write(stack->midev, buf, frm->len + mISDN_HEADER_LEN, TIMEOUT_1SEC);
			if (r<frm->len) {
				perror("Error written less than told bytes :(\n");
			}
		} else if (bc->tone_cnt2 <= (TONE_ALERT_SILENCE_CNT)) {
			frm->len = TONE_SILENCE_SIZE;
			memcpy(&buf[mISDN_HEADER_LEN], tone_silence_flip ,TONE_SILENCE_SIZE);
			r=mISDN_write(stack->midev, buf, frm->len + mISDN_HEADER_LEN, TIMEOUT_1SEC);
		} else {
			bc->tone_cnt2=-1;
		}
		break;
	case TONE_BUSY:
		bc->tone_cnt2++;
    
		if (bc->tone_cnt2 <= TONE_BUSY_CNT) {
			frm->len = TONE_425_SIZE;
			memcpy(&buf[mISDN_HEADER_LEN], tone_425_flip,TONE_425_SIZE);
			r=mISDN_write(stack->midev, buf, frm->len + mISDN_HEADER_LEN, TIMEOUT_1SEC);
			if (r<frm->len) {
				perror("Error written less than told bytes :(\n");
			}
		} else if (bc->tone_cnt2 <= (TONE_BUSY_SILENCE_CNT)) {
			frm->len = TONE_SILENCE_SIZE;
			memcpy(&buf[mISDN_HEADER_LEN], tone_silence_flip ,TONE_SILENCE_SIZE);
			r=mISDN_write(stack->midev, buf, frm->len + mISDN_HEADER_LEN, TIMEOUT_1SEC);
		} else {
			bc->tone_cnt2=-1;
		}
		break;
	case TONE_FILE:
		break;
	case TONE_NONE:
		return 0;
	}
  
	bc->tone_cnt -= TONE_425_SIZE ;
	return 1;
}



int handle_bchan(msg_t *msg)
{
	iframe_t *frm= (iframe_t*)msg->data;
	struct misdn_bchannel *bc;

	//cb_log(0,0,"handle_bchan:  a:%x a&M:%x prim:%x dinfo:%x\n",frm->addr,frm->addr&STACK_ID_MASK, frm->prim, frm->dinfo);
	
	bc=find_bc_by_addr(frm->addr);
	
	if (!bc) {
		cb_log(0,0,"handle_bchan: BC not found for prim:%x with addr:%x dinfo:%x\n", frm->prim, frm->addr, frm->dinfo);
		return 0 ;
	}
	
	struct misdn_stack *stack=get_stack_by_bc(bc);
	
	if (!stack) {
		cb_log(0,0,"handle_bchan: STACK not found for prim:%x with addr:%x dinfo:%x\n", frm->prim, frm->addr, frm->dinfo);
		return 0;
	}
	
	switch (frm->prim) {
	case MGR_SETSTACK| INDICATION:
		cb_log(2, stack->port, "BCHAN: MGR_SETSTACK|IND \n");
		
		
		bc->addr = mISDN_get_layerid(stack->midev, bc->b_stid, bc->layer);
		if (!bc->addr) {
			cb_log(0,stack->port,"$$$ Get Layer (%d) Id Error: %s\n",bc->layer,strerror(errno));
			return(-EINVAL);
		}
		cb_log(4, stack->port," --> Got Adr %x\n", bc->addr);
		
		bc->upset=2;
		
		free_msg(msg);
		
		manager_bchannel_activate(bc);
		return 1;
	case MGR_DELLAYER| CONFIRM:
		cb_log(2, stack->port, "BCHAN: MGR_DELLAYER|CNF \n");
		
		free_msg(msg);
		return 1;
		
	case PH_ACTIVATE | INDICATION:
	case DL_ESTABLISH | INDICATION:
		cb_log(4, stack->port, "BCHAN: ACT Ind\n");
		free_msg(msg);
		return 1;    

	case PH_ACTIVATE | CONFIRM:
	case DL_ESTABLISH | CONFIRM:
		cb_log(4, stack->port, "BCHAN: bchan ACT Confirm\n");
		free_msg(msg);

		return 1;    
		
	case PH_DEACTIVATE | INDICATION:
	case DL_RELEASE | INDICATION:
		cb_log (4, stack->port, "BCHAN: DeACT Ind\n");
		free_msg(msg);
		return 1;
    
	case PH_DEACTIVATE | CONFIRM:
	case DL_RELEASE | CONFIRM:
		cb_log(4, stack->port, "BCHAN: DeACT Conf\n");
		free_msg(msg);
		return 1;
    
	case PH_CONTROL|INDICATION:
	{
		unsigned long cont = *((unsigned long *)&frm->data.p);
		
		cb_log(4, stack->port, "PH_CONTROL: channel:%d oad%d:%s dad%d:%s \n", bc->channel, bc->onumplan,bc->oad, bc->dnumplan,bc->dad);

		if ((cont&~DTMF_TONE_MASK) == DTMF_TONE_VAL) {
			int dtmf = cont & DTMF_TONE_MASK;
			cb_log(4, stack->port, " --> DTMF TONE: %c\n",dtmf);
			bc->dtmf=dtmf;
			cb_event(EVENT_DTMF_TONE, bc, glob_mgr->user_data);
	
			free_msg(msg);
			return 1;
		}
		if (cont == BF_REJECT) {
			cb_log(4, stack->port, " --> BF REJECT\n");
			free_msg(msg);
			return 1;
		}
		if (cont == BF_ACCEPT) {
			cb_log(4, stack->port, " --> BF ACCEPT\n");
			free_msg(msg);
			return 1;
		}
	}
	break;

	case PH_DATA|REQUEST:
	case DL_DATA|REQUEST:
		cb_log(0, stack->port, "DL_DATA REQUEST \n");
		do_tone(bc, 64);
		
		free_msg(msg);
		return 1;
		
	case PH_DATA|INDICATION:
	case DL_DATA|INDICATION:
	{
		bc->bframe = (void*)&frm->data.i;
		bc->bframe_len = frm->len;

		/** Anyway flip the bufbits **/
		if ( misdn_cap_is_speech(bc->capability) ) 
			flip_buf_bits(bc->bframe, bc->bframe_len);
		
		
#if MISDN_DEBUG
		cb_log(-1, stack->port, "DL_DATA INDICATION Len %d\n", frm->len);

#endif
		
		if (bc->active && frm->len > 0) {
			if (  !do_tone(bc, frm->len)   ) {
				
				if ( misdn_cap_is_speech(bc->capability)) {
					if ( !bc->nojitter ) {
						char buf[4096 + mISDN_HEADER_LEN];
						iframe_t *txfrm= (iframe_t*)buf;
						int len, r;
						
						len=cb_jb_empty(bc,&buf[mISDN_HEADER_LEN],frm->len);

						if (len) {
							
							if (len < frm->len) {
								cb_log(0,bc->port,"Jitterbuffer Underrun.\n");
							}
							
							txfrm->prim = DL_DATA|REQUEST;
							
							txfrm->dinfo = 0;
							
							txfrm->addr = bc->addr|FLG_MSG_DOWN; /*  | IF_DOWN; */

							txfrm->len = len;
							cb_log(9, stack->port, "Transmitting %d samples 2 misdn\n", txfrm->len);
							
							r=mISDN_write(stack->midev, buf, txfrm->len + mISDN_HEADER_LEN, 8000 );
						} else {
							//jb is filling
						}
					}
				}
				
				cb_event( EVENT_BCHAN_DATA, bc, glob_mgr->user_data);
			}
		}
		free_msg(msg);
		return 1;
	}


	case PH_DATA | CONFIRM:
	case DL_DATA|CONFIRM:
#if MISDN_DEBUG

		cb_log(-1, stack->port, "Data confirmed\n");

#endif
		free_msg(msg);
		return 1;
		break;
	case DL_DATA|RESPONSE:
#if MISDN_DEBUG
		cb_log(-1, stack->port, "Data response\n");

#endif
		break;
	}
  
	return 0;
}



int handle_frm_nt(msg_t *msg)
{
	iframe_t *frm= (iframe_t*)msg->data;
	struct misdn_stack *stack;
	int err=0;

	stack=find_stack_by_addr( frm->addr );

	
  
	if (!stack || !stack->nt) {
		return 0;
	}
  
	if ((err=stack->nst.l1_l2(&stack->nst,msg))) {
    
		if (nt_err_cnt > 0 ) {
			if (nt_err_cnt < 100) {
				nt_err_cnt++; 
				cb_log(-1, stack->port, "NT Stack sends us error: %d \n", err);
			} else if (nt_err_cnt < 105){
				cb_log(-1, stack->port, "NT Stack sends us error: %d over 100 times, so I'll stop this message\n", err);
				nt_err_cnt = - 1; 
			}
		}
		free_msg(msg);
		return 1;
		
	}
	
	return 1;
}


int handle_frm(msg_t *msg)
{
	iframe_t *frm = (iframe_t*) msg->data;
	
	struct misdn_stack *stack=find_stack_by_addr(frm->addr);

	
	cb_log(4,stack?stack->port:0,"handle_frm: frm->addr:%x frm->prim:%x\n",frm->addr,frm->prim);
	
  
	if (!stack || stack->nt) {
		return 0;
	}

	{
		struct misdn_bchannel *bc;

		if(handle_cr(stack, frm)) {
			free_msg(msg);
			return 1;
		}
    
		bc=find_bc_by_l3id(stack, frm->dinfo);
    
		if (bc ) {
			enum event_e event = isdn_msg_get_event(msgs_g, msg, 0);
			enum event_response_e response=RESPONSE_OK;
      
			isdn_msg_parse_event(msgs_g,msg,bc, 0);
			
			/** Preprocess some Events **/
			handle_event(bc, event, frm);
			/*  shoot up event to App: */
			cb_log(5, stack->port, "lib Got Prim: Addr %x prim %x dinfo %x\n",frm->addr, frm->prim, frm->dinfo);
      
			if(!isdn_get_info(msgs_g,event,0)) 
				cb_log(-1, stack->port, "Unknown Event Ind: Addr:%x prim %x dinfo %x\n",frm->addr, frm->prim, frm->dinfo);
			else 
				response=cb_event(event, bc, glob_mgr->user_data);
#if 1
			if (event == EVENT_SETUP) {
				switch (response) {
				case RESPONSE_IGNORE_SETUP_WITHOUT_CLOSE:

					cb_log(-1, stack->port, "TOTALY IGNORING SETUP \n");					
					
					break;
				case RESPONSE_IGNORE_SETUP:
					/* I think we should send CC_RELEASE_CR, but am not sure*/
					empty_chan_in_stack(stack, bc->channel);
					empty_bc(bc);
					cb_log(-1, stack->port, "GOT IGNORE SETUP\n");

					
					break;
				case RESPONSE_OK:
					cb_log(4, stack->port, "GOT SETUP OK\n");

					
					break;
				default:
					break;
				}
			}

			cb_log(5, stack->port, "Freeing Msg on prim:%x \n",frm->prim);

			
			free_msg(msg);
			return 1;
#endif
      
		} else {
	
			cb_log(-1, stack->port, "NO BC FOR STACK\n");		
			
		}

	}

	cb_log(4, stack->port, "TE_FRM_HANDLER: Returning 0 on prim:%x \n",frm->prim);

	
  
	return 0;
}


int handle_l1(msg_t *msg)
{
	iframe_t *frm = (iframe_t*) msg->data;

	struct misdn_stack *stack = find_stack_by_addr(frm->addr);

	
	int i ;
	
	if (!stack) return 0 ;
  
	switch (frm->prim) {
	case PH_ACTIVATE | CONFIRM:
	case PH_ACTIVATE | INDICATION:
		cb_log (1, stack->port, "L1: PH L1Link Up!\n");
		stack->l1link=1;
		
		if (stack->nt) {
			
			if (stack->nst.l1_l2(&stack->nst, msg))
				free_msg(msg);
		} else {
			free_msg(msg);
		}
		
		for (i=0;i<stack->b_num; i++) {
			if (stack->bc[i].evq != EVENT_NOTHING) {
				cb_log(4, stack->port, "Fireing Queued Event %s because L1 got up\n", isdn_get_info(msgs_g, stack->bc[i].evq, 0));
				misdn_lib_send_event(&stack->bc[i],stack->bc[i].evq);
				stack->bc[i].evq=EVENT_NOTHING;
			}
			
		}
		
		return 1;

	case PH_ACTIVATE | REQUEST:
		free_msg(msg);
		cb_log(1,stack->port,"L1: PH_ACTIVATE|REQUEST \n");
		
		return 1;
		
	case PH_DEACTIVATE | REQUEST:
		free_msg(msg);
		cb_log(1,stack->port,"L1: PH_DEACTIVATE|REQUEST \n");
		
		return 1;
		
	case PH_DEACTIVATE | CONFIRM:
	case PH_DEACTIVATE | INDICATION:
		cb_log (1, stack->port, "L1: PH L1Link Down! \n");
		
		for (i=0; i<stack->b_num; i++) {
			if (global_state == MISDN_INITIALIZED)  {
				cb_event(EVENT_CLEANUP, &stack->bc[i], glob_mgr->user_data);
			}
			
		}
		
		if (stack->nt) {
			if (stack->nst.l1_l2(&stack->nst, msg))
				free_msg(msg);
			
		} else {
			free_msg(msg);
		}
		
		stack->l1link=0;
		stack->l2link=0;
		
		return 1;
	}
  
	return 0;
}

int handle_l2(msg_t *msg)
{
	iframe_t *frm = (iframe_t*) msg->data;

	struct misdn_stack *stack = find_stack_by_addr(frm->addr);

	
	
	if (!stack) {
		return 0 ;
	}
	
	switch(frm->prim) {

	case DL_ESTABLISH | REQUEST:
		cb_log(1,stack->port,"DL_ESTABLISH|REQUEST \n");
		return 1;
	case DL_RELEASE | REQUEST:
		cb_log(1,stack->port,"DL_RELEASE|REQUEST \n");
		return 1;
		
	case DL_ESTABLISH | INDICATION:
	case DL_ESTABLISH | CONFIRM:
	{
		cb_log (3, stack->port, "L2: L2Link Up! \n");
		stack->l2link=1;
		free_msg(msg);
		return 1;
	}
	break;
    
	case DL_RELEASE | INDICATION:
	case DL_RELEASE | CONFIRM:
	{
		cb_log (3, stack->port, "L2: L2Link Down! \n");
		stack->l2link=0;
		
		free_msg(msg);
		return 1;
	}
	break;
	}
	return 0;
}


int handle_mgmt(msg_t *msg)
{
	iframe_t *frm = (iframe_t*) msg->data;
	


	if ( (frm->addr == 0) && (frm->prim == (MGR_DELLAYER|CONFIRM)) ) {
		cb_log(2, 0, "MGMT: DELLAYER|CONFIRM Addr: 0 !\n") ;
		free(msg);
		return 1;
	}
	
	struct misdn_stack * stack=find_stack_by_addr(frm->addr);
	
	if (!stack) {
		return 0;
	}
	
	//cb_log(1, 0, "MGMT: Found Stack: addr:%x dinfo:%x prim:%x\n",frm->addr,frm->dinfo, frm->prim) ;
	
	switch(frm->prim) {
	case MGR_SHORTSTATUS | INDICATION:
		cb_log(2, 0, "MGMT: Short status dinfo %x\n",frm->dinfo);
		
		switch (frm->dinfo) {
		case SSTATUS_L1_ACTIVATED:
			cb_log(1, 0, "MGMT: SSTATUS: L1_ACTIVATED \n");
			stack->l1link=1;
			break;
		case SSTATUS_L1_DEACTIVATED:
			cb_log(1, 0, "MGMT: SSTATUS: L1_DEACTIVATED \n");

			/*reopen L1 if down*/
			if (stack->l1link==2)
				stack->l1link--;
			else			
				stack->l1link=0;
			
			break;
		case SSTATUS_L2_ESTABLISHED:
			cb_log(1, 0, "MGMT: SSTATUS: L2_ESTABLISH \n");
			stack->l2link=1;
			
			break;
			
		case SSTATUS_L2_RELEASED:
			cb_log(1, 0, "MGMT: SSTATUS: L2_RELEASED \n");
			stack->l2link=0;
			stack->l1link=2;
			
			break;
			
		}
		
		
		free_msg(msg);
		return 1;
		
	case MGR_SETSTACK | INDICATION:
		cb_log(2, 0, "MGMT: SETSTACK|IND dinfo %x\n",frm->dinfo);
		free_msg(msg);
		return 1;
	case MGR_DELLAYER | CONFIRM:
		cb_log(2, 0, "MGMT: DELLAYER|CNF dinfo %x\n",frm->dinfo) ;
		free_msg(msg);
		return 1;
		
	}
	
	/*
	if ( (frm->prim & 0x0f0000) ==  0x0f0000) {
	cb_log(5, 0, "$$$ MGMT FRAME: prim %x addr %x dinfo %x\n",frm->prim, frm->addr, frm->dinfo) ;
	free_msg(msg);
	return 1;
	} */
    
	return 0;
}


msg_t *fetch_msg(int midev) 
{
	msg_t *msg=alloc_msg(MAX_MSG_SIZE);
	int r;
	fd_set rdfs;

	if (!msg) {
		cb_log(-1, 0, "fetch_msg: alloc msg failed !!");
		return NULL;
	}
	
	FD_ZERO(&rdfs);
	FD_SET(midev,&rdfs);
  
	mISDN_select(FD_SETSIZE, &rdfs, NULL, NULL, NULL);
  
	if (FD_ISSET(midev, &rdfs)) {
    
		r=mISDN_read(midev,msg->data,MAX_MSG_SIZE,0);
		msg->len=r;
    
		if (r==0) {
			free_msg(msg); /* danger, cauz usualy freeing in main_loop */
			printf ("Got empty Msg?\n");
			return NULL;
		}

		return msg;
	} else {
		printf ("Select timeout\n");
	}
  
	return NULL;
}


static void misdn_lib_isdn_event_catcher(void *arg)
{
	struct misdn_lib *mgr = arg;
	int zero_frm=0 , fff_frm=0 ;
	int midev= mgr->midev;
	int port=0;
	
	//cb_log(5, 0, "In event_catcher thread\n");
	
	while (1) {
		msg_t *msg = fetch_msg(midev); 
		iframe_t *frm;
		
		
		if (!msg) continue;
		
		frm = (iframe_t*) msg->data;
		
		//port = frm->addr&CHANNEL_NUMBER;
		
		
		/** When we make a call from NT2Ast we get this frames **/
		if (frm->len == 0 && frm->addr == 0 && frm->dinfo == 0 && frm->prim == 0 ) {
			zero_frm++; 
			free_msg(msg);
			continue;
		} else {
			if (zero_frm) {
				cb_log(-1, port, "*** Alert: %d zero_frms caught\n", zero_frm);
				zero_frm = 0 ;
			}
		}
		
		/** I get this sometimes after setup_bc **/
		if (frm->len == 0 &&  frm->dinfo == 0 && frm->prim == 0xffffffff ) {
			fff_frm++; 
			free_msg(msg);
			continue;
		} else {
			if (fff_frm) {
				cb_log(-1, port, "*** Alert: %d fff_frms caught\n", fff_frm);
				fff_frm = 0 ;
			}
		}
		
		manager_isdn_handler(frm, msg);
	}

}


/** App Interface **/

int te_lib_init() {
	char buff[1025];
	iframe_t *frm=(iframe_t*)buff;
	int midev=mISDN_open();
	int ret;

	memset(buff,0,1025);
  
	if  (midev<=0) return midev;
  
/* create entity for layer 3 TE-mode */
	mISDN_write_frame(midev, buff, 0, MGR_NEWENTITY | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);
	ret = mISDN_read_frame(midev, frm, sizeof(iframe_t), 0, MGR_NEWENTITY | CONFIRM, TIMEOUT_1SEC);
  
	if (ret < mISDN_HEADER_LEN) {
	noentity:
		fprintf(stderr, "cannot request MGR_NEWENTITY from mISDN: %s\n",strerror(errno));
		exit(-1);
	}
  
	entity = frm->dinfo & 0xffff ;
  
	if (!entity)
		goto noentity;

	return midev;
  
}

void te_lib_destroy(int midev)
{
	char buf[1024];
	mISDN_write_frame(midev, buf, 0, MGR_DELENTITY | REQUEST, entity, 0, NULL, TIMEOUT_1SEC);

	cb_log(4, 0, "Entetity deleted\n");
	mISDN_close(midev);
	cb_log(4, 0, "midev closed\n");
}



void misdn_lib_transfer(struct misdn_bchannel* holded_bc)
{
	holded_bc->holded=0;
}

struct misdn_bchannel *manager_find_bc_by_pid(int pid)
{
	struct misdn_stack *stack;
	int i;
  
	for (stack=glob_mgr->stack_list;
	     stack;
	     stack=stack->next) {
		for (i=0; i<stack->b_num; i++)
			if (stack->bc[i].pid == pid) return &stack->bc[i];
	}
  
	return NULL;
}

struct misdn_bchannel *manager_find_bc_holded(struct misdn_bchannel* bc)
{
	struct misdn_stack *stack=get_stack_by_bc(bc);
	return find_bc_holded(stack);
}



struct misdn_bchannel* misdn_lib_get_free_bc(int port, int channel)
{
	struct misdn_stack *stack;
	int i;
	
	if (channel < 0 || channel > MAX_BCHANS)
		return NULL;

	for (stack=glob_mgr->stack_list; stack; stack=stack->next) {
    
		if (stack->port == port) {
			if (channel > 0) {
				if (channel <= stack->b_num) {
					for (i = 0; i < stack->b_num; i++) {
						if (stack->bc[i].in_use && stack->bc[i].channel == channel) {
							return NULL;
						}
					}
				} else
					return NULL;
			}
			for (i = 0; i < stack->b_num; i++) {
				if (!stack->bc[i].in_use) {
					stack->bc[i].channel = channel;
					stack->bc[i].channel_preselected = channel?1:0;
					stack->bc[i].in_use = 1;
					return &stack->bc[i];
				}
			}
			return NULL;
		}
	}
	return NULL;
}

void misdn_lib_log_ies(struct misdn_bchannel *bc)
{
	if (!bc) return;

	struct misdn_stack *stack=get_stack_by_bc(bc);

	if (!stack) return;
	

	cb_log(2, stack->port, " --> mode:%s cause:%d ocause:%d rad:%s\n", stack->nt?"NT":"TE", bc->cause, bc->out_cause, bc->rad);
	
	cb_log(2, stack->port,
	       " --> info_dad:%s onumplan:%c dnumplan:%c rnumplan:%c\n",
	       bc->info_dad,
	       bc->onumplan>=0?'0'+bc->onumplan:' ',
	       bc->dnumplan>=0?'0'+bc->dnumplan:' ',
	       bc->rnumplan>=0?'0'+bc->rnumplan:' '
		);
	
	cb_log(2, stack->port, " --> channel:%d caps:%s pi:%x keypad:%s\n", bc->channel, bearer2str(bc->capability),bc->progress_indicator, bc->keypad);

	cb_log(3, stack->port, " --> urate:%d rate:%d mode:%d user1:%d\n", bc->urate, bc->rate, bc->mode,bc->user1);
	
	cb_log(3, stack->port, " --> pid:%d addr:%x l3id:%x\n", bc->pid, bc->addr, bc->l3_id);
	cb_log(3, stack->port, " --> b_stid:%x layer_id:%x\n", bc->b_stid, bc->layer_id);
	
	cb_log(4, stack->port, " --> bc:%x h:%d sh:%d\n", bc, bc->holded, bc->stack_holder);
}

int misdn_lib_send_event(struct misdn_bchannel *bc, enum event_e event )
{
	msg_t *msg; 
	int err = -1 ;
  
	if (!bc) goto ERR; 

	
	struct misdn_stack *stack=get_stack_by_bc(bc);


	cb_log(0,0,"SENDEVENT: stack->nt:%d stack->uperid:%x\n",stack->nt, stack->upper_id);

	if ( stack->nt && !stack->l1link) {
		/** Queue Event **/
		bc->evq=event;
		cb_log(1, stack->port, "Queueing Event %s because L1 is down (btw. Activating L1)\n", isdn_get_info(msgs_g, event, 0));
		misdn_lib_get_l1_up(stack);
		return 0;
	}
	
	cb_log(1, stack->port, "I SEND:%s oad:%s dad:%s \n", isdn_get_info(msgs_g, event, 0), bc->oad, bc->dad);
	misdn_lib_log_ies(bc);
	
	switch (event) {
	case EVENT_SETUP:
		if (create_process(glob_mgr->midev, bc)<0) {
			cb_log(-1,  stack->port, " No free channel at the moment @ send_event\n");

			err=-ENOCHAN;
			goto ERR;
		}
		break;

		

	case EVENT_CONNECT:
	case EVENT_PROCEEDING:
	case EVENT_PROGRESS:
	case EVENT_ALERTING:
	case EVENT_SETUP_ACKNOWLEDGE:
	case EVENT_RETRIEVE_ACKNOWLEDGE:
		
		if (stack->nt) {
			if (bc->channel <=0 ) { /*  else we have the channel already */
				bc->channel = find_free_chan_in_stack(stack, 0);
				if (!bc->channel) {
					cb_log(-1, stack->port, " No free channel at the moment\n");

					err=-ENOCHAN;
					goto ERR;
				}
			}
			/* Its that i generate channels */
		}

		int ret=setup_bc(bc);
		if (ret == -EINVAL){
			cb_log(-1,bc->port,"send_event: setup_bc failed\n");
			
		}
		
		
		if ( event == EVENT_CONNECT ) {
			if ( *bc->crypt_key ) {
				cb_log(4, stack->port,  " --> ENABLING BLOWFISH channel:%d oad%d:%s dad%d:%s \n", bc->channel, bc->onumplan,bc->oad, bc->dnumplan,bc->dad);
				
				manager_ph_control_block(bc,  BF_ENABLE_KEY, bc->crypt_key, strlen(bc->crypt_key) );
			}
			
			if ( misdn_cap_is_speech(bc->capability)) {
				if (!bc->nodsp) manager_ph_control(bc,  DTMF_TONE_START, 0);
				
				if (bc->ec_enable) manager_ec_enable(bc);
				
				if (bc->txgain != 0) {
					cb_log(4, stack->port,  "--> Changing txgain to %d\n", bc->txgain);
					manager_ph_control(bc, VOL_CHANGE_TX, bc->txgain);
				}
				
				if ( bc->rxgain != 0 ) {
					cb_log(4, stack->port,  "--> Changing rxgain to %d\n", bc->rxgain);
					manager_ph_control(bc, VOL_CHANGE_RX, bc->rxgain);
				}
			}
		}
		
		if (event == EVENT_RETRIEVE_ACKNOWLEDGE) {
			manager_bchannel_activate(bc);
		}
		
		break;
    
	case EVENT_HOLD_ACKNOWLEDGE:
	{
		struct misdn_bchannel *holded_bc=malloc(sizeof(struct misdn_bchannel));
		
		memcpy(holded_bc,bc,sizeof(struct misdn_bchannel));
		
		holded_bc->holded=1;
		stack_holder_add(stack,holded_bc);
		
		if (stack->nt) {
			empty_chan_in_stack(stack,bc->channel);
			empty_bc(bc);
			clean_up_bc(bc);
		}
		
		/** we set it up later at RETRIEVE_ACK again.**/
		holded_bc->upset=0;
		holded_bc->active=0;
		
		cb_event( EVENT_NEW_BC, holded_bc, glob_mgr->user_data);
	}
	break;
	
	case EVENT_RELEASE:
		break;
		
	case EVENT_RELEASE_COMPLETE:
		empty_chan_in_stack(stack,bc->channel);
		empty_bc(bc);
		clean_up_bc(bc);
		break;
    
	case EVENT_CONNECT_ACKNOWLEDGE:
		
		if (misdn_cap_is_speech(bc->capability)) {
			if (  !bc->nodsp) manager_ph_control(bc,  DTMF_TONE_START, 0);
			if (bc->ec_enable) manager_ec_enable(bc);
			if ( bc->txgain != 0 ) {
				cb_log(4, stack->port, "--> Changing txgain to %d\n", bc->txgain);
				manager_ph_control(bc, VOL_CHANGE_TX, bc->txgain);
			}
			if ( bc->rxgain != 0 ) {
				cb_log(4, stack->port, "--> Changing rxgain to %d\n", bc->rxgain);
				manager_ph_control(bc, VOL_CHANGE_RX, bc->rxgain);
			}
		}
		break;
    
	default:
		break;
	}
  
	/* Later we should think about sending bchannel data directly to misdn. */
	
	msg = isdn_msg_build_event(msgs_g, bc, event, stack->nt);

	msg_queue_tail(&stack->downqueue, msg);

	sem_post(&glob_mgr->new_msg);
  
	return 0;
  
 ERR:
	return -1; 
}



int manager_isdn_handler(iframe_t *frm ,msg_t *msg)
{  

	if (frm->dinfo==(signed long)0xffffffff && frm->prim==(PH_DATA|CONFIRM)) {
		printf("SERIOUS BUG, dinfo == 0xffffffff, prim == PH_DATA | CONFIRM !!!!\n");
	}

	if ( ((frm->addr | ISDN_PID_BCHANNEL_BIT )>> 28 ) == 0x5) {
		cb_log(9,0,"BCHANNEL_BIT\n");
		if (handle_bchan(msg)) 
			return 0 ;
	} else {
		cb_log(9,0,"NO BCHANNEL_BIT !\n");
	}
	
	if (handle_timers(msg)) 
		return 0 ;
	
	if (handle_mgmt(msg)) 
		return 0 ; 
	
	if (handle_l2(msg)) 
		return 0 ;

	/* Its important to handle l1 AFTER l2  */
	if (handle_l1(msg)) 
		return 0 ;
	
	
	/** Handle L2/3 Signalling after bchans **/ 
	if (handle_frm_nt(msg)) 
		return 0 ;
	
	if (handle_frm(msg)) 
		return 0 ;
	
	cb_log(-1, 0, "Unhandled Message: prim %x len %d from addr %x, dinfo %x on this port.\n",frm->prim, frm->len, frm->addr, frm->dinfo);		
   
	free_msg(msg);

	return 0;
}




int misdn_lib_get_port_info(int port)
{
	msg_t *msg=alloc_msg(MAX_MSG_SIZE);
	iframe_t *frm;
	struct misdn_stack *stack=find_stack_by_port(port);
	if (!msg) {
		cb_log(-1, port, "misgn_lib_get_port: alloc_msg failed!\n");
		return -1;
	}
	frm=(iframe_t*)msg->data;
	if (!stack ) {
		cb_log(-1, port, "There is no Stack for this port.\n");
		return -1;
	}
	/* activate bchannel */
	frm->prim = CC_STATUS_ENQUIRY | REQUEST;

	frm->addr = stack->upper_id| FLG_MSG_DOWN;

	frm->dinfo = 0;
	frm->len = 0;
  
	msg_queue_tail(&glob_mgr->activatequeue, msg);
	sem_post(&glob_mgr->new_msg);

  
	return 0; 
}

int misdn_lib_port_restart(int port)
{
	struct misdn_stack *stack=find_stack_by_port(port);
 
	cb_log(0, port, "Restarting this port.\n");
	if (stack) {
		cb_log(0, port, "Stack:%p\n",stack);

		
		clear_l3(stack);
		
		{
			msg_t *msg=alloc_msg(MAX_MSG_SIZE);
			iframe_t *frm;

			if (!msg) {
				cb_log(-1, port, "port_restart: alloc_msg failed\n");
				return -1;
			}
			
			frm=(iframe_t*)msg->data;
			/* we must activate if we are deactivated */
			/* activate bchannel */
	
			frm->prim = DL_RELEASE | REQUEST;

			frm->addr = stack->upper_id | FLG_MSG_DOWN;

	
			frm->dinfo = 0;
			frm->len = 0;
			msg_queue_tail(&glob_mgr->activatequeue, msg);
			sem_post(&glob_mgr->new_msg);
		}
		return 0;
    
		stack_te_destroy(stack);
      
		{
			struct misdn_stack *tmpstack;
			struct misdn_stack *newstack=stack_init(stack->midev ,port, stack->ptp);
      
      
			if (stack == glob_mgr->stack_list) {
				struct misdn_stack *n=glob_mgr->stack_list->next;
				glob_mgr->stack_list = newstack ;
				glob_mgr->stack_list->next = n;
			} else {
				for (tmpstack=glob_mgr->stack_list;
				     tmpstack->next;
				     tmpstack=tmpstack->next) 
					if (tmpstack->next == stack) break;

				if (!tmpstack->next) {
					cb_log(-1, port, "Stack to restart not found\n");
					return 0;
				}  else {
					struct misdn_stack *n=tmpstack->next->next;
					tmpstack->next=newstack;
					newstack->next=n;
				}
			}
      
			{
				int i;
				for(i=0;i<newstack->b_num; i++) {
					int r;
					if ((r=init_bc(newstack, &newstack->bc[i], newstack->midev,port,i, "", 1))<0) {
						cb_log(-1, port, "Got Err @ init_bc :%d\n",r);
						return 0;
					}
				}
			}
      
			free(stack);
		}
	}

	return 0;
}



sem_t handler_started; 

void manager_event_handler(void *arg)
{
	sem_post(&handler_started); 
	while (1) {
		struct misdn_stack *stack;
		msg_t *msg;
    
		/** wait for events **/
		sem_wait(&glob_mgr->new_msg);
    
		for (msg=msg_dequeue(&glob_mgr->activatequeue);
		     msg;
		     msg=msg_dequeue(&glob_mgr->activatequeue)
			)
		{
	
			iframe_t *frm =  (iframe_t*) msg->data ;

			switch ( frm->prim) {
			case MGR_SETSTACK | REQUEST :
				break;
			default:
				mISDN_write(glob_mgr->midev, frm, mISDN_HEADER_LEN+frm->len, TIMEOUT_1SEC);
				free_msg(msg);
			}
		}

		for (stack=glob_mgr->stack_list;
		     stack;
		     stack=stack->next ) { 
			while ( (msg=msg_dequeue(&stack->downqueue)) ) {
				
				if (stack->nt ){
					if (stack->nst.manager_l3(&stack->nst, msg))
						cb_log(-1, stack->port, "Error@ Sending Message in NT-Stack.\n");
	  
				} else {
					if (msg) {
						iframe_t *frm = (iframe_t *)msg->data;
						struct misdn_bchannel *bc = find_bc_by_l3id(stack, frm->dinfo);
	    
						if (bc) send_msg(glob_mgr->midev, bc, msg);
					}
				}
			}
		}
	}
}


int misdn_lib_maxports_get() { /** BE AWARE WE HAVE NO CB_LOG HERE! **/
	
	int i = mISDN_open();
	int max=0;
	
	if (i<0)
		return -1;

	max = mISDN_get_stack_count(i);
	
	mISDN_close(i);
	
	return max;
}

int misdn_lib_init(char *portlist, struct misdn_lib_iface *iface, void *user_data)
{
	struct misdn_lib *mgr=calloc(1, sizeof(struct misdn_lib));
	char *tok, *tokb;
	char plist[1024];
	int midev;
	int port_count=0;
 
	cb_log = iface->cb_log;
	cb_event = iface->cb_event;
	cb_jb_empty = iface->cb_jb_empty;
	cb_clearl3_true = iface->cb_clearl3_true;
	
	glob_mgr = mgr;
  
	msg_init();
	debug_init(0 , NULL, NULL, NULL);
	
	if (!portlist || (*portlist == 0) ) return 1;
	
	init_flip_bits();
	
	{
		strncpy(plist,portlist, 1024);
		plist[1023] = 0;
	}
  
	memcpy(tone_425_flip,tone_425,TONE_425_SIZE);
	flip_buf_bits(tone_425_flip,TONE_425_SIZE);

	memcpy(tone_silence_flip,tone_SILENCE,TONE_SILENCE_SIZE);
	flip_buf_bits(tone_silence_flip,TONE_SILENCE_SIZE);
  
	midev=te_lib_init();
	mgr->midev=midev;

	port_count=mISDN_get_stack_count(midev);
  
	msg_queue_init(&mgr->activatequeue);
  
	if (sem_init(&mgr->new_msg, 1, 0)<0)
		sem_init(&mgr->new_msg, 0, 0);
  
	for (tok=strtok_r(plist," ,",&tokb );
	     tok; 
	     tok=strtok_r(NULL," ,",&tokb)) {
		int port = atoi(tok);
		struct misdn_stack *stack;
		static int first=1;
		int ptp=0;
    
		if (strstr(tok, "ptp"))
			ptp=1;

		if (port > port_count) {
			cb_log(-1, port, "Couldn't Initialize this port since we have only %d ports\n", port_count);
			exit(1);
		}
		stack=stack_init(midev, port, ptp);
    
    
    
		if (!stack) {
			perror("init_stack");
			exit(1);
		}
    
		if (stack && first) {
			mgr->stack_list=stack;
			first=0;
			{
				int i;
				for(i=0;i<stack->b_num; i++) {
					int r;
					if ((r=init_bc(stack, &stack->bc[i], stack->midev,port,i, "", 1))<0) {
						cb_log(-1, port, "Got Err @ init_bc :%d\n",r);
						exit(1);
					}
				}
			}
      
			continue;
		}
    
		if (stack) {
			struct misdn_stack * help;
			for ( help=mgr->stack_list; help; help=help->next ) 
				if (help->next == NULL) break;
      
      
			help->next=stack;

			{
				int i;
				for(i=0;i<stack->b_num; i++) {
					int r;
					if ((r=init_bc(stack, &stack->bc[i], stack->midev,port,i, "",1 ))<0) {
						cb_log(-1, port, "Got Err @ init_bc :%d\n",r);
						exit(1);
					} 
				}
			}
		}
    
	}
  
	if (sem_init(&handler_started, 1, 0)<0)
		sem_init(&handler_started, 0, 0);
  
	cb_log(4, 0, "Starting Event Handler\n");
	pthread_create( &mgr->event_handler_thread, NULL,(void*)manager_event_handler, mgr);
  
	sem_wait(&handler_started) ;
	cb_log(4, 0, "Starting Event Catcher\n");
	pthread_create( &mgr->event_thread, NULL, (void*)misdn_lib_isdn_event_catcher, mgr);
  
	cb_log(4, 0, "Event Catcher started\n");
  
	global_state= MISDN_INITIALIZED; 
  
	return (mgr == NULL);
}

void misdn_lib_destroy()
{
	struct misdn_stack *help;
	int i;
  
	for ( help=glob_mgr->stack_list; help; help=help->next ) {
		for(i=0;i<help->b_num; i++) {
			char buf[1024];
			mISDN_write_frame(help->midev, buf, help->bc[i].addr, MGR_DELLAYER | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);
			help->bc[i].addr = 0;
		}
		cb_log (1, help->port, "Destroying this port.\n");
		stack_te_destroy(help);
	}
	
	if (global_state == MISDN_INITIALIZED) {
		cb_log(4, 0, "Killing Handler Thread\n");
		if ( pthread_cancel(glob_mgr->event_handler_thread) == 0 ) {
			cb_log(4, 0, "Joining Handler Thread\n");
			pthread_join(glob_mgr->event_handler_thread, NULL);
		}
	  
		cb_log(4, 0, "Killing Main Thread\n");
		if ( pthread_cancel(glob_mgr->event_thread) == 0 ) {
			cb_log(4, 0, "Joining Main Thread\n");
			pthread_join(glob_mgr->event_thread, NULL);
		}
	}
  
	cb_log(1, 0, "Closing mISDN device\n");
	te_lib_destroy(glob_mgr->midev);
}

char *manager_isdn_get_info(enum event_e event)
{
	return isdn_get_info(msgs_g , event, 0);
}

void manager_bchannel_activate(struct misdn_bchannel *bc)
{
	msg_t *msg=alloc_msg(MAX_MSG_SIZE);
	iframe_t *frm;

	struct misdn_stack *stack=get_stack_by_bc(bc);
	
	if (!msg) {
		cb_log(-1, stack->port, "bchannel_activate: alloc_msg failed !");

		return ;
	}

	if (bc->upset != 2 ) {
		cb_log(1, stack->port, "bchannel_activate: BC Not properly upsetted addr:%x\n", bc->addr);
		return;
	}
	
	
	frm=(iframe_t*)msg->data;
	/* we must activate if we are deactivated */
	clear_ibuffer(bc->astbuf);
	
	if (bc->active) return;
  
	cb_log(5, stack->port, "$$$ Bchan Activated addr %x\n", bc->addr);

	/* activate bchannel */
	frm->prim = DL_ESTABLISH | REQUEST;
	frm->addr = bc->addr | FLG_MSG_DOWN ;
	frm->dinfo = 0;
	frm->len = 0;
	
	msg_queue_tail(&glob_mgr->activatequeue, msg);
	sem_post(&glob_mgr->new_msg);

	bc->active=1;
  
	return ;
  
}


void manager_bchannel_deactivate(struct misdn_bchannel * bc)
{
	iframe_t dact;

	struct misdn_stack *stack=get_stack_by_bc(bc);
	if (!bc->active) return;
  
	cb_log(5, stack->port, "$$$ Bchan deActivated addr %x\n", bc->addr);
  
	bc->tone=TONE_NONE;

	dact.prim = DL_RELEASE | REQUEST;
	dact.addr = bc->addr | FLG_MSG_DOWN;
	dact.dinfo = 0;
	dact.len = 0;
  
	mISDN_write(stack->midev, &dact, mISDN_HEADER_LEN+dact.len, TIMEOUT_1SEC);
	clear_ibuffer(bc->astbuf);
	bc->active=0;
  
	return;
}


int misdn_lib_tx2misdn_frm(struct misdn_bchannel *bc, void *data, int len)
{
	struct misdn_stack *stack=get_stack_by_bc(bc);
	if (!bc->active) return -1;   
	
	char buf[4096 + mISDN_HEADER_LEN];
	iframe_t *frm= (iframe_t*)buf;
	int  r;
	
	frm->prim = DL_DATA|REQUEST;
	frm->dinfo = 0;
	frm->addr = bc->addr | FLG_MSG_DOWN ;
	
	frm->len = len;
	memcpy(&buf[mISDN_HEADER_LEN], data,len);
	
	if ( ! misdn_cap_is_speech(bc->capability))
		cb_log(6, stack->port, "Writing %d data bytes\n",len);
	
	cb_log(9, stack->port, "Writing %d bytes 2 mISDN\n",len);
	r=mISDN_write(stack->midev, buf, frm->len + mISDN_HEADER_LEN, TIMEOUT_INFINIT);
	
	return 0;
}




void manager_send_tone (struct misdn_bchannel *bc, enum tone_e tone)
{
	if (tone != TONE_NONE) manager_bchannel_activate(bc);
	bc->tone=tone;
	bc->tone_cnt2=-1;
	bc->tone_cnt=0;
}



/*
 * send control information to the channel (dsp-module)
 */
void manager_ph_control(struct misdn_bchannel *bc, int c1, int c2)
{
	unsigned char buffer[mISDN_HEADER_LEN+sizeof(int)+sizeof(int)];
	iframe_t *ctrl = (iframe_t *)buffer; /* preload data */
	unsigned long *d = (unsigned long *)&ctrl->data.p;
	struct misdn_stack *stack=get_stack_by_bc(bc);
	
	ctrl->prim = PH_CONTROL | REQUEST;
	ctrl->addr = bc->addr | FLG_MSG_DOWN;
	ctrl->dinfo = 0;
	ctrl->len = sizeof(unsigned long)*2;
	*d++ = c1;
	*d++ = c2;
	mISDN_write(stack->midev, ctrl, mISDN_HEADER_LEN+ctrl->len, TIMEOUT_1SEC);
}

/*
 * send control information to the channel (dsp-module)
 */
void manager_ph_control_block(struct misdn_bchannel *bc, int c1, void *c2, int c2_len)
{
	unsigned char buffer[mISDN_HEADER_LEN+sizeof(int)+c2_len];
	iframe_t *ctrl = (iframe_t *)buffer;
	unsigned long *d = (unsigned long *)&ctrl->data.p;
	struct misdn_stack *stack=get_stack_by_bc(bc);
	
	ctrl->prim = PH_CONTROL | REQUEST;
	ctrl->addr = bc->addr | FLG_MSG_DOWN;
	ctrl->dinfo = 0;
	ctrl->len = sizeof(unsigned long) + c2_len;
	*d++ = c1;
	memcpy(d, c2, c2_len);
	mISDN_write(stack->midev, ctrl, mISDN_HEADER_LEN+ctrl->len, TIMEOUT_1SEC);
}




void manager_clean_bc(struct misdn_bchannel *bc )
{
	struct misdn_stack *stack=get_stack_by_bc(bc);
	
	if (bc->state == STATE_CONNECTED)
		misdn_lib_send_event(bc,EVENT_DISCONNECT);

	empty_chan_in_stack(stack, bc->channel);
	empty_bc(bc);
  
	misdn_lib_send_event(bc,EVENT_RELEASE_COMPLETE);
}


void stack_holder_add(struct misdn_stack *stack, struct misdn_bchannel *holder)
{
	struct misdn_bchannel *help;
	cb_log(4,stack->port, "*HOLDER: add %x\n",holder->l3_id);
	
	holder->stack_holder=1;

	if (!stack ) return ;
	
	holder->next=NULL;
	
	if (!stack->holding) {
		stack->holding = holder;
		return;
	}
  
	for (help=stack->holding;
	     help;
	     help=help->next) {
		if (!help->next) {
			help->next=holder;
		}
	}
  
}

void stack_holder_remove(struct misdn_stack *stack, struct misdn_bchannel *holder)
{
	struct misdn_bchannel *h1;

	if (!holder->stack_holder) return;
	
	cb_log(4,stack->port, "*HOLDER: remove %x\n",holder->l3_id);
	if (!stack || ! stack->holding) return;
  
	if (holder == stack->holding) {
		stack->holding = stack->holding->next;
		return;
	}
	
	for (h1=stack->holding;
	     h1;
	     h1=h1->next) {
		if (h1->next == holder) {
			h1->next=h1->next->next;
			return ;
		}
	}
}


struct misdn_bchannel *stack_holder_find(struct misdn_stack *stack, unsigned long l3id)
{
	struct misdn_bchannel *help;

	cb_log(4,stack?stack->port:0, "*HOLDER: find %x\n",l3id);
	
	if (!stack) return NULL;
	
	for (help=stack->holding;
	     help;
	     help=help->next) {
		if (help->l3_id == l3id) {
			cb_log(4,stack->port, "*HOLDER: found bc\n");
			return help;
		}
	}

	cb_log(4,stack->port, "*HOLDER: find nothing\n");
	return NULL;
}



void manager_ec_enable(struct misdn_bchannel *bc)
{
	int ec_arr[2];

	struct misdn_stack *stack=get_stack_by_bc(bc);
	
	cb_log(1, stack?stack->port:0,"Sending Control ECHOCAN_ON taps:%d training:%d\n",bc->ec_deftaps, bc->ec_training);
	
	switch (bc->ec_deftaps) {
	case 4:
	case 8:
	case 16:
	case 32:
	case 64:
	case 128:
	case 256:
	case 512:
	case 1024:
		cb_log(4, stack->port, "Taps is %d\n",bc->ec_deftaps);
		break;
	default:
		cb_log(-1, stack->port, "Taps should be power of 2\n");
		bc->ec_deftaps=128;
	}

	ec_arr[0]=bc->ec_deftaps;
	ec_arr[1]=bc->ec_training;
	
	manager_ph_control_block(bc,  ECHOCAN_ON,  ec_arr, sizeof(ec_arr));
}



void manager_ec_disable(struct misdn_bchannel *bc)
{
	struct misdn_stack *stack=get_stack_by_bc(bc);
	
	cb_log(1, stack?stack->port:0, "Sending Control ECHOCAN_OFF\n");
	manager_ph_control(bc,  ECHOCAN_OFF, 0);
}

struct misdn_stack* get_misdn_stack() {
	return glob_mgr->stack_list;
}



void misdn_lib_bridge( struct misdn_bchannel * bc1, struct misdn_bchannel *bc2) {
	manager_ph_control(bc1, CMX_RECEIVE_OFF, 0);
	manager_ph_control(bc2, CMX_RECEIVE_OFF, 0);
	
	manager_ph_control(bc1, CMX_CONF_JOIN, (bc1->pid<<1) +1);
	manager_ph_control(bc2, CMX_CONF_JOIN, (bc1->pid<<1) +1);
}

void misdn_lib_split_bridge( struct misdn_bchannel * bc1, struct misdn_bchannel *bc2)
{
	
	manager_ph_control(bc1, CMX_RECEIVE_ON, 0) ;
	manager_ph_control(bc2, CMX_RECEIVE_ON, 0);
	
	manager_ph_control(bc1, CMX_CONF_SPLIT, (bc1->pid<<1) +1);
	manager_ph_control(bc2, CMX_CONF_SPLIT, (bc1->pid<<1) +1);
	
}
