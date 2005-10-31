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

#ifndef TE_LIB
#define TE_LIB

#include <mISDNlib.h>
#include <isdn_net.h>
#include <l3dss1.h>
#include <net_l3.h>

#include <pthread.h>

#ifndef mISDNUSER_HEAD_SIZE

#ifdef MISDNUSER_JOLLY
#define mISDNUSER_HEAD_SIZE (sizeof(mISDNuser_head_t))
#else
#define mISDNUSER_HEAD_SIZE (sizeof(mISDN_head_t))
#endif
#endif

#define MISDN_ASTERISK_TECH_PVT(ast) ast->tech_pvt
#define MISDN_ASTERISK_PVT(ast) 1
#define MISDN_ASTERISK_TYPE(ast) ast->tech->type


/* #include "ies.h" */

#define MAX_BCHANS 30


/** For initialization usage **/
/* typedef int ie_nothing_t ;*/
/** end of init usage **/


enum bc_state_e {
	STATE_NOTHING=0,
	STATE_NULL,
	STATE_CALL_INIT,
	STATE_CONNECTED,
	STATE_HOLD_ACKNOWLEDGE
};


enum tone_e {
	TONE_NONE=0,
	TONE_DIAL,
	TONE_ALERTING,
	TONE_BUSY,
	TONE_FILE
};

enum misdn_err_e {
	ENOCHAN=1
};



enum mISDN_NUMBER_PLAN {
	NUMPLAN_UNINITIALIZED=-1,
	NUMPLAN_INTERNATIONAL=0x1,
	NUMPLAN_NATIONAL=0x2,
	NUMPLAN_SUBSCRIBER=0x4,
	NUMPLAN_UNKNOWN=0x0
}; 


enum event_response_e {
	RESPONSE_IGNORE_SETUP_WITHOUT_CLOSE,
	RESPONSE_IGNORE_SETUP,
	RESPONSE_ERR,
	RESPONSE_OK
};



enum event_e {
	EVENT_NOTHING,
	EVENT_BCHAN_DATA,
	EVENT_CLEANUP,
	EVENT_PROCEEDING,
	EVENT_PROGRESS,
	EVENT_SETUP,
	EVENT_ALERTING,
	EVENT_CONNECT,
	EVENT_SETUP_ACKNOWLEDGE,
	EVENT_CONNECT_ACKNOWLEDGE ,
	EVENT_USER_INFORMATION,
	EVENT_SUSPEND_REJECT,
	EVENT_RESUME_REJECT,
	EVENT_HOLD,
	EVENT_SUSPEND,
	EVENT_RESUME,
	EVENT_HOLD_ACKNOWLEDGE,
	EVENT_SUSPEND_ACKNOWLEDGE,
	EVENT_RESUME_ACKNOWLEDGE,
	EVENT_HOLD_REJECT,
	EVENT_RETRIEVE,
	EVENT_RETRIEVE_ACKNOWLEDGE,
	EVENT_RETRIEVE_REJECT,
	EVENT_DISCONNECT,
	EVENT_RESTART,
	EVENT_RELEASE,
	EVENT_RELEASE_COMPLETE,
	EVENT_FACILITY,
	EVENT_NOTIFY,
	EVENT_STATUS_ENQUIRY,
	EVENT_INFORMATION,
	EVENT_STATUS,
	EVENT_TIMEOUT,
	EVENT_DTMF_TONE,
	EVENT_NEW_L3ID,
	EVENT_NEW_BC,
	EVENT_UNKNOWN
}; 


enum ie_name_e {
	IE_DUMMY,
	IE_LAST
};

enum { /* bearer capability */
	INFO_CAPABILITY_SPEECH=0,
	INFO_CAPABILITY_AUDIO_3_1K=0x10 ,
	INFO_CAPABILITY_AUDIO_7K=0x11 ,
	INFO_CAPABILITY_VIDEO =0x18,
	INFO_CAPABILITY_DIGITAL_UNRESTRICTED =0x8,
	INFO_CAPABILITY_DIGITAL_RESTRICTED =0x09,
	INFO_CAPABILITY_DIGITAL_UNRESTRICTED_TONES
};

enum { /* progress indicators */
	INFO_PI_CALL_NOT_E2E_ISDN =0x01,
	INFO_PI_CALLED_NOT_ISDN =0x02,
	INFO_PI_CALLER_NOT_ISDN =0x03,
	INFO_PI_CALLER_RETURNED_TO_ISDN =0x04,
	INFO_PI_INBAND_AVAILABLE =0x08,
	INFO_PI_DELAY_AT_INTERF =0x0a,
	INFO_PI_INTERWORKING_WITH_PUBLIC =0x10,
	INFO_PI_INTERWORKING_NO_RELEASE =0x11,
	INFO_PI_INTERWORKING_NO_RELEASE_PRE_ANSWER =0x12,
	INFO_PI_INTERWORKING_NO_RELEASE_POST_ANSWER =0x13
};

enum { /*CODECS*/
	INFO_CODEC_ULAW=2,
	INFO_CODEC_ALAW=3
}; 


enum layer_e {
	L3,
	L2,
	L1,
	UNKNOWN
}; 

enum facility_type {
	FACILITY_NONE,
	FACILITY_CALLDEFLECT
};

struct misdn_bchannel {
	/** init stuff **/
	int b_stid;
	/* int b_addr; */
	int layer_id;
  
	/** var stuff**/
	int l3_id;
	int pid;
	int ces;
  
	int channel;
	int channel_preselected;
	
	int in_use;
	int addr;

	unsigned char * bframe;
	int bframe_len;
	int time_usec;
	
	sem_t astsem;
	sem_t misdnsem;
	ibuffer_t *astbuf;
	ibuffer_t *misdnbuf;
  
	/* dtmf digit */
	int dtmf;
	int send_dtmf;

	/* wether we should use jollys dsp or not */
	int nodsp;
	
	/* wether we should use our jitter buf system or not */
	int nojitter;
	
	enum mISDN_NUMBER_PLAN dnumplan;
	enum mISDN_NUMBER_PLAN rnumplan;
	enum mISDN_NUMBER_PLAN onumplan;

	int progress_coding;
	int progress_location;
	int progress_indicator;
	
	enum facility_type facility;
	char facility_calldeflect_nr[15];
	
	enum event_e evq;
	
	/*** CRYPTING STUFF ***/
	
	int crypt;
	int curprx;
	int curptx; 
	char crypt_key[255];
  
	int crypt_state;
    
	/*char ast_dtmf_buf[255];
	  char misdn_dtmf_buf[255]; */
  
	/*** CRYPTING STUFF END***/
  
	int active;
	int upset;

	enum tone_e tone;
	int tone_cnt;
	int tone_cnt2;
  
	enum bc_state_e state;

	int holded;
	int stack_holder;
	
	int pres;
  
	int nohdlc;
	
	int capability;
	int law;
	/** V110 Stuff **/
	int rate;
	int mode;

	int user1;
	int urate;
	int async;
	/* V110 */
  
	unsigned char display[84];
	unsigned char msn[32];
	unsigned char oad[32];
	unsigned char rad[32];
	unsigned char dad[32];
	unsigned char orig_dad[32];
	unsigned char keypad[32];
  
	unsigned char info_dad[64];
	unsigned char infos_pending[64];
	unsigned char info_keypad[32];
	unsigned char clisub[24];
	unsigned char cldsub[24];
	unsigned char fac[132];
	unsigned char uu[256];
  
	int cause;
	int out_cause;
  
	/* struct misdn_bchannel hold_bc; */
  
	/** list stuf **/

	int ec_enable;
	int ec_deftaps;
	int ec_whenbridged;
	int ec_training;
	
	int orig;

	int txgain;
	int rxgain;
  
	struct misdn_bchannel *next;
	struct misdn_stack *stack;
};

struct misdn_stack {
	/** is first element because &nst equals &mISDNlist **/
	net_stack_t nst;
	manager_t mgr;
  
	int d_stid;
  
	int b_num;
  
	int b_stids[MAX_BCHANS + 1];
  
	int ptp;
	int lower_id;
	int upper_id;
  
	int l2link;
  
	time_t l2establish;
  
	int l1link;
	int midev;
  
	enum mode_e {NT_MODE, TE_MODE} mode;
	int pri;
  

	int procids[0x100+1];

	msg_queue_t downqueue;
	int busy;
  
	int port;
	struct misdn_bchannel bc[MAX_BCHANS + 1];
  
	struct misdn_bchannel* bc_list; 
  
	int channels[MAX_BCHANS + 1];

  
  
	int te_choose_channel;
  

	struct misdn_bchannel *holding; /* Queue which holds holded channels :) */
  
	struct misdn_stack *next;
}; 

struct misdn_stack* get_misdn_stack( void );

enum event_response_e (*cb_event) (enum event_e event, struct misdn_bchannel *bc, void *user_data);
void (*cb_log) (int level, int port, char *tmpl, ...);
int (*cb_clearl3_true)(void);

struct misdn_lib_iface {
	
	enum event_response_e (*cb_event)(enum event_e event, struct misdn_bchannel *bc, void *user_data);
	void (*cb_log)(int level, int port, char *tmpl, ...);
	int (*cb_clearl3_true)(void);
};

/***** USER IFACE **********/

int misdn_lib_init(char *portlist, struct misdn_lib_iface* iface, void *user_data);
int misdn_lib_send_event(struct misdn_bchannel *bc, enum event_e event );
void misdn_lib_destroy(void);

void misdn_lib_log_ies(struct misdn_bchannel *bc);

char *manager_isdn_get_info(enum event_e event);

void misdn_lib_transfer(struct misdn_bchannel* holded_bc);

struct misdn_bchannel* misdn_lib_get_free_bc(int port, int channel);

void manager_bchannel_activate(struct misdn_bchannel *bc);
void manager_bchannel_deactivate(struct misdn_bchannel * bc);
int manager_tx2misdn_frm(struct misdn_bchannel *bc, void *data, int len);
void manager_send_tone (struct misdn_bchannel *bc, enum tone_e tone);

void manager_ph_control(struct misdn_bchannel *bc, int c1, int c2);


int misdn_lib_port_restart(int port);
int misdn_lib_get_port_info(int port);

int misdn_lib_port_up(int port);

int misdn_lib_get_port_up (int port) ;
     
int misdn_lib_maxports_get(void) ;

void misdn_lib_release(struct misdn_bchannel *bc);

int misdn_cap_is_speech(int cap);
int misdn_inband_avail(struct misdn_bchannel *bc);

int misdn_lib_send_facility(struct misdn_bchannel *bc, enum facility_type fac, void *data);


struct isdn_msg {
	unsigned long misdn_msg;
  
	enum layer_e layer;
	enum event_e event;
  
	void (*msg_parser)(struct isdn_msg *msgs, msg_t *msg, struct misdn_bchannel *bc, int nt);
	msg_t *(*msg_builder)(struct isdn_msg *msgs, struct misdn_bchannel *bc, int nt);
	void (*msg_printer)(struct isdn_msg *msgs);
  
	char *info;
  
} ; 








void manager_ec_enable(struct misdn_bchannel *bc);
void manager_ec_disable(struct misdn_bchannel *bc);


/* for isdn_msg_parser.c */
msg_t *create_l3msg(int prim, int mt, int dinfo , int size, int nt);



#define PRI_TRANS_CAP_SPEECH                                    0x0
#define PRI_TRANS_CAP_DIGITAL                                   0x08
#define PRI_TRANS_CAP_RESTRICTED_DIGITAL                        0x09
#define PRI_TRANS_CAP_3_1K_AUDIO                                0x10
#define PRI_TRANS_CAP_7K_AUDIO                                  0x11

#endif
