/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Implementation of the Skinny protocol
 * 
 * Asterisk is Copyright (C) 1999-2003 Mark Spencer
 *
 * chan_skinny was developed by Jeremy McNamara & Florian Overkamp
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <asterisk/lock.h>
#include <asterisk/channel.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/config.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/lock.h>
#include <asterisk/sched.h>
#include <asterisk/io.h>
#include <asterisk/rtp.h>
#include <asterisk/acl.h>
#include <asterisk/callerid.h>
#include <asterisk/cli.h>
#include <asterisk/say.h>
#include <asterisk/cdr.h>
#include <asterisk/astdb.h>
#include <asterisk/features.h>
#include <asterisk/app.h>
#include <asterisk/musiconhold.h>
#include <asterisk/utils.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/signal.h>
#include <signal.h>
#include <asterisk/dsp.h>
#include <ctype.h>

/************************************************************************************/
/*                         Skinny/Asterisk Protocol Settings                        */
/************************************************************************************/
static char *desc = "Skinny Client Control Protocol (Skinny)";
static char *tdesc = "Skinny Client Control Protocol (Skinny)";
static char *type = "Skinny";
static char *config = "skinny.conf";

/* Just about everybody seems to support ulaw, so make it a nice default */
static int capability = AST_FORMAT_ULAW;

#define DEFAULT_SKINNY_PORT	2000
#define DEFAULT_SKINNY_BACKLOG  2
#define SKINNY_MAX_PACKET	1000

static int  keep_alive = 120;
static char date_format[6] = "D-M-Y";
static char version_id[16] = "P002F202";

/* these should be in an include file, but dunno what to include */
typedef unsigned char  UINT8;
typedef unsigned short UINT16;
typedef unsigned int   UINT32;

/************************************************************************************/
/*                                Protocol Messages                                 */
/************************************************************************************/
/* message types */
#define	KEEP_ALIVE_MESSAGE 0x0000
/* no additional struct */

#define	REGISTER_MESSAGE 0x0001
typedef struct register_message {
	char name[16];
	int userId;
	int instance;
	char ip[4];
	int type;
	int maxStreams;
} register_message;

#define IP_PORT_MESSAGE	0x0002

#define KEYPAD_BUTTON_MESSAGE 0x0003
typedef struct keypad_button_message {
	int button;
} keypad_button_message;

#define STIMULUS_MESSAGE 0x0005
typedef struct stimulus_message {
	int stimulus;
	int stimulusInstance;
} stimulus_message;
		
#define OFFHOOK_MESSAGE 0x0006
#define ONHOOK_MESSAGE 0x0007

#define	CAPABILITIES_RES_MESSAGE 0x0010
typedef struct station_capabilities {	
	int codec;
	int frames;
	union {
		char res[8];
		long rate;
	} payloads;	
} station_capabilities;

typedef struct capabilities_res_message {
	int count;
	struct station_capabilities caps[18];
} capabilities_res_message;

#define SPEED_DIAL_STAT_REQ_MESSAGE 0x000A
typedef struct speed_dial_stat_req_message {
	int speedDialNumber;
} speed_dial_stat_req_message;

#define	LINE_STATE_REQ_MESSAGE 0x000B
typedef struct line_state_req_message {
	int lineNumber;
} line_state_req_message;

#define	TIME_DATE_REQ_MESSAGE 0x000D
#define	VERSION_REQ_MESSAGE 0x000F
#define BUTTON_TEMPLATE_REQ_MESSAGE 0x000E
#define SERVER_REQUEST_MESSAGE 0x0012
#define ALARM_MESSAGE 0x0020

#define OPEN_RECIEVE_CHANNEL_ACK_MESSAGE 0x0022	
typedef struct open_recieve_channel_ack_message {
	int status;
	char ipAddr[4];
	int port;
	int passThruId;
} open_recieve_channel_ack_message;

#define	SOFT_KEY_SET_REQ_MESSAGE 0x0025
#define UNREGISTER_MESSAGE 0x0027
#define	SOFT_KEY_TEMPLATE_REQ_MESSAGE 0x0028

#define	REGISTER_ACK_MESSAGE 0x0081
typedef struct register_ack_message {
	int keepAlive;
	char dateTemplate[6];
	char res[2];
	int secondaryKeepAlive;
	char res2[4];
} register_ack_message;

#define	START_TONE_MESSAGE 0x0082
typedef struct start_tone_message {
	int tone;
} start_tone_message;

#define STOP_TONE_MESSAGE 0x0083

#define SET_RINGER_MESSAGE 0x0085
typedef struct set_ringer_message {
	int ringerMode;
} set_ringer_message;

#define SET_LAMP_MESSAGE 0x0086
typedef struct set_lamp_message {
	int stimulus;
	int stimulusInstance;
	int deviceStimulus;
} set_lamp_message;

#define SET_SPEAKER_MESSAGE 0x0088 
typedef struct set_speaker_message {
	int mode;
} set_speaker_message;

#define START_MEDIA_TRANSMISSION_MESSAGE 0x008A
typedef struct media_qualifier {
	int precedence;
	int vad;
	int packets;
	int bitRate;
} media_qualifier;

typedef struct start_media_transmission_message {
	int conferenceId;
	int passThruPartyId;
	char remoteIp[4];
	int remotePort;
	int packetSize;
	int payloadType;
	media_qualifier qualifier;
} start_media_transmission_message;

#define STOP_MEDIA_TRANSMISSION_MESSAGE 0x008B
typedef struct stop_media_transmission_message {
	int conferenceId;
        int passThruPartyId;
} stop_media_transmission_message;

#define CALL_INFO_MESSAGE 0x008F
typedef struct call_info_message {
	char callingPartyName[40];
	char callingParty[24];
	char calledPartyName[40];
	char calledParty[24];
	int  instance;
	int  reference;
	int  type;
	char originalCalledPartyName[40];
	char originalCalledParty[24];
} call_info_message;

#define SPEED_DIAL_STAT_RES_MESSAGE 0x0091
typedef struct speed_dial_stat_res_message {
	int speedDialNumber;
	char speedDialDirNumber[24];
	char speedDialDisplayName[40];
} speed_dial_stat_res_message;

#define LINE_STAT_RES_MESSAGE 0x0092
typedef struct line_stat_res_message {
	int  linenumber;
	char lineDirNumber[24];
	char lineDisplayName[42];
	int  space;
} line_stat_res_message;

#define DEFINETIMEDATE_MESSAGE 0x0094
typedef struct definetimedate_message {
	int year;	/* since 1900 */
	int month;
	int dayofweek;	/* monday = 1 */
	int day;
	int hour;
	int minute;
	int seconds;
	int milliseconds;
	int timestamp;
} definetimedate_message;
 
#define DISPLAYTEXT_MESSAGE 0x0099
typedef struct displaytext_message {
	char text[40];
} displaytext_message;

#define	REGISTER_REJ_MESSAGE 0x009D
typedef struct register_rej_message {
	char errMsg[33];
} register_rej_message;

#define CAPABILITIES_REQ_MESSAGE 0x009B

#define SERVER_RES_MESSAGE 0x009E
typedef struct server_identifier {
	char serverName[48];
} server_identifier;

typedef struct server_res_message {
	server_identifier server[5];
	int serverListenPort[5];
	int serverIpAddr[5];
} server_res_message;

#define BUTTON_TEMPLATE_RES_MESSAGE 0x0097
static const char *button_definition_hack = {
	"\x01\x09\x01\x02\x02\x02\x03\x02\x04\x02\x05\x02\x06\x02\x07\x02"
	"\x08\x02\x09\x02\x00\xff\x00\xff\x00\xff\x00\xff\x00\xff\x00\xff"
	"\x00\xff\x00\xff\x00\xff\x00\xff\x00\xff\x00\xff\x00\xff\x00\xff"
	"\x00\xff\x00\xff\x00\xff\x00\xff\x00\xff\x00\xff\x00\xff\x00\xff"
	"\x00\xff\x00\xff\x00\xff\x00\xff\x00\xff\x00\xff\x00\xff\x00\xff"
	"\x00\xff\x00\xff"
};

typedef struct buttondefinition {
	UINT8 instanceNumber;
	UINT8 buttonDefinition;
} button_definition;

typedef struct button_template_res_message {
	UINT32 buttonOffset;
	UINT32 buttonCount;
	UINT32 totalButtonCount;
	button_definition definition[42];
} button_template_res_message;

#define	VERSION_RES_MESSAGE 0x0098
typedef struct version_res_message {
	char version[16];
} version_res_message;

#define	KEEP_ALIVE_ACK_MESSAGE 0x0100

#define OPEN_RECIEVE_CHANNEL_MESSAGE 0x0105
typedef struct open_recieve_channel_message {
	int conferenceId;
	int partyId;
	int packets;
	int capability;
	int echo;
	int bitrate;
} open_recieve_channel_message;

#define CLOSE_RECIEVE_CHANNEL_MESSAGE 0x0106
typedef struct close_recieve_channel_message {
	int conferenceId;
	int partyId;
} close_recieve_channel_message;

#define	SOFT_KEY_TEMPLATE_RES_MESSAGE 0x0108
static const char *soft_key_template_hack = {
	"\x52\x65\x64\x69\x61\x6c\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x01\x00\x00\x00\x4e\x65\x77\x43\x61\x6c\6c\\x00\x00\x00\x00\x00"
	"\x00\x00\x00\x00\x02\x00\x00\x00\x48\x6f\x6c\x64\x00\x00\x00\x00"
	"\x00\x00\x00\x00\x00\x00\x00\x00\x03\x00\x00\x00\x54\x72\x6e\x73"
	"\x66\x65\x72\x00\x00\x00\x00\x00\x00\x00\x00\x00\x04\x00\x00\x00"
	"\x43\x46\x77\x64\x41\x6c\x00\x00\x00\x00\x00\x00\x00\x00\x00\x05"
	"\x00\x00\x00\x43\x46\x77\x64\x20\x42\x75\x73\x79\x00\x00\x00\x00"
	"\x00\x00\x00\x06\x00\x00\x00\x43\x46\x77\x64\x4e\x6f\x41\x6e\x73"
	"\x77\x65\x72\x00\x00\x00\x00\x07\x00\x00\x00\x3c\x3c\x00\x00\x00"
	"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x08\x00\x00\x00\x45"
	"\x6e\x64\x43\x61\x6c\x6c\x00\x00\x00\x00\x00\x00\x00\x00\x00\x09"
	"\x00\x00\x00\x52\x65\x73\x75\x6d\x65\x00\x00\x00\x00\x00\x00\x00"
	"\x00\x00\x0a\x00\x00\x00\x41\x6e\x73\x77\x65\x72\x00\x00\x00\x00"
	"\x00\x00\x00\x00\x00\x00\x0b\x00\x00\x00\x49\x6e\x66\x6f\x00\x00"
	"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0c\x00\x00\x00\x43\x6f"
	"\x6e\x66\x72\x6e\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0d\x00\x00"
	"\x00\x50\x61\x72\x6b\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x00\x0e\x00\x00\x00\x4a\x6f\x69\x6e\x00\x00\x00\x00\x00\x00\x00"
	"\x00\x00\x00\x00\x0f\x00\x00\x00\x4d\x65\x65\x74\x4d\x65\x00\x00"
	"\x00\x00\x00\x00\x00\x00\x00\x00\x10\x00\x00\x00\x50\x69\x63\x6b"
	"\x55\x70\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x11\x00\x00\x00"
	"\x47\x50\x69\x63\x6b\x55\x70\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x12\x00\x00\x00\x52\x6d\x4c\x73\x43\x00\x00\x00\x00\x00\x00\x00"
	"\x00\x00\x00\x13\x00\x00\x00\x42\x61\x72\x67\x65\x00\x00\x00\x00"
	"\x00\x00\x00\x00\x00\x00\x00\x14\x00\x00\x00\x42\x61\x72\x67\x65"
	"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x15\x00\x00\x00"
};

typedef struct soft_key_template_definition {
	char softKeyLabel[16];
	int softKeyEvent;
} soft_key_template_definition;

typedef struct soft_key_template {
	int softKeyOffset;
	int softKeyCount;
	int totalSoftKeyCount;
    soft_key_template_definition softKeyTemplateDefinition[32];
} soft_key_template;

#define	SOFT_KEY_SET_RES_MESSAGE 0x0109
static const char *soft_key_set_hack = {
	"\x01\x02\x05\x03\x09\x0a\x0b\x10\x11\x12\x04\x0e\x0d\x00\x00\x00"
	"\x2d\x01\x2e\x01\x31\x01\x2f\x01\x35\x01\x36\x01\x37\x01\x3c\x01"
	"\x3d\x01\x3e\x01\x30\x01\x3a\x01\x39\x01\x00\x00\x00\x00\x00\x00"
	"\x03\x09\x04\x0e\x0d\x13\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x2f\x01\x35\x01\x30\x01\x3a\x01\x39\x01\x3f\x01\x00\x00\x00\x00"
	"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x0a\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x36\x01\x2e\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x0b\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x37\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x01\x09\x05\x10\x11\x12\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x2d\x01\x35\x01\x31\x01\x3c\x01\x3d\x01\x3e\x01\x00\x00\x00\x00"
	"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x00\x09\x04\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x00\x00\x35\x01\x30\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x08\x09\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x34\x01\x35\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x00\x09\x0d\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x00\x00\x35\x01\x39\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x00\x09\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x00\x00\x35\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x01\x09\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x2d\x01\x35\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x15\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x41\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
};

typedef struct soft_key_set_definition {
	UINT8  softKeyTemplateIndex[16];
	UINT16 softKeyInfoIndex[16];
} soft_key_set_definition;

typedef struct soft_key_sets {
	UINT32 softKeySetOffset;
	UINT32 softKeySetCount;
	UINT32 totalSoftKeySetCount;
	soft_key_set_definition softKeySetDefinition[16];
	UINT32 res;
} soft_key_sets;

#define SELECT_SOFT_KEYS_MESSAGE 0x0110
typedef struct select_soft_keys_message {
	int instance;
	int reference;
	int softKeySetIndex;
	int validKeyMask;
} select_soft_keys_message;

#define CALL_STATE_MESSAGE 0x0111
typedef struct call_state_message {
	int callState;
	int lineInstance;
	int callReference;
} call_state_message;

#define ACTIVATE_CALL_PLANE_MESSAGE 0x0116
typedef struct activate_call_plane_message {
	int lineInstance;
} activate_call_plane_message;

/* packet composition */
typedef struct {
	int len;
	int res;
	int e;
	union {
		speed_dial_stat_req_message speeddialreq;
		register_message reg;
		register_ack_message regack;
		register_rej_message regrej;
		capabilities_res_message caps;
		version_res_message version;
		button_template_res_message buttontemplate;
		displaytext_message displaytext;
		definetimedate_message definetimedate;
		start_tone_message starttone;
		speed_dial_stat_res_message speeddial;
		line_state_req_message line;
		line_stat_res_message linestat;
		soft_key_sets softkeysets;
		soft_key_template softkeytemplate;
		server_res_message serverres;
		set_lamp_message setlamp;
		set_ringer_message setringer;
		call_state_message callstate;
		keypad_button_message keypad;
		select_soft_keys_message selectsoftkey;
		activate_call_plane_message activatecallplane;
		stimulus_message stimulus;
		set_speaker_message setspeaker;
		call_info_message callinfo;
		start_media_transmission_message startmedia;
		stop_media_transmission_message stopmedia;
		open_recieve_channel_message openrecievechannel;
		open_recieve_channel_ack_message openrecievechannelack;
		close_recieve_channel_message closerecievechannel;
	} data;
} skinny_req;

/************************************************************************************/
/*                            Asterisk specific globals                             */
/************************************************************************************/

static int skinnydebug = 1;	/* XXX for now, enable debugging default */

/* a hostname, portnumber, socket and such is usefull for VoIP protocols */
static struct sockaddr_in bindaddr;
static char ourhost[256];
static int ourport;
static struct in_addr __ourip;
struct ast_hostent ahp; struct hostent *hp;
static int skinnysock  = -1;
static pthread_t tcp_thread;
static pthread_t accept_t;
static char context[AST_MAX_EXTENSION] = "default";
static char language[MAX_LANGUAGE] = "";
static char musicclass[MAX_LANGUAGE] = "";
static char cid_num[AST_MAX_EXTENSION] = "";
static char cid_name[AST_MAX_EXTENSION] = "";
static char linelabel[AST_MAX_EXTENSION] ="";
static int nat = 0;
static unsigned int cur_callergroup = 0;
static unsigned int cur_pickupgroup = 0;
static int immediate = 0;
static int callwaiting = 0;
static int callreturn = 0;
static int threewaycalling = 0;
/* This is for flashhook transfers */
static int transfer = 0;
static int cancallforward = 0;
/* static int busycount = 3;*/
static char accountcode[20] = "";
static char mailbox[AST_MAX_EXTENSION];
static int amaflags = 0;
static int callnums = 1;

#define SUB_REAL 0
#define SUB_ALT  1
#define MAX_SUBS 2

#define SKINNY_SPEAKERON 1
#define SKINNY_SPEAKEROFF 2

#define SKINNY_OFFHOOK 1
#define SKINNY_ONHOOK 2
#define SKINNY_RINGOUT 3
#define SKINNY_RINGIN 4
#define SKINNY_CONNECTED 5
#define SKINNY_BUSY 6
#define SKINNY_CONGESTION 7
#define SKINNY_HOLD 8
#define SKINNY_CALLWAIT 9
#define SKINNY_TRANSFER 10
#define SKINNY_PARK 11
#define SKINNY_PROGRESS 12
#define SKINNY_INVALID 14

#define SKINNY_SILENCE 0
#define SKINNY_DIALTONE 33
#define SKINNY_BUSYTONE 35
#define SKINNY_ALERT 36
#define SKINNY_REORDER 37
#define SKINNY_CALLWAITTONE 45

#define SKINNY_LAMP_OFF 1
#define SKINNY_LAMP_ON  2
#define SKINNY_LAMP_WINK 3
#define SKINNY_LAMP_FLASH 4
#define SKINNY_LAMP_BLINK 5

#define SKINNY_RING_OFF 1
#define SKINNY_RING_INSIDE 2
#define SKINNY_RING_OUTSIDE 3
#define SKINNY_RING_FEATURE 4

#define TYPE_TRUNK 1
#define TYPE_LINE 2

#define STIMULUS_REDIAL 1
#define STIMULUS_SPEEDDIAL 2
#define STIMULUS_HOLD 3
#define STIMULUS_TRANSFER 4
#define STIMULUS_FORWARDALL 5
#define STIMULUS_FORWARDBUSY 6
#define STIMULUS_FORWARDNOANSWER 7
#define STIMULUS_DISPLAY 8
#define STIMULUS_LINE 9

/* Skinny rtp stream modes. Do we really need this? */
#define SKINNY_CX_SENDONLY 0
#define SKINNY_CX_RECVONLY 1
#define SKINNY_CX_SENDRECV 2
#define SKINNY_CX_CONF     3
#define SKINNY_CX_CONFERENCE 3
#define SKINNY_CX_MUTE     4
#define SKINNY_CX_INACTIVE 4

#if 0
static char *skinny_cxmodes[] = {
    "sendonly",
    "recvonly",
    "sendrecv",
    "confrnce",
    "inactive"
};
#endif

/* driver scheduler */
static struct sched_context *sched;
static struct io_context *io;

/* usage count and locking */
static int usecnt = 0;
AST_MUTEX_DEFINE_STATIC(usecnt_lock);

/* Protect the monitoring thread, so only one process can kill or start it, and not
   when it's doing something critical. */
AST_MUTEX_DEFINE_STATIC(monlock);
/* Protect the network socket */
AST_MUTEX_DEFINE_STATIC(netlock);
/* Protect the session list */
AST_MUTEX_DEFINE_STATIC(sessionlock);
/* Protect the device list */
AST_MUTEX_DEFINE_STATIC(devicelock);

/* This is the thread for the monitor which checks for input on the channels
   which are not currently in use.  */
static pthread_t monitor_thread = AST_PTHREADT_NULL;

/* Wait up to 16 seconds for first digit */
static int firstdigittimeout = 16000;

/* How long to wait for following digits */
static int gendigittimeout = 8000;

/* How long to wait for an extra digit, if there is an ambiguous match */
static int matchdigittimeout = 3000;

struct skinny_subchannel {
	ast_mutex_t lock;
	unsigned int callid;
	struct ast_channel *owner;
	struct skinny_line *parent;
	struct ast_rtp *rtp;
	time_t lastouttime;
	int progress;
	int ringing;
	int lastout;
	int cxmode;
	int nat;
	int outgoing;
	int alreadygone;
	struct skinny_subchannel *next; 
};

struct skinny_line {
	ast_mutex_t lock;
	char name[80];
	char label[42];					/* Label that shows next to the line buttons */
  	struct skinny_subchannel *sub;			/* pointer to our current connection, channel and stuff */
	char accountcode[80];
	char exten[AST_MAX_EXTENSION];			/* Extention where to start */
	char context[AST_MAX_EXTENSION];
	char language[MAX_LANGUAGE];
	char cid_num[AST_MAX_EXTENSION];		/* Caller*ID */
	char cid_name[AST_MAX_EXTENSION];		/* Caller*ID */
	char lastcallerid[AST_MAX_EXTENSION];		/* Last Caller*ID */
	char call_forward[AST_MAX_EXTENSION];	
	char mailbox[AST_MAX_EXTENSION];
	char musicclass[MAX_LANGUAGE];
	int curtone;					/* Current tone being played */
	unsigned int callgroup;
	unsigned int pickupgroup;
	int callwaiting;
	int transfer;
	int threewaycalling;
	int cancallforward;
	int callreturn;
	int dnd; /* How does this affect callwait?  Do we just deny a skinny_request if we're dnd? */
	int hascallerid;
	int hidecallerid;
	int amaflags;
	int type;
	int instance;
	int group;
	int needdestroy;
	int capability;
	int nonCodecCapability;
	int onhooktime;
	int msgstate;		/* voicemail message state */
	int immediate;
	int hookstate;
	int progress;
	struct skinny_line *next;
	struct skinny_device *parent;
};

static struct skinny_device {
	/* A device containing one or more lines */
	char name[80];
	char id[16];
	char version_id[16];	
	int type;
	int registered;
	struct sockaddr_in addr;
	struct in_addr ourip;
	struct skinny_line *lines;
	struct ast_ha *ha;
	struct skinnysession *session;
	struct skinny_device *next;
} *devices = NULL;

static struct skinnysession {
	pthread_t t;
	ast_mutex_t lock;
	struct sockaddr_in sin;
	int fd;
	char inbuf[SKINNY_MAX_PACKET];
	struct skinny_device *device;
	struct skinnysession *next;
} *sessions = NULL;

static skinny_req *req_alloc(size_t size)
{
	skinny_req *req;
	req = malloc(size+12);
	if (!req) {
		return NULL;
	}	
	memset(req, 0, size+12);
	return req;
}

static struct skinny_subchannel *find_subchannel_by_line(struct skinny_line *l)
{
	/* XXX Need to figure out how to determine which sub we want */
	struct skinny_subchannel *sub = l->sub;
	return sub;
}

static struct skinny_subchannel *find_subchannel_by_name(char *dest)
{
	struct skinny_line *l;
	struct skinny_device *d;
	char line[256];
	char *at;
	char *device;
	
	strncpy(line, dest, sizeof(line) - 1);
	at = strchr(line, '@');
	if (!at) {
		ast_log(LOG_NOTICE, "Device '%s' has no @ (at) sign!\n", dest);
		return NULL;
	}
	*at = '\0';
	at++;
	device = at;
	ast_mutex_lock(&devicelock);
	d = devices;
	while(d) {
		if (!strcasecmp(d->name, device)) {
			if (skinnydebug) {
				ast_verbose("Found device: %s\n", d->name);
			}
			/* Found the device */
			l = d->lines;
			while (l) {
				/* Search for the right line */
				if (!strcasecmp(l->name, line)) {
					ast_mutex_unlock(&devicelock);
					return l->sub;
				}
				l = l->next;
			}
		}
		d = d->next;
	}
	/* Device not found*/
	ast_mutex_unlock(&devicelock);
	return NULL;
}

static int transmit_response(struct skinnysession *s, skinny_req *req)
{
	int res = 0;
	ast_mutex_lock(&s->lock);
#if 0
	ast_verbose("writing packet type %d (%d bytes) to socket %d\n", req->e, req->len+8, s->fd);
#endif
	res = write(s->fd, req, req->len+8);
	if (res != req->len+8) {
		ast_log(LOG_WARNING, "Transmit: write only sent %d out of %d bytes: %s\n", res, req->len+8, strerror(errno));
	}
	ast_mutex_unlock(&s->lock);
	return 1;
}

/* XXX Do this right */
static int convert_cap(int capability)
{
	return 4; /* ulaw (this is not the same as asterisk's '4'  */

}

static void transmit_speaker_mode(struct skinnysession *s, int mode)
{
	skinny_req *req;

	req = req_alloc(sizeof(struct set_speaker_message));
	if (!req) {
		ast_log(LOG_ERROR, "Unable to allocate skinny_request, this is bad\n");
		return;
	}
	req->len = sizeof(set_speaker_message)+4;
	req->e = SET_SPEAKER_MESSAGE;
	req->data.setspeaker.mode = mode; 
	transmit_response(s, req);
}

static void transmit_callstate(struct skinnysession *s, int instance, int state, unsigned callid)
{ 
	skinny_req *req;
	int memsize = sizeof(struct call_state_message);

	req = req_alloc(memsize);
	if (!req) {
		ast_log(LOG_ERROR, "Unable to allocate skinny_request, this is bad\n");
		return;
	}	
	if (state == SKINNY_ONHOOK) {
		transmit_speaker_mode(s, SKINNY_SPEAKEROFF);
	}
	req->len = sizeof(call_state_message)+4;
	req->e = CALL_STATE_MESSAGE;
	req->data.callstate.callState = state;
	req->data.callstate.lineInstance = instance;
	req->data.callstate.callReference = callid;
	transmit_response(s, req);
	if (state == SKINNY_OFFHOOK) {
		memset(req, 0, memsize);
		req->len = sizeof(activate_call_plane_message)+4;
		req->e = ACTIVATE_CALL_PLANE_MESSAGE;
		req->data.activatecallplane.lineInstance = instance;
		transmit_response(s, req);
	} else if (state == SKINNY_ONHOOK) {
		memset(req, 0, memsize);
		req->len = sizeof(activate_call_plane_message)+4;
		req->e = ACTIVATE_CALL_PLANE_MESSAGE;
		req->data.activatecallplane.lineInstance = 0;
		transmit_response(s, req);
		memset(req, 0, memsize);
		req->len = sizeof(close_recieve_channel_message)+4;
		req->e = CLOSE_RECIEVE_CHANNEL_MESSAGE;
		req->data.closerecievechannel.conferenceId = 0;
		req->data.closerecievechannel.partyId = 0;
		transmit_response(s, req);
		memset(req, 0, memsize);
                req->len = sizeof(stop_media_transmission_message)+4;
                req->e = STOP_MEDIA_TRANSMISSION_MESSAGE;
                req->data.stopmedia.conferenceId = 0;   
                req->data.stopmedia.passThruPartyId = 0;
                transmit_response(s, req);	
	}
}	

static void transmit_connect(struct skinnysession *s)
{
	skinny_req *req;
	struct skinny_line *l = s->device->lines;

	req = req_alloc(sizeof(struct open_recieve_channel_message));
	if (!req) {
		ast_log(LOG_ERROR, "Unable to allocate skinny_request, this is bad\n");
		return;
	}	
	req->len = sizeof(struct call_info_message);
	req->e = OPEN_RECIEVE_CHANNEL_MESSAGE;
	req->data.openrecievechannel.conferenceId = 0;
	req->data.openrecievechannel.partyId = 0;
	req->data.openrecievechannel.packets = 20;
	req->data.openrecievechannel.capability = convert_cap(l->capability); 
	req->data.openrecievechannel.echo = 0;
	req->data.openrecievechannel.bitrate = 0;
	transmit_response(s, req);
}	

static void transmit_tone(struct skinnysession *s, int tone)
{
	skinny_req *req;

	if (tone > 0)
		req = req_alloc(sizeof(struct start_tone_message));
	else 
		req = req_alloc(4);
	if (!req) {
		ast_log(LOG_ERROR, "Unable to allocate skinny_request, this is bad\n");
		return;
	}	
	if (tone > 0) {
		req->len = sizeof(start_tone_message)+4;
		req->e = START_TONE_MESSAGE;
		req->data.starttone.tone = tone; 
	} else {
		req->len = 4;
		req->e = STOP_TONE_MESSAGE;
	}
	transmit_response(s, req);
}

#if 0
/* XXX need to properly deal with softkeys */
static void transmit_selectsoftkeys(struct skinnysession *s, int instance, int callid, int softkey)
{
	skinny_req *req;
	int memsize = sizeof(struct select_soft_keys_message);

	req = req_alloc(memsize);
	if (!req) {
		ast_log(LOG_ERROR, "Unable to allocate skinny_request, this is bad\n");
		return;
	}	
	memset(req, 0, memsize);
	req->len = sizeof(select_soft_keys_message)+4;
	req->e = SELECT_SOFT_KEYS_MESSAGE;
	req->data.selectsoftkey.instance = instance;
	req->data.selectsoftkey.reference = callid;
	req->data.selectsoftkey.softKeySetIndex = softkey;
	transmit_response(s, req);
}
#endif

static void transmit_lamp_indication(struct skinnysession *s, int instance, int indication)
{
	skinny_req *req;

	req = req_alloc(sizeof(struct set_lamp_message));
	if (!req) {
		ast_log(LOG_ERROR, "Unable to allocate skinny_request, this is bad\n");
		return;
	}	
	req->len = sizeof(set_lamp_message)+4;
	req->e = SET_LAMP_MESSAGE;
	req->data.setlamp.stimulus = 0x9;  /* magic number */
	req->data.setlamp.stimulusInstance = instance;
	req->data.setlamp.deviceStimulus = indication;
	transmit_response(s, req);
}

static void transmit_ringer_mode(struct skinnysession *s, int mode)
{
	skinny_req *req;

	req = req_alloc(sizeof(struct set_ringer_message));
	if (!req) {
		ast_log(LOG_ERROR, "Unable to allocate skinny_request, this is bad\n");
		return;
	}
	req->len = sizeof(set_ringer_message)+4;
	req->e = SET_RINGER_MESSAGE; 
	req->data.setringer.ringerMode = mode; 
	transmit_response(s, req);
}

/* I do not believe skinny can deal with video. 
   Anyone know differently? */
static struct ast_rtp *skinny_get_vrtp_peer(struct ast_channel *chan)
{
	return NULL;
}

static struct ast_rtp *skinny_get_rtp_peer(struct ast_channel *chan)
{
	struct skinny_subchannel *sub;
	sub = chan->pvt->pvt;
	if (sub && sub->rtp)
		return sub->rtp;
	return NULL;
}

static int skinny_set_rtp_peer(struct ast_channel *chan, struct ast_rtp *rtp, struct ast_rtp *vrtp, int codecs)
{
	struct skinny_subchannel *sub;
	sub = chan->pvt->pvt;
	if (sub) {
		/* transmit_modify_with_sdp(sub, rtp); @@FIXME@@ if needed */
		return 0;
	}
	return -1;
}

static struct ast_rtp_protocol skinny_rtp = {
	get_rtp_info: skinny_get_rtp_peer,
	get_vrtp_info: skinny_get_vrtp_peer,
	set_rtp_peer: skinny_set_rtp_peer,
};

static int skinny_do_debug(int fd, int argc, char *argv[])
{
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	skinnydebug = 1;
	ast_cli(fd, "Skinny Debugging Enabled\n");
	return RESULT_SUCCESS;
}

static int skinny_no_debug(int fd, int argc, char *argv[])
{
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	skinnydebug = 0;
	ast_cli(fd, "Skinny Debugging Disabled\n");
	return RESULT_SUCCESS;
}

static int skinny_show_lines(int fd, int argc, char *argv[])
{
	struct skinny_device  *d;
	struct skinny_line *l;
	int haslines = 0;
	char iabuf[INET_ADDRSTRLEN];
	if (argc != 3) 
		return RESULT_SHOWUSAGE;
	ast_mutex_lock(&devicelock);
	d = devices;
	while(d) {
		l = d->lines;
		ast_cli(fd, "Device '%s' at %s\n", d->name, ast_inet_ntoa(iabuf, sizeof(iabuf), d->addr.sin_addr));
		while(l) {
			ast_cli(fd, "   -- '%s@%s in '%s' is %s\n", l->name, d->name, l->context, l->sub->owner ? "active" : "idle");
			haslines = 1;
			l = l->next;
		}
		if (!haslines) {
			ast_cli(fd, "   << No Lines Defined >>     ");
		}
		d = d->next;
	}
	ast_mutex_unlock(&devicelock);
	return RESULT_SUCCESS;
}

static char show_lines_usage[] = 
"Usage: skinny show lines\n"
"       Lists all lines known to the Skinny subsystem.\n";

static char debug_usage[] = 
"Usage: skinny debug\n"
"       Enables dumping of Skinny packets for debugging purposes\n";

static char no_debug_usage[] = 
"Usage: skinny no debug\n"
"       Disables dumping of Skinny packets for debugging purposes\n";

static struct ast_cli_entry  cli_show_lines =
	{ { "skinny", "show", "lines", NULL }, skinny_show_lines, "Show defined Skinny lines per device", show_lines_usage };
static struct ast_cli_entry  cli_debug =
	{ { "skinny", "debug", NULL }, skinny_do_debug, "Enable Skinny debugging", debug_usage };
static struct ast_cli_entry  cli_no_debug =
	{ { "skinny", "no", "debug", NULL }, skinny_no_debug, "Disable Skinny debugging", no_debug_usage };

static struct skinny_device *build_device(char *cat, struct ast_variable *v)
{
	struct skinny_device *d;
	struct skinny_line *l;
	struct skinny_subchannel *sub;
	int i=0, y=0;
	
	d = malloc(sizeof(struct skinny_device));
	if (d) {
		memset(d, 0, sizeof(struct skinny_device));
		strncpy(d->name, cat, sizeof(d->name) - 1);
		while(v) {
			if (!strcasecmp(v->name, "host")) {
					if (ast_get_ip(&d->addr, v->value)) {
						free(d);
						return NULL;
					}				
			} else if (!strcasecmp(v->name, "port")) {
				d->addr.sin_port = htons(atoi(v->value));
			} else if (!strcasecmp(v->name, "device")) {
           		strncpy(d->id, v->value, sizeof(d->id)-1);
			} else if (!strcasecmp(v->name, "permit") || !strcasecmp(v->name, "deny")) {
				d->ha = ast_append_ha(v->name, v->value, d->ha);
			} else if (!strcasecmp(v->name, "context")) {
				strncpy(context, v->value, sizeof(context) - 1);
			} else if (!strcasecmp(v->name, "version")) {
                		strncpy(d->version_id, v->value, sizeof(d->version_id) -1); 
			} else if (!strcasecmp(v->name, "nat")) {
				nat = ast_true(v->value);
			} else if (!strcasecmp(v->name, "callerid")) {
				if (!strcasecmp(v->value, "asreceived")) {
					cid_num[0] = '\0';
					cid_name[0] = '\0';
				} else {
					ast_callerid_split(v->value, cid_name, sizeof(cid_name), cid_num, sizeof(cid_num));
				}
			} else if (!strcasecmp(v->name, "language")) {
				strncpy(language, v->value, sizeof(language)-1);
       			} else if (!strcasecmp(v->name, "accountcode")) {
           			strncpy(accountcode, v->value, sizeof(accountcode)-1);
       			} else if (!strcasecmp(v->name, "amaflags")) {
           			y = ast_cdr_amaflags2int(v->value);
           			if (y < 0) {
		   			ast_log(LOG_WARNING, "Invalid AMA flags: %s at line %d\n", v->value, v->lineno);
          			} else {
           			amaflags = y;
            			}
			} else if (!strcasecmp(v->name, "musiconhold")) {
            			strncpy(musicclass, v->value, sizeof(musicclass)-1);
            		} else if (!strcasecmp(v->name, "callgroup")) {
             			cur_callergroup = ast_get_group(v->value);
            		} else if (!strcasecmp(v->name, "pickupgroup")) {
            			cur_pickupgroup = ast_get_group(v->value);
            		} else if (!strcasecmp(v->name, "immediate")) {
            			immediate = ast_true(v->value);
            		} else if (!strcasecmp(v->name, "cancallforward")) {
            			cancallforward = ast_true(v->value);
            		} else if (!strcasecmp(v->name, "mailbox")) {
            			strncpy(mailbox, v->value, sizeof(mailbox) -1);
	    		} else if (!strcasecmp(v->name, "callreturn")) {
				callreturn = ast_true(v->value);
            		} else if (!strcasecmp(v->name, "immediate")) {
				immediate = ast_true(v->value);
            		} else if (!strcasecmp(v->name, "callwaiting")) {
            			callwaiting = ast_true(v->value);
            		} else if (!strcasecmp(v->name, "transfer")) {
            			transfer = ast_true(v->value);
            		} else if (!strcasecmp(v->name, "threewaycalling")) {
                		threewaycalling = ast_true(v->value);
	    		} else if (!strcasecmp(v->name, "linelabel")) {
           			strncpy(linelabel, v->value, sizeof(linelabel)-1);
       	    		} else if (!strcasecmp(v->name, "trunk") || !strcasecmp(v->name, "line")) {
				l = malloc(sizeof(struct skinny_line));;
				if (l) {
					memset(l, 0, sizeof(struct skinny_line));
                                        ast_mutex_init(&l->lock);
					strncpy(l->name, v->value, sizeof(l->name) - 1);
					
					/* XXX Should we check for uniqueness?? XXX */
					
					strncpy(l->context, context, sizeof(l->context) - 1);
					strncpy(l->cid_num, cid_num, sizeof(l->cid_num) - 1);
					strncpy(l->cid_name, cid_name, sizeof(l->cid_name) - 1);
					strncpy(l->label, linelabel, sizeof(l->label) - 1);
					strncpy(l->language, language, sizeof(l->language) - 1);
        				strncpy(l->musicclass, musicclass, sizeof(l->musicclass)-1);
					strncpy(l->mailbox, mailbox, sizeof(l->mailbox)-1);
					strncpy(l->mailbox, mailbox, sizeof(l->mailbox)-1);
					if (!ast_strlen_zero(mailbox)) {
						ast_verbose(VERBOSE_PREFIX_3 "Setting mailbox '%s' on %s@%s\n", mailbox, d->name, l->name);
					}
				
					l->msgstate = -1;
					l->capability = capability;
					l->parent = d;
					if (!strcasecmp(v->name, "trunk")) {
						l->type = TYPE_TRUNK;
					} else {
						l->type = TYPE_LINE;
					}
					l->immediate = immediate;
					l->callgroup = cur_callergroup;
					l->pickupgroup = cur_pickupgroup;
					l->callreturn = callreturn;
		        		l->cancallforward = cancallforward;
		        		l->callwaiting = callwaiting;
		        		l->transfer = transfer;	
		        		l->threewaycalling = threewaycalling;
		        		l->onhooktime = time(NULL);
					l->instance = 1;
		        		/* ASSUME we're onhook at this point*/
              				l->hookstate = SKINNY_ONHOOK;

		       			for (i = 0; i < MAX_SUBS; i++) {
               			        	sub = malloc(sizeof(struct skinny_subchannel));
                       				if (sub) {
                           				ast_verbose(VERBOSE_PREFIX_3 "Allocating Skinny subchannel '%d' on %s@%s\n", i, l->name, d->name);
                           				memset(sub, 0, sizeof(struct skinny_subchannel));
                                                        ast_mutex_init(&sub->lock);
                       					sub->parent = l;
                       					/* Make a call*ID */
							sub->callid = callnums;
							callnums++;
                      					sub->cxmode = SKINNY_CX_INACTIVE;
                       					sub->nat = nat;
                       					sub->next = l->sub;
                       					l->sub = sub;
                       				} else {
                       					/* XXX Should find a way to clean up our memory */
                       					ast_log(LOG_WARNING, "Out of memory allocating subchannel");
                      					return NULL;
                       				}
                    			}
		    			l->next = d->lines;
					d->lines = l;			
	    			} else {
			        	/* XXX Should find a way to clean up our memory */
                    			ast_log(LOG_WARNING, "Out of memory allocating line");
                    			return NULL;
				}
			} else {
				ast_log(LOG_WARNING, "Don't know keyword '%s' at line %d\n", v->name, v->lineno);
			}
			v = v->next;
	 	}
	
	 	if (!d->lines) {
			ast_log(LOG_ERROR, "A Skinny device must have at least one line!\n");
			return NULL;
		}

		if (d->addr.sin_addr.s_addr && !ntohs(d->addr.sin_port))
			d->addr.sin_port = htons(DEFAULT_SKINNY_PORT);
		if (d->addr.sin_addr.s_addr) {
			if (ast_ouraddrfor(&d->addr.sin_addr, &d->ourip)) {
				memcpy(&d->ourip, &__ourip, sizeof(d->ourip));
			}
		} else {
			memcpy(&d->ourip, &__ourip, sizeof(d->ourip));
		}
	}
	return d;
}

static int has_voicemail(struct skinny_line *l)
{
	return ast_app_has_voicemail(l->mailbox, NULL);
}

static int skinny_register(skinny_req *req, struct skinnysession *s)
{
	struct skinny_device *d;
	
	ast_mutex_lock(&devicelock);
	d = devices;
	while (d) {
		if (!strcasecmp(req->data.reg.name, d->id)) {
			/* XXX Deal with IP authentication */
			s->device = d;
			d->type = req->data.reg.type;
			if (ast_strlen_zero(d->version_id)) {
				strncpy(d->version_id, version_id, sizeof(d->version_id) - 1);
			}
			d->registered = 1;
			d->session = s;
			break;
		}
		d = d->next;
	}
	ast_mutex_unlock(&devicelock);

	if (!d)
		return 0;
	
	return 1;
}		

static void start_rtp(struct skinny_subchannel *sub)
{
		ast_mutex_lock(&sub->lock);
		/* Allocate the RTP */
		sub->rtp = ast_rtp_new(sched, io, 1, 0);
		if (sub->rtp && sub->owner)
			sub->owner->fds[0] = ast_rtp_fd(sub->rtp);
		if (sub->rtp)
			ast_rtp_setnat(sub->rtp, sub->nat);
		
		/* Create the RTP connection */
		transmit_connect(sub->parent->parent->session);
 		ast_mutex_unlock(&sub->lock);
}


static void *skinny_ss(void *data)
{
	struct ast_channel *chan = data;
	struct skinny_subchannel *sub = chan->pvt->pvt;
	struct skinny_line *l = sub->parent;
	struct skinnysession *s = l->parent->session;
	char exten[AST_MAX_EXTENSION] = "";
	int len = 0;
	int timeout = firstdigittimeout;
	int res;
	int getforward=0;
    
	if (option_verbose > 2) {
		ast_verbose( VERBOSE_PREFIX_3 "Starting simple switch on '%s@%s'\n", l->name, l->parent->name);
	}
	while(len < AST_MAX_EXTENSION-1) {
        res = ast_waitfordigit(chan, timeout);
        timeout = 0;
        if (res < 0) {
		if (skinnydebug) {
			ast_verbose("Skinny(%s@%s): waitfordigit returned < 0\n", l->name, l->parent->name);
        	}
		ast_indicate(chan, -1);
		ast_hangup(chan);
            	return NULL;
        } else if (res)  {
            exten[len++]=res;
            exten[len] = '\0';
        }
        if (!ast_ignore_pattern(chan->context, exten)) {
			transmit_tone(s, SKINNY_SILENCE);
        } 
        if (ast_exists_extension(chan, chan->context, exten, 1, l->cid_num)) {
            if (!res || !ast_matchmore_extension(chan, chan->context, exten, 1, l->cid_num)) {
                if (getforward) {
                    /* Record this as the forwarding extension */
                    strncpy(l->call_forward, exten, sizeof(l->call_forward) - 1); 
                    if (option_verbose > 2) {
                        ast_verbose(VERBOSE_PREFIX_3 "Setting call forward to '%s' on channel %s\n", 
                                l->call_forward, chan->name);
                    }
                    transmit_tone(s, SKINNY_DIALTONE); 
		    if (res) {
	        	    break;
		    }
                    usleep(500000);
                    ast_indicate(chan, -1);
                    sleep(1);
                    memset(exten, 0, sizeof(exten));
		    transmit_tone(s, SKINNY_DIALTONE); 
                    len = 0;
                    getforward = 0;
                } else  {
                    strncpy(chan->exten, exten, sizeof(chan->exten)-1);
                    if (!ast_strlen_zero(l->cid_num)) {
                        if (!l->hidecallerid)
                            chan->cid.cid_num = strdup(l->cid_num);
                        chan->cid.cid_ani = strdup(l->cid_num);
                    }
                    ast_setstate(chan, AST_STATE_RING);
                    res = ast_pbx_run(chan);
                    if (res) {
                        ast_log(LOG_WARNING, "PBX exited non-zero\n");
						transmit_tone(s, SKINNY_REORDER); 
                    }
                    return NULL;
                }
            } else {
                /* It's a match, but they just typed a digit, and there is an ambiguous match,
                   so just set the timeout to matchdigittimeout and wait some more */
                timeout = matchdigittimeout;
            }
        } else if (res == 0) {
            ast_log(LOG_DEBUG, "Not enough digits (and no ambiguous match)...\n");
    		transmit_tone(s, SKINNY_REORDER); 
            ast_hangup(chan);
            return NULL;
        } else if (l->callwaiting && !strcmp(exten, "*70")) {
            if (option_verbose > 2) {
                ast_verbose(VERBOSE_PREFIX_3 "Disabling call waiting on %s\n", chan->name);
            }
            /* Disable call waiting if enabled */
            l->callwaiting = 0;
            transmit_tone(s, SKINNY_DIALTONE);
			len = 0;
            memset(exten, 0, sizeof(exten));
            timeout = firstdigittimeout;
                
        } else if (!strcmp(exten,ast_pickup_ext())) {
            /* Scan all channels and see if any there
             * ringing channqels with that have call groups
             * that equal this channels pickup group  
             */
            if (ast_pickup_call(chan)) {
                ast_log(LOG_WARNING, "No call pickup possible...\n");
				transmit_tone(s, SKINNY_REORDER);
            }
            ast_hangup(chan);
            return NULL;
            
        } else if (!l->hidecallerid && !strcmp(exten, "*67")) {
            if (option_verbose > 2) {
                ast_verbose(VERBOSE_PREFIX_3 "Disabling Caller*ID on %s\n", chan->name);
            }
            /* Disable Caller*ID if enabled */
            l->hidecallerid = 1;
            if (chan->cid.cid_num)
                free(chan->cid.cid_num);
            chan->cid.cid_num = NULL;
            
			if (chan->cid.cid_name)
                free(chan->cid.cid_name);
            chan->cid.cid_name = NULL;
			
            transmit_tone(s, SKINNY_DIALTONE);
            len = 0;
            memset(exten, 0, sizeof(exten));
            timeout = firstdigittimeout;
        } else if (l->callreturn && !strcmp(exten, "*69")) {
            res = 0;
            if (!ast_strlen_zero(l->lastcallerid)) {
                res = ast_say_digit_str(chan, l->lastcallerid, "", chan->language);
            }
            if (!res) {
                transmit_tone(s, SKINNY_DIALTONE);
			}
            break;
        } else if (!strcmp(exten, "*78")) {
            /* Do not disturb */
            if (option_verbose > 2) {
                ast_verbose(VERBOSE_PREFIX_3 "Enabled DND on channel %s\n", chan->name);
            }
            transmit_tone(s, SKINNY_DIALTONE);
            l->dnd = 1;
            getforward = 0;
            memset(exten, 0, sizeof(exten));
            len = 0;
        } else if (!strcmp(exten, "*79")) {
            /* Do not disturb */
            if (option_verbose > 2) {
                ast_verbose(VERBOSE_PREFIX_3 "Disabled DND on channel %s\n", chan->name);
            }
			transmit_tone(s, SKINNY_DIALTONE);
            l->dnd = 0;
            getforward = 0;
            memset(exten, 0, sizeof(exten));
            len = 0;
        } else if (l->cancallforward && !strcmp(exten, "*72")) {
            transmit_tone(s, SKINNY_DIALTONE);
            getforward = 1;
            memset(exten, 0, sizeof(exten));
            len = 0;
        } else if (l->cancallforward && !strcmp(exten, "*73")) {
            if (option_verbose > 2) {
                ast_verbose(VERBOSE_PREFIX_3 "Cancelling call forwarding on channel %s\n", chan->name);
            }
            transmit_tone(s, SKINNY_DIALTONE); 
            memset(l->call_forward, 0, sizeof(l->call_forward));
            getforward = 0;
            memset(exten, 0, sizeof(exten));
            len = 0;
        } else if (!strcmp(exten, ast_parking_ext()) && 
                    sub->next->owner &&
                    sub->next->owner->bridge) {
            /* This is a three way call, the main call being a real channel, 
                and we're parking the first call. */
            ast_masq_park_call(sub->next->owner->bridge, chan, 0, NULL);
            if (option_verbose > 2) {
                ast_verbose(VERBOSE_PREFIX_3 "Parking call to '%s'\n", chan->name);
            }
            break;
        } else if (!ast_strlen_zero(l->lastcallerid) && !strcmp(exten, "*60")) {
            if (option_verbose > 2) {
                ast_verbose(VERBOSE_PREFIX_3 "Blacklisting number %s\n", l->lastcallerid);
            }
            res = ast_db_put("blacklist", l->lastcallerid, "1");
            if (!res) {
                transmit_tone(s, SKINNY_DIALTONE);		
                memset(exten, 0, sizeof(exten));
                len = 0;
            }
        } else if (l->hidecallerid && !strcmp(exten, "*82")) {
            if (option_verbose > 2) {
                ast_verbose(VERBOSE_PREFIX_3 "Enabling Caller*ID on %s\n", chan->name);
            }
            /* Enable Caller*ID if enabled */
            l->hidecallerid = 0;
            if (chan->cid.cid_num)
                free(chan->cid.cid_num);
            if (!ast_strlen_zero(l->cid_num))
                chan->cid.cid_num = strdup(l->cid_num);

            if (chan->cid.cid_name)
                free(chan->cid.cid_name);
            if (!ast_strlen_zero(l->cid_name))
                chan->cid.cid_name = strdup(l->cid_name);

            transmit_tone(s, SKINNY_DIALTONE);
            len = 0;
            memset(exten, 0, sizeof(exten));
            timeout = firstdigittimeout;
        } else if (!ast_canmatch_extension(chan, chan->context, exten, 1, chan->cid.cid_num) &&
                        ((exten[0] != '*') || (!ast_strlen_zero(exten) > 2))) {
            ast_log(LOG_WARNING, "Can't match [%s] from '%s' in context %s\n", exten, chan->cid.cid_num ? chan->cid.cid_num : "<Unknown Caller>", chan->context);
            transmit_tone(s, SKINNY_REORDER); 
			sleep(3); // hang out for 3 seconds to let congestion play
			break;
        }
        if (!timeout)
            timeout = gendigittimeout;
        if (len && !ast_ignore_pattern(chan->context, exten))
			ast_indicate(chan, -1);
    }
	ast_hangup(chan);
	return NULL;
}



static int skinny_call(struct ast_channel *ast, char *dest, int timeout)
{
	int res = 0;
	int tone = 0;
	struct skinny_line *l;
        struct skinny_subchannel *sub;
	struct skinnysession *session;
	
	sub = ast->pvt->pvt;
        l = sub->parent;
	session = l->parent->session;

	if (!l->parent->registered) {
		ast_log(LOG_ERROR, "Device not registered, cannot call %s\n", dest);
		return -1;
	}
	
	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "skinny_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}

        if (skinnydebug) {
        	ast_verbose(VERBOSE_PREFIX_3 "skinny_call(%s)\n", ast->name);
    	}

	if (l->dnd) {
		ast_queue_control(ast, AST_CONTROL_BUSY);
		return -1;
	}
   
	switch (l->hookstate) {
        case SKINNY_OFFHOOK:
            tone = SKINNY_CALLWAITTONE;
            break;
        case SKINNY_ONHOOK:
			tone = SKINNY_ALERT;
			break;
        default:
            ast_log(LOG_ERROR, "Don't know how to deal with hookstate %d\n", l->hookstate);
            break;
    	}

	transmit_lamp_indication(session, l->instance, SKINNY_LAMP_BLINK);
	transmit_ringer_mode(session, SKINNY_RING_INSIDE);
	transmit_tone(session, tone);
	transmit_callstate(session, l->instance, SKINNY_RINGIN, sub->callid);

	/* XXX need to set the prompt */
	/* XXX need to deal with softkeys */

	ast_setstate(ast, AST_STATE_RINGING);
	ast_queue_control(ast, AST_CONTROL_RINGING);

	sub->outgoing = 1;
	if (l->type == TYPE_LINE) {
        if (!sub->rtp) {
            start_rtp(sub);
        } else {
		/* do/should we need to anything if there already is an RTP allocated? */
        }

	} else {
		ast_log(LOG_ERROR, "I don't know how to dial on trunks, yet\n");
		res = -1;
	}
	return res;
}


static int skinny_hangup(struct ast_channel *ast)
{
    struct skinny_subchannel *sub = ast->pvt->pvt;
    struct skinny_line *l = sub->parent;
    struct skinnysession *s = l->parent->session;

    if (skinnydebug) {
        ast_verbose("skinny_hangup(%s) on %s@%s\n", ast->name, l->name, l->parent->name);
    }
    if (!ast->pvt->pvt) {
        ast_log(LOG_DEBUG, "Asked to hangup channel not connected\n");
        return 0;
    }

    if (l->parent->registered) {
	if ((sub->parent->type = TYPE_LINE) && (sub->parent->hookstate == SKINNY_OFFHOOK)) {
			sub->parent->hookstate = SKINNY_ONHOOK;
			transmit_callstate(s, l->instance, SKINNY_ONHOOK, sub->callid);
			transmit_speaker_mode(s, SKINNY_SPEAKEROFF); 
		} else if ((sub->parent->type = TYPE_LINE) && (sub->parent->hookstate == SKINNY_ONHOOK)) {
			transmit_callstate(s, l->instance, SKINNY_ONHOOK, sub->callid);
			transmit_speaker_mode(s, SKINNY_SPEAKEROFF); 
			transmit_ringer_mode(s, SKINNY_RING_OFF);
			transmit_tone(s, SKINNY_SILENCE);
		} 
    }
    ast_mutex_lock(&sub->lock);
    sub->owner = NULL;
    ast->pvt->pvt = NULL;
    sub->alreadygone = 0;
    sub->outgoing = 0;
    if (sub->rtp) {
        ast_rtp_destroy(sub->rtp);
        sub->rtp = NULL;
    }
    ast_mutex_unlock(&sub->lock);
    return 0;
}

static int skinny_answer(struct ast_channel *ast)
{
    int res = 0;
    struct skinny_subchannel *sub = ast->pvt->pvt;
    struct skinny_line *l = sub->parent;
    sub->cxmode = SKINNY_CX_SENDRECV;
    if (!sub->rtp) {
		start_rtp(sub);
    } 
    ast_verbose("skinny_answer(%s) on %s@%s-%d\n", ast->name, l->name, l->parent->name, sub->callid);
    if (ast->_state != AST_STATE_UP) {
	ast_setstate(ast, AST_STATE_UP);
    }
    return res;
}

static struct ast_frame *skinny_rtp_read(struct skinny_subchannel *sub)
{
	/* Retrieve audio/etc from channel.  Assumes sub->lock is already held. */
	struct ast_frame *f;
	f = ast_rtp_read(sub->rtp);
	if (sub->owner) {
		/* We already hold the channel lock */
		if (f->frametype == AST_FRAME_VOICE) {
			if (f->subclass != sub->owner->nativeformats) {
				ast_log(LOG_DEBUG, "Oooh, format changed to %d\n", f->subclass);
				sub->owner->nativeformats = f->subclass;
				ast_set_read_format(sub->owner, sub->owner->readformat);
				ast_set_write_format(sub->owner, sub->owner->writeformat);
			}
		}
	}
	return f;
}

static struct ast_frame  *skinny_read(struct ast_channel *ast)
{
	struct ast_frame *fr;
	struct skinny_subchannel *sub = ast->pvt->pvt;
	ast_mutex_lock(&sub->lock);
	fr = skinny_rtp_read(sub);
	ast_mutex_unlock(&sub->lock);
	return fr;
}

static int skinny_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct skinny_subchannel *sub = ast->pvt->pvt;
	int res = 0;
	if (frame->frametype != AST_FRAME_VOICE) {
		if (frame->frametype == AST_FRAME_IMAGE)
			return 0;
		else {
			ast_log(LOG_WARNING, "Can't send %d type frames with skinny_write\n", frame->frametype);
			return 0;
		}
	} else {
		if (!(frame->subclass & ast->nativeformats)) {
			ast_log(LOG_WARNING, "Asked to transmit frame type %d, while native formats is %d (read/write = %d/%d)\n",
				frame->subclass, ast->nativeformats, ast->readformat, ast->writeformat);
			return -1;
		}
	}
	if (sub) {
		ast_mutex_lock(&sub->lock);
		if (sub->rtp) {
			res =  ast_rtp_write(sub->rtp, frame);
		}
		ast_mutex_unlock(&sub->lock);
	}
	return res;
}

static int skinny_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct skinny_subchannel *sub = newchan->pvt->pvt;
    	ast_log(LOG_NOTICE, "skinny_fixup(%s, %s)\n", oldchan->name, newchan->name);
	if (sub->owner != oldchan) {
		ast_log(LOG_WARNING, "old channel wasn't %p but was %p\n", oldchan, sub->owner);
		return -1;
	}
	sub->owner = newchan;
	return 0;
}

static int skinny_senddigit(struct ast_channel *ast, char digit)
{
#if 0
	struct skinny_subchannel *sub = ast->pvt->pvt;
	int tmp;
	/* not right */
	sprintf(tmp, "%d", digit);  
	transmit_tone(sub->parent->parent->session, digit);
#endif
	return -1;
}

static char *control2str(int ind) {
    switch (ind) {
        case AST_CONTROL_HANGUP:
            return "Other end has hungup";
        case AST_CONTROL_RING:
            return "Local ring";
        case AST_CONTROL_RINGING:
            return "Remote end is ringing";
        case AST_CONTROL_ANSWER:
            return "Remote end has answered";
        case AST_CONTROL_BUSY:
            return "Remote end is busy";
        case AST_CONTROL_TAKEOFFHOOK:
            return "Make it go off hook";
        case AST_CONTROL_OFFHOOK:
            return "Line is off hook";
        case AST_CONTROL_CONGESTION:
            return "Congestion (circuits busy)";
        case AST_CONTROL_FLASH:
            return "Flash hook";
        case AST_CONTROL_WINK:
            return "Wink";
        case AST_CONTROL_OPTION:
            return "Set a low-level option";
        case AST_CONTROL_RADIO_KEY:
            return "Key Radio";
        case AST_CONTROL_RADIO_UNKEY:
            return "Un-Key Radio";
		case -1:
			return "Stop tone";
    }
    return "UNKNOWN";
}


static int skinny_indicate(struct ast_channel *ast, int ind)
{
	struct skinny_subchannel *sub = ast->pvt->pvt;
	struct skinny_line *l = sub->parent;
	struct skinnysession *s = l->parent->session;

    	if (skinnydebug) {
        	ast_verbose(VERBOSE_PREFIX_3 "Asked to indicate '%s' condition on channel %s\n", control2str(ind), ast->name);
    	}
	switch(ind) {
	case AST_CONTROL_RINGING:
		if (ast->_state == AST_STATE_RINGING) {
			if (!sub->progress) {		
				transmit_tone(s, SKINNY_ALERT);
				transmit_callstate(s, l->instance, SKINNY_RINGOUT, sub->callid);
				sub->ringing = 1;
				break;
			}
		}
		return -1;
	case AST_CONTROL_BUSY:
		if (ast->_state != AST_STATE_UP) {		
			transmit_tone(s, SKINNY_BUSYTONE);
			transmit_callstate(s, l->instance, SKINNY_BUSY, sub->callid);
			sub->alreadygone = 1;
			ast_softhangup_nolock(ast, AST_SOFTHANGUP_DEV);
                        break;
                }
                return -1;
	case AST_CONTROL_CONGESTION:
		if (ast->_state != AST_STATE_UP) {		
			transmit_tone(s, SKINNY_REORDER);
			transmit_callstate(s, l->instance, SKINNY_CONGESTION, sub->callid);
			sub->alreadygone = 1;
                        ast_softhangup_nolock(ast, AST_SOFTHANGUP_DEV);
                        break;
                }
                return -1;
	case AST_CONTROL_PROGRESS:
                if ((ast->_state != AST_STATE_UP) && !sub->progress && !sub->outgoing) {
			transmit_callstate(s, l->instance, SKINNY_PROGRESS, sub->callid);
                        sub->progress = 1;
                        break;
                }
                return -1;  
	case -1:
		transmit_tone(s, SKINNY_SILENCE);
		break;		
	case AST_CONTROL_PROCEEDING:
		break;
	default:
		ast_log(LOG_WARNING, "Don't know how to indicate condition %d\n", ind);
		return -1;
	}
	return 0;
}
	
static struct ast_channel *skinny_new(struct skinny_subchannel *sub, int state)
{
	struct ast_channel *tmp;
	struct skinny_line *l = sub->parent;
	int fmt;
	l = sub->parent;
	tmp = ast_channel_alloc(1);
	if (tmp) {
		tmp->nativeformats = l->capability;
		if (!tmp->nativeformats)
			tmp->nativeformats = capability;
		fmt = ast_best_codec(tmp->nativeformats);
		snprintf(tmp->name, sizeof(tmp->name), "Skinny/%s@%s-%d", l->name, l->parent->name, sub->callid);
		if (sub->rtp)
			tmp->fds[0] = ast_rtp_fd(sub->rtp);
		tmp->type = type;
		ast_setstate(tmp, state);
		if (state == AST_STATE_RING)
			tmp->rings = 1;
		tmp->writeformat = fmt;
		tmp->pvt->rawwriteformat = fmt;
		tmp->readformat = fmt;
		tmp->pvt->rawreadformat = fmt;
		tmp->pvt->pvt = sub;
		tmp->pvt->call = skinny_call;
		tmp->pvt->hangup = skinny_hangup;
		tmp->pvt->answer = skinny_answer;
		tmp->pvt->read = skinny_read;
		tmp->pvt->write = skinny_write;
		tmp->pvt->indicate = skinny_indicate;
		tmp->pvt->fixup = skinny_fixup;
		tmp->pvt->send_digit = skinny_senddigit;
/*		tmp->pvt->bridge = ast_rtp_bridge; */
		if (!ast_strlen_zero(l->language))
			strncpy(tmp->language, l->language, sizeof(tmp->language)-1);
		if (!ast_strlen_zero(l->accountcode))
			strncpy(tmp->accountcode, l->accountcode, sizeof(tmp->accountcode)-1);
		if (l->amaflags)
			tmp->amaflags = l->amaflags;
		sub->owner = tmp;
		ast_mutex_lock(&usecnt_lock);
		usecnt++;
		ast_mutex_unlock(&usecnt_lock);
		ast_update_use_count();
		tmp->callgroup = l->callgroup;
		tmp->pickupgroup = l->pickupgroup;
		strncpy(tmp->call_forward, l->call_forward, sizeof(tmp->call_forward) - 1);
		strncpy(tmp->context, l->context, sizeof(tmp->context)-1);
		strncpy(tmp->exten,l->exten, sizeof(tmp->exten)-1);
		if (!ast_strlen_zero(l->cid_num)) {
			tmp->cid.cid_num = strdup(l->cid_num);
		}
		if (!ast_strlen_zero(l->cid_name)) {
			tmp->cid.cid_name = strdup(l->cid_name);
		}
		tmp->priority = 1;
		if (state != AST_STATE_DOWN) {
			if (ast_pbx_start(tmp)) {
				ast_log(LOG_WARNING, "Unable to start PBX on %s\n", tmp->name);
				ast_hangup(tmp);
				tmp = NULL;
			}
		}
	} else {
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
    }

	return tmp;
}

static int handle_message(skinny_req *req, struct skinnysession *s)
{
	struct skinny_subchannel *sub;
	struct ast_channel *c;
	struct ast_frame f = { 0, };	
	struct sockaddr_in sin;
	struct sockaddr_in us;
	struct skinny_line *lines;
	char name[16];
	char addr[4];
	char d;
	int digit;
	int res=0;
	int speedDialNum;
	int lineNumber;
	int stimulus;
	int stimulusInstance;
	int status;
	int port;
	int i;
	time_t timer;
	struct tm *cmtime;
	pthread_t t;
	
	if ( (!s->device) && (req->e != REGISTER_MESSAGE && req->e != ALARM_MESSAGE)) {
		ast_log(LOG_WARNING, "Client sent message #%d without first registering.\n", req->e);
		free(req);
		return 0;
	}
		
	switch(req->e)	{
	case ALARM_MESSAGE:
		/* no response necessary */
		break;
	case REGISTER_MESSAGE:
		if (skinnydebug) {
			ast_verbose("Device %s is attempting to register\n", req->data.reg.name);
		}
		res = skinny_register(req, s);	
		if (!res) {
			ast_log(LOG_ERROR, "Rejecting Device %s: Device not found\n", req->data.reg.name);
			memcpy(&name, req->data.reg.name, sizeof(req->data.reg.name));
			memset(req, 0, sizeof(skinny_req));
			req->len = sizeof(register_rej_message)+4;
			req->e = REGISTER_REJ_MESSAGE;
			snprintf(req->data.regrej.errMsg, sizeof(req->data.regrej.errMsg), "No Authority: %s", name);
			transmit_response(s, req);
			break;
		}
		if (option_verbose > 2) {
			ast_verbose(VERBOSE_PREFIX_3 "Device '%s' successfuly registered\n", s->device->name); 
		}
		memset(req, 0, SKINNY_MAX_PACKET);
		req->len = sizeof(register_ack_message)+4;
		req->e = REGISTER_ACK_MESSAGE;
		req->data.regack.res[0] = '0';
		req->data.regack.res[1] = '\0';
		req->data.regack.keepAlive = keep_alive;
		strncpy(req->data.regack.dateTemplate, date_format, sizeof(req->data.regack.dateTemplate) - 1);	
		req->data.regack.res2[0] = '0';
		req->data.regack.res2[1] = '\0';
		req->data.regack.secondaryKeepAlive = keep_alive;
		transmit_response(s, req);
		if (skinnydebug) {
			ast_verbose("Requesting capabilities\n");
		}
		memset(req, 0, SKINNY_MAX_PACKET);
		req->len = 4;
		req->e = CAPABILITIES_REQ_MESSAGE;
		transmit_response(s, req);
		break;
	case UNREGISTER_MESSAGE:
		/* XXX Acutally unregister the device */
		break;
	case IP_PORT_MESSAGE:
		/* no response necessary */
		break;
	case STIMULUS_MESSAGE:
		stimulus = req->data.stimulus.stimulus;
		stimulusInstance = req->data.stimulus.stimulusInstance;
		
		switch(stimulus) {
		case STIMULUS_REDIAL:
			/* XXX how we gonna deal with redial ?!?! */
			if (skinnydebug) {
				ast_verbose("Recieved Stimulus: Redial(%d)\n", stimulusInstance);
			}
			break;
		case STIMULUS_SPEEDDIAL:
			if (skinnydebug) {
				ast_verbose("Recieved Stimulus: SpeedDial(%d)\n", stimulusInstance);
			}
			break;
		case STIMULUS_HOLD:
			/* start moh? set RTP to 0.0.0.0? */
			if (skinnydebug) {
				ast_verbose("Recieved Stimulus: Hold(%d)\n", stimulusInstance);
			}
			break;
		case STIMULUS_TRANSFER:
			if (skinnydebug) {
				ast_verbose("Recieved Stimulus: Transfer(%d)\n", stimulusInstance);
			}
			transmit_tone(s, SKINNY_DIALTONE);
				
			/* figure out how to transfer */

			break;
		case STIMULUS_FORWARDALL:
		case STIMULUS_FORWARDBUSY:
		case STIMULUS_FORWARDNOANSWER:
			/* Gonna be fun, not */
			if (skinnydebug) {
				ast_verbose("Recieved Stimulus: Forward (%d)\n", stimulusInstance);
			}
			break;
		case STIMULUS_DISPLAY:
			/* Not sure what this is */
			if (skinnydebug) {
				ast_verbose("Recieved Stimulus: Display(%d)\n", stimulusInstance);
			}
			break;
		case STIMULUS_LINE:
			if (skinnydebug) {
				ast_verbose("Recieved Stimulus: Line(%d)\n", stimulusInstance);
			}		
			sub = find_subchannel_by_line(s->device->lines);
			/* turn the speaker on */
			transmit_speaker_mode(s, 1);  
		break;
		default:
			ast_verbose("RECEIVED UNKNOWN STIMULUS:  %d(%d)\n", stimulus, stimulusInstance);			
			break;
		}
		break;
	case VERSION_REQ_MESSAGE:
		if (skinnydebug) {
			ast_verbose("Version Request\n");
		}
		memset(req, 0, SKINNY_MAX_PACKET);
		req->len = sizeof(version_res_message)+4;
		req->e = VERSION_RES_MESSAGE;
		snprintf(req->data.version.version, sizeof(req->data.version.version), s->device->version_id);
		transmit_response(s, req);
		break;
	case SERVER_REQUEST_MESSAGE:
		if (skinnydebug) {
			ast_verbose("Recieved Server Request\n");
		}
		memset(req, 0, SKINNY_MAX_PACKET);
		req->len = sizeof(server_res_message)+4;
		req->e = SERVER_RES_MESSAGE;
		memcpy(req->data.serverres.server[0].serverName, ourhost, 
				sizeof(req->data.serverres.server[0].serverName));
		req->data.serverres.serverListenPort[0] = ourport;
		req->data.serverres.serverIpAddr[0] = __ourip.s_addr;
		transmit_response(s, req);	
		break;
	case BUTTON_TEMPLATE_REQ_MESSAGE:
		if (skinnydebug) {
			ast_verbose("Buttontemplate requested\n");
		}
		memset(req, 0, SKINNY_MAX_PACKET);
		req->len = sizeof(button_template_res_message)+4;
		req->e = BUTTON_TEMPLATE_RES_MESSAGE;	
		req->data.buttontemplate.buttonOffset = 0;
		req->data.buttontemplate.buttonCount  = 10;
		req->data.buttontemplate.totalButtonCount = 10;
		/* XXX Figure out how to do this correctly */
		memcpy(req->data.buttontemplate.definition,
			   button_definition_hack, 
			   sizeof(req->data.buttontemplate.definition));
		transmit_response(s, req);
		break;
	case SOFT_KEY_SET_REQ_MESSAGE:
		if (skinnydebug)  {
			ast_verbose("Received SoftKeySetReq\n");
		}
		memset(req, 0, SKINNY_MAX_PACKET);
		req->len = sizeof(soft_key_sets)+4;
		req->e = SOFT_KEY_SET_RES_MESSAGE;
		req->data.softkeysets.softKeySetOffset		= 0;
		req->data.softkeysets.softKeySetCount		= 11;
		req->data.softkeysets.totalSoftKeySetCount  = 11;	
		/* XXX Wicked hack XXX */
		memcpy(req->data.softkeysets.softKeySetDefinition, 
			   soft_key_set_hack, 
			   sizeof(req->data.softkeysets.softKeySetDefinition));
		transmit_response(s,req);
		break;
	case SOFT_KEY_TEMPLATE_REQ_MESSAGE:
		if (skinnydebug) {
			ast_verbose("Recieved SoftKey Template Request\n");
		}
		memset(req, 0, SKINNY_MAX_PACKET);
		req->len = sizeof(soft_key_template)+4;
		req->e = SOFT_KEY_TEMPLATE_RES_MESSAGE;
		req->data.softkeytemplate.softKeyOffset		= 0;
		req->data.softkeytemplate.softKeyCount		= 21;
		req->data.softkeytemplate.totalSoftKeyCount = 21; 
		/* XXX Another wicked hack XXX */
		memcpy(req->data.softkeytemplate.softKeyTemplateDefinition,
			   soft_key_template_hack,
			   sizeof(req->data.softkeytemplate.softKeyTemplateDefinition));
		transmit_response(s,req);
		break;
	case TIME_DATE_REQ_MESSAGE:
		if (skinnydebug) {
			ast_verbose("Received Time/Date Request\n");
		}
		memset(req, 0, SKINNY_MAX_PACKET);
		req->len = sizeof(definetimedate_message)+4;
		req->e = DEFINETIMEDATE_MESSAGE;
		timer=time(NULL);
		cmtime = localtime(&timer);
		req->data.definetimedate.year = cmtime->tm_year+1900;
		req->data.definetimedate.month = cmtime->tm_mon+1;
		req->data.definetimedate.dayofweek = cmtime->tm_wday;
		req->data.definetimedate.day = cmtime->tm_mday;
		req->data.definetimedate.hour = cmtime->tm_hour;
		req->data.definetimedate.minute = cmtime->tm_min;
		req->data.definetimedate.seconds = cmtime->tm_sec;
		transmit_response(s, req);
		break;
	case SPEED_DIAL_STAT_REQ_MESSAGE:
		/* Not really sure how Speed Dial's are different than the 
		   Softkey templates */
		speedDialNum = req->data.speeddialreq.speedDialNumber;
		memset(req, 0, SKINNY_MAX_PACKET);
		req->len = sizeof(speed_dial_stat_res_message)+4;
		req->e = SPEED_DIAL_STAT_RES_MESSAGE;
#if 0	
		/* XXX Do this right XXX */	
		req->data.speeddialreq.speedDialNumber = speedDialNum;
		snprintf(req->data.speeddial.speedDialDirNumber, sizeof(req->data.speeddial.speedDialDirNumber), "31337");
		snprintf(req->data.speeddial.speedDialDisplayName,  sizeof(req->data.speeddial.speedDialDisplayName),"Asterisk Rules!");
#endif		
		transmit_response(s, req);
		break;
	case LINE_STATE_REQ_MESSAGE:
		lineNumber = req->data.line.lineNumber;
		if (skinnydebug) {
			ast_verbose("Received LineStateReq\n");
		}
		memset(req, 0, SKINNY_MAX_PACKET);
		req->len = sizeof(line_stat_res_message)+4;
		req->e = LINE_STAT_RES_MESSAGE;	
		sub = find_subchannel_by_line(s->device->lines);
		if (!sub) {
			ast_log(LOG_NOTICE, "No available lines on: %s\n", s->device->name);
			return 0;
		}
		lines = sub->parent;
		ast_mutex_lock(&devicelock);
		for (i=1; i < lineNumber; i++) {
			lines = lines->next;
		}
		ast_mutex_unlock(&devicelock);
		req->data.linestat.linenumber = lineNumber;		
		memcpy(req->data.linestat.lineDirNumber, lines->name,
				sizeof(req->data.linestat.lineDirNumber));
		memcpy(req->data.linestat.lineDisplayName, lines->label,
				sizeof(req->data.linestat.lineDisplayName)); 
		transmit_response(s,req);
		break;
	case CAPABILITIES_RES_MESSAGE:
		if (skinnydebug) {
			ast_verbose("Received CapabilitiesRes\n");	
		}
		/* XXX process the capabilites  */
		break;
	case KEEP_ALIVE_MESSAGE:
		memset(req, 0, SKINNY_MAX_PACKET);
		req->len = 4;
		req->e = KEEP_ALIVE_ACK_MESSAGE;
		transmit_response(s, req);
		break;
	case OFFHOOK_MESSAGE:
		transmit_ringer_mode(s,SKINNY_RING_OFF);
		transmit_lamp_indication(s, s->device->lines->instance, SKINNY_LAMP_ON); 
		
		sub = find_subchannel_by_line(s->device->lines);
		if (!sub) {
			ast_log(LOG_NOTICE, "No available lines on: %s\n", s->device->name);
			return 0;
		}
		sub->parent->hookstate = SKINNY_OFFHOOK;
		
		if (sub->outgoing) {
			transmit_callstate(s, s->device->lines->instance, SKINNY_OFFHOOK, sub->callid);
			transmit_tone(s, SKINNY_SILENCE);
			ast_setstate(sub->owner, AST_STATE_UP);
			/* XXX select the appropriate soft key here */
			} else { 	
				if (!sub->owner) {	
					transmit_callstate(s, s->device->lines->instance, SKINNY_OFFHOOK, sub->callid);
					transmit_tone(s, SKINNY_DIALTONE);
					c = skinny_new(sub, AST_STATE_DOWN);			
					if(c) {
						/* start switch */
						if (ast_pthread_create(&t, NULL, skinny_ss, c)) {
							ast_log(LOG_WARNING, "Unable to create switch thread: %s\n", strerror(errno));
							ast_hangup(c);
						}
					} else {
						ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", sub->parent->name, s->device->name);
					}
				
				} else {
					ast_log(LOG_DEBUG, "Current sub [%s] already has owner\n", sub->owner->name);
				}
		}
		break;
	case ONHOOK_MESSAGE:
		sub = find_subchannel_by_line(s->device->lines);
		if (sub->parent->hookstate == SKINNY_ONHOOK) {
			/* Somthing else already put us back on hook */ 
			break;
		}
		sub->cxmode = SKINNY_CX_RECVONLY;
		sub->parent->hookstate = SKINNY_ONHOOK;
		transmit_callstate(s, s->device->lines->instance, sub->parent->hookstate,sub->callid);
	    if (skinnydebug) {
			ast_verbose("Skinny %s@%s went on hook\n",sub->parent->name, sub->parent->parent->name);
	    }
            if (sub->parent->transfer && (sub->owner && sub->next->owner) && ((!sub->outgoing) || (!sub->next->outgoing))) {
			/* We're allowed to transfer, we have two active calls and */
			/* we made at least one of the calls.  Let's try and transfer */
#if 0
             if ((res = attempt_transfer(p)) < 0) {
				 if (p->sub->next->owner) {
					sub->next->alreadygone = 1;
					ast_queue_hangup(sub->next->owner,1);
				}
			 } else if (res) {
				    ast_log(LOG_WARNING, "Transfer attempt failed\n");
					return -1;
             }
#endif
       	} else {
           /* Hangup the current call */
           /* If there is another active call, skinny_hangup will ring the phone with the other call */
           if (sub->owner) {
               sub->alreadygone = 1;
               ast_queue_hangup(sub->owner);
           } else {
               ast_log(LOG_WARNING, "Skinny(%s@%s-%d) channel already destroyed\n", 
                           sub->parent->name, sub->parent->parent->name, sub->callid);
           }
       	}

       	if ((sub->parent->hookstate == SKINNY_ONHOOK) && (!sub->next->rtp)) {
	    if (has_voicemail(sub->parent)) {
	    	transmit_lamp_indication(s, s->device->lines->instance, SKINNY_LAMP_FLASH); 
            } else {
		transmit_lamp_indication(s, s->device->lines->instance, SKINNY_LAMP_OFF);
            }
       	}
	break;
	case KEYPAD_BUTTON_MESSAGE:
		digit = req->data.keypad.button;
		if (skinnydebug) {
			ast_verbose("Collected digit: [%d]\n", digit);
		}
		f.frametype = AST_FRAME_DTMF;
		if (digit == 14) {
			d = '*';
		} else if (digit == 15) {
			d = '#';
		} else if (digit >=0 && digit <= 9) {
			d = '0' + digit;
		} else {
			/* digit=10-13 (A,B,C,D ?), or
			 * digit is bad value
			 * 
			 * probably should not end up here, but set
			 * value for backward compatibility, and log
			 * a warning.
			 */
			d = '0' + digit;
			ast_log(LOG_WARNING, "Unsupported digit %d\n", digit);
		}
		f.subclass  = d;  
		f.src = "skinny";

		sub = find_subchannel_by_line(s->device->lines);		

		if (sub->owner) {
			/* XXX MUST queue this frame to all subs in threeway call if threeway call is active */
			ast_queue_frame(sub->owner, &f);
            		if (sub->next->owner) {
				ast_queue_frame(sub->next->owner, &f);
            		}
        	} else {
			ast_verbose("No owner: %s\n", s->device->lines->name);
		}
		break;
	case OPEN_RECIEVE_CHANNEL_ACK_MESSAGE:
		ast_verbose("Recieved Open Recieve Channel Ack\n");
		status = req->data.openrecievechannelack.status;
		if (status) {
			ast_log(LOG_ERROR, "Open Recieve Channel Failure\n");
			break;
		}
		memcpy(addr, req->data.openrecievechannelack.ipAddr, sizeof(addr));
		port = req->data.openrecievechannelack.port;
				
		sin.sin_family = AF_INET;
		/* I smell endian problems */
		memcpy(&sin.sin_addr, addr, sizeof(sin.sin_addr));  
		sin.sin_port = htons(port);
	
		sub = find_subchannel_by_line(s->device->lines);
		if (sub->rtp) {
			ast_rtp_set_peer(sub->rtp, &sin);
			ast_rtp_get_us(sub->rtp, &us);	
		} else {
			ast_log(LOG_ERROR, "No RTP structure, this is very bad\n");
			break;
		}
		memset(req, 0, SKINNY_MAX_PACKET);
        	req->len = sizeof(start_media_transmission_message)+4;
        	req->e = START_MEDIA_TRANSMISSION_MESSAGE;
        	req->data.startmedia.conferenceId = 0;
        	req->data.startmedia.passThruPartyId = 0;
        	memcpy(req->data.startmedia.remoteIp, &s->device->ourip, 4); /* Endian? */
	    	req->data.startmedia.remotePort = ntohs(us.sin_port);
        	req->data.startmedia.packetSize = 20;
        	req->data.startmedia.payloadType = convert_cap(s->device->lines->capability);
        	req->data.startmedia.qualifier.precedence = 127;
        	req->data.startmedia.qualifier.vad = 0;
        	req->data.startmedia.qualifier.packets = 0;
        	req->data.startmedia.qualifier.bitRate = 0;
        	transmit_response(s, req);
		break;	
	default:
		ast_verbose("RECEIVED UNKNOWN MESSAGE TYPE:  %x\n", req->e);
		break;
	}
	free(req);
	return 1;

}

static void destroy_session(struct skinnysession *s)
{
	struct skinnysession *cur, *prev = NULL;
	ast_mutex_lock(&sessionlock);
	cur = sessions;
	while(cur) {
		if (cur == s)
			break;
		prev = cur;
		cur = cur->next;
	}
	if (cur) {
		if (prev)
			prev->next = cur->next;
		else
			sessions = cur->next;
		if (s->fd > -1)
			close(s->fd);
		ast_mutex_destroy(&s->lock);
		free(s);
	} else
		ast_log(LOG_WARNING, "Trying to delete non-existant session %p?\n", s);
	ast_mutex_unlock(&sessionlock);
}

static int get_input(struct skinnysession *s)  
{  
	int res;  
	int dlen = 0;
	struct pollfd fds[1];  
 
 	fds[0].fd = s->fd;
	fds[0].events = POLLIN;
	res = poll(fds, 1, -1);
 
	if (res < 0) {
		ast_log(LOG_WARNING, "Select returned error: %s\n", strerror(errno));
 	} else if (res > 0) {
		memset(s->inbuf,0,sizeof(s->inbuf));
		res = read(s->fd, s->inbuf, 4);
		if (res != 4) {
			ast_log(LOG_WARNING, "Skinny Client sent less data than expected.\n");
			return -1;
		}
		dlen = *(int *)s->inbuf;
		if (dlen+8 > sizeof(s->inbuf))
			dlen = sizeof(s->inbuf) - 8;
		res = read(s->fd, s->inbuf+4, dlen+4);
		ast_mutex_unlock(&s->lock);
		if (res != (dlen+4)) {
			ast_log(LOG_WARNING, "Skinny Client sent less data than expected.\n");
			return -1;
		} 
	}  
	return res;  
}   

static skinny_req *skinny_req_parse(struct skinnysession *s)
{
	skinny_req *req;
	
	req = malloc(SKINNY_MAX_PACKET);
	if (!req) {
		ast_log(LOG_ERROR, "Unable to allocate skinny_request, this is bad\n");
		return NULL;
	}
	memset(req, 0, sizeof(skinny_req));
	/* +8 to account for reserved and length fields */
	memcpy(req, s->inbuf, *(int*)(s->inbuf)+8); 
	if (req->e < 0) {
		ast_log(LOG_ERROR, "Event Message is NULL from socket %d, This is bad\n", s->fd);
		free(req);
		return NULL;
	}
	return req;
}

static void *skinny_session(void *data)
{
	int res;
	skinny_req *req;
	struct skinnysession *s = data;
	char iabuf[INET_ADDRSTRLEN];
	
	ast_verbose(VERBOSE_PREFIX_3 "Starting Skinny session from %s\n",  ast_inet_ntoa(iabuf, sizeof(iabuf), s->sin.sin_addr));

	for (;;) {
		res = 0;
		res = get_input(s);
		if (res < 0)
			break;
		req = skinny_req_parse(s);
		if (!req) {
			return NULL;
		}
		res = handle_message(req, s);
		if (res < 0) {
			destroy_session(s);
			return NULL;
		} 
	}
	ast_log(LOG_NOTICE, "Skinny Session returned: %s\n", strerror(errno));
	destroy_session(s);
	return 0;
}

static void *accept_thread(void *ignore)
{
	int as;
	struct sockaddr_in sin;
	int sinlen;
	struct skinnysession *s;
	struct protoent *p;
	int arg = 1;
	pthread_attr_t attr;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	for (;;) {
		sinlen = sizeof(sin);
		as = accept(skinnysock, (struct sockaddr *)&sin, &sinlen);
		if (as < 0) {
			ast_log(LOG_NOTICE, "Accept returned -1: %s\n", strerror(errno));
			continue;
		}
		p = getprotobyname("tcp");
		if(p) {
			if( setsockopt(as, p->p_proto, TCP_NODELAY, (char *)&arg, sizeof(arg) ) < 0 ) {
				ast_log(LOG_WARNING, "Failed to set Skinny tcp connection to TCP_NODELAY mode: %s\n", strerror(errno));
			}
		}
		s = malloc(sizeof(struct skinnysession));
		if (!s) {
			ast_log(LOG_WARNING, "Failed to allocate Skinny session: %s\n", strerror(errno));
			continue;
		} 
		memset(s, 0, sizeof(struct skinnysession));
		memcpy(&s->sin, &sin, sizeof(sin));
		ast_mutex_init(&s->lock);
		s->fd = as;
		ast_mutex_lock(&sessionlock);
		s->next = sessions;
		sessions = s;
		ast_mutex_unlock(&sessionlock);
		
		if (ast_pthread_create(&tcp_thread, NULL, skinny_session, s)) {
			destroy_session(s);
		}
	}
	
	
	if (skinnydebug) {
		ast_verbose("killing accept thread\n");
	}
	close(as);
	return 0;
}

static void *do_monitor(void *data)
{
	int res;

	/* This thread monitors all the interfaces which are not yet in use
	   (and thus do not have a separate thread) indefinitely */
	/* From here on out, we die whenever asked */
	for(;;) {
		pthread_testcancel();
		/* Wait for sched or io */
		res = ast_sched_wait(sched);
		if ((res < 0) || (res > 1000)) {
			res = 1000;
		}
		res = ast_io_wait(io, res);
		ast_mutex_lock(&monlock);
		if (res >= 0) {
			ast_sched_runq(sched);
		}
		ast_mutex_unlock(&monlock);
	}
	/* Never reached */
	return NULL;
	
}

static int restart_monitor(void)
{
	/* If we're supposed to be stopped -- stay stopped */
	if (monitor_thread == AST_PTHREADT_STOP)
		return 0;
	if (ast_mutex_lock(&monlock)) {
		ast_log(LOG_WARNING, "Unable to lock monitor\n");
		return -1;
	}
	if (monitor_thread == pthread_self()) {
		ast_mutex_unlock(&monlock);
		ast_log(LOG_WARNING, "Cannot kill myself\n");
		return -1;
	}
	if (monitor_thread != AST_PTHREADT_NULL) {
		/* Wake up the thread */
		pthread_kill(monitor_thread, SIGURG);
	} else {
		/* Start a new monitor */
		if (ast_pthread_create(&monitor_thread, NULL, do_monitor, NULL) < 0) {
			ast_mutex_unlock(&monlock);
			ast_log(LOG_ERROR, "Unable to start monitor thread.\n");
			return -1;
		}
	}
	ast_mutex_unlock(&monlock);
	return 0;
}

static struct ast_channel *skinny_request(const char *type, int format, void *data)
{
	int oldformat;
	struct skinny_subchannel *sub;
	struct ast_channel *tmpc = NULL;
	char tmp[256];
	char *dest = data;

	oldformat = format;
	format &= capability;
	if (!format) {
		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%d'\n", format);
		return NULL;
	}
	
	strncpy(tmp, dest, sizeof(tmp) - 1);
	if (ast_strlen_zero(tmp)) {
		ast_log(LOG_NOTICE, "Skinny channels require a device\n");
		return NULL;
	}
	
	sub = find_subchannel_by_name(tmp);  
	if (!sub) {
		ast_log(LOG_NOTICE, "No available lines on: %s\n", dest);
		return NULL;
	}
	
   	if (option_verbose > 2) {
        	ast_verbose(VERBOSE_PREFIX_3 "skinny_request(%s)\n", tmp);
        	ast_verbose(VERBOSE_PREFIX_3 "Skinny cw: %d, dnd: %d, so: %d, sno: %d\n", 
                   	sub->parent->callwaiting, sub->parent->dnd, sub->owner ? 1 : 0, sub->next->owner ? 1: 0);
    	}
	tmpc = skinny_new(sub->owner ? sub->next : sub, AST_STATE_DOWN);
	if (!tmpc) {
		ast_log(LOG_WARNING, "Unable to make channel for '%s'\n", tmp);
	}
	restart_monitor();

	/* and finish */	
	return tmpc;
}

static int reload_config(void)
{
	int on=1;
	struct ast_config *cfg;
	struct ast_variable *v;
	int format;
	char *cat;
	char iabuf[INET_ADDRSTRLEN];
	struct skinny_device *d;
	int oldport = ntohs(bindaddr.sin_port);

#if 0		
	hp = ast_gethostbyname(ourhost, &ahp);
	if (!hp) {
		ast_log(LOG_WARNING, "Unable to get hostname, Skinny disabled\n");
		return 0;
	}
#endif
	cfg = ast_load(config);

	/* We *must* have a config file otherwise stop immediately */
	if (!cfg) {
		ast_log(LOG_NOTICE, "Unable to load config %s, Skinny disabled\n", config);
		return 0;
	}

	/* load the general section */
	memset(&bindaddr, 0, sizeof(bindaddr));
	v = ast_variable_browse(cfg, "general");
	while(v) {
		/* Create the interface list */
		if (!strcasecmp(v->name, "bindaddr")) {
			if (!(hp = ast_gethostbyname(v->value, &ahp))) {
				ast_log(LOG_WARNING, "Invalid address: %s\n", v->value);
			} else {
				memcpy(&bindaddr.sin_addr, hp->h_addr, sizeof(bindaddr.sin_addr));
			}
		} else if (!strcasecmp(v->name, "keepAlive")) {
			keep_alive = atoi(v->value);		
		} else if (!strcasecmp(v->name, "dateFormat")) {
			strncpy(date_format, v->value, sizeof(date_format) - 1);	
		} else if (!strcasecmp(v->name, "allow")) {
			format = ast_getformatbyname(v->value);
			if (format < 1) 
				ast_log(LOG_WARNING, "Cannot allow unknown format '%s'\n", v->value);
			else
				capability |= format;
		} else if (!strcasecmp(v->name, "disallow")) {
			format = ast_getformatbyname(v->value);
			if (format < 1) 
				ast_log(LOG_WARNING, "Cannot disallow unknown format '%s'\n", v->value);
			else
				capability &= ~format;
		} else if (!strcasecmp(v->name, "port")) {
			if (sscanf(v->value, "%i", &ourport) == 1) {
				bindaddr.sin_port = htons(ourport);
			} else {
				ast_log(LOG_WARNING, "Invalid port number '%s' at line %d of %s\n", v->value, v->lineno, config);
			}
		}
		v = v->next;
	}

	if (ntohl(bindaddr.sin_addr.s_addr)) {
		memcpy(&__ourip, &bindaddr.sin_addr, sizeof(__ourip));
	} else {
		hp = ast_gethostbyname(ourhost, &ahp);
		if (!hp) {
			ast_log(LOG_WARNING, "Unable to get our IP address, Skinny disabled\n");
			ast_destroy(cfg);
			return 0;
		}
		memcpy(&__ourip, hp->h_addr, sizeof(__ourip));
	}
	if (!ntohs(bindaddr.sin_port)) {
		bindaddr.sin_port = ntohs(DEFAULT_SKINNY_PORT);
	}
	bindaddr.sin_family = AF_INET;
	
	/* load the device sections */
	cat = ast_category_browse(cfg, NULL);
	while(cat) {
		if (strcasecmp(cat, "general")) {
			d = build_device(cat, ast_variable_browse(cfg, cat));
			if (d) {
				if (option_verbose > 2) {
					ast_verbose(VERBOSE_PREFIX_3 "Added device '%s'\n", d->name);
                }
				ast_mutex_lock(&devicelock);
				d->next = devices;
				devices = d;
				ast_mutex_unlock(&devicelock);
			}
		}
		cat = ast_category_browse(cfg, cat);
	}
	ast_mutex_lock(&netlock);
	if ((skinnysock > -1) && (ntohs(bindaddr.sin_port) != oldport)) {
		close(skinnysock);
		skinnysock = -1;
	}
	if (skinnysock < 0) {
		skinnysock = socket(AF_INET, SOCK_STREAM, 0);
		if(setsockopt(skinnysock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {
			ast_log(LOG_ERROR, "Set Socket Options failed: errno %d, %s", errno, strerror(errno));
			ast_destroy(cfg);
			return 0;
		}

		if (skinnysock < 0) {
			ast_log(LOG_WARNING, "Unable to create Skinny socket: %s\n", strerror(errno));
		} else {
			if (bind(skinnysock, (struct sockaddr *)&bindaddr, sizeof(bindaddr)) < 0) {
				ast_log(LOG_WARNING, "Failed to bind to %s:%d: %s\n",
						ast_inet_ntoa(iabuf, sizeof(iabuf), bindaddr.sin_addr), ntohs(bindaddr.sin_port),
							strerror(errno));
				close(skinnysock);
				skinnysock = -1;
				ast_destroy(cfg);
				return 0;
			} 

			if (listen(skinnysock,DEFAULT_SKINNY_BACKLOG)) {
					ast_log(LOG_WARNING, "Failed to start listening to %s:%d: %s\n",
						ast_inet_ntoa(iabuf, sizeof(iabuf), bindaddr.sin_addr), ntohs(bindaddr.sin_port),
							strerror(errno));
					close(skinnysock);
					skinnysock = -1;
					ast_destroy(cfg);
					return 0;
			}
		
			if (option_verbose > 1)
				ast_verbose(VERBOSE_PREFIX_2 "Skinny listening on %s:%d\n", 
					ast_inet_ntoa(iabuf, sizeof(iabuf), bindaddr.sin_addr), ntohs(bindaddr.sin_port));

			ast_pthread_create(&accept_t,NULL, accept_thread, NULL);
		}
	}
	ast_mutex_unlock(&netlock);

	/* and unload the configuration when were done */
	ast_destroy(cfg);

	return 0;
}

#if 0
void delete_devices(void)
{
	struct skinny_device *d, *dlast;
	struct skinny_line *l, *llast;
	struct skinny_subchannel *sub, *slast;
	
	ast_mutex_lock(&devicelock);
	
	/* Delete all devices */
	for (d=devices;d;) {
		
		/* Delete all lines for this device */
		for (l=d->lines;l;) {
			/* Delete all subchannels for this line */
			for (sub=l->sub;sub;) {
				slast = sub;
				sub = sub->next;
				ast_mutex_destroy(&slast->lock);
				free(slast);
			}
			llast = l;
			l = l->next;
			ast_mutex_destroy(&llast->lock);
			free(llast);
		}
		dlast = d;
		d = d->next;
		free(dlast);
	}
	devices=NULL;
	ast_mutex_unlock(&devicelock);
}
#endif

int reload(void)
{
#if 0
// XXX Causes Seg

	delete_devices();
	reload_config();
	restart_monitor();
#endif
	return 0;
}


int load_module()
{
	int res = 0;

	/* load and parse config */
	res = reload_config();
	
	/* Announce our presence to Asterisk */	
	if (!res) {
		/* Make sure we can register our skinny channel type */
		if (ast_channel_register(type, tdesc, capability, skinny_request)) {
			ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
			return -1;
		}
	}
	skinny_rtp.type = type;
	ast_rtp_proto_register(&skinny_rtp);
	ast_cli_register(&cli_show_lines);
	ast_cli_register(&cli_debug);
	ast_cli_register(&cli_no_debug);
	sched = sched_context_create();
	if (!sched) {
		ast_log(LOG_WARNING, "Unable to create schedule context\n");
	}
	io = io_context_create();
	if (!io) {
		ast_log(LOG_WARNING, "Unable to create I/O context\n");
	}
	/* And start the monitor for the first time */
	restart_monitor();
	return res;
}

int unload_module()
{
#if 0
	struct skinny_session *session, s;
	struct skinny_subchannel *sub;
	struct skinny_line *line = session;

	/* close all IP connections */
	if (!ast_mutex_lock(&devicelock)) {
		/* Terminate tcp listener thread */
		

	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}
	if (!ast_mutex_lock(&monlock)) {
		if (monitor_thread && (monitor_thread != AST_PTHREADT_STOP)) {
			pthread_cancel(monitor_thread);
			pthread_kill(monitor_thread, SIGURG);
			pthread_join(monitor_thread, NULL);
		}
		monitor_thread = AST_PTHREADT_STOP;
		ast_mutex_unlock(&monlock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}
	if (!ast_mutex_lock(&iflock)) {
		/* Destroy all the interfaces and free their memory */
		p = iflist;
		while(p) {
			pl = p;
			p = p->next;
			/* Free associated memory */
			ast_mutex_destroy(&pl->lock);
			free(pl);
		}
		iflist = NULL;
		ast_mutex_unlock(&iflock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}

        ast_rtp_proto_register(&skinny_rtp);
        ast_cli_register(&cli_show_lines);
        ast_cli_register(&cli_debug);
        ast_cli_register(&cli_no_debug);

	return 0;
#endif
	return -1;
}

int usecount()
{
	int res;
	ast_mutex_lock(&usecnt_lock);
	res = usecnt;
	ast_mutex_unlock(&usecnt_lock);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}

char *description()
{
	return desc;
}
