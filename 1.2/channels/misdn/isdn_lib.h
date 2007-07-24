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


/** For initialization usage **/
/* typedef int ie_nothing_t ;*/
/** end of init usage **/


/* 
 * uncomment the following to make chan_misdn create
 * record files in /tmp/misdn-{rx|tx}-PortChannel format 
 * */

/*#define MISDN_SAVE_DATA*/

#ifdef WITH_BEROEC
typedef int beroec_t;


enum beroec_type {
	BEROEC_FULLBAND=0,
	BEROEC_SUBBAND,
	BEROEC_FASTSUBBAND
};

void beroec_init(void);
void beroec_exit(void);
beroec_t *beroec_new(int tail, enum beroec_type type, int anti_howl,
		     int tonedisable, int zerocoeff, int adapt, int nlp);

void beroec_destroy(beroec_t *ec);
int beroec_cancel_alaw_chunk(beroec_t *ec, 
	char *send, 
	char *receive , 
	int len);

int beroec_version(void);
#endif



enum tone_e {
	TONE_NONE=0,
	TONE_DIAL,
	TONE_ALERTING,
	TONE_FAR_ALERTING,
	TONE_BUSY,
	TONE_HANGUP,
	TONE_CUSTOM,
	TONE_FILE
};



#define MAX_BCHANS 31

enum bchannel_state {
	BCHAN_CLEANED=0,
	BCHAN_EMPTY,
	BCHAN_SETUP,
	BCHAN_SETUPED,
	BCHAN_ACTIVE,
	BCHAN_ACTIVATED,
	BCHAN_BRIDGE,
	BCHAN_BRIDGED,
	BCHAN_RELEASE,
	BCHAN_RELEASED,
	BCHAN_CLEAN,
	BCHAN_CLEAN_REQUEST,
	BCHAN_ERROR
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
	RESPONSE_RELEASE_SETUP,
	RESPONSE_ERR,
	RESPONSE_OK
};


enum event_e {
	EVENT_NOTHING,
	EVENT_TONE_GENERATE,
	EVENT_BCHAN_DATA,
	EVENT_BCHAN_ACTIVATED,
	EVENT_BCHAN_ERROR,
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
	EVENT_PORT_ALARM,
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



/** FACILITY STUFF **/

enum facility_type {
	FACILITY_NONE,
	FACILITY_CALLDEFLECT=0x91,
	FACILITY_CENTREX=0x88
};

union facility {
	char calldeflect_nr[15];
	char cnip[256];
};




struct misdn_bchannel {
	struct send_lock *send_lock;

	int dummy;

	int nt;
	int pri;

	int port;
	/** init stuff **/
	int b_stid;
	/* int b_addr; */
	int layer_id;

	int layer;
	
	/*state stuff*/
	int need_disconnect;
	int need_release;
	int need_release_complete;

	int dec;
	/** var stuff**/
	int l3_id;
	int pid;
	int ces;

	int restart_channel;
	int channel;
	int channel_preselected;
	
	int in_use;
	int cw;
	int addr;

	unsigned char * bframe;
	int bframe_len;
	int time_usec;
	
	
	void *astbuf;

	void *misdnbuf;

	int te_choose_channel;
	int early_bconnect;
	
	/* dtmf digit */
	int dtmf;
	int send_dtmf;

	/* get setup ack */
	int need_more_infos;

	/* may there be more infos ?*/
	int sending_complete;


	/* wether we should use jollys dsp or not */
	int nodsp;
	
	/* wether we should use our jitter buf system or not */
	int nojitter;
	
	enum mISDN_NUMBER_PLAN dnumplan;
	enum mISDN_NUMBER_PLAN rnumplan;
	enum mISDN_NUMBER_PLAN onumplan;
	enum mISDN_NUMBER_PLAN cpnnumplan;

	int progress_coding;
	int progress_location;
	int progress_indicator;
	
	enum facility_type fac_type;
	union facility fac;
	
	enum facility_type out_fac_type;
	union facility out_fac;
	
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

	int generate_tone;
	int tone_cnt;
 
	enum bchannel_state bc_state;
	enum bchannel_state next_bc_state;

	int conf_id;
	
	int holded;
	int stack_holder;

	int pres;
	int screen;
	
	int capability;
	int law;
	/** V110 Stuff **/
	int rate;
	int mode;

	int user1;
	int urate;
	int hdlc;
	/* V110 */
  
	char display[84];
	char msn[32];
	char oad[32];
	char rad[32];
	char dad[32];
	char cad[32];
	char orig_dad[32];
	char keypad[32];

	char info_dad[64];
	char infos_pending[64];

/* 	unsigned char info_keypad[32]; */
/* 	unsigned char clisub[24]; */
/* 	unsigned char cldsub[24]; */
/* 	unsigned char uu[256]; */
  
	int cause;
	int out_cause;
  
	/* struct misdn_bchannel hold_bc; */
  
	/** list stuf **/

#ifdef MISDN_1_2
	char pipeline[128];
#else
	int ec_enable;
	int ec_deftaps;
	int ec_training;
#endif
	
	int channel_found;
	
	int orig;

	int txgain;
	int rxgain;
  
	struct misdn_bchannel *next;
};


enum event_response_e (*cb_event) (enum event_e event, struct misdn_bchannel *bc, void *user_data);
void (*cb_log) (int level, int port, char *tmpl, ...);
int (*cb_jb_empty)(struct misdn_bchannel *bc, char *buffer, int len);

struct misdn_lib_iface {
	
	enum event_response_e (*cb_event)(enum event_e event, struct misdn_bchannel *bc, void *user_data);
	void (*cb_log)(int level, int port, char *tmpl, ...);
	int (*cb_jb_empty)(struct misdn_bchannel *bc, char *buffer, int len);
};

/***** USER IFACE **********/

void misdn_lib_nt_debug_init( int flags, char *file );

int misdn_lib_init(char *portlist, struct misdn_lib_iface* iface, void *user_data);
int misdn_lib_send_event(struct misdn_bchannel *bc, enum event_e event );
void misdn_lib_destroy(void);

void misdn_lib_log_ies(struct misdn_bchannel *bc);

char *manager_isdn_get_info(enum event_e event);

void misdn_lib_transfer(struct misdn_bchannel* holded_bc);

struct misdn_bchannel* misdn_lib_get_free_bc(int port, int channel, int inout, int dec);

void manager_bchannel_activate(struct misdn_bchannel *bc);
void manager_bchannel_deactivate(struct misdn_bchannel * bc);

int misdn_lib_tx2misdn_frm(struct misdn_bchannel *bc, void *data, int len);

void manager_ph_control(struct misdn_bchannel *bc, int c1, int c2);


int misdn_lib_port_restart(int port);
int misdn_lib_pid_restart(int pid);
int misdn_lib_send_restart(int port, int channel);

int misdn_lib_get_port_info(int port);

int misdn_lib_is_port_blocked(int port);
int misdn_lib_port_block(int port);
int misdn_lib_port_unblock(int port);

int misdn_lib_port_up(int port, int notcheck);

int misdn_lib_get_port_down(int port);

int misdn_lib_get_port_up (int port) ;
     
int misdn_lib_maxports_get(void) ;

void misdn_lib_release(struct misdn_bchannel *bc);

int misdn_cap_is_speech(int cap);
int misdn_inband_avail(struct misdn_bchannel *bc);

int misdn_lib_send_facility(struct misdn_bchannel *bc, enum facility_type fac, void *data);


void manager_ec_enable(struct misdn_bchannel *bc);
void manager_ec_disable(struct misdn_bchannel *bc);

void misdn_lib_send_tone(struct misdn_bchannel *bc, enum tone_e tone);

void get_show_stack_details(int port, char *buf);


void misdn_lib_tone_generator_start(struct misdn_bchannel *bc);
void misdn_lib_tone_generator_stop(struct misdn_bchannel *bc);


void misdn_lib_setup_bc(struct misdn_bchannel *bc);

void misdn_lib_bridge( struct misdn_bchannel * bc1, struct misdn_bchannel *bc2);
void misdn_lib_split_bridge( struct misdn_bchannel * bc1, struct misdn_bchannel *bc2);

void misdn_lib_echo(struct misdn_bchannel *bc, int onoff);

int misdn_lib_is_ptp(int port);
int misdn_lib_get_maxchans(int port);

void misdn_lib_reinit_nt_stack(int port);

#define PRI_TRANS_CAP_SPEECH                                    0x0
#define PRI_TRANS_CAP_DIGITAL                                   0x08
#define PRI_TRANS_CAP_RESTRICTED_DIGITAL                        0x09
#define PRI_TRANS_CAP_3_1K_AUDIO                                0x10
#define PRI_TRANS_CAP_7K_AUDIO                                  0x11



char *bc_state2str(enum bchannel_state state);
void bc_state_change(struct misdn_bchannel *bc, enum bchannel_state state);

void misdn_dump_chanlist(void);


#endif
