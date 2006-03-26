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


#define MAX_BCHANS 30

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

	int nt;
	int port;
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
	
	
	void *astbuf;
	void *misdnbuf;
	

	int te_choose_channel;
	int early_bconnect;
	
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
	int screen;
	
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
};


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


void manager_ec_enable(struct misdn_bchannel *bc);
void manager_ec_disable(struct misdn_bchannel *bc);

void get_show_stack_details(int port, char *buf);


/** Ibuf interface **/
int misdn_ibuf_usedcount(void *buf);
int misdn_ibuf_freecount(void *buf);
void misdn_ibuf_memcpy_r(char *to, void *from, int len);
void misdn_ibuf_memcpy_w(void *buf, char *from, int len);

/** Ibuf interface End **/

void misdn_lib_setup_bc(struct misdn_bchannel *bc);

void misdn_lib_bridge( struct misdn_bchannel * bc1, struct misdn_bchannel *bc2);
void misdn_lib_split_bridge( struct misdn_bchannel * bc1, struct misdn_bchannel *bc2);


int misdn_lib_is_ptp(int port);

#define PRI_TRANS_CAP_SPEECH                                    0x0
#define PRI_TRANS_CAP_DIGITAL                                   0x08
#define PRI_TRANS_CAP_RESTRICTED_DIGITAL                        0x09
#define PRI_TRANS_CAP_3_1K_AUDIO                                0x10
#define PRI_TRANS_CAP_7K_AUDIO                                  0x11

#endif
