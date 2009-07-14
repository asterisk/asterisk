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

/*! \file
 * \brief Interface to mISDN
 * \author Christian Richter <crich@beronet.com>
 */



#include <syslog.h>
#include <sys/time.h>
#include <mISDNuser/isdn_debug.h>

#include "isdn_lib_intern.h"
#include "isdn_lib.h"

enum event_response_e (*cb_event)(enum event_e event, struct misdn_bchannel *bc, void *user_data);

void (*cb_log)(int level, int port, char *tmpl, ...)
	__attribute__ ((format (printf, 3, 4)));

int (*cb_jb_empty)(struct misdn_bchannel *bc, char *buffer, int len);


/*
 * Define ARRAY_LEN() because I cannot
 * #include "asterisk/utils.h"
 */
#define ARRAY_LEN(a) (sizeof(a) / sizeof(a[0]))

#include "asterisk/causes.h"

void misdn_join_conf(struct misdn_bchannel *bc, int conf_id);
void misdn_split_conf(struct misdn_bchannel *bc, int conf_id);

int queue_cleanup_bc(struct misdn_bchannel *bc) ;

int misdn_lib_get_l2_up(struct misdn_stack *stack);

struct misdn_stack *get_misdn_stack(void);

int misdn_lib_port_is_pri(int port)
{
	struct misdn_stack *stack=get_misdn_stack();
	for ( ; stack; stack=stack->next) {
		if (stack->port == port) {
			return stack->pri;
		}
	}

	return -1;
}

int misdn_lib_port_is_nt(int port)
{
	struct misdn_stack *stack=get_misdn_stack();
	for ( ; stack; stack=stack->next) {
		if (stack->port == port) {
			return stack->nt;
		}
	}

	return -1;
}

void misdn_make_dummy(struct misdn_bchannel *dummybc, int port, int l3id, int nt, int channel)
{
	memset (dummybc,0,sizeof(struct misdn_bchannel));
	dummybc->port=port;
	if (l3id==0)
		dummybc->l3_id = MISDN_ID_DUMMY;
	else
		dummybc->l3_id=l3id;

	dummybc->nt=nt;
	dummybc->dummy=1;
	dummybc->channel=channel;
}

int misdn_lib_port_block(int port)
{
	struct misdn_stack *stack=get_misdn_stack();
	for ( ; stack; stack=stack->next) {
		if (stack->port == port) {
			stack->blocked=1;
			return 0;
		}
	}
	return -1;

}

int misdn_lib_port_unblock(int port)
{
	struct misdn_stack *stack=get_misdn_stack();
	for ( ; stack; stack=stack->next) {
		if (stack->port == port) {
			stack->blocked=0;
			return 0;
		}
	}
	return -1;

}

int misdn_lib_is_port_blocked(int port)
{
	struct misdn_stack *stack=get_misdn_stack();
	for ( ; stack; stack=stack->next) {
		if (stack->port == port) {
			return stack->blocked;
		}
	}
	return -1;
}

int misdn_lib_is_ptp(int port)
{
	struct misdn_stack *stack=get_misdn_stack();
	for ( ; stack; stack=stack->next) {
		if (stack->port == port) return stack->ptp;
	}
	return -1;
}

int misdn_lib_get_maxchans(int port)
{
	struct misdn_stack *stack=get_misdn_stack();
	for ( ; stack; stack=stack->next) {
		if (stack->port == port) {
			if (stack->pri)
				return 30;
			else
				return 2;
		}
	}
	return -1;
}


struct misdn_stack *get_stack_by_bc(struct misdn_bchannel *bc)
{
	struct misdn_stack *stack = get_misdn_stack();

	if (!bc)
		return NULL;

	for ( ; stack; stack = stack->next) {
		if (bc->port == stack->port)
			return stack;
	}

	return NULL;
}


void get_show_stack_details(int port, char *buf)
{
	struct misdn_stack *stack = get_misdn_stack();

	for (; stack; stack = stack->next) {
		if (stack->port == port) {
			break;
		}
	}

	if (stack) {
		sprintf(buf, "* Port %2d Type %s Prot. %s L2Link %s L1Link:%s Blocked:%d",
			stack->port,
			stack->nt ? "NT" : "TE",
			stack->ptp ? "PTP" : "PMP",
			stack->l2link ? "UP  " : "DOWN",
			stack->l1link ? "UP  " : "DOWN",
			stack->blocked);
	} else {
		buf[0] = 0;
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
	/*! \brief mISDN device handle returned by mISDN_open() */
	int midev;
	int midev_nt;	/* Not used */

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

void misdn_tx_jitter(struct misdn_bchannel *bc, int len);

struct misdn_bchannel *find_bc_by_l3id(struct misdn_stack *stack, unsigned long l3id);

struct misdn_bchannel *find_bc_by_confid(unsigned long confid);

int setup_bc(struct misdn_bchannel *bc);

int manager_isdn_handler(iframe_t *frm ,msg_t *msg);

int misdn_lib_port_restart(int port);
int misdn_lib_pid_restart(int pid);

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

static int entity;

static struct misdn_lib *glob_mgr;

static char tone_425_flip[TONE_425_SIZE];
static char tone_silence_flip[TONE_SILENCE_SIZE];

static void misdn_lib_isdn_event_catcher(void *arg);
static int handle_event_nt(void *dat, void *arg);


void stack_holder_add(struct misdn_stack *stack, struct misdn_bchannel *holder);
void stack_holder_remove(struct misdn_stack *stack, struct misdn_bchannel *holder);
struct misdn_bchannel *stack_holder_find(struct misdn_stack *stack, unsigned long l3id);

/* from isdn_lib.h */
	/* user iface */
void te_lib_destroy(int midev) ;
struct misdn_bchannel *manager_find_bc_by_pid(int pid);
struct misdn_bchannel *manager_find_bc_holded(struct misdn_bchannel* bc);
void manager_ph_control_block(struct misdn_bchannel *bc, int c1, void *c2, int c2_len);
void manager_clean_bc(struct misdn_bchannel *bc );
void manager_bchannel_setup (struct misdn_bchannel *bc);
void manager_bchannel_cleanup (struct misdn_bchannel *bc);

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

static void init_flip_bits(void)
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

static char * flip_buf_bits ( char * buf , int len)
{
	int i;
	char * start = buf;

	for (i = 0 ; i < len; i++) {
		buf[i] = flip_table[(unsigned char)buf[i]];
	}

	return start;
}




static msg_t *create_l2msg(int prim, int dinfo, int size) /* NT only */
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


static int send_msg (int midev, struct misdn_bchannel *bc, msg_t *dmsg)
{
	iframe_t *frm = (iframe_t *)dmsg->data;
	struct misdn_stack *stack=get_stack_by_bc(bc);

	if (!stack) {
		cb_log(0,bc->port,"send_msg: IEK!! no stack\n ");
		return -1;
	}

	frm->addr = (stack->upper_id | FLG_MSG_DOWN);
	frm->dinfo = bc->l3_id;
	frm->len = (dmsg->len) - mISDN_HEADER_LEN;

	cb_log(4,stack->port,"Sending msg, prim:%x addr:%x dinfo:%x\n",frm->prim,frm->addr,frm->dinfo);

	mISDN_write(midev, dmsg->data, dmsg->len, TIMEOUT_1SEC);
	free_msg(dmsg);

	return 0;
}


static int mypid=1;


int misdn_cap_is_speech(int cap)
/** Poor mans version **/
{
	if ( (cap != INFO_CAPABILITY_DIGITAL_UNRESTRICTED) &&
	     (cap != INFO_CAPABILITY_DIGITAL_RESTRICTED) ) return 1;
	return 0;
}

int misdn_inband_avail(struct misdn_bchannel *bc)
{

	if (!bc->early_bconnect) {
		/* We have opted to never receive any available inband recorded messages */
		return 0;
	}

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


static void dump_chan_list(struct misdn_stack *stack)
{
	int i;

	for (i = 0; i <= stack->b_num; ++i) {
		cb_log(6, stack->port, "Idx:%d stack->cchan:%d in_use:%d Chan:%d\n",
			i, stack->channels[i], stack->bc[i].in_use, i + 1);
	}
#if defined(AST_MISDN_ENHANCEMENTS)
	for (i = MAX_BCHANS + 1; i < ARRAY_LEN(stack->bc); ++i) {
		if (stack->bc[i].in_use) {
			cb_log(6, stack->port, "Idx:%d stack->cchan:%d REGISTER Chan:%d in_use\n",
				i, stack->channels[i], i + 1);
		}
	}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */
}


void misdn_dump_chanlist(void)
{
	struct misdn_stack *stack=get_misdn_stack();
	for ( ; stack; stack=stack->next) {
		dump_chan_list(stack);
	}

}

static int set_chan_in_stack(struct misdn_stack *stack, int channel)
{
	cb_log(4,stack->port,"set_chan_in_stack: %d\n",channel);
	dump_chan_list(stack);
	if (1 <= channel && channel <= ARRAY_LEN(stack->channels)) {
		if (!stack->channels[channel-1])
			stack->channels[channel-1] = 1;
		else {
			cb_log(4,stack->port,"channel already in use:%d\n", channel );
			return -1;
		}
	} else {
		cb_log(0,stack->port,"couldn't set channel %d in\n", channel );
		return -1;
	}

	return 0;
}



static int find_free_chan_in_stack(struct misdn_stack *stack, struct misdn_bchannel *bc, int channel, int dec)
{
	int i;
	int chan = 0;
	int bnums;

	if (bc->channel_found) {
		return 0;
	}

	bc->channel_found = 1;

#if defined(AST_MISDN_ENHANCEMENTS)
	if (bc->is_register_pool) {
		for (i = MAX_BCHANS + 1; i < ARRAY_LEN(stack->channels); ++i) {
			if (!stack->channels[i]) {
				chan = i + 1;
				cb_log(3, stack->port, " --> found REGISTER chan: %d\n", chan);
				break;
			}
		}
	} else
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */
	{
		cb_log(5, stack->port, "find_free_chan: req_chan:%d\n", channel);

		if (channel < 0 || channel > MAX_BCHANS) {
			cb_log(0, stack->port, " !! out of bound call to find_free_chan_in_stack! (ch:%d)\n", channel);
			return 0;
		}

		--channel;

		bnums = stack->pri ? stack->b_num : stack->b_num - 1;
		if (dec) {
			for (i = bnums; i >= 0; --i) {
				if (i != 15 && (channel < 0 || i == channel)) { /* skip E1 D channel ;) and work with chan preselection */
					if (!stack->channels[i]) {
						chan = i + 1;
						cb_log(3, stack->port, " --> found chan%s: %d\n", channel >= 0 ? " (preselected)" : "", chan);
						break;
					}
				}
			}
		} else {
			for (i = 0; i <= bnums; ++i) {
				if (i != 15 && (channel < 0 || i == channel)) { /* skip E1 D channel ;) and work with chan preselection */
					if (!stack->channels[i]) {
						chan = i + 1;
						cb_log(3, stack->port, " --> found chan%s: %d\n", channel >= 0 ? " (preselected)" : "", chan);
						break;
					}
				}
			}
		}
	}

	if (!chan) {
		cb_log(1, stack->port, " !! NO FREE CHAN IN STACK\n");
		dump_chan_list(stack);
		bc->out_cause = AST_CAUSE_NORMAL_CIRCUIT_CONGESTION;
		return -1;
	}

	if (set_chan_in_stack(stack, chan) < 0) {
		cb_log(0, stack->port, "Channel Already in use:%d\n", chan);
		bc->out_cause = AST_CAUSE_REQUESTED_CHAN_UNAVAIL;
		return -1;
	}

	bc->channel = chan;
	return 0;
}

static void empty_chan_in_stack(struct misdn_stack *stack, int channel)
{
	if (channel < 1 || ARRAY_LEN(stack->channels) < channel) {
		cb_log(0, stack->port, "empty_chan_in_stack: cannot empty channel %d\n", channel);
		return;
	}

	cb_log(4, stack->port, "empty_chan_in_stack: %d\n", channel);
	stack->channels[channel - 1] = 0;
	dump_chan_list(stack);
}

char *bc_state2str(enum bchannel_state state) {
	int i;

	struct bchan_state_s {
		char *n;
		enum bchannel_state s;
	} states[] = {
		{"BCHAN_CLEANED", BCHAN_CLEANED },
		{"BCHAN_EMPTY", BCHAN_EMPTY},
		{"BCHAN_SETUP", BCHAN_SETUP},
		{"BCHAN_SETUPED", BCHAN_SETUPED},
		{"BCHAN_ACTIVE", BCHAN_ACTIVE},
		{"BCHAN_ACTIVATED", BCHAN_ACTIVATED},
		{"BCHAN_BRIDGE",  BCHAN_BRIDGE},
		{"BCHAN_BRIDGED", BCHAN_BRIDGED},
		{"BCHAN_RELEASE", BCHAN_RELEASE},
		{"BCHAN_RELEASED", BCHAN_RELEASED},
		{"BCHAN_CLEAN", BCHAN_CLEAN},
		{"BCHAN_CLEAN_REQUEST", BCHAN_CLEAN_REQUEST},
		{"BCHAN_ERROR", BCHAN_ERROR}
	};

	for (i=0; i< sizeof(states)/sizeof(struct bchan_state_s); i++)
		if ( states[i].s == state)
			return states[i].n;

	return "UNKNOWN";
}

void bc_state_change(struct misdn_bchannel *bc, enum bchannel_state state)
{
	cb_log(5,bc->port,"BC_STATE_CHANGE: l3id:%x from:%s to:%s\n",
		bc->l3_id,
	       bc_state2str(bc->bc_state),
	       bc_state2str(state) );

	switch (state) {
		case BCHAN_ACTIVATED:
			if (bc->next_bc_state ==  BCHAN_BRIDGED) {
				misdn_join_conf(bc, bc->conf_id);
				bc->next_bc_state = BCHAN_EMPTY;
				return;
			}
		default:
			bc->bc_state=state;
			break;
	}
}

static void bc_next_state_change(struct misdn_bchannel *bc, enum bchannel_state state)
{
	cb_log(5,bc->port,"BC_NEXT_STATE_CHANGE: from:%s to:%s\n",
	       bc_state2str(bc->next_bc_state),
	       bc_state2str(state) );

	bc->next_bc_state=state;
}


static void empty_bc(struct misdn_bchannel *bc)
{
	bc->caller.presentation = 0;	/* allowed */
	bc->caller.number_plan = NUMPLAN_ISDN;
	bc->caller.number_type = NUMTYPE_UNKNOWN;
	bc->caller.name[0] = 0;
	bc->caller.number[0] = 0;
	bc->caller.subaddress[0] = 0;

	bc->connected.presentation = 0;	/* allowed */
	bc->connected.number_plan = NUMPLAN_ISDN;
	bc->connected.number_type = NUMTYPE_UNKNOWN;
	bc->connected.name[0] = 0;
	bc->connected.number[0] = 0;
	bc->connected.subaddress[0] = 0;

	bc->redirecting.from.presentation = 0;	/* allowed */
	bc->redirecting.from.number_plan = NUMPLAN_ISDN;
	bc->redirecting.from.number_type = NUMTYPE_UNKNOWN;
	bc->redirecting.from.name[0] = 0;
	bc->redirecting.from.number[0] = 0;
	bc->redirecting.from.subaddress[0] = 0;

	bc->redirecting.to.presentation = 0;	/* allowed */
	bc->redirecting.to.number_plan = NUMPLAN_ISDN;
	bc->redirecting.to.number_type = NUMTYPE_UNKNOWN;
	bc->redirecting.to.name[0] = 0;
	bc->redirecting.to.number[0] = 0;
	bc->redirecting.to.subaddress[0] = 0;

	bc->redirecting.reason = mISDN_REDIRECTING_REASON_UNKNOWN;
	bc->redirecting.count = 0;
	bc->redirecting.to_changed = 0;

	bc->dummy=0;

	bc->bframe_len=0;

	bc->cw= 0;

	bc->dec=0;
	bc->channel = 0;

	bc->sending_complete = 0;

	bc->restart_channel=0;

	bc->conf_id = 0;

	bc->need_more_infos = 0;

	bc->send_dtmf=0;
	bc->nodsp=0;
	bc->nojitter=0;

	bc->time_usec=0;

	bc->rxgain=0;
	bc->txgain=0;

	bc->crypt=0;
	bc->curptx=0; bc->curprx=0;

	bc->crypt_key[0] = 0;

	bc->generate_tone=0;
	bc->tone_cnt=0;

	bc->active = 0;

	bc->early_bconnect = 1;

#ifdef MISDN_1_2
	*bc->pipeline = 0;
#else
	bc->ec_enable = 0;
	bc->ec_deftaps = 128;
#endif

	bc->AOCD_need_export = 0;

	bc->orig=0;

	bc->cause = AST_CAUSE_NORMAL_CLEARING;
	bc->out_cause = AST_CAUSE_NORMAL_CLEARING;

	bc->display_connected = 0;	/* none */
	bc->display_setup = 0;	/* none */

	bc->outgoing_colp = 0;/* pass */

	bc->presentation = 0;	/* allowed */
	bc->set_presentation = 0;

	bc->notify_description_code = mISDN_NOTIFY_CODE_INVALID;

	bc->evq=EVENT_NOTHING;

	bc->progress_coding=0;
	bc->progress_location=0;
	bc->progress_indicator=0;

#if defined(AST_MISDN_ENHANCEMENTS)
	bc->div_leg_3_rx_wanted = 0;
	bc->div_leg_3_tx_pending = 0;
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

/** Set Default Bearer Caps **/
	bc->capability=INFO_CAPABILITY_SPEECH;
	bc->law=INFO_CODEC_ALAW;
	bc->mode=0;
	bc->rate=0x10;
	bc->user1=0;
	bc->urate=0;

	bc->hdlc=0;

	bc->dialed.number_plan = NUMPLAN_ISDN;
	bc->dialed.number_type = NUMTYPE_UNKNOWN;
	bc->dialed.number[0] = 0;
	bc->dialed.subaddress[0] = 0;

	bc->info_dad[0] = 0;
	bc->display[0] = 0;
	bc->infos_pending[0] = 0;
	bc->uu[0]=0;
	bc->uulen=0;

	bc->fac_in.Function = Fac_None;
	bc->fac_out.Function = Fac_None;

	bc->te_choose_channel = 0;
	bc->channel_found= 0;

	gettimeofday(&bc->last_used, NULL);
}


static int clean_up_bc(struct misdn_bchannel *bc)
{
	int ret=0;
	unsigned char buff[32];
	struct misdn_stack * stack;

	cb_log(3, bc?bc->port:0, "$$$ CLEANUP CALLED pid:%d\n", bc?bc->pid:-1);

	if (!bc  ) return -1;
	stack=get_stack_by_bc(bc);

	if (!stack) return -1;

	switch (bc->bc_state ) {
	case BCHAN_CLEANED:
		cb_log(5, stack->port, "$$$ Already cleaned up bc with stid :%x\n", bc->b_stid);
		return -1;

	default:
		break;
	}

	cb_log(2, stack->port, "$$$ Cleaning up bc with stid :%x pid:%d\n", bc->b_stid, bc->pid);

	manager_ec_disable(bc);

	manager_bchannel_deactivate(bc);

	mISDN_write_frame(stack->midev, buff, bc->layer_id|FLG_MSG_TARGET|FLG_MSG_DOWN, MGR_DELLAYER | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);

	bc->b_stid = 0;
	bc_state_change(bc, BCHAN_CLEANED);

	return ret;
}



static void clear_l3(struct misdn_stack *stack)
{
	int i;

	if (global_state == MISDN_INITIALIZED) {
		for (i = 0; i <= stack->b_num; ++i) {
			cb_event(EVENT_CLEANUP, &stack->bc[i], NULL);
			empty_chan_in_stack(stack, i + 1);
			empty_bc(&stack->bc[i]);
			clean_up_bc(&stack->bc[i]);
			stack->bc[i].in_use = 0;
		}
#if defined(AST_MISDN_ENHANCEMENTS)
		for (i = MAX_BCHANS + 1; i < ARRAY_LEN(stack->bc); ++i) {
			empty_chan_in_stack(stack, i + 1);
			empty_bc(&stack->bc[i]);
			stack->bc[i].in_use = 0;
		}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */
	}
}

static int new_te_id = 0;

static int misdn_lib_get_l1_down(struct misdn_stack *stack)
{
	/* Pull Up L1 */
	iframe_t act;
	act.prim = PH_DEACTIVATE | REQUEST;
	act.addr = stack->lower_id|FLG_MSG_DOWN;
	act.dinfo = 0;
	act.len = 0;

	cb_log(1, stack->port, "SENDING PH_DEACTIVATE | REQ\n");
	return mISDN_write(stack->midev, &act, mISDN_HEADER_LEN+act.len, TIMEOUT_1SEC);
}


static int misdn_lib_get_l2_down(struct misdn_stack *stack)
{

	if (stack->ptp && (stack->nt) ) {
		msg_t *dmsg;
		/* L2 */
		dmsg = create_l2msg(DL_RELEASE| REQUEST, 0, 0);

		if (stack->nst.manager_l3(&stack->nst, dmsg))
			free_msg(dmsg);

	} else {
		iframe_t act;

		act.prim = DL_RELEASE| REQUEST;
		act.addr = (stack->upper_id |FLG_MSG_DOWN)  ;

		act.dinfo = 0;
		act.len = 0;
		return mISDN_write(stack->midev, &act, mISDN_HEADER_LEN+act.len, TIMEOUT_1SEC);
	}

	return 0;
}


static int misdn_lib_get_l1_up(struct misdn_stack *stack)
{
	/* Pull Up L1 */
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

#if 0
static int misdn_lib_get_l2_te_ptp_up(struct misdn_stack *stack)
{
	iframe_t act;

	act.prim = DL_ESTABLISH | REQUEST;
	act.addr = (stack->upper_id  & ~LAYER_ID_MASK) | 3 | FLG_MSG_DOWN;

	act.dinfo = 0;
	act.len = 0;
	return mISDN_write(stack->midev, &act, mISDN_HEADER_LEN+act.len, TIMEOUT_1SEC);
	return 0;
}
#endif

static int misdn_lib_get_short_status(struct misdn_stack *stack)
{
	iframe_t act;


	act.prim = MGR_SHORTSTATUS | REQUEST;

	act.addr = (stack->upper_id | MSG_BROADCAST)  ;

	act.dinfo = SSTATUS_BROADCAST_BIT | SSTATUS_ALL;

	act.len = 0;
	return mISDN_write(stack->midev, &act, mISDN_HEADER_LEN+act.len, TIMEOUT_1SEC);
}



static int create_process(int midev, struct misdn_bchannel *bc)
{
	iframe_t ncr;
	int l3_id;
	int proc_id;
	struct misdn_stack *stack;

	stack = get_stack_by_bc(bc);
	if (stack->nt) {
		if (find_free_chan_in_stack(stack, bc, bc->channel_preselected ? bc->channel : 0, 0) < 0) {
			return -1;
		}
		cb_log(4, stack->port, " -->  found channel: %d\n", bc->channel);

		for (proc_id = 0; proc_id < MAXPROCS; ++proc_id) {
			if (stack->procids[proc_id] == 0) {
				break;
			}
		}
		if (proc_id == MAXPROCS) {
			cb_log(0, stack->port, "Couldn't Create New ProcId.\n");
			return -1;
		}

		stack->procids[proc_id] = 1;

		l3_id = 0xff00 | proc_id;
		bc->l3_id = l3_id;
		cb_log(3, stack->port, " --> new_l3id %x\n", l3_id);
	} else {
		if ((stack->pri && stack->ptp) || bc->te_choose_channel) {
			/* we know exactly which channels are in use */
			if (find_free_chan_in_stack(stack, bc, bc->channel_preselected ? bc->channel : 0, bc->dec) < 0) {
				return -1;
			}
			cb_log(2, stack->port, " -->  found channel: %d\n", bc->channel);
		} else {
			/* other phones could have made a call also on this port (ptmp) */
			bc->channel = 0xff;
		}

		/* if we are in te-mode, we need to create a process first */
		if (++new_te_id > 0xffff) {
			new_te_id = 0x0001;
		}

		l3_id = (entity << 16) | new_te_id;
		bc->l3_id = l3_id;
		cb_log(3, stack->port, "--> new_l3id %x\n", l3_id);

		/* send message */
		ncr.prim = CC_NEW_CR | REQUEST;
		ncr.addr = (stack->upper_id | FLG_MSG_DOWN);
		ncr.dinfo = l3_id;
		ncr.len = 0;
		mISDN_write(midev, &ncr, mISDN_HEADER_LEN + ncr.len, TIMEOUT_1SEC);
	}

	return l3_id;
}


void misdn_lib_setup_bc(struct misdn_bchannel *bc)
{
	clean_up_bc(bc);
	setup_bc(bc);
}


int setup_bc(struct misdn_bchannel *bc)
{
	unsigned char buff[1025];
	int midev;
	int channel;
	int b_stid;
	int i;
	mISDN_pid_t pid;
	int ret;

	struct misdn_stack *stack=get_stack_by_bc(bc);

	if (!stack) {
		cb_log(0, bc->port, "setup_bc: NO STACK FOUND!!\n");
		return -1;
	}

	midev = stack->midev;
	channel = bc->channel - 1 - (bc->channel > 16);
	b_stid = stack->b_stids[channel >= 0 ? channel : 0];

	switch (bc->bc_state) {
		case BCHAN_CLEANED:
			break;
		default:
			cb_log(4, stack->port, "$$$ bc already setup stid :%x (state:%s)\n", b_stid, bc_state2str(bc->bc_state) );
			return -1;
	}

	cb_log(5, stack->port, "$$$ Setting up bc with stid :%x\n", b_stid);

	/*check if the b_stid is already initialized*/
	for (i=0; i <= stack->b_num; i++) {
		if (stack->bc[i].b_stid == b_stid) {
			cb_log(0, bc->port, "setup_bc: b_stid:%x already in use !!!\n", b_stid);
			return -1;
		}
	}

	if (b_stid <= 0) {
		cb_log(0, stack->port," -- Stid <=0 at the moment in channel:%d\n",channel);

		bc_state_change(bc,BCHAN_ERROR);
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
		if ( bc->hdlc || bc->nodsp) {
			cb_log(4, stack->port,"setup_bc: without dsp\n");
			{
				int l = sizeof(li.name);
				strncpy(li.name, "B L3", l);
				li.name[l-1] = 0;
			}
			li.pid.layermask = ISDN_LAYER((3));
			li.pid.protocol[3] = ISDN_PID_L3_B_USER;

			bc->layer=3;
		} else {
			cb_log(4, stack->port,"setup_bc: with dsp\n");
			{
				int l = sizeof(li.name);
				strncpy(li.name, "B L4", l);
				li.name[l-1] = 0;
			}
			li.pid.layermask = ISDN_LAYER((4));
			li.pid.protocol[4] = ISDN_PID_L4_B_USER;

			bc->layer=4;
		}

		ret = mISDN_new_layer(midev, &li);
		if (ret ) {
			cb_log(0, stack->port,"New Layer Err: %d %s\n",ret,strerror(errno));

			bc_state_change(bc,BCHAN_ERROR);
			return(-EINVAL);
		}

		bc->layer_id = li.id;
	}

	memset(&pid, 0, sizeof(pid));



	cb_log(4, stack->port," --> Channel is %d\n", bc->channel);

	if (bc->nodsp) {
		cb_log(2, stack->port," --> TRANSPARENT Mode (no DSP, no HDLC)\n");
		pid.protocol[1] = ISDN_PID_L1_B_64TRANS;
		pid.protocol[2] = ISDN_PID_L2_B_TRANS;
		pid.protocol[3] = ISDN_PID_L3_B_USER;
		pid.layermask = ISDN_LAYER((1)) | ISDN_LAYER((2)) | ISDN_LAYER((3));

	} else if ( bc->hdlc ) {
		cb_log(2, stack->port," --> HDLC Mode\n");
		pid.protocol[1] = ISDN_PID_L1_B_64HDLC ;
		pid.protocol[2] = ISDN_PID_L2_B_TRANS  ;
		pid.protocol[3] = ISDN_PID_L3_B_USER;
		pid.layermask = ISDN_LAYER((1)) | ISDN_LAYER((2)) | ISDN_LAYER((3)) ;
	} else {
		cb_log(2, stack->port," --> TRANSPARENT Mode\n");
		pid.protocol[1] = ISDN_PID_L1_B_64TRANS;
		pid.protocol[2] = ISDN_PID_L2_B_TRANS;
		pid.protocol[3] = ISDN_PID_L3_B_DSP;
		pid.protocol[4] = ISDN_PID_L4_B_USER;
		pid.layermask = ISDN_LAYER((1)) | ISDN_LAYER((2)) | ISDN_LAYER((3)) | ISDN_LAYER((4));

	}

	ret = mISDN_set_stack(midev, bc->b_stid, &pid);

	if (ret){
		cb_log(0, stack->port,"$$$ Set Stack Err: %d %s\n",ret,strerror(errno));

		mISDN_write_frame(midev, buff, bc->layer_id, MGR_DELLAYER | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);

		bc_state_change(bc,BCHAN_ERROR);
		cb_event(EVENT_BCHAN_ERROR, bc, glob_mgr->user_data);
		return(-EINVAL);
	}

	ret = mISDN_get_setstack_ind(midev, bc->layer_id);

	if (ret) {
		cb_log(0, stack->port,"$$$ Set StackIND Err: %d %s\n",ret,strerror(errno));
		mISDN_write_frame(midev, buff, bc->layer_id, MGR_DELLAYER | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);

		bc_state_change(bc,BCHAN_ERROR);
		cb_event(EVENT_BCHAN_ERROR, bc, glob_mgr->user_data);
		return(-EINVAL);
	}

	ret = mISDN_get_layerid(midev, bc->b_stid, bc->layer) ;

	bc->addr = ret>0? ret : 0;

	if (!bc->addr) {
		cb_log(0, stack->port,"$$$ Get Layerid Err: %d %s\n",ret,strerror(errno));
		mISDN_write_frame(midev, buff, bc->layer_id, MGR_DELLAYER | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);

		bc_state_change(bc,BCHAN_ERROR);
		cb_event(EVENT_BCHAN_ERROR, bc, glob_mgr->user_data);
		return (-EINVAL);
	}

	manager_bchannel_activate(bc);

	bc_state_change(bc,BCHAN_ACTIVATED);

	return 0;
}



/** IFACE **/
static int init_bc(struct misdn_stack *stack, struct misdn_bchannel *bc, int midev, int port, int bidx)
{
	if (!bc) {
		return -1;
	}

	cb_log(8, port, "Init.BC %d.\n",bidx);

	bc->send_lock=malloc(sizeof(struct send_lock));
	if (!bc->send_lock) {
		return -1;
	}
	pthread_mutex_init(&bc->send_lock->lock, NULL);

	empty_bc(bc);
	bc_state_change(bc, BCHAN_CLEANED);

	bc->port=stack->port;
	bc->nt=stack->nt?1:0;
	bc->pri=stack->pri;

	{
		ibuffer_t* ibuf= init_ibuffer(MISDN_IBUF_SIZE);

		if (!ibuf) return -1;

		clear_ibuffer( ibuf);

		ibuf->rsem=malloc(sizeof(sem_t));
		if (!ibuf->rsem) {
			return -1;
		}

		bc->astbuf=ibuf;

		if (sem_init(ibuf->rsem,1,0)<0)
			sem_init(ibuf->rsem,0,0);

	}

#if 0	/* This code does not seem to do anything useful */
	if (bidx <= stack->b_num) {
		unsigned char buff[1025];
		iframe_t *frm = (iframe_t *) buff;
		stack_info_t *stinf;
		int ret;

		ret = mISDN_get_stack_info(midev, stack->port, buff, sizeof(buff));
		if (ret < 0) {
			cb_log(0, port, "%s: Cannot get stack info for this port. (ret=%d)\n", __FUNCTION__, ret);
			return -1;
		}

		stinf = (stack_info_t *)&frm->data.p;

		cb_log(8, port, " --> Child %x\n",stinf->child[bidx]);
	}
#endif

	return 0;
}



static struct misdn_stack *stack_init(int midev, int port, int ptp)
{
	int ret;
	unsigned char buff[1025];
	iframe_t *frm = (iframe_t *)buff;
	stack_info_t *stinf;
	struct misdn_stack *stack;
	int i;
	layer_info_t li;

	stack = calloc(1, sizeof(struct misdn_stack));
	if (!stack) {
		return NULL;
	}

	cb_log(8, port, "Init. Stack.\n");

	stack->port=port;
	stack->midev=midev;
	stack->ptp=ptp;

	stack->holding=NULL;
	stack->pri=0;

	msg_queue_init(&stack->downqueue);
	msg_queue_init(&stack->upqueue);

	/* query port's requirements */
	ret = mISDN_get_stack_info(midev, port, buff, sizeof(buff));
	if (ret < 0) {
		cb_log(0, port, "%s: Cannot get stack info for this port. (ret=%d)\n", __FUNCTION__, ret);
		return(NULL);
	}

	stinf = (stack_info_t *)&frm->data.p;

	stack->d_stid = stinf->id;
	stack->b_num = stinf->childcnt;

	for (i=0; i<=stinf->childcnt; i++)
		stack->b_stids[i] = stinf->child[i];

	switch(stinf->pid.protocol[0] & ~ISDN_PID_FEATURE_MASK) {
	case ISDN_PID_L0_TE_S0:
		stack->nt=0;
		break;
	case ISDN_PID_L0_NT_S0:
		cb_log(8, port, "NT Stack\n");

		stack->nt=1;
		break;
	case ISDN_PID_L0_TE_E1:
		cb_log(8, port, "TE S2M Stack\n");
		stack->nt=0;
		stack->pri=1;
		break;
	case ISDN_PID_L0_NT_E1:
		cb_log(8, port, "TE S2M Stack\n");
		stack->nt=1;
		stack->pri=1;

		break;
	default:
		cb_log(0, port, "this is a unknown port type 0x%08x\n", stinf->pid.protocol[0]);

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

		cb_log(8, port, "Init. Stack.\n");

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
			cb_log(0, port, "%s: Cannot add layer %d to this port.\n", __FUNCTION__, nt?2:4);
			return(NULL);
		}


		stack->upper_id = li.id;
		ret = mISDN_register_layer(midev, stack->d_stid, stack->upper_id);
		if (ret)
		{
			cb_log(0,port,"Cannot register layer %d of this port.\n", nt?2:4);
			return(NULL);
		}

		stack->lower_id = mISDN_get_layerid(midev, stack->d_stid, nt?1:3);
		if (stack->lower_id < 0) {
			cb_log(0, port, "%s: Cannot get layer(%d) id of this port.\n", __FUNCTION__, nt?1:3);
			return(NULL);
		}

		stack->upper_id = mISDN_get_layerid(midev, stack->d_stid, nt?2:4);
		if (stack->upper_id < 0) {
			cb_log(0, port, "%s: Cannot get layer(%d) id of this port.\n", __FUNCTION__, 2);
			return(NULL);
		}

		cb_log(8, port, "NT Stacks upper_id %x\n",stack->upper_id);


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

		}

 		stack->l1link=0;
 		stack->l2link=0;
#if 0
		if (!stack->nt) {
 			misdn_lib_get_short_status(stack);
 		} else {
 			misdn_lib_get_l1_up(stack);
 			if (!stack->ptp) misdn_lib_get_l1_up(stack);
 			misdn_lib_get_l2_up(stack);
 		}
#endif

		misdn_lib_get_short_status(stack);
		misdn_lib_get_l1_up(stack);
		misdn_lib_get_l2_up(stack);

	}

	cb_log(8,0,"stack_init: port:%d lowerId:%x  upperId:%x\n",stack->port,stack->lower_id, stack->upper_id);

	return stack;
}


static void stack_destroy(struct misdn_stack *stack)
{
	char buf[1024];
	if (!stack) return;

	if (stack->nt) {
		cleanup_Isdnl2(&stack->nst);
		cleanup_Isdnl3(&stack->nst);
	}

	if (stack->lower_id)
		mISDN_write_frame(stack->midev, buf, stack->lower_id, MGR_DELLAYER | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);

	if (stack->upper_id)
		mISDN_write_frame(stack->midev, buf, stack->upper_id, MGR_DELLAYER | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);
}


static struct misdn_stack * find_stack_by_addr(int  addr)
{
	struct misdn_stack *stack;

	for (stack = glob_mgr->stack_list; stack; stack = stack->next) {
		if ((stack->upper_id & STACK_ID_MASK) == (addr & STACK_ID_MASK)) {
			/* Found the stack */
			break;
		}
	}

	return stack;
}


static struct misdn_stack *find_stack_by_port(int port)
{
	struct misdn_stack *stack;

	for (stack = glob_mgr->stack_list; stack; stack = stack->next) {
		if (stack->port == port) {
			/* Found the stack */
			break;
		}
	}

	return stack;
}

static struct misdn_stack *find_stack_by_mgr(manager_t *mgr_nt)
{
	struct misdn_stack *stack;

	for (stack = glob_mgr->stack_list; stack; stack = stack->next) {
		if (&stack->mgr == mgr_nt) {
			/* Found the stack */
			break;
		}
	}

	return stack;
}

static struct misdn_bchannel *find_bc_by_masked_l3id(struct misdn_stack *stack, unsigned long l3id, unsigned long mask)
{
	int i;

	for (i = 0; i <= stack->b_num; ++i) {
		if ((stack->bc[i].l3_id & mask) == (l3id & mask)) {
			return &stack->bc[i];
		}
	}
#if defined(AST_MISDN_ENHANCEMENTS)
	/* Search the B channel records for a REGISTER signaling link. */
	for (i = MAX_BCHANS + 1; i < ARRAY_LEN(stack->bc); ++i) {
		if ((stack->bc[i].l3_id & mask) == (l3id & mask)) {
			return &stack->bc[i];
		}
	}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */
	return stack_holder_find(stack, l3id);
}


struct misdn_bchannel *find_bc_by_l3id(struct misdn_stack *stack, unsigned long l3id)
{
	int i;

	for (i = 0; i <= stack->b_num; ++i) {
		if (stack->bc[i].l3_id == l3id) {
			return &stack->bc[i];
		}
	}
#if defined(AST_MISDN_ENHANCEMENTS)
	/* Search the B channel records for a REGISTER signaling link. */
	for (i = MAX_BCHANS + 1; i < ARRAY_LEN(stack->bc); ++i) {
		if (stack->bc[i].l3_id == l3id) {
			return &stack->bc[i];
		}
	}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */
	return stack_holder_find(stack, l3id);
}

static struct misdn_bchannel *find_bc_holded(struct misdn_stack *stack)
{
	int i;
	for (i=0; i<=stack->b_num; i++) {
		if (stack->bc[i].holded ) return &stack->bc[i] ;
	}
	return NULL;
}


static struct misdn_bchannel *find_bc_by_addr(unsigned long addr)
{
	struct misdn_stack* stack;
	int i;

	for (stack=glob_mgr->stack_list;
	     stack;
	     stack=stack->next) {
		for (i=0; i<=stack->b_num; i++) {
			if ( (stack->bc[i].addr&STACK_ID_MASK)==(addr&STACK_ID_MASK) ||  stack->bc[i].layer_id== addr ) {
				return &stack->bc[i];
			}
		}
	}

	return NULL;
}

struct misdn_bchannel *find_bc_by_confid(unsigned long confid)
{
	struct misdn_stack* stack;
	int i;

	for (stack=glob_mgr->stack_list;
	     stack;
	     stack=stack->next) {
		for (i=0; i<=stack->b_num; i++) {
			if ( stack->bc[i].conf_id==confid ) {
				return &stack->bc[i];
			}
		}
	}
	return NULL;
}


static struct misdn_bchannel *find_bc_by_channel(int port, int channel)
{
	struct misdn_stack* stack=find_stack_by_port(port);
	int i;

	if (!stack) return NULL;

	for (i=0; i<=stack->b_num; i++) {
		if ( stack->bc[i].channel== channel ) {
			return &stack->bc[i];
		}
	}

	return NULL;
}





static int handle_event ( struct misdn_bchannel *bc, enum event_e event, iframe_t *frm)
{
	struct misdn_stack *stack=get_stack_by_bc(bc);

	if (!stack->nt) {

		switch (event) {

		case EVENT_CONNECT_ACKNOWLEDGE:
			setup_bc(bc);

			if ( *bc->crypt_key ) {
				cb_log(4, stack->port,
					"ENABLING BLOWFISH channel:%d caller%d:\"%s\" <%s> dialed%d:%s\n",
					bc->channel,
					bc->caller.number_type,
					bc->caller.name,
					bc->caller.number,
					bc->dialed.number_type,
					bc->dialed.number);
				manager_ph_control_block(bc,  BF_ENABLE_KEY, bc->crypt_key, strlen(bc->crypt_key) );
			}

			if (misdn_cap_is_speech(bc->capability)) {
				if (  !bc->nodsp) manager_ph_control(bc,  DTMF_TONE_START, 0);
				manager_ec_enable(bc);

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
		case EVENT_CONNECT:

			if ( *bc->crypt_key ) {
				cb_log(4, stack->port,
					"ENABLING BLOWFISH channel:%d caller%d:\"%s\" <%s> dialed%d:%s\n",
					bc->channel,
					bc->caller.number_type,
					bc->caller.name,
					bc->caller.number,
					bc->dialed.number_type,
					bc->dialed.number);
				manager_ph_control_block(bc,  BF_ENABLE_KEY, bc->crypt_key, strlen(bc->crypt_key) );
			}
		case EVENT_ALERTING:
		case EVENT_PROGRESS:
		case EVENT_PROCEEDING:
		case EVENT_SETUP_ACKNOWLEDGE:
		case EVENT_SETUP:
		{
			if (bc->channel == 0xff || bc->channel<=0)
				bc->channel=0;

			if (find_free_chan_in_stack(stack, bc, bc->channel, 0)<0){
				if (!stack->pri && !stack->ptp)  {
					bc->cw=1;
					break;
				}

				if (!bc->channel) {
					cb_log(0, stack->port, "Any Channel Requested, but we have no more!!\n");
				} else {
					cb_log(0, stack->port,
						"Requested Channel Already in Use releasing this call with cause %d!!!!\n",
						bc->out_cause);
				}

				/* when the channel is already in use, we can't
				 * simply clear it, we need to make sure that
				 * it will still be marked as in_use in the
				 * available channels list.*/
				bc->channel=0;

				misdn_lib_send_event(bc,EVENT_RELEASE_COMPLETE);
				return -1;
			}

			setup_bc(bc);
			break;
		}

		case EVENT_RELEASE_COMPLETE:
		case EVENT_RELEASE:
			break;
		default:
			break;
		}
	} else {    /** NT MODE **/

	}
	return 0;
}

static int handle_cr ( struct misdn_stack *stack, iframe_t *frm)
{
	struct misdn_bchannel dummybc;
	struct misdn_bchannel *bc;
	int channel;

	if (!stack) return -1;

	switch (frm->prim) {
	case CC_NEW_CR|INDICATION:
		cb_log(7, stack->port, " --> lib: NEW_CR Ind with l3id:%x on this port.\n",frm->dinfo);

		bc = misdn_lib_get_free_bc(stack->port, 0, 1, 0);
		if (!bc) {
			cb_log(0, stack->port, " --> !! lib: No free channel!\n");
			return -1;
		}

		cb_log(7, stack->port, " --> new_process: New L3Id: %x\n",frm->dinfo);
		bc->l3_id=frm->dinfo;
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
		cb_log(4, stack->port, " --> lib: RELEASE_CR Ind with l3id:%x\n", frm->dinfo);
		bc = find_bc_by_l3id(stack, frm->dinfo);
		if (!bc) {
			cb_log(4, stack->port, " --> Didn't find BC so temporarily creating dummy BC (l3id:%x) on this port.\n", frm->dinfo);
			misdn_make_dummy(&dummybc, stack->port, frm->dinfo, stack->nt, 0);
			bc = &dummybc;
		}

		channel = bc->channel;
		cb_log(4, stack->port, " --> lib: CLEANING UP l3id: %x\n", frm->dinfo);

		/* bc->pid = 0; */
		bc->need_disconnect = 0;
		bc->need_release = 0;
		bc->need_release_complete = 0;

		cb_event(EVENT_CLEANUP, bc, glob_mgr->user_data);

		empty_bc(bc);
		clean_up_bc(bc);

		if (channel > 0)
			empty_chan_in_stack(stack, channel);
		bc->in_use = 0;

		dump_chan_list(stack);

		if (bc->stack_holder) {
			cb_log(4, stack->port, "REMOVING Holder\n");
			stack_holder_remove(stack, bc);
			free(bc);
		}

		return 1;
	default:
		break;
	}

	return 0;
}


/* Empties bc if it's reserved (no SETUP out yet) */
void misdn_lib_release(struct misdn_bchannel *bc)
{
	struct misdn_stack *stack=get_stack_by_bc(bc);

	if (!stack) {
		cb_log(1,0,"misdn_release: No Stack found\n");
		return;
	}

	if (bc->channel>0)
		empty_chan_in_stack(stack,bc->channel);

	empty_bc(bc);
	clean_up_bc(bc);
	bc->in_use=0;
}




int misdn_lib_get_port_up (int port)
{ /* Pull Up L1 */
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


int misdn_lib_get_port_down (int port)
{ /* Pull Down L1 */
	struct misdn_stack *stack;
	for (stack=glob_mgr->stack_list;
	     stack;
	     stack=stack->next) {
		if (stack->port == port) {
				if (stack->l2link)
					misdn_lib_get_l2_down(stack);
				misdn_lib_get_l1_down(stack);
			return 0;
		}
	}
	return 0;
}

int misdn_lib_port_up(int port, int check)
{
	struct misdn_stack *stack;


	for (stack=glob_mgr->stack_list;
	     stack;
	     stack=stack->next) {

		if (stack->port == port) {

			if (stack->blocked) {
				cb_log(0,port, "Port Blocked:%d L2:%d L1:%d\n", stack->blocked, stack->l2link, stack->l1link);
				return -1;
			}

			if (stack->ptp ) {

				if (stack->l1link && stack->l2link) {
					return 1;
				} else {
					cb_log(1,port, "Port Down L2:%d L1:%d\n",
						stack->l2link, stack->l1link);
					return 0;
				}
			} else {
				if ( !check || stack->l1link )
					return 1;
				else {
					cb_log(1,port, "Port down PMP\n");
					return 0;
				}
			}
		}
	}

	return -1;
}


static int release_cr(struct misdn_stack *stack, mISDNuser_head_t *hh)
{
	struct misdn_bchannel *bc=find_bc_by_l3id(stack, hh->dinfo);
	struct misdn_bchannel dummybc;
	iframe_t frm; /* fake te frm to remove callref from global callreflist */

	frm.dinfo = hh->dinfo;
	frm.addr=stack->upper_id | FLG_MSG_DOWN;
	frm.prim = CC_RELEASE_CR|INDICATION;
	cb_log(4, stack->port, " --> CC_RELEASE_CR: Faking Release_cr for %x l3id:%x\n",frm.addr, frm.dinfo);

	/** removing procid **/
	if (!bc) {
		cb_log(4, stack->port, " --> Didn't find BC so temporarily creating dummy BC (l3id:%x) on this port.\n", hh->dinfo);
		misdn_make_dummy(&dummybc, stack->port, hh->dinfo, stack->nt, 0);
		bc=&dummybc;
	}

	if ((bc->l3_id & 0xff00) == 0xff00) {
		cb_log(4, stack->port, " --> Removing Process Id:%x on this port.\n", bc->l3_id & 0xff);
		stack->procids[bc->l3_id & 0xff] = 0;
	}

	if (handle_cr(stack, &frm)<0) {
	}

	return 0 ;
}

static int handle_event_nt(void *dat, void *arg)
{
	struct misdn_bchannel dummybc;
	struct misdn_bchannel *bc;
	manager_t *mgr = (manager_t *)dat;
	msg_t *msg = (msg_t *)arg;
	msg_t *dmsg;
	mISDNuser_head_t *hh;
	struct misdn_stack *stack;
	enum event_e event;
	int reject=0;
	int port;
	int l3id;
	int channel;
	int tmpcause;

	if (!msg || !mgr)
		return(-EINVAL);

	stack = find_stack_by_mgr(mgr);
	hh=(mISDNuser_head_t*)msg->data;
	port=stack->port;

	cb_log(5, stack->port, " --> lib: prim %x dinfo %x\n",hh->prim, hh->dinfo);
	switch(hh->prim) {
	case CC_RETRIEVE|INDICATION:
	{
		struct misdn_bchannel *hold_bc;
		iframe_t frm; /* fake te frm to add callref to global callreflist */

		frm.dinfo = hh->dinfo;
		frm.addr=stack->upper_id | FLG_MSG_DOWN;
		frm.prim = CC_NEW_CR|INDICATION;
		if (handle_cr( stack, &frm)< 0) {
			goto ERR_NO_CHANNEL;
		}

		bc = find_bc_by_l3id(stack, hh->dinfo);
		hold_bc = stack_holder_find(stack, bc->l3_id);
		cb_log(4, stack->port, "bc_l3id:%x holded_bc_l3id:%x\n",bc->l3_id, hold_bc->l3_id);

		if (hold_bc) {
			cb_log(4, stack->port, "REMOVING Holder\n");

			/* swap the backup to our new channel back */
			stack_holder_remove(stack, hold_bc);
			memcpy(bc, hold_bc, sizeof(*bc));
			free(hold_bc);

			bc->holded=0;
			bc->b_stid=0;
		}
		break;
	}

	case CC_SETUP | CONFIRM:
		l3id = *((int *) (msg->data + mISDNUSER_HEAD_SIZE));

		cb_log(4, stack->port, " --> lib: Event_ind:SETUP CONFIRM [NT] : new L3ID is %x\n", l3id);

		bc = find_bc_by_l3id(stack, hh->dinfo);
		if (bc) {
			cb_log (2, bc->port, "I IND :CC_SETUP|CONFIRM: old l3id:%x new l3id:%x\n", bc->l3_id, l3id);
			bc->l3_id = l3id;
			cb_event(EVENT_NEW_L3ID, bc, glob_mgr->user_data);
		} else {
			cb_log(4, stack->port, "Bc Not found (after SETUP CONFIRM)\n");
		}
		free_msg(msg);
		return 0;

	case CC_SETUP | INDICATION:
		bc = misdn_lib_get_free_bc(stack->port, 0, 1, 0);
		if (!bc) {
			goto ERR_NO_CHANNEL;
		}

		cb_log(4, stack->port, " --> new_process: New L3Id: %x\n",hh->dinfo);
		bc->l3_id=hh->dinfo;
		break;

#if defined(AST_MISDN_ENHANCEMENTS)
	case CC_REGISTER | CONFIRM:
		l3id = *((int *) (msg->data + mISDNUSER_HEAD_SIZE));

		cb_log(4, stack->port, " --> lib: Event_ind:REGISTER CONFIRM [NT] : new L3ID is %x\n", l3id);

		bc = find_bc_by_l3id(stack, hh->dinfo);
		if (bc) {
			cb_log (2, bc->port, "I IND :CC_REGISTER|CONFIRM: old l3id:%x new l3id:%x\n", bc->l3_id, l3id);
			bc->l3_id = l3id;
		} else {
			cb_log(4, stack->port, "Bc Not found (after REGISTER CONFIRM)\n");
		}
		free_msg(msg);
		return 0;
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

#if defined(AST_MISDN_ENHANCEMENTS)
	case CC_REGISTER | INDICATION:
		bc = misdn_lib_get_register_bc(stack->port);
		if (!bc) {
			goto ERR_NO_CHANNEL;
		}

		cb_log(4, stack->port, " --> new_process: New L3Id: %x\n",hh->dinfo);
		bc->l3_id=hh->dinfo;
		break;
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

	case CC_CONNECT_ACKNOWLEDGE|INDICATION:
		break;

	case CC_ALERTING|INDICATION:
	case CC_PROCEEDING|INDICATION:
	case CC_SETUP_ACKNOWLEDGE|INDICATION:
	case CC_CONNECT|INDICATION:
		break;
	case CC_DISCONNECT|INDICATION:
		bc = find_bc_by_l3id(stack, hh->dinfo);
		if (!bc) {
			bc=find_bc_by_masked_l3id(stack, hh->dinfo, 0xffff0000);
			if (bc) {
				int myprocid=bc->l3_id&0x0000ffff;

				hh->dinfo=(hh->dinfo&0xffff0000)|myprocid;
				cb_log(3,stack->port,"Reject dinfo: %x cause:%d\n",hh->dinfo,bc->cause);
				reject=1;
			}
		}
		break;

	case CC_FACILITY|INDICATION:
		bc = find_bc_by_l3id(stack, hh->dinfo);
		if (!bc) {
			bc=find_bc_by_masked_l3id(stack, hh->dinfo, 0xffff0000);
			if (bc) {
				int myprocid=bc->l3_id&0x0000ffff;

				hh->dinfo=(hh->dinfo&0xffff0000)|myprocid;
				cb_log(4,bc->port,"Repaired reject Bug, new dinfo: %x\n",hh->dinfo);
			}
		}
		break;

	case CC_RELEASE_COMPLETE|INDICATION:
		break;

	case CC_SUSPEND|INDICATION:
		cb_log(4, stack->port, " --> Got Suspend, sending Reject for now\n");
		dmsg = create_l3msg(CC_SUSPEND_REJECT | REQUEST,MT_SUSPEND_REJECT, hh->dinfo,sizeof(RELEASE_COMPLETE_t), 1);
		stack->nst.manager_l3(&stack->nst, dmsg);
		free_msg(msg);
		return 0;

	case CC_RESUME|INDICATION:
		break;

	case CC_RELEASE|CONFIRM:
		bc = find_bc_by_l3id(stack, hh->dinfo);
		if (bc) {
			cb_log(1, stack->port, "CC_RELEASE|CONFIRM (l3id:%x), sending RELEASE_COMPLETE\n", hh->dinfo);
			misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE);
		}
		break;

	case CC_RELEASE|INDICATION:
		break;

	case CC_RELEASE_CR|INDICATION:
		release_cr(stack, hh);
		free_msg(msg);
		return 0;

	case CC_NEW_CR|INDICATION:
		/*  Got New CR for bchan, for now I handle this one in */
		/*  connect_ack, Need to be changed */
		l3id = *((int *) (msg->data + mISDNUSER_HEAD_SIZE));

		bc = find_bc_by_l3id(stack, hh->dinfo);
		if (!bc) {
			cb_log(0, stack->port, " --> In NEW_CR: didn't found bc ??\n");
			return -1;
		}
		if (((l3id&0xff00)!=0xff00) && ((bc->l3_id&0xff00)==0xff00)) {
			cb_log(4, stack->port, " --> Removing Process Id:%x on this port.\n", 0xff&bc->l3_id);
			stack->procids[bc->l3_id&0xff] = 0 ;
		}
		cb_log(4, stack->port, "lib: Event_ind:CC_NEW_CR : very new L3ID  is %x\n",l3id );

		bc->l3_id =l3id;
		if (!bc->is_register_pool) {
			cb_event(EVENT_NEW_L3ID, bc, glob_mgr->user_data);
		}

		free_msg(msg);
		return 0;

	case DL_ESTABLISH | INDICATION:
	case DL_ESTABLISH | CONFIRM:
		cb_log(3, stack->port, "%% GOT L2 Activate Info.\n");

		if (stack->ptp && stack->l2link) {
			cb_log(0, stack->port, "%% GOT L2 Activate Info. but we're activated already.. this l2 is faulty, blocking port\n");
			cb_event(EVENT_PORT_ALARM, &stack->bc[0], glob_mgr->user_data);
		}

		if (stack->ptp && !stack->restart_sent) {
			/* make sure we restart the interface of the
			 * other side */
			stack->restart_sent=1;
			misdn_lib_send_restart(stack->port, -1);

		}

		/* when we get the L2 UP, the L1 is UP definitely too*/
		stack->l2link = 1;
		stack->l2upcnt=0;

		free_msg(msg);
		return 0;

	case DL_RELEASE | INDICATION:
	case DL_RELEASE | CONFIRM:
		if (stack->ptp) {
			cb_log(3 , stack->port, "%% GOT L2 DeActivate Info.\n");

			if (stack->l2upcnt>3) {
				cb_log(0 , stack->port, "!!! Could not Get the L2 up after 3 Attempts!!!\n");
			} else {
#if 0
				if (stack->nt)
					misdn_lib_reinit_nt_stack(stack->port);
#endif
				if (stack->l1link) {
					misdn_lib_get_l2_up(stack);
					stack->l2upcnt++;
				}
			}

		} else
			cb_log(3, stack->port, "%% GOT L2 DeActivate Info.\n");

		stack->l2link = 0;
		free_msg(msg);
		return 0;

	default:
		break;
	}

	/*  Parse Events and fire_up to App. */
	event = isdn_msg_get_event(msgs_g, msg, 1);

	bc = find_bc_by_l3id(stack, hh->dinfo);
	if (!bc) {
		cb_log(4, stack->port, " --> Didn't find BC so temporarily creating dummy BC (l3id:%x).\n", hh->dinfo);
		misdn_make_dummy(&dummybc, stack->port,  hh->dinfo, stack->nt, 0);
		bc = &dummybc;
	}

	isdn_msg_parse_event(msgs_g, msg, bc, 1);

	switch (event) {
	case EVENT_SETUP:
		if (bc->channel <= 0 || bc->channel == 0xff) {
			bc->channel = 0;
		}

		if (find_free_chan_in_stack(stack, bc, bc->channel, 0) < 0) {
			goto ERR_NO_CHANNEL;
		}
		break;
	case EVENT_RELEASE:
	case EVENT_RELEASE_COMPLETE:
		channel = bc->channel;
		tmpcause = bc->cause;

		empty_bc(bc);
		bc->cause = tmpcause;
		clean_up_bc(bc);

		if (channel > 0)
			empty_chan_in_stack(stack, channel);
		bc->in_use = 0;
		break;
	default:
		break;
	}

	if(!isdn_get_info(msgs_g, event, 1)) {
		cb_log(4, stack->port, "Unknown Event Ind: prim %x dinfo %x\n", hh->prim, hh->dinfo);
	} else {
		if (reject) {
			switch(bc->cause) {
			case AST_CAUSE_USER_BUSY:
				cb_log(1, stack->port, "Siemens Busy reject..\n");
				break;
			default:
				break;
			}
		}
		cb_event(event, bc, glob_mgr->user_data);
	}

	free_msg(msg);
	return 0;

ERR_NO_CHANNEL:
	cb_log(4, stack->port, "Patch from MEIDANIS:Sending RELEASE_COMPLETE %x (No free Chan for you..)\n", hh->dinfo);
	dmsg = create_l3msg(CC_RELEASE_COMPLETE | REQUEST, MT_RELEASE_COMPLETE, hh->dinfo, sizeof(RELEASE_COMPLETE_t), 1);
	stack->nst.manager_l3(&stack->nst, dmsg);
	free_msg(msg);
	return 0;
}


static int handle_timers(msg_t* msg)
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

		cb_log(0, 0, "Timer Msg without Timer ??\n");
		free_msg(msg);
		return 1;
	}

	return 0;
}



void misdn_lib_tone_generator_start(struct misdn_bchannel *bc)
{
	bc->generate_tone=1;
}

void misdn_lib_tone_generator_stop(struct misdn_bchannel *bc)
{
	bc->generate_tone=0;
}


static int do_tone(struct misdn_bchannel *bc, int len)
{
	bc->tone_cnt=len;

	if (bc->generate_tone) {
		cb_event(EVENT_TONE_GENERATE, bc, glob_mgr->user_data);

		if ( !bc->nojitter ) {
			misdn_tx_jitter(bc,len);
		}

		return 1;
	}

	return 0;
}


#ifdef MISDN_SAVE_DATA
static void misdn_save_data(int id, char *p1, int l1, char *p2, int l2)
{
	char n1[32],n2[32];
	FILE *rx, *tx;

	sprintf(n1,"/tmp/misdn-rx-%d.raw",id);
	sprintf(n2,"/tmp/misdn-tx-%d.raw",id);

	rx = fopen(n1,"a+");
	tx = fopen(n2,"a+");

	if (!rx || !tx) {
		cb_log(0,0,"Couldn't open files: %s\n",strerror(errno));
		if (rx)
			fclose(rx);
		if (tx)
			fclose(tx);
		return ;
	}

	fwrite(p1,1,l1,rx);
	fwrite(p2,1,l2,tx);

	fclose(rx);
	fclose(tx);

}
#endif

void misdn_tx_jitter(struct misdn_bchannel *bc, int len)
{
	char buf[4096 + mISDN_HEADER_LEN];
	char *data=&buf[mISDN_HEADER_LEN];
	iframe_t *txfrm= (iframe_t*)buf;
	int jlen, r;

	jlen=cb_jb_empty(bc,data,len);

	if (jlen) {
#ifdef MISDN_SAVE_DATA
		misdn_save_data((bc->port*100+bc->channel), data, jlen, bc->bframe, bc->bframe_len);
#endif
		flip_buf_bits( data, jlen);

		if (jlen < len) {
			cb_log(1, bc->port, "Jitterbuffer Underrun. Got %d of expected %d\n", jlen, len);
		}

		txfrm->prim = DL_DATA|REQUEST;

		txfrm->dinfo = 0;

		txfrm->addr = bc->addr|FLG_MSG_DOWN; /*  | IF_DOWN; */

		txfrm->len =jlen;
		cb_log(9, bc->port, "Transmitting %d samples 2 misdn\n", txfrm->len);

		r=mISDN_write( glob_mgr->midev, buf, txfrm->len + mISDN_HEADER_LEN, 8000 );
	} else {
#define MISDN_GEN_SILENCE
#ifdef MISDN_GEN_SILENCE
		int cnt=len/TONE_SILENCE_SIZE;
		int rest=len%TONE_SILENCE_SIZE;
		int i;

		for (i=0; i<cnt; i++) {
			memcpy(data, tone_silence_flip, TONE_SILENCE_SIZE );
			data +=TONE_SILENCE_SIZE;
		}

		if (rest) {
			memcpy(data, tone_silence_flip, rest);
		}

		txfrm->prim = DL_DATA|REQUEST;

		txfrm->dinfo = 0;

		txfrm->addr = bc->addr|FLG_MSG_DOWN; /*  | IF_DOWN; */

		txfrm->len =len;
		cb_log(5, bc->port, "Transmitting %d samples of silence to misdn\n", len);

		r=mISDN_write( glob_mgr->midev, buf, txfrm->len + mISDN_HEADER_LEN, 8000 );
#else
		r = 0;
#endif
	}

	if (r < 0) {
		cb_log(1, bc->port, "Error in mISDN_write (%s)\n", strerror(errno));
	}
}

static int handle_bchan(msg_t *msg)
{
	iframe_t *frm= (iframe_t*)msg->data;
	struct misdn_bchannel *bc=find_bc_by_addr(frm->addr);
	struct misdn_stack *stack;

	if (!bc) {
		cb_log(1,0,"handle_bchan: BC not found for prim:%x with addr:%x dinfo:%x\n", frm->prim, frm->addr, frm->dinfo);
		return 0 ;
	}

	stack = get_stack_by_bc(bc);

	if (!stack) {
		cb_log(0, bc->port,"handle_bchan: STACK not found for prim:%x with addr:%x dinfo:%x\n", frm->prim, frm->addr, frm->dinfo);
		return 0;
	}

	switch (frm->prim) {

	case MGR_SETSTACK| CONFIRM:
		cb_log(3, stack->port, "BCHAN: MGR_SETSTACK|CONFIRM pid:%d\n",bc->pid);
		break;

	case MGR_SETSTACK| INDICATION:
		cb_log(3, stack->port, "BCHAN: MGR_SETSTACK|IND pid:%d\n",bc->pid);
	break;
#if 0
	AGAIN:
		bc->addr = mISDN_get_layerid(stack->midev, bc->b_stid, bc->layer);
		if (!bc->addr) {

			if (errno == EAGAIN) {
				usleep(1000);
				goto AGAIN;
			}

			cb_log(0,stack->port,"$$$ Get Layer (%d) Id Error: %s\n",bc->layer,strerror(errno));

			/* we kill the channel later, when we received some
			   data. */
			bc->addr= frm->addr;
		} else if ( bc->addr < 0) {
			cb_log(0, stack->port,"$$$ bc->addr <0 Error:%s\n",strerror(errno));
			bc->addr=0;
		}

		cb_log(4, stack->port," --> Got Adr %x\n", bc->addr);

		free_msg(msg);


		switch(bc->bc_state) {
		case BCHAN_SETUP:
			bc_state_change(bc,BCHAN_SETUPED);
		break;

		case BCHAN_CLEAN_REQUEST:
		default:
			cb_log(0, stack->port," --> STATE WASN'T SETUP (but %s) in SETSTACK|IND pid:%d\n",bc_state2str(bc->bc_state), bc->pid);
			clean_up_bc(bc);
		}
		return 1;
#endif

	case MGR_DELLAYER| INDICATION:
		cb_log(3, stack->port, "BCHAN: MGR_DELLAYER|IND pid:%d\n",bc->pid);
		break;

	case MGR_DELLAYER| CONFIRM:
		cb_log(3, stack->port, "BCHAN: MGR_DELLAYER|CNF pid:%d\n",bc->pid);

		bc->pid=0;
		bc->addr=0;

		free_msg(msg);
		return 1;

	case PH_ACTIVATE | INDICATION:
	case DL_ESTABLISH | INDICATION:
		cb_log(3, stack->port, "BCHAN: ACT Ind pid:%d\n", bc->pid);

		free_msg(msg);
		return 1;

	case PH_ACTIVATE | CONFIRM:
	case DL_ESTABLISH | CONFIRM:

		cb_log(3, stack->port, "BCHAN: bchan ACT Confirm pid:%d\n",bc->pid);
		free_msg(msg);

		return 1;

	case DL_ESTABLISH | REQUEST:
		{
			char buf[128];
			mISDN_write_frame(stack->midev, buf, bc->addr | FLG_MSG_TARGET | FLG_MSG_DOWN,  DL_ESTABLISH | CONFIRM, 0,0, NULL, TIMEOUT_1SEC);
		}
		free_msg(msg);
		return 1;

	case DL_RELEASE|REQUEST:
		{
			char buf[128];
			mISDN_write_frame(stack->midev, buf, bc->addr | FLG_MSG_TARGET | FLG_MSG_DOWN,  DL_RELEASE| CONFIRM, 0,0, NULL, TIMEOUT_1SEC);
		}
		free_msg(msg);
		return 1;

	case PH_DEACTIVATE | INDICATION:
	case DL_RELEASE | INDICATION:
		cb_log (3, stack->port, "BCHAN: DeACT Ind pid:%d\n",bc->pid);

		free_msg(msg);
		return 1;

	case PH_DEACTIVATE | CONFIRM:
	case DL_RELEASE | CONFIRM:
		cb_log(3, stack->port, "BCHAN: DeACT Conf pid:%d\n",bc->pid);

		free_msg(msg);
		return 1;

	case PH_CONTROL|INDICATION:
	{
		unsigned int *cont = (unsigned int *) &frm->data.p;

		cb_log(4, stack->port,
			"PH_CONTROL: channel:%d caller%d:\"%s\" <%s> dialed%d:%s \n",
			bc->channel,
			bc->caller.number_type,
			bc->caller.name,
			bc->caller.number,
			bc->dialed.number_type,
			bc->dialed.number);

		if ((*cont & ~DTMF_TONE_MASK) == DTMF_TONE_VAL) {
			int dtmf = *cont & DTMF_TONE_MASK;
			cb_log(4, stack->port, " --> DTMF TONE: %c\n",dtmf);
			bc->dtmf=dtmf;
			cb_event(EVENT_DTMF_TONE, bc, glob_mgr->user_data);

			free_msg(msg);
			return 1;
		}
		if (*cont == BF_REJECT) {
			cb_log(4, stack->port, " --> BF REJECT\n");
			free_msg(msg);
			return 1;
		}
		if (*cont == BF_ACCEPT) {
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


		if (!bc->bframe_len) {
			cb_log(2, stack->port, "DL_DATA INDICATION bc->addr:%x frm->addr:%x\n", bc->addr, frm->addr);
			free_msg(msg);
			return 1;
		}

		if ( (bc->addr&STACK_ID_MASK) != (frm->addr&STACK_ID_MASK) ) {
			cb_log(2, stack->port, "DL_DATA INDICATION bc->addr:%x frm->addr:%x\n", bc->addr, frm->addr);
			free_msg(msg);
			return 1;
		}

#if MISDN_DEBUG
		cb_log(0, stack->port, "DL_DATA INDICATION Len %d\n", frm->len);

#endif

		if ( (bc->bc_state == BCHAN_ACTIVATED) && frm->len > 0) {
			int t;

#ifdef MISDN_B_DEBUG
			cb_log(0,bc->port,"do_tone START\n");
#endif
			t=do_tone(bc,frm->len);

#ifdef MISDN_B_DEBUG
			cb_log(0,bc->port,"do_tone STOP (%d)\n",t);
#endif
			if (  !t ) {
				int i;

				if ( misdn_cap_is_speech(bc->capability)) {
					if ( !bc->nojitter ) {
#ifdef MISDN_B_DEBUG
						cb_log(0,bc->port,"tx_jitter START\n");
#endif
						misdn_tx_jitter(bc,frm->len);
#ifdef MISDN_B_DEBUG
						cb_log(0,bc->port,"tx_jitter STOP\n");
#endif
					}
				}

#ifdef MISDN_B_DEBUG
				cb_log(0,bc->port,"EVENT_B_DATA START\n");
#endif

				i = cb_event(EVENT_BCHAN_DATA, bc, glob_mgr->user_data);
#ifdef MISDN_B_DEBUG
				cb_log(0,bc->port,"EVENT_B_DATA STOP\n");
#endif

				if (i<0) {
					cb_log(10,stack->port,"cb_event returned <0\n");
					/*clean_up_bc(bc);*/
				}
			}
		}
		free_msg(msg);
		return 1;
	}


	case PH_CONTROL | CONFIRM:
		cb_log(4, stack->port, "PH_CONTROL|CNF bc->addr:%x\n", frm->addr);
		free_msg(msg);
		return 1;

	case PH_DATA | CONFIRM:
	case DL_DATA|CONFIRM:
#if MISDN_DEBUG

		cb_log(0, stack->port, "Data confirmed\n");

#endif
		free_msg(msg);
		return 1;
	case DL_DATA|RESPONSE:
#if MISDN_DEBUG
		cb_log(0, stack->port, "Data response\n");

#endif
		break;
	}

	return 0;
}



static int handle_frm_nt(msg_t *msg)
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
				cb_log(0, stack->port, "NT Stack sends us error: %d \n", err);
			} else if (nt_err_cnt < 105){
				cb_log(0, stack->port, "NT Stack sends us error: %d over 100 times, so I'll stop this message\n", err);
				nt_err_cnt = - 1;
			}
		}
		free_msg(msg);
		return 1;

	}

	return 1;
}


static int handle_frm(msg_t *msg)
{
	struct misdn_bchannel dummybc;
	struct misdn_bchannel *bc;
	iframe_t *frm;
	struct misdn_stack *stack;
	enum event_e event;
	enum event_response_e response;
	int ret;
	int channel;
	int tmpcause;
	int tmp_out_cause;

	frm = (iframe_t*) msg->data;
	stack = find_stack_by_addr(frm->addr);
	if (!stack || stack->nt) {
		return 0;
	}

	cb_log(4, stack ? stack->port : 0, "handle_frm: frm->addr:%x frm->prim:%x\n", frm->addr, frm->prim);

	ret = handle_cr(stack, frm);
	if (ret < 0) {
		cb_log(3, stack ? stack->port : 0, "handle_frm: handle_cr <0 prim:%x addr:%x\n", frm->prim, frm->addr);
	}
	if (ret) {
		free_msg(msg);
		return 1;
	}

	bc = find_bc_by_l3id(stack, frm->dinfo);
	if (!bc) {
		misdn_make_dummy(&dummybc, stack->port, 0, stack->nt, 0);
		switch (frm->prim) {
		case CC_RESTART | CONFIRM:
			dummybc.l3_id = MISDN_ID_GLOBAL;
			bc = &dummybc;
			break;
		case CC_SETUP | INDICATION:
			dummybc.l3_id = frm->dinfo;
			bc = &dummybc;

			misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE);

			free_msg(msg);
			return 1;
		default:
			if (frm->prim == (CC_FACILITY | INDICATION)) {
				cb_log(5, stack->port, " --> Using Dummy BC for FACILITY\n");
			} else {
				cb_log(0, stack->port, " --> Didn't find BC so temporarily creating dummy BC (l3id:%x) on this port.\n", frm->dinfo);
				dummybc.l3_id = frm->dinfo;
			}
			bc = &dummybc;
			break;
		}
	}

	event = isdn_msg_get_event(msgs_g, msg, 0);
	isdn_msg_parse_event(msgs_g, msg, bc, 0);

	/* Preprocess some Events */
	ret = handle_event(bc, event, frm);
	if (ret < 0) {
		cb_log(0, stack->port, "couldn't handle event\n");
		free_msg(msg);
		return 1;
	}

	/* shoot up event to App: */
	cb_log(5, stack->port, "lib Got Prim: Addr %x prim %x dinfo %x\n", frm->addr, frm->prim, frm->dinfo);

	if (!isdn_get_info(msgs_g, event, 0)) {
		cb_log(0, stack->port, "Unknown Event Ind: Addr:%x prim %x dinfo %x\n", frm->addr, frm->prim, frm->dinfo);
		response = RESPONSE_OK;
	} else {
		response = cb_event(event, bc, glob_mgr->user_data);
	}

	switch (event) {
	case EVENT_SETUP:
		switch (response) {
		case RESPONSE_IGNORE_SETUP_WITHOUT_CLOSE:
			cb_log(0, stack->port, "TOTALLY IGNORING SETUP\n");
			break;
		case RESPONSE_IGNORE_SETUP:
			/* I think we should send CC_RELEASE_CR, but am not sure*/
			bc->out_cause = AST_CAUSE_NORMAL_CLEARING;
			/* fall through */
		case RESPONSE_RELEASE_SETUP:
			misdn_lib_send_event(bc, EVENT_RELEASE_COMPLETE);
			if (bc->channel > 0) {
				empty_chan_in_stack(stack, bc->channel);
			}
			empty_bc(bc);
			bc_state_change(bc, BCHAN_CLEANED);
			bc->in_use = 0;

			cb_log(0, stack->port, "GOT IGNORE SETUP\n");
			break;
		case RESPONSE_OK:
			cb_log(4, stack->port, "GOT SETUP OK\n");
			break;
		default:
			break;
		}
		break;
	case EVENT_RELEASE_COMPLETE:
		/* release bchannel only after we've announced the RELEASE_COMPLETE */
		channel = bc->channel;
		tmpcause = bc->cause;
		tmp_out_cause = bc->out_cause;

		empty_bc(bc);
		bc->cause = tmpcause;
		bc->out_cause = tmp_out_cause;
		clean_up_bc(bc);

		if (tmpcause == AST_CAUSE_REQUESTED_CHAN_UNAVAIL) {
			cb_log(0, stack->port, "**** Received CAUSE:%d, so not cleaning up channel %d\n", AST_CAUSE_REQUESTED_CHAN_UNAVAIL, channel);
			cb_log(0, stack->port, "**** This channel is now no longer available,\nplease try to restart it with 'misdn send restart <port> <channel>'\n");
			set_chan_in_stack(stack, channel);
			bc->channel = channel;
			misdn_lib_send_restart(stack->port, channel);
		} else if (channel > 0) {
			empty_chan_in_stack(stack, channel);
		}
		bc->in_use = 0;
		break;
	case EVENT_RESTART:
		cb_log(0, stack->port, "**** Received RESTART_ACK channel:%d\n", bc->restart_channel);
		empty_chan_in_stack(stack, bc->restart_channel);
		break;
	default:
		break;
	}

	cb_log(5, stack->port, "Freeing Msg on prim:%x \n", frm->prim);
	free_msg(msg);
	return 1;
}


static int handle_l1(msg_t *msg)
{
	iframe_t *frm = (iframe_t*) msg->data;
	struct misdn_stack *stack = find_stack_by_addr(frm->addr);
	int i ;

	if (!stack) return 0 ;

	switch (frm->prim) {
	case PH_ACTIVATE | CONFIRM:
	case PH_ACTIVATE | INDICATION:
		cb_log (3, stack->port, "L1: PH L1Link Up!\n");
		stack->l1link=1;

		if (stack->nt) {

			if (stack->nst.l1_l2(&stack->nst, msg))
				free_msg(msg);

			if (stack->ptp)
				misdn_lib_get_l2_up(stack);
		} else {
			free_msg(msg);
		}

		for (i=0;i<=stack->b_num; i++) {
			if (stack->bc[i].evq != EVENT_NOTHING) {
				cb_log(4, stack->port, "Firing Queued Event %s because L1 got up\n", isdn_get_info(msgs_g, stack->bc[i].evq, 0));
				misdn_lib_send_event(&stack->bc[i],stack->bc[i].evq);
				stack->bc[i].evq=EVENT_NOTHING;
			}

		}
		return 1;

	case PH_ACTIVATE | REQUEST:
		free_msg(msg);
		cb_log(3,stack->port,"L1: PH_ACTIVATE|REQUEST \n");
		return 1;

	case PH_DEACTIVATE | REQUEST:
		free_msg(msg);
		cb_log(3,stack->port,"L1: PH_DEACTIVATE|REQUEST \n");
		return 1;

	case PH_DEACTIVATE | CONFIRM:
	case PH_DEACTIVATE | INDICATION:
		cb_log (3, stack->port, "L1: PH L1Link Down! \n");

#if 0
		for (i=0; i<=stack->b_num; i++) {
			if (global_state == MISDN_INITIALIZED)  {
				cb_event(EVENT_CLEANUP, &stack->bc[i], glob_mgr->user_data);
			}
		}
#endif

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

static int handle_l2(msg_t *msg)
{
	iframe_t *frm = (iframe_t*) msg->data;

	struct misdn_stack *stack = find_stack_by_addr(frm->addr);

	if (!stack) {
		return 0 ;
	}

	switch(frm->prim) {

	case DL_ESTABLISH | REQUEST:
		cb_log(1,stack->port,"DL_ESTABLISH|REQUEST \n");
		free_msg(msg);
		return 1;
	case DL_RELEASE | REQUEST:
		cb_log(1,stack->port,"DL_RELEASE|REQUEST \n");
		free_msg(msg);
		return 1;

	case DL_ESTABLISH | INDICATION:
	case DL_ESTABLISH | CONFIRM:
	{
		cb_log (3, stack->port, "L2: L2Link Up! \n");
		if (stack->ptp && stack->l2link) {
			cb_log (-1, stack->port, "L2: L2Link Up! but it's already UP.. must be faulty, blocking port\n");
			cb_event(EVENT_PORT_ALARM, &stack->bc[0], glob_mgr->user_data);
		}
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

static int handle_mgmt(msg_t *msg)
{
	iframe_t *frm = (iframe_t*) msg->data;
	struct misdn_stack *stack;

	if ( (frm->addr == 0) && (frm->prim == (MGR_DELLAYER|CONFIRM)) ) {
		cb_log(2, 0, "MGMT: DELLAYER|CONFIRM Addr: 0 !\n") ;
		free_msg(msg);
		return 1;
	}

	stack = find_stack_by_addr(frm->addr);

	if (!stack) {
		if (frm->prim == (MGR_DELLAYER|CONFIRM)) {
			cb_log(2, 0, "MGMT: DELLAYER|CONFIRM Addr: %x !\n",
					frm->addr) ;
			free_msg(msg);
			return 1;
		}

		return 0;
	}

	switch(frm->prim) {
	case MGR_SHORTSTATUS | INDICATION:
	case MGR_SHORTSTATUS | CONFIRM:
		cb_log(5, 0, "MGMT: Short status dinfo %x\n",frm->dinfo);

		switch (frm->dinfo) {
		case SSTATUS_L1_ACTIVATED:
			cb_log(3, 0, "MGMT: SSTATUS: L1_ACTIVATED \n");
			stack->l1link=1;

			break;
		case SSTATUS_L1_DEACTIVATED:
			cb_log(3, 0, "MGMT: SSTATUS: L1_DEACTIVATED \n");
			stack->l1link=0;
#if 0
			clear_l3(stack);
#endif
			break;

		case SSTATUS_L2_ESTABLISHED:
			cb_log(3, stack->port, "MGMT: SSTATUS: L2_ESTABLISH \n");
			stack->l2link=1;
			break;

		case SSTATUS_L2_RELEASED:
			cb_log(3, stack->port, "MGMT: SSTATUS: L2_RELEASED \n");
			stack->l2link=0;
			break;
		}

		free_msg(msg);
		return 1;

	case MGR_SETSTACK | INDICATION:
		cb_log(4, stack->port, "MGMT: SETSTACK|IND dinfo %x\n",frm->dinfo);
		free_msg(msg);
		return 1;
	case MGR_DELLAYER | CONFIRM:
		cb_log(4, stack->port, "MGMT: DELLAYER|CNF dinfo %x\n",frm->dinfo) ;
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


static msg_t *fetch_msg(int midev)
{
	msg_t *msg=alloc_msg(MAX_MSG_SIZE);
	int r;

	if (!msg) {
		cb_log(0, 0, "fetch_msg: alloc msg failed !!");
		return NULL;
	}

	AGAIN:
		r=mISDN_read(midev,msg->data,MAX_MSG_SIZE, TIMEOUT_10SEC);
		msg->len=r;

		if (r==0) {
			free_msg(msg); /* danger, cause usually freeing in main_loop */
			cb_log(6,0,"Got empty Msg..\n");
			return NULL;
		}

		if (r<0) {
			if (errno == EAGAIN) {
				/*we wait for mISDN here*/
				cb_log(4,0,"mISDN_read wants us to wait\n");
				usleep(5000);
				goto AGAIN;
			}

			cb_log(0,0,"mISDN_read returned :%d error:%s (%d)\n",r,strerror(errno),errno);
		}

#if 0
               if  (!(frm->prim == (DL_DATA|INDICATION) )|| (frm->prim == (PH_DATA|INDICATION)))
                       cb_log(0,0,"prim: %x dinfo:%x addr:%x msglen:%d frm->len:%d\n",frm->prim, frm->dinfo, frm->addr, msg->len,frm->len );
#endif
		return msg;
}

void misdn_lib_isdn_l1watcher(int port)
{
	struct misdn_stack *stack;

	for (stack = glob_mgr->stack_list; stack && (stack->port != port); stack = stack->next)
		;

	if (stack) {
		cb_log(4, port, "Checking L1 State\n");
		if (!stack->l1link) {
			cb_log(4, port, "L1 State Down, trying to get it up again\n");
			misdn_lib_get_short_status(stack);
			misdn_lib_get_l1_up(stack);
			misdn_lib_get_l2_up(stack);
		}
	}
}

/* This is a thread */
static void misdn_lib_isdn_event_catcher(void *arg)
{
	struct misdn_lib *mgr = arg;
	int zero_frm=0 , fff_frm=0 ;
	int midev= mgr->midev;
	int port=0;

	while (1) {
		msg_t *msg = fetch_msg(midev);
		iframe_t *frm;


		if (!msg) continue;

		frm = (iframe_t*) msg->data;

		/** When we make a call from NT2Ast we get these frames **/
		if (frm->len == 0 && frm->addr == 0 && frm->dinfo == 0 && frm->prim == 0 ) {
			zero_frm++;
			free_msg(msg);
			continue;
		} else {
			if (zero_frm) {
				cb_log(0, port, "*** Alert: %d zero_frms caught\n", zero_frm);
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
				cb_log(0, port, "*** Alert: %d fff_frms caught\n", fff_frm);
				fff_frm = 0 ;
			}
		}

		manager_isdn_handler(frm, msg);
	}

}


/** App Interface **/

static int te_lib_init(void)
{
	char buff[1025] = "";
	iframe_t *frm = (iframe_t *) buff;
	int midev;
	int ret;

	midev = mISDN_open();
	if (midev <= 0) {
		return midev;
	}

	/* create entity for layer 3 TE-mode */
	mISDN_write_frame(midev, buff, 0, MGR_NEWENTITY | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);

	ret = mISDN_read_frame(midev, frm, sizeof(iframe_t), 0, MGR_NEWENTITY | CONFIRM, TIMEOUT_1SEC);
	entity = frm->dinfo & 0xffff;
	if (ret < mISDN_HEADER_LEN || !entity) {
		fprintf(stderr, "cannot request MGR_NEWENTITY from mISDN: %s\n", strerror(errno));
		exit(-1);
	}

	return midev;
}

void te_lib_destroy(int midev)
{
	char buf[1024];
	mISDN_write_frame(midev, buf, 0, MGR_DELENTITY | REQUEST, entity, 0, NULL, TIMEOUT_1SEC);

	cb_log(4, 0, "Entity deleted\n");
	mISDN_close(midev);
	cb_log(4, 0, "midev closed\n");
}

struct misdn_bchannel *manager_find_bc_by_pid(int pid)
{
	struct misdn_stack *stack;
	int i;

	for (stack=glob_mgr->stack_list;
	     stack;
	     stack=stack->next) {
		for (i=0; i<=stack->b_num; i++)
			if (stack->bc[i].pid == pid) return &stack->bc[i];
	}

	return NULL;
}

struct misdn_bchannel *manager_find_bc_holded(struct misdn_bchannel* bc)
{
	struct misdn_stack *stack=get_stack_by_bc(bc);
	return find_bc_holded(stack);
}



static int test_inuse(struct misdn_bchannel *bc)
{
	struct timeval now;

	if (!bc->in_use) {
		gettimeofday(&now, NULL);
		if (bc->last_used.tv_sec == now.tv_sec
			&& misdn_lib_port_is_pri(bc->port)) {
			cb_log(2, bc->port, "channel with stid:%x for one second still in use! (n:%d lu:%d)\n",
				bc->b_stid, (int) now.tv_sec, (int) bc->last_used.tv_sec);
			return 1;
		}

		cb_log(3,bc->port, "channel with stid:%x not in use!\n", bc->b_stid);
		return 0;
	}

	cb_log(2,bc->port, "channel with stid:%x in use!\n", bc->b_stid);
	return 1;
}


static void prepare_bc(struct misdn_bchannel*bc, int channel)
{
	bc->channel = channel;
	bc->channel_preselected = channel?1:0;
	bc->in_use = 1;
	bc->need_disconnect=1;
	bc->need_release=1;
	bc->need_release_complete=1;
	bc->cause = AST_CAUSE_NORMAL_CLEARING;

	if (++mypid>5000) mypid=1;
	bc->pid=mypid;

#if 0
	bc->addr=0;
	bc->b_stid=0;
	bc->layer_id=0;
#endif
}

struct misdn_bchannel *misdn_lib_get_free_bc(int port, int channel, int inout, int dec)
{
	struct misdn_stack *stack;
	int i;
	int maxnum;

	if (channel < 0 || channel > MAX_BCHANS) {
		cb_log(0, port, "Requested channel out of bounds (%d)\n", channel);
		return NULL;
	}

	usleep(1000);

	/* Find the port stack structure */
	stack = find_stack_by_port(port);
	if (!stack) {
		cb_log(0, port, "Port is not configured (%d)\n", port);
		return NULL;
	}

	if (stack->blocked) {
		cb_log(0, port, "Port is blocked\n");
		return NULL;
	}

	if (channel > 0) {
		if (channel <= stack->b_num) {
			for (i = 0; i < stack->b_num; i++) {
				if (stack->bc[i].channel == channel) {
					if (test_inuse(&stack->bc[i])) {
						cb_log(0, port, "Requested channel:%d on port:%d is already in use\n", channel, port);
						return NULL;
					} else {
						prepare_bc(&stack->bc[i], channel);
						return &stack->bc[i];
					}
				}
			}
		} else {
			cb_log(0, port, "Requested channel:%d is out of bounds on port:%d\n", channel, port);
			return NULL;
		}
	}

	/* Note: channel == 0 here */
	maxnum = (inout && !stack->pri && !stack->ptp) ? stack->b_num + 1 : stack->b_num;
	if (dec) {
		for (i = maxnum - 1; i >= 0; --i) {
			if (!test_inuse(&stack->bc[i])) {
				/* 3. channel on bri means CW*/
				if (!stack->pri && i == stack->b_num) {
					stack->bc[i].cw = 1;
				}

				prepare_bc(&stack->bc[i], channel);
				stack->bc[i].dec = 1;
				return &stack->bc[i];
			}
		}
	} else {
		for (i = 0; i < maxnum; ++i) {
			if (!test_inuse(&stack->bc[i])) {
				/* 3. channel on bri means CW */
				if (!stack->pri && i == stack->b_num) {
					stack->bc[i].cw = 1;
				}

				prepare_bc(&stack->bc[i], channel);
				return &stack->bc[i];
			}
		}
	}

	cb_log(1, port, "There is no free channel on port (%d)\n", port);
	return NULL;
}

#if defined(AST_MISDN_ENHANCEMENTS)
/*!
 * \brief Allocate a B channel struct from the REGISTER pool
 *
 * \param port Logical port number
 *
 * \retval B channel struct on success.
 * \retval NULL on error.
 */
struct misdn_bchannel *misdn_lib_get_register_bc(int port)
{
	struct misdn_stack *stack;
	struct misdn_bchannel *bc;
	unsigned index;

	/* Find the port stack structure */
	stack = find_stack_by_port(port);
	if (!stack) {
		cb_log(0, port, "Port is not configured (%d)\n", port);
		return NULL;
	}

	if (stack->blocked) {
		cb_log(0, port, "Port is blocked\n");
		return NULL;
	}

	for (index = MAX_BCHANS + 1; index < ARRAY_LEN(stack->bc); ++index) {
		bc = &stack->bc[index];
		if (!test_inuse(bc)) {
			prepare_bc(bc, 0);
			bc->need_disconnect = 0;
			bc->need_release = 0;
			return bc;
		}
	}

	cb_log(1, port, "There is no free REGISTER link on port (%d)\n", port);
	return NULL;
}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

/*!
 * \internal
 * \brief Convert the facility function enum value into a string.
 *
 * \return String version of the enum value
 */
static const char *fac2str(enum FacFunction facility)
{
	static const struct {
		enum FacFunction facility;
		char *name;
	} arr[] = {
/* *INDENT-OFF* */
		{ Fac_None, "Fac_None" },
#if defined(AST_MISDN_ENHANCEMENTS)
		{ Fac_ERROR, "Fac_ERROR" },
		{ Fac_RESULT, "Fac_RESULT" },
		{ Fac_REJECT, "Fac_REJECT" },

		{ Fac_ActivationDiversion, "Fac_ActivationDiversion" },
		{ Fac_DeactivationDiversion, "Fac_DeactivationDiversion" },
		{ Fac_ActivationStatusNotificationDiv, "Fac_ActivationStatusNotificationDiv" },
		{ Fac_DeactivationStatusNotificationDiv, "Fac_DeactivationStatusNotificationDiv" },
		{ Fac_InterrogationDiversion, "Fac_InterrogationDiversion" },
		{ Fac_DiversionInformation, "Fac_DiversionInformation" },
		{ Fac_CallDeflection, "Fac_CallDeflection" },
		{ Fac_CallRerouteing, "Fac_CallRerouteing" },
		{ Fac_DivertingLegInformation2, "Fac_DivertingLegInformation2" },
		{ Fac_InterrogateServedUserNumbers, "Fac_InterrogateServedUserNumbers" },
		{ Fac_DivertingLegInformation1, "Fac_DivertingLegInformation1" },
		{ Fac_DivertingLegInformation3, "Fac_DivertingLegInformation3" },

		{ Fac_EctExecute, "Fac_EctExecute" },
		{ Fac_ExplicitEctExecute, "Fac_ExplicitEctExecute" },
		{ Fac_RequestSubaddress, "Fac_RequestSubaddress" },
		{ Fac_SubaddressTransfer, "Fac_SubaddressTransfer" },
		{ Fac_EctLinkIdRequest, "Fac_EctLinkIdRequest" },
		{ Fac_EctInform, "Fac_EctInform" },
		{ Fac_EctLoopTest, "Fac_EctLoopTest" },

		{ Fac_ChargingRequest, "Fac_ChargingRequest" },
		{ Fac_AOCSCurrency, "Fac_AOCSCurrency" },
		{ Fac_AOCSSpecialArr, "Fac_AOCSSpecialArr" },
		{ Fac_AOCDCurrency, "Fac_AOCDCurrency" },
		{ Fac_AOCDChargingUnit, "Fac_AOCDChargingUnit" },
		{ Fac_AOCECurrency, "Fac_AOCECurrency" },
		{ Fac_AOCEChargingUnit, "Fac_AOCEChargingUnit" },

		{ Fac_StatusRequest, "Fac_StatusRequest" },

		{ Fac_CallInfoRetain, "Fac_CallInfoRetain" },
		{ Fac_EraseCallLinkageID, "Fac_EraseCallLinkageID" },
		{ Fac_CCBSDeactivate, "Fac_CCBSDeactivate" },
		{ Fac_CCBSErase, "Fac_CCBSErase" },
		{ Fac_CCBSRemoteUserFree, "Fac_CCBSRemoteUserFree" },
		{ Fac_CCBSCall, "Fac_CCBSCall" },
		{ Fac_CCBSStatusRequest, "Fac_CCBSStatusRequest" },
		{ Fac_CCBSBFree, "Fac_CCBSBFree" },
		{ Fac_CCBSStopAlerting, "Fac_CCBSStopAlerting" },

		{ Fac_CCBSRequest, "Fac_CCBSRequest" },
		{ Fac_CCBSInterrogate, "Fac_CCBSInterrogate" },

		{ Fac_CCNRRequest, "Fac_CCNRRequest" },
		{ Fac_CCNRInterrogate, "Fac_CCNRInterrogate" },

		{ Fac_CCBS_T_Call, "Fac_CCBS_T_Call" },
		{ Fac_CCBS_T_Suspend, "Fac_CCBS_T_Suspend" },
		{ Fac_CCBS_T_Resume, "Fac_CCBS_T_Resume" },
		{ Fac_CCBS_T_RemoteUserFree, "Fac_CCBS_T_RemoteUserFree" },
		{ Fac_CCBS_T_Available, "Fac_CCBS_T_Available" },

		{ Fac_CCBS_T_Request, "Fac_CCBS_T_Request" },

		{ Fac_CCNR_T_Request, "Fac_CCNR_T_Request" },

#else

		{ Fac_CFActivate, "Fac_CFActivate" },
		{ Fac_CFDeactivate, "Fac_CFDeactivate" },
		{ Fac_CD, "Fac_CD" },

		{ Fac_AOCDCurrency, "Fac_AOCDCurrency" },
		{ Fac_AOCDChargingUnit, "Fac_AOCDChargingUnit" },
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */
/* *INDENT-ON* */
	};

	unsigned index;

	for (index = 0; index < ARRAY_LEN(arr); ++index) {
		if (arr[index].facility == facility) {
			return arr[index].name;
		}
	}

	return "unknown";
}

void misdn_lib_log_ies(struct misdn_bchannel *bc)
{
	struct misdn_stack *stack;

	if (!bc) return;

	stack = get_stack_by_bc(bc);

	if (!stack) return;

	cb_log(2, stack->port,
		" --> channel:%d mode:%s cause:%d ocause:%d\n",
		bc->channel,
		stack->nt ? "NT" : "TE",
		bc->cause,
		bc->out_cause);

	cb_log(2, stack->port,
		" --> info_dad:%s dialed numtype:%d plan:%d\n",
		bc->info_dad,
		bc->dialed.number_type,
		bc->dialed.number_plan);

	cb_log(2, stack->port,
		" --> caller:\"%s\" <%s> type:%d plan:%d pres:%d screen:%d\n",
		bc->caller.name,
		bc->caller.number,
		bc->caller.number_type,
		bc->caller.number_plan,
		bc->caller.presentation,
		bc->caller.screening);

	cb_log(2, stack->port,
		" --> redirecting-from:\"%s\" <%s> type:%d plan:%d pres:%d screen:%d\n",
		bc->redirecting.from.name,
		bc->redirecting.from.number,
		bc->redirecting.from.number_type,
		bc->redirecting.from.number_plan,
		bc->redirecting.from.presentation,
		bc->redirecting.from.screening);
	cb_log(2, stack->port,
		" --> redirecting-to:\"%s\" <%s> type:%d plan:%d pres:%d screen:%d\n",
		bc->redirecting.to.name,
		bc->redirecting.to.number,
		bc->redirecting.to.number_type,
		bc->redirecting.to.number_plan,
		bc->redirecting.to.presentation,
		bc->redirecting.to.screening);
	cb_log(2, stack->port,
		" --> redirecting reason:%d count:%d\n",
		bc->redirecting.reason,
		bc->redirecting.count);

	cb_log(2, stack->port,
		" --> connected:\"%s\" <%s> type:%d plan:%d pres:%d screen:%d\n",
		bc->connected.name,
		bc->connected.number,
		bc->connected.number_type,
		bc->connected.number_plan,
		bc->connected.presentation,
		bc->connected.screening);

	cb_log(3, stack->port, " --> caps:%s pi:%x keypad:%s sending_complete:%d\n", bearer2str(bc->capability),bc->progress_indicator, bc->keypad, bc->sending_complete);

	cb_log(4, stack->port, " --> set_pres:%d pres:%d\n", bc->set_presentation, bc->presentation);

	cb_log(4, stack->port, " --> addr:%x l3id:%x b_stid:%x layer_id:%x\n", bc->addr, bc->l3_id, bc->b_stid, bc->layer_id);

	cb_log(4, stack->port, " --> facility in:%s out:%s\n", fac2str(bc->fac_in.Function), fac2str(bc->fac_out.Function));

	cb_log(5, stack->port, " --> urate:%d rate:%d mode:%d user1:%d\n", bc->urate, bc->rate, bc->mode,bc->user1);

	cb_log(5, stack->port, " --> bc:%p h:%d sh:%d\n", bc, bc->holded, bc->stack_holder);
}


#define RETURN(a,b) {retval=a; goto b;}

static void misdn_send_lock(struct misdn_bchannel *bc)
{
	//cb_log(0,bc->port,"Locking bc->pid:%d\n", bc->pid);
	if (bc->send_lock)
		pthread_mutex_lock(&bc->send_lock->lock);
}

static void misdn_send_unlock(struct misdn_bchannel *bc)
{
	//cb_log(0,bc->port,"UnLocking bc->pid:%d\n", bc->pid);
	if (bc->send_lock)
		pthread_mutex_unlock(&bc->send_lock->lock);
}

int misdn_lib_send_event(struct misdn_bchannel *bc, enum event_e event )
{
	msg_t *msg;
	struct misdn_bchannel *bc2;
	struct misdn_bchannel *held_bc;
	struct misdn_stack *stack;
	int retval = 0;
	int channel;
	int tmpcause;
	int tmp_out_cause;

	if (!bc)
		RETURN(-1,OUT_POST_UNLOCK);

	stack = get_stack_by_bc(bc);
	if (!stack) {
		cb_log(0,bc->port,
			"SENDEVENT: no Stack for event:%s caller:\"%s\" <%s> dialed:%s \n",
			isdn_get_info(msgs_g, event, 0),
			bc->caller.name,
			bc->caller.number,
			bc->dialed.number);
		RETURN(-1,OUT);
	}

	misdn_send_lock(bc);


	cb_log(6,stack->port,"SENDEVENT: stack->nt:%d stack->upperid:%x\n",stack->nt, stack->upper_id);

	if ( stack->nt && !stack->l1link) {
		/** Queue Event **/
		bc->evq=event;
		cb_log(1, stack->port, "Queueing Event %s because L1 is down (btw. Activating L1)\n", isdn_get_info(msgs_g, event, 0));
		misdn_lib_get_l1_up(stack);
		RETURN(0,OUT);
	}

	cb_log(1, stack->port,
		"I SEND:%s caller:\"%s\" <%s> dialed:%s pid:%d\n",
		isdn_get_info(msgs_g, event, 0),
		bc->caller.name,
		bc->caller.number,
		bc->dialed.number,
		bc->pid);
	cb_log(4, stack->port, " --> bc_state:%s\n",bc_state2str(bc->bc_state));
	misdn_lib_log_ies(bc);

	switch (event) {
	case EVENT_REGISTER:
	case EVENT_SETUP:
		if (create_process(glob_mgr->midev, bc) < 0) {
			cb_log(0, stack->port, " No free channel at the moment @ send_event\n");

			RETURN(-ENOCHAN,OUT);
		}
		break;

	case EVENT_PROGRESS:
	case EVENT_ALERTING:
	case EVENT_PROCEEDING:
	case EVENT_SETUP_ACKNOWLEDGE:
	case EVENT_CONNECT:
		if (!stack->nt)
			break;

	case EVENT_RETRIEVE_ACKNOWLEDGE:
		if (stack->nt) {
			if (bc->channel <=0 ) { /*  else we have the channel already */
				if (find_free_chan_in_stack(stack, bc, 0, 0)<0) {
					cb_log(0, stack->port, " No free channel at the moment\n");
					/*FIXME: add disconnect*/
					RETURN(-ENOCHAN,OUT);
				}
			}
			/* Its that i generate channels */
		}

		retval=setup_bc(bc);
		if (retval == -EINVAL) {
			cb_log(0,bc->port,"send_event: setup_bc failed\n");
		}

		if (misdn_cap_is_speech(bc->capability)) {
			if ((event==EVENT_CONNECT)||(event==EVENT_RETRIEVE_ACKNOWLEDGE)) {
				if ( *bc->crypt_key ) {
					cb_log(4, stack->port,
						" --> ENABLING BLOWFISH channel:%d caller%d:\"%s\" <%s> dialed%d:%s\n",
						bc->channel,
						bc->caller.number_type,
						bc->caller.name,
						bc->caller.number,
						bc->dialed.number_type,
						bc->dialed.number);

					manager_ph_control_block(bc,  BF_ENABLE_KEY, bc->crypt_key, strlen(bc->crypt_key) );
				}

				if (!bc->nodsp) manager_ph_control(bc,  DTMF_TONE_START, 0);
				manager_ec_enable(bc);

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
		break;

	case EVENT_HOLD_ACKNOWLEDGE:
		held_bc = malloc(sizeof(struct misdn_bchannel));
		if (!held_bc) {
			cb_log(0, bc->port, "Could not allocate held_bc!!!\n");
			RETURN(-1,OUT);
		}

		/* backup the bc and put it in storage */
		*held_bc = *bc;
		held_bc->holded = 1;
		held_bc->channel = 0;/* A held call does not have a channel anymore. */
		held_bc->channel_preselected = 0;
		held_bc->channel_found = 0;
		bc_state_change(held_bc, BCHAN_CLEANED);
		stack_holder_add(stack, held_bc);

		/* kill the bridge and clean the real b-channel record */
		if (stack->nt) {
			if (bc->bc_state == BCHAN_BRIDGED) {
				misdn_split_conf(bc,bc->conf_id);
				bc2 = find_bc_by_confid(bc->conf_id);
				if (!bc2) {
					cb_log(0,bc->port,"We have no second bc in bridge???\n");
				} else {
					misdn_split_conf(bc2,bc->conf_id);
				}
			}

			channel = bc->channel;

			empty_bc(bc);
			clean_up_bc(bc);

			if (channel>0)
				empty_chan_in_stack(stack,channel);

			bc->in_use=0;
		}
		break;

	/* finishing the channel eh ? */
	case EVENT_DISCONNECT:
		if (!bc->need_disconnect) {
			cb_log(0, bc->port, " --> we have already sent DISCONNECT\n");
			RETURN(-1,OUT);
		}

		bc->need_disconnect=0;
		break;
	case EVENT_RELEASE:
		if (!bc->need_release) {
			cb_log(0, bc->port, " --> we have already sent RELEASE\n");
			RETURN(-1,OUT);
		}
		bc->need_disconnect=0;
		bc->need_release=0;
		break;
	case EVENT_RELEASE_COMPLETE:
		if (!bc->need_release_complete) {
			cb_log(0, bc->port, " --> we have already sent RELEASE_COMPLETE\n");
			RETURN(-1,OUT);
		}
		bc->need_disconnect=0;
		bc->need_release=0;
		bc->need_release_complete=0;

		if (!stack->nt) {
			/* create cleanup in TE */
			channel = bc->channel;
			tmpcause = bc->cause;
			tmp_out_cause = bc->out_cause;

			empty_bc(bc);
			bc->cause=tmpcause;
			bc->out_cause=tmp_out_cause;
			clean_up_bc(bc);

			if (channel>0)
				empty_chan_in_stack(stack,channel);

			bc->in_use=0;
		}
		break;

	case EVENT_CONNECT_ACKNOWLEDGE:
		if ( bc->nt || misdn_cap_is_speech(bc->capability)) {
			int retval=setup_bc(bc);
			if (retval == -EINVAL){
				cb_log(0,bc->port,"send_event: setup_bc failed\n");

			}
		}

		if (misdn_cap_is_speech(bc->capability)) {
			if (  !bc->nodsp) manager_ph_control(bc,  DTMF_TONE_START, 0);
			manager_ec_enable(bc);

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
	if (!msg) {
		/*
		 * The message was not built.
		 *
		 * NOTE:  The only time that the message will fail to build
		 * is because the requested FACILITY message is not supported.
		 * A failed malloc() results in exit() being called.
		 */
		RETURN(-1, OUT);
	} else {
		msg_queue_tail(&stack->downqueue, msg);
		sem_post(&glob_mgr->new_msg);
	}

OUT:
	misdn_send_unlock(bc);

OUT_POST_UNLOCK:
	return retval;
}


static int handle_err(msg_t *msg)
{
	iframe_t *frm = (iframe_t*) msg->data;


	if (!frm->addr) {
		static int cnt=0;
		if (!cnt)
			cb_log(0,0,"mISDN Msg without Address pr:%x dinfo:%x\n",frm->prim,frm->dinfo);
		cnt++;
		if (cnt>100) {
			cb_log(0,0,"mISDN Msg without Address pr:%x dinfo:%x (already more than 100 of them)\n",frm->prim,frm->dinfo);
			cnt=0;
		}

		free_msg(msg);
		return 1;

	}

	switch (frm->prim) {
		case MGR_SETSTACK|INDICATION:
			return handle_bchan(msg);
		break;

		case MGR_SETSTACK|CONFIRM:
		case MGR_CLEARSTACK|CONFIRM:
			free_msg(msg) ;
			return 1;
		break;

		case DL_DATA|CONFIRM:
			cb_log(4,0,"DL_DATA|CONFIRM\n");
			free_msg(msg);
			return 1;

		case PH_CONTROL|CONFIRM:
			cb_log(4,0,"PH_CONTROL|CONFIRM\n");
			free_msg(msg);
			return 1;

		case DL_DATA|INDICATION:
		{
			int port=(frm->addr&MASTER_ID_MASK) >> 8;
			int channel=(frm->addr&CHILD_ID_MASK) >> 16;
			struct misdn_bchannel *bc;

			/*we flush the read buffer here*/

			cb_log(9,0,"BCHAN DATA without BC: addr:%x port:%d channel:%d\n",frm->addr, port,channel);

			free_msg(msg);
			return 1;


			bc = find_bc_by_channel(port, channel);

			if (!bc) {
				struct misdn_stack *stack = find_stack_by_port(port);

				if (!stack) {
					cb_log(0,0," --> stack not found\n");
					free_msg(msg);
					return 1;
				}

				cb_log(0,0," --> bc not found by channel\n");
				if (stack->l2link)
					misdn_lib_get_l2_down(stack);

				if (stack->l1link)
					misdn_lib_get_l1_down(stack);

				free_msg(msg);
				return 1;
			}

			cb_log(3,port," --> BC in state:%s\n", bc_state2str(bc->bc_state));
		}
	}

	return 0;
}

#if 0
static int queue_l2l3(msg_t *msg)
{
	iframe_t *frm= (iframe_t*)msg->data;
	struct misdn_stack *stack;
	stack=find_stack_by_addr( frm->addr );


	if (!stack) {
		return 0;
	}

	msg_queue_tail(&stack->upqueue, msg);
	sem_post(&glob_mgr->new_msg);
	return 1;
}
#endif

int manager_isdn_handler(iframe_t *frm ,msg_t *msg)
{

	if (frm->dinfo==0xffffffff && frm->prim==(PH_DATA|CONFIRM)) {
		cb_log(0,0,"SERIOUS BUG, dinfo == 0xffffffff, prim == PH_DATA | CONFIRM !!!!\n");
	}

	if ( ((frm->addr | ISDN_PID_BCHANNEL_BIT )>> 28 ) == 0x5) {
		static int unhandled_bmsg_count=1000;
		if (handle_bchan(msg)) {
			return 0 ;
		}

		if (unhandled_bmsg_count==1000) {
			cb_log(0, 0, "received 1k Unhandled Bchannel Messages: prim %x len %d from addr %x, dinfo %x on this port.\n",frm->prim, frm->len, frm->addr, frm->dinfo);
			unhandled_bmsg_count=0;
		}

		unhandled_bmsg_count++;
		free_msg(msg);
		return 0;
	}

#ifdef RECV_FRM_SYSLOG_DEBUG
	syslog(LOG_NOTICE,"mISDN recv: P(%02d): ADDR:%x PRIM:%x DINFO:%x\n",stack->port, frm->addr, frm->prim, frm->dinfo);
#endif

	if (handle_timers(msg))
		return 0 ;


	if (handle_mgmt(msg))
		return 0 ;

	if (handle_l2(msg))
		return 0 ;

	/* Its important to handle l1 AFTER l2  */
	if (handle_l1(msg))
		return 0 ;

	if (handle_frm_nt(msg)) {
		return 0;
	}

	if (handle_frm(msg)) {
		return 0;
	}

	if (handle_err(msg)) {
		return 0 ;
	}

	cb_log(0, 0, "Unhandled Message: prim %x len %d from addr %x, dinfo %x on this port.\n",frm->prim, frm->len, frm->addr, frm->dinfo);
	free_msg(msg);


	return 0;
}




int misdn_lib_get_port_info(int port)
{
	msg_t *msg=alloc_msg(MAX_MSG_SIZE);
	iframe_t *frm;
	struct misdn_stack *stack=find_stack_by_port(port);
	if (!msg) {
		cb_log(0, port, "misdn_lib_get_port_info: alloc_msg failed!\n");
		return -1;
	}
	frm=(iframe_t*)msg->data;
	if (!stack ) {
		cb_log(0, port, "There is no Stack for this port.\n");
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


int queue_cleanup_bc(struct misdn_bchannel *bc)
{
	msg_t *msg=alloc_msg(MAX_MSG_SIZE);
	iframe_t *frm;
	if (!msg) {
		cb_log(0, bc->port, "queue_cleanup_bc: alloc_msg failed!\n");
		return -1;
	}
	frm=(iframe_t*)msg->data;

	/* activate bchannel */
	frm->prim = MGR_CLEARSTACK| REQUEST;

	frm->addr = bc->l3_id;

	frm->dinfo = bc->port;
	frm->len = 0;

	msg_queue_tail(&glob_mgr->activatequeue, msg);
	sem_post(&glob_mgr->new_msg);

	return 0;

}

int misdn_lib_pid_restart(int pid)
{
	struct misdn_bchannel *bc=manager_find_bc_by_pid(pid);

	if (bc) {
		manager_clean_bc(bc);
	}
	return 0;
}

/*Sends Restart message for every bchannel*/
int misdn_lib_send_restart(int port, int channel)
{
	struct misdn_stack *stack=find_stack_by_port(port);
	struct misdn_bchannel dummybc;
	/*default is all channels*/
	cb_log(0, port, "Sending Restarts on this port.\n");

	misdn_make_dummy(&dummybc, stack->port, MISDN_ID_GLOBAL, stack->nt, 0);

	/*default is all channels*/
	if (channel <0) {
		dummybc.channel=-1;
		cb_log(0, port, "Restarting and all Interfaces\n");
		misdn_lib_send_event(&dummybc, EVENT_RESTART);

		return 0;
	}

	/*if a channel is specified we restart only this one*/
	if (channel >0) {
		int cnt;
		dummybc.channel=channel;
		cb_log(0, port, "Restarting and cleaning channel %d\n",channel);
		misdn_lib_send_event(&dummybc, EVENT_RESTART);
		/* clean up chan in stack, to be sure we don't think it's
		 * in use anymore */
		for (cnt=0; cnt<=stack->b_num; cnt++) {
			if (stack->bc[cnt].channel == channel) {
				empty_bc(&stack->bc[cnt]);
				clean_up_bc(&stack->bc[cnt]);
				stack->bc[cnt].in_use=0;
			}
		}
	}

	return 0;
}

/*reinitializes the L2/L3*/
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
				cb_log(0, port, "port_restart: alloc_msg failed\n");
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

		if (stack->nt)
			misdn_lib_reinit_nt_stack(stack->port);

	}

	return 0;
}



static sem_t handler_started;

/* This is a thread */
static void manager_event_handler(void *arg)
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

			case MGR_CLEARSTACK | REQUEST:
				/*a queued bchannel cleanup*/
				{
					struct misdn_stack *stack=find_stack_by_port(frm->dinfo);
					struct misdn_bchannel *bc;
					if (!stack) {
						cb_log(0,0,"no stack found with port [%d]!! so we cannot cleanup the bc\n",frm->dinfo);
						free_msg(msg);
						break;
					}

					bc = find_bc_by_l3id(stack, frm->addr);
					if (bc) {
						cb_log(1,bc->port,"CLEARSTACK queued, cleaning up\n");
						clean_up_bc(bc);
					} else {
						cb_log(0,stack->port,"bc could not be cleaned correctly !! addr [%x]\n",frm->addr);
					}
				}
				free_msg(msg);
				break;
			case MGR_SETSTACK | REQUEST :
				free_msg(msg);
				break;
			default:
				mISDN_write(glob_mgr->midev, frm, mISDN_HEADER_LEN+frm->len, TIMEOUT_1SEC);
				free_msg(msg);
			}
		}

		for (stack=glob_mgr->stack_list;
		     stack;
		     stack=stack->next ) {

			while ( (msg=msg_dequeue(&stack->upqueue)) ) {
				/** Handle L2/3 Signalling after bchans **/
				if (!handle_frm_nt(msg)) {
					/* Maybe it's TE */
					if (!handle_frm(msg)) {
						/* wow none! */
						cb_log(0,stack->port,"Wow we've got a strange issue while dequeueing a Frame\n");
					}
				}
			}

			/* Here we should check if we really want to
				send all the messages we've queued, lets
				assume we've queued a Disconnect, but
				received it already from the other side!*/

			while ( (msg=msg_dequeue(&stack->downqueue)) ) {
				if (stack->nt ) {
					if (stack->nst.manager_l3(&stack->nst, msg))
						cb_log(0, stack->port, "Error@ Sending Message in NT-Stack.\n");

				} else {
					iframe_t *frm = (iframe_t *)msg->data;
					struct misdn_bchannel *bc = find_bc_by_l3id(stack, frm->dinfo);
					if (bc)
						send_msg(glob_mgr->midev, bc, msg);
					else  {
						if (frm->dinfo == MISDN_ID_GLOBAL || frm->dinfo == MISDN_ID_DUMMY ) {
							struct misdn_bchannel dummybc;
							cb_log(5,0," --> GLOBAL/DUMMY\n");
							misdn_make_dummy(&dummybc, stack->port, frm->dinfo, stack->nt, 0);
							send_msg(glob_mgr->midev, &dummybc, msg);
						} else {
							cb_log(0,0,"No bc for Message\n");
						}
					}
				}
			}
		}
	}
}


int misdn_lib_maxports_get(void)
{
	/* BE AWARE WE HAVE NO cb_log() HERE! */

	int i = mISDN_open();
	int max=0;

	if (i<0)
		return -1;

	max = mISDN_get_stack_count(i);

	mISDN_close(i);

	return max;
}


void misdn_lib_nt_keepcalls( int kc)
{
#ifdef FEATURE_NET_KEEPCALLS
	if (kc) {
		struct misdn_stack *stack=get_misdn_stack();
		for ( ; stack; stack=stack->next) {
			stack->nst.feature |= FEATURE_NET_KEEPCALLS;
		}
	}
#endif
}

void misdn_lib_nt_debug_init( int flags, char *file )
{
	static int init=0;
	char *f;

	if (!flags)
		f=NULL;
	else
		f=file;

	if (!init) {
		debug_init( flags , f, f, f);
		init=1;
	} else {
		debug_close();
		debug_init( flags , f, f, f);
	}
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

	glob_mgr = mgr;

	msg_init();

	misdn_lib_nt_debug_init(0,NULL);

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
		struct misdn_stack *help;
		int ptp=0;
		int i;
		int r;

		if (strstr(tok, "ptp"))
			ptp=1;

		if (port > port_count) {
			cb_log(0, port, "Couldn't Initialize this port since we have only %d ports\n", port_count);
			exit(1);
		}

		stack = stack_init(midev, port, ptp);
		if (!stack) {
			perror("stack_init");
			exit(1);
		}

		/* Initialize the B channel records for real B channels. */
		for (i = 0; i <= stack->b_num; i++) {
			r = init_bc(stack, &stack->bc[i], stack->midev, port, i);
			if (r < 0) {
				cb_log(0, port, "Got Err @ init_bc :%d\n", r);
				exit(1);
			}
		}
#if defined(AST_MISDN_ENHANCEMENTS)
		/* Initialize the B channel records for REGISTER signaling links. */
		for (i = MAX_BCHANS + 1; i < ARRAY_LEN(stack->bc); ++i) {
			r = init_bc(stack, &stack->bc[i], stack->midev, port, i);
			if (r < 0) {
				cb_log(0, port, "Got Err @ init_bc :%d\n", r);
				exit(1);
			}
			stack->bc[i].is_register_pool = 1;
		}
#endif	/* defined(AST_MISDN_ENHANCEMENTS) */

		/* Add the new stack to the end of the list */
		help = mgr->stack_list;
		if (!help) {
			mgr->stack_list = stack;
		} else {
			while (help->next) {
				help = help->next;
			}
			help->next = stack;
		}
	}

	if (sem_init(&handler_started, 1, 0)<0)
		sem_init(&handler_started, 0, 0);

	cb_log(8, 0, "Starting Event Handler\n");
	pthread_create( &mgr->event_handler_thread, NULL,(void*)manager_event_handler, mgr);

	sem_wait(&handler_started) ;
	cb_log(8, 0, "Starting Event Catcher\n");
	pthread_create( &mgr->event_thread, NULL, (void*)misdn_lib_isdn_event_catcher, mgr);

	cb_log(8, 0, "Event Catcher started\n");

	global_state= MISDN_INITIALIZED;

	return (mgr == NULL);
}

void misdn_lib_destroy(void)
{
	struct misdn_stack *help;
	int i;

	for ( help=glob_mgr->stack_list; help; help=help->next ) {
		for(i=0;i<=help->b_num; i++) {
			char buf[1024];
			mISDN_write_frame(help->midev, buf, help->bc[i].addr, MGR_DELLAYER | REQUEST, 0, 0, NULL, TIMEOUT_1SEC);
			help->bc[i].addr = 0;
		}
		cb_log (1, help->port, "Destroying this port.\n");
		stack_destroy(help);
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
	char buf[128];

	struct misdn_stack *stack=get_stack_by_bc(bc);

	if (!stack) {
		cb_log(0, bc->port, "bchannel_activate: Stack not found !");
		return ;
	}

	/* we must activate if we are deactivated */
	clear_ibuffer(bc->astbuf);

	cb_log(5, stack->port, "$$$ Bchan Activated addr %x\n", bc->addr);

	mISDN_write_frame(stack->midev, buf, bc->addr | FLG_MSG_DOWN,  DL_ESTABLISH | REQUEST, 0,0, NULL, TIMEOUT_1SEC);

	return ;
}


void manager_bchannel_deactivate(struct misdn_bchannel * bc)
{
	struct misdn_stack *stack=get_stack_by_bc(bc);
	iframe_t dact;
	char buf[128];

	switch (bc->bc_state) {
		case BCHAN_ACTIVATED:
			break;
		case BCHAN_BRIDGED:
			misdn_split_conf(bc,bc->conf_id);
			break;
		default:
			cb_log( 4, bc->port,"bchan_deactivate: called but not activated\n");
			return ;

	}

	cb_log(5, stack->port, "$$$ Bchan deActivated addr %x\n", bc->addr);

	bc->generate_tone=0;

	dact.prim = DL_RELEASE | REQUEST;
	dact.addr = bc->addr | FLG_MSG_DOWN;
	dact.dinfo = 0;
	dact.len = 0;
	mISDN_write_frame(stack->midev, buf, bc->addr | FLG_MSG_DOWN, DL_RELEASE|REQUEST,0,0,NULL, TIMEOUT_1SEC);

	clear_ibuffer(bc->astbuf);

	bc_state_change(bc,BCHAN_RELEASE);

	return;
}


int misdn_lib_tx2misdn_frm(struct misdn_bchannel *bc, void *data, int len)
{
	struct misdn_stack *stack=get_stack_by_bc(bc);
	char buf[4096 + mISDN_HEADER_LEN];
	iframe_t *frm = (iframe_t*)buf;
	int r;

	switch (bc->bc_state) {
		case BCHAN_ACTIVATED:
		case BCHAN_BRIDGED:
			break;
		default:
			cb_log(3, bc->port, "BC not yet activated (state:%s)\n",bc_state2str(bc->bc_state));
			return -1;
	}

	frm->prim = DL_DATA|REQUEST;
	frm->dinfo = 0;
	frm->addr = bc->addr | FLG_MSG_DOWN ;

	frm->len = len;
	memcpy(&buf[mISDN_HEADER_LEN], data,len);

	if ( misdn_cap_is_speech(bc->capability) )
		flip_buf_bits( &buf[mISDN_HEADER_LEN], len);
	else
		cb_log(6, stack->port, "Writing %d data bytes\n",len);

	cb_log(9, stack->port, "Writing %d bytes 2 mISDN\n",len);
	r=mISDN_write(stack->midev, buf, frm->len + mISDN_HEADER_LEN, TIMEOUT_INFINIT);
	return 0;
}



/*
 * send control information to the channel (dsp-module)
 */
void manager_ph_control(struct misdn_bchannel *bc, int c1, int c2)
{
	unsigned char buffer[mISDN_HEADER_LEN+2*sizeof(int)];
	iframe_t *ctrl = (iframe_t *)buffer; /* preload data */
	unsigned int *d = (unsigned int*)&ctrl->data.p;
	/*struct misdn_stack *stack=get_stack_by_bc(bc);*/

	cb_log(4,bc->port,"ph_control: c1:%x c2:%x\n",c1,c2);

	ctrl->prim = PH_CONTROL | REQUEST;
	ctrl->addr = bc->addr | FLG_MSG_DOWN;
	ctrl->dinfo = 0;
	ctrl->len = sizeof(unsigned int)*2;
	*d++ = c1;
	*d++ = c2;
	mISDN_write(glob_mgr->midev, ctrl, mISDN_HEADER_LEN+ctrl->len, TIMEOUT_1SEC);
}

/*
 * allow live control of channel parameters
 */
void isdn_lib_update_rxgain (struct misdn_bchannel *bc)
{
	manager_ph_control(bc, VOL_CHANGE_RX, bc->rxgain);
}

void isdn_lib_update_txgain (struct misdn_bchannel *bc)
{
	manager_ph_control(bc, VOL_CHANGE_TX, bc->txgain);
}

void isdn_lib_update_ec (struct misdn_bchannel *bc)
{
#ifdef MISDN_1_2
	if (*bc->pipeline)
#else
	if (bc->ec_enable)
#endif
		manager_ec_enable(bc);
	else
		manager_ec_disable(bc);
}

void isdn_lib_stop_dtmf (struct misdn_bchannel *bc)
{
	manager_ph_control(bc, DTMF_TONE_STOP, 0);
}

/*
 * send control information to the channel (dsp-module)
 */
void manager_ph_control_block(struct misdn_bchannel *bc, int c1, void *c2, int c2_len)
{
	unsigned char buffer[mISDN_HEADER_LEN+sizeof(int)+c2_len];
	iframe_t *ctrl = (iframe_t *)buffer;
	unsigned int *d = (unsigned int *)&ctrl->data.p;
	/*struct misdn_stack *stack=get_stack_by_bc(bc);*/

	ctrl->prim = PH_CONTROL | REQUEST;
	ctrl->addr = bc->addr | FLG_MSG_DOWN;
	ctrl->dinfo = 0;
	ctrl->len = sizeof(unsigned int) + c2_len;
	*d++ = c1;
	memcpy(d, c2, c2_len);
	mISDN_write(glob_mgr->midev, ctrl, mISDN_HEADER_LEN+ctrl->len, TIMEOUT_1SEC);
}




void manager_clean_bc(struct misdn_bchannel *bc )
{
	struct misdn_stack *stack=get_stack_by_bc(bc);

	if (stack && bc->channel > 0) {
		empty_chan_in_stack(stack, bc->channel);
	}
	empty_bc(bc);
 	bc->in_use=0;

	cb_event(EVENT_CLEANUP, bc, NULL);
}


void stack_holder_add(struct misdn_stack *stack, struct misdn_bchannel *holder)
{
	struct misdn_bchannel *help;
	cb_log(4,stack->port, "*HOLDER: add %x\n",holder->l3_id);

	holder->stack_holder=1;
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
			break;
		}
	}

}

void stack_holder_remove(struct misdn_stack *stack, struct misdn_bchannel *holder)
{
	struct misdn_bchannel *h1;

	if (!holder->stack_holder) return;

	holder->stack_holder=0;

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

	cb_log(4,stack?stack->port:0, "*HOLDER: find %lx\n",l3id);

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

/*!
 * \brief Find a held call's B channel record.
 *
 * \param port Port the call is on.
 * \param l3_id mISDN Layer 3 ID of held call.
 *
 * \return Found bc-record or NULL.
 */
struct misdn_bchannel *misdn_lib_find_held_bc(int port, int l3_id)
{
	struct misdn_bchannel *bc;
	struct misdn_stack *stack;

	bc = NULL;
	for (stack = get_misdn_stack(); stack; stack = stack->next) {
		if (stack->port == port) {
			bc = stack_holder_find(stack, l3_id);
			break;
		}
	}

	return bc;
}

void misdn_lib_send_tone(struct misdn_bchannel *bc, enum tone_e tone)
{
	char buf[mISDN_HEADER_LEN + 128] = "";
	iframe_t *frm = (iframe_t*)buf;

	switch(tone) {
	case TONE_DIAL:
		manager_ph_control(bc, TONE_PATT_ON, TONE_GERMAN_DIALTONE);
	break;

	case TONE_ALERTING:
		manager_ph_control(bc, TONE_PATT_ON, TONE_GERMAN_RINGING);
	break;

	case TONE_HANGUP:
		manager_ph_control(bc, TONE_PATT_ON, TONE_GERMAN_HANGUP);
	break;

	case TONE_NONE:
	default:
		manager_ph_control(bc, TONE_PATT_OFF, TONE_GERMAN_HANGUP);
	}

	frm->prim=DL_DATA|REQUEST;
	frm->addr=bc->addr|FLG_MSG_DOWN;
	frm->dinfo=0;
	frm->len=128;

	mISDN_write(glob_mgr->midev, frm, mISDN_HEADER_LEN+frm->len, TIMEOUT_1SEC);
}


void manager_ec_enable(struct misdn_bchannel *bc)
{
	struct misdn_stack *stack=get_stack_by_bc(bc);

	cb_log(4, stack?stack->port:0,"ec_enable\n");

	if (!misdn_cap_is_speech(bc->capability)) {
		cb_log(1, stack?stack->port:0, " --> no speech? cannot enable EC\n");
	} else {

#ifdef MISDN_1_2
	if (*bc->pipeline) {
		cb_log(3, stack?stack->port:0,"Sending Control PIPELINE_CFG %s\n",bc->pipeline);
		manager_ph_control_block(bc, PIPELINE_CFG, bc->pipeline, strlen(bc->pipeline) + 1);
 	}
#else
	int ec_arr[2];

	if (bc->ec_enable) {
		cb_log(3, stack?stack->port:0,"Sending Control ECHOCAN_ON taps:%d\n",bc->ec_deftaps);

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
			cb_log(0, stack->port, "Taps should be power of 2\n");
			bc->ec_deftaps=128;
		}

		ec_arr[0]=bc->ec_deftaps;
		ec_arr[1]=0;

		manager_ph_control_block(bc,  ECHOCAN_ON,  ec_arr, sizeof(ec_arr));
	}
#endif
	}
}



void manager_ec_disable(struct misdn_bchannel *bc)
{
	struct misdn_stack *stack=get_stack_by_bc(bc);

	cb_log(4, stack?stack->port:0," --> ec_disable\n");

	if (!misdn_cap_is_speech(bc->capability)) {
		cb_log(1, stack?stack->port:0, " --> no speech? cannot disable EC\n");
		return;
	}

#ifdef MISDN_1_2
	manager_ph_control_block(bc, PIPELINE_CFG, "", 0);
#else
	if ( ! bc->ec_enable) {
		cb_log(3, stack?stack->port:0, "Sending Control ECHOCAN_OFF\n");
		manager_ph_control(bc,  ECHOCAN_OFF, 0);
	}
#endif
}

struct misdn_stack *get_misdn_stack(void)
{
	return glob_mgr->stack_list;
}



void misdn_join_conf(struct misdn_bchannel *bc, int conf_id)
{
	char data[16] = "";

	bc_state_change(bc,BCHAN_BRIDGED);
	manager_ph_control(bc, CMX_RECEIVE_OFF, 0);
	manager_ph_control(bc, CMX_CONF_JOIN, conf_id);

	cb_log(3,bc->port, "Joining bc:%x in conf:%d\n",bc->addr,conf_id);

	misdn_lib_tx2misdn_frm(bc, data, sizeof(data) - 1);
}


void misdn_split_conf(struct misdn_bchannel *bc, int conf_id)
{
	bc_state_change(bc,BCHAN_ACTIVATED);
	manager_ph_control(bc, CMX_RECEIVE_ON, 0);
	manager_ph_control(bc, CMX_CONF_SPLIT, conf_id);

	cb_log(4,bc->port, "Splitting bc:%x in conf:%d\n",bc->addr,conf_id);
}

void misdn_lib_bridge( struct misdn_bchannel * bc1, struct misdn_bchannel *bc2)
{
	int conf_id = bc1->pid + 1;
	struct misdn_bchannel *bc_list[] = { bc1, bc2, NULL };
	struct misdn_bchannel **bc;

	cb_log(4, bc1->port, "I Send: BRIDGE from:%d to:%d\n",bc1->port,bc2->port);

	for (bc=bc_list; *bc;  bc++) {
		(*bc)->conf_id=conf_id;
		cb_log(4, (*bc)->port, " --> bc_addr:%x\n",(*bc)->addr);

		switch((*bc)->bc_state) {
			case BCHAN_ACTIVATED:
				misdn_join_conf(*bc,conf_id);
				break;
			default:
				bc_next_state_change(*bc,BCHAN_BRIDGED);
				break;
		}
	}
}

void misdn_lib_split_bridge( struct misdn_bchannel * bc1, struct misdn_bchannel *bc2)
{

	struct misdn_bchannel *bc_list[]={
		bc1,bc2,NULL
	};
	struct misdn_bchannel **bc;

	for (bc=bc_list; *bc;  bc++) {
		if ( (*bc)->bc_state == BCHAN_BRIDGED){
			misdn_split_conf( *bc, (*bc)->conf_id);
		} else {
			cb_log( 2, (*bc)->port, "BC not bridged (state:%s) so not splitting it\n",bc_state2str((*bc)->bc_state));
		}
	}

}



void misdn_lib_echo(struct misdn_bchannel *bc, int onoff)
{
	cb_log(3,bc->port, " --> ECHO %s\n", onoff?"ON":"OFF");
	manager_ph_control(bc, onoff?CMX_ECHO_ON:CMX_ECHO_OFF, 0);
}



void misdn_lib_reinit_nt_stack(int port)
{
	struct misdn_stack *stack=find_stack_by_port(port);

	if (stack) {
		stack->l2link=0;
		stack->blocked=0;

		cleanup_Isdnl3(&stack->nst);
		cleanup_Isdnl2(&stack->nst);


		memset(&stack->nst, 0, sizeof(net_stack_t));
		memset(&stack->mgr, 0, sizeof(manager_t));

		stack->mgr.nst = &stack->nst;
		stack->nst.manager = &stack->mgr;

		stack->nst.l3_manager = handle_event_nt;
		stack->nst.device = glob_mgr->midev;
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

		if (!stack->ptp)
			misdn_lib_get_l1_up(stack);
		misdn_lib_get_l2_up(stack);
	}
}


