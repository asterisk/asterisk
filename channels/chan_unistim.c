/*
 * Asterisk -- An open source telephony toolkit.
 *
 * UNISTIM channel driver for asterisk
 *
 * Copyright (C) 2005 - 2007, Cedric Hans
 * 
 * Cedric Hans <cedric.hans@mlkj.net>
 *
 * Asterisk 1.4 patch by Peter Be
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

/*!
 * \file
 *
 * \brief chan_unistim channel driver for Asterisk
 * \author Cedric Hans <cedric.hans@mlkj.net>
 *
 * Unistim (Unified Networks IP Stimulus) channel driver
 * for Nortel i2002, i2004 and i2050
 *
 * \ingroup channel_drivers
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/stat.h>
#include <signal.h>

#if defined(__CYGWIN__)
/*
 * cygwin headers are partly inconsistent. struct iovec is defined in sys/uio.h
 * which is not included by default by sys/socket.h - in_pktinfo is defined in
 * w32api/ws2tcpip.h but this probably has compatibility problems with sys/socket.h
 * So for the time being we simply disable HAVE_PKTINFO when building under cygwin.
 *    This should be done in some common header, but for now this is the only file
 * using iovec and in_pktinfo so it suffices to apply the fix here.
 */
#ifdef HAVE_PKTINFO
#undef HAVE_PKTINFO
#endif
#endif /* __CYGWIN__ */

#include "asterisk/paths.h"	/* ast_config_AST_LOG_DIR used in (too ?) many places */
#include "asterisk/network.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/event.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/netsock.h"
#include "asterisk/acl.h"
#include "asterisk/callerid.h"
#include "asterisk/cli.h"
#include "asterisk/app.h"
#include "asterisk/musiconhold.h"
#include "asterisk/causes.h"
#include "asterisk/indications.h"

/*! Beware, G729 and G723 are not supported by asterisk, except with the proper licence */

#define DEFAULTCONTEXT	  "default"
#define DEFAULTCALLERID	 "Unknown"
#define DEFAULTCALLERNAME       " "
#define DEFAULTHEIGHT	 3
#define USTM_LOG_DIR	    "unistimHistory"

/*! Size of the transmit buffer */
#define MAX_BUF_SIZE	    64
/*! Number of slots for the transmit queue */
#define MAX_BUF_NUMBER	  50
/*! Try x times before removing the phone */
#define NB_MAX_RETRANSMIT       8
/*! Nb of milliseconds waited when no events are scheduled */
#define IDLE_WAIT	       1000
/*! Wait x milliseconds before resending a packet */
#define RETRANSMIT_TIMER	2000
/*! How often the mailbox is checked for new messages */
#define TIMER_MWI	       10000
/*! Not used */
#define DEFAULT_CODEC	   0x00
#define SIZE_PAGE	       4096
#define DEVICE_NAME_LEN	 16
#define AST_CONFIG_MAX_PATH     255
#define MAX_ENTRY_LOG	   30

#define SUB_REAL		0
#define SUB_THREEWAY	    1
#define MAX_SUBS		2

struct ast_format_cap *global_cap;

enum autoprovision {
	AUTOPROVISIONING_NO = 0,
	AUTOPROVISIONING_YES,
	AUTOPROVISIONING_DB,
	AUTOPROVISIONING_TN
};

enum autoprov_extn {
	/*! Do not create an extension into the default dialplan */
	EXTENSION_NONE = 0,
	/*! Prompt user for an extension number and register it */
	EXTENSION_ASK,
	/*! Register an extension with the line=> value */
	EXTENSION_LINE,
	/*! Used with AUTOPROVISIONING_TN */
	EXTENSION_TN
};
#define OUTPUT_HANDSET	  0xC0
#define OUTPUT_HEADPHONE	0xC1
#define OUTPUT_SPEAKER	  0xC2

#define VOLUME_LOW	      0x01
#define VOLUME_LOW_SPEAKER      0x03
#define VOLUME_NORMAL	   0x02
#define VOLUME_INSANELY_LOUD    0x07

#define MUTE_OFF		0x00
#define MUTE_ON		 0xFF
#define MUTE_ON_DISCRET	 0xCE

#define SIZE_HEADER	     6
#define SIZE_MAC_ADDR	   17
#define TEXT_LENGTH_MAX	 24
#define TEXT_LINE0	      0x00
#define TEXT_LINE1	      0x20
#define TEXT_LINE2	      0x40
#define TEXT_NORMAL	     0x05
#define TEXT_INVERSE	    0x25
#define STATUS_LENGTH_MAX       28

#define FAV_ICON_NONE		   0x00
#define FAV_ICON_ONHOOK_BLACK	   0x20
#define FAV_ICON_ONHOOK_WHITE	   0x21
#define FAV_ICON_SPEAKER_ONHOOK_BLACK   0x22
#define FAV_ICON_SPEAKER_ONHOOK_WHITE   0x23
#define FAV_ICON_OFFHOOK_BLACK	  0x24
#define FAV_ICON_OFFHOOK_WHITE	  0x25
#define FAV_ICON_ONHOLD_BLACK	   0x26
#define FAV_ICON_ONHOLD_WHITE	   0x27
#define FAV_ICON_SPEAKER_OFFHOOK_BLACK  0x28
#define FAV_ICON_SPEAKER_OFFHOOK_WHITE  0x29
#define FAV_ICON_PHONE_BLACK	    0x2A
#define FAV_ICON_PHONE_WHITE	    0x2B
#define FAV_ICON_SPEAKER_ONHOLD_BLACK   0x2C
#define FAV_ICON_SPEAKER_ONHOLD_WHITE   0x2D
#define FAV_ICON_HEADPHONES	     0x2E
#define FAV_ICON_HEADPHONES_ONHOLD      0x2F
#define FAV_ICON_HOME		   0x30
#define FAV_ICON_CITY		   0x31
#define FAV_ICON_SHARP		  0x32
#define FAV_ICON_PAGER		  0x33
#define FAV_ICON_CALL_CENTER	    0x34
#define FAV_ICON_FAX		    0x35
#define FAV_ICON_MAILBOX		0x36
#define FAV_ICON_REFLECT		0x37
#define FAV_ICON_COMPUTER	       0x38
#define FAV_ICON_FORWARD		0x39
#define FAV_ICON_LOCKED		 0x3A
#define FAV_ICON_TRASH		  0x3B
#define FAV_ICON_INBOX		  0x3C
#define FAV_ICON_OUTBOX		 0x3D
#define FAV_ICON_MEETING		0x3E
#define FAV_ICON_BOX		    0x3F

#define FAV_BLINK_FAST		  0x20
#define FAV_BLINK_SLOW		  0x40

#define FAV_MAX_LENGTH		  0x0A

static void dummy(char *unused, ...)
{
	return;
}

/*! \brief Global jitterbuffer configuration - by default, jb is disabled */
static struct ast_jb_conf default_jbconf =
{
	.flags = 0,
	.max_size = -1,
	.resync_threshold = -1,
	.impl = "",
	.target_extra = -1,
};
static struct ast_jb_conf global_jbconf;
				

/* #define DUMP_PACKET 1 */
/* #define DEBUG_TIMER ast_verbose */

#define DEBUG_TIMER dummy
/*! Enable verbose output. can also be set with the CLI */
static int unistimdebug = 0;
static int unistim_port;
static enum autoprovision autoprovisioning = AUTOPROVISIONING_NO;
static int unistim_keepalive;
static int unistimsock = -1;

static struct {
	unsigned int tos;
	unsigned int tos_audio;
	unsigned int cos;
	unsigned int cos_audio;
} qos = { 0, 0, 0, 0 };

static struct io_context *io;
static struct ast_sched_context *sched;
static struct sockaddr_in public_ip = { 0, };
/*! give the IP address for the last packet received */
static struct sockaddr_in address_from;
/*! size of the sockaddr_in (in WSARecvFrom) */
static unsigned int size_addr_from = sizeof(address_from);
/*! Receive buffer address */
static unsigned char *buff;
static int unistim_reloading = 0;
AST_MUTEX_DEFINE_STATIC(unistim_reload_lock);
AST_MUTEX_DEFINE_STATIC(usecnt_lock);
static int usecnt = 0;
/* extern char ast_config_AST_LOG_DIR[AST_CONFIG_MAX_PATH]; */

/*! This is the thread for the monitor which checks for input on the channels
 * which are not currently in use.  */
static pthread_t monitor_thread = AST_PTHREADT_NULL;

/*! Protect the monitoring thread, so only one process can kill or start it, and not
 *    when it's doing something critical. */
AST_MUTEX_DEFINE_STATIC(monlock);
/*! Protect the session list */
AST_MUTEX_DEFINE_STATIC(sessionlock);
/*! Protect the device list */
AST_MUTEX_DEFINE_STATIC(devicelock);

enum phone_state {
	STATE_INIT,
	STATE_AUTHDENY,
	STATE_MAINPAGE,
	STATE_EXTENSION,
	STATE_DIALPAGE,
	STATE_RINGING,
	STATE_CALL,
	STATE_SELECTCODEC,
	STATE_CLEANING,
	STATE_HISTORY
};

enum handset_state {
	STATE_ONHOOK,
	STATE_OFFHOOK,
};

enum phone_key {
	KEY_0 = 0x40,
	KEY_1 = 0x41,
	KEY_2 = 0x42,
	KEY_3 = 0x43,
	KEY_4 = 0x44,
	KEY_5 = 0x45,
	KEY_6 = 0x46,
	KEY_7 = 0x47,
	KEY_8 = 0x48,
	KEY_9 = 0x49,
	KEY_STAR = 0x4a,
	KEY_SHARP = 0x4b,
	KEY_UP = 0x4c,
	KEY_DOWN = 0x4d,
	KEY_RIGHT = 0x4e,
	KEY_LEFT = 0x4f,
	KEY_QUIT = 0x50,
	KEY_COPY = 0x51,
	KEY_FUNC1 = 0x54,
	KEY_FUNC2 = 0x55,
	KEY_FUNC3 = 0x56,
	KEY_FUNC4 = 0x57,
	KEY_ONHOLD = 0x5b,
	KEY_HANGUP = 0x5c,
	KEY_MUTE = 0x5d,
	KEY_HEADPHN = 0x5e,
	KEY_LOUDSPK = 0x5f,
	KEY_FAV0 = 0x60,
	KEY_FAV1 = 0x61,
	KEY_FAV2 = 0x62,
	KEY_FAV3 = 0x63,
	KEY_FAV4 = 0x64,
	KEY_FAV5 = 0x65,
	KEY_COMPUTR = 0x7b,
	KEY_CONF = 0x7c,
	KEY_SNDHIST = 0x7d,
	KEY_RCVHIST = 0x7e,
	KEY_INDEX = 0x7f
};

struct tone_zone_unistim {
	char country[3];
	int freq1;
	int freq2;
};

static const struct tone_zone_unistim frequency[] = {
	{"us", 350, 440},
	{"fr", 440, 0},
	{"au", 413, 438},
	{"nl", 425, 0},
	{"uk", 350, 440},
	{"fi", 425, 0},
	{"es", 425, 0},
	{"jp", 400, 0},
	{"no", 425, 0},
	{"at", 420, 0},
	{"nz", 400, 0},
	{"tw", 350, 440},
	{"cl", 400, 0},
	{"se", 425, 0},
	{"be", 425, 0},
	{"sg", 425, 0},
	{"il", 414, 0},
	{"br", 425, 0},
	{"hu", 425, 0},
	{"lt", 425, 0},
	{"pl", 425, 0},
	{"za", 400, 0},
	{"pt", 425, 0},
	{"ee", 425, 0},
	{"mx", 425, 0},
	{"in", 400, 0},
	{"de", 425, 0},
	{"ch", 425, 0},
	{"dk", 425, 0},
	{"cn", 450, 0},
	{"--", 0, 0}
};

struct wsabuf {
	u_long len;
	unsigned char *buf;
};

struct systemtime {
	unsigned short w_year;
	unsigned short w_month;
	unsigned short w_day_of_week;
	unsigned short w_day;
	unsigned short w_hour;
	unsigned short w_minute;
	unsigned short w_second;
	unsigned short w_milliseconds;
};

struct unistim_subchannel {
	ast_mutex_t lock;
	/*! SUBS_REAL or SUBS_THREEWAY */
	unsigned int subtype;
	/*! Asterisk channel used by the subchannel */
	struct ast_channel *owner;
	/*! Unistim line */
	struct unistim_line *parent;
	/*! RTP handle */
	struct ast_rtp_instance *rtp;
	int alreadygone;
	char ringvolume;
	char ringstyle;
};

/*!
 * \todo Convert to stringfields
 */
struct unistim_line {
	ast_mutex_t lock;
	/*! Like 200 */
	char name[80];
	/*! Like USTM/200\@black */
	char fullname[80];
	/*! pointer to our current connection, channel... */
	struct unistim_subchannel *subs[MAX_SUBS];
	/*! Extension where to start */
	char exten[AST_MAX_EXTENSION];
	/*! Context to start in */
	char context[AST_MAX_EXTENSION];
	/*! Language for asterisk sounds */
	char language[MAX_LANGUAGE];
	/*! CallerID Number */
	char cid_num[AST_MAX_EXTENSION];
	/*! Mailbox for MWI */
	char mailbox[AST_MAX_EXTENSION];
	/*! Used by MWI */
	int lastmsgssent;
	/*! Used by MWI */
	time_t nextmsgcheck;
	/*! MusicOnHold class */
	char musicclass[MAX_MUSICCLASS];
	/*! Call group */
	unsigned int callgroup;
	/*! Pickup group */
	unsigned int pickupgroup;
	/*! Account code (for billing) */
	char accountcode[80];
	/*! AMA flags (for billing) */
	int amaflags;
	/*! Codec supported */
	struct ast_format_cap *cap;
	/*! Parkinglot */
	char parkinglot[AST_MAX_CONTEXT];
	struct unistim_line *next;
	struct unistim_device *parent;
};

/*! 
 * \brief A device containing one or more lines 
 */
static struct unistim_device {
	int receiver_state;	      /*!< state of the receiver (see ReceiverState) */
	int size_phone_number;	  /*!< size of the phone number */
	char phone_number[16];	  /*!< the phone number entered by the user */
	char redial_number[16];	 /*!< the last phone number entered by the user */
	int phone_current;		      /*!< Number of the current phone */
	int pos_fav;			    /*!< Position of the displayed favorites (used for scrolling) */
	char id[18];			    /*!< mac address of the current phone in ascii */
	char name[DEVICE_NAME_LEN];     /*!< name of the device */
	int softkeylinepos;		     /*!< position of the line softkey (default 0) */
	char softkeylabel[6][11];       /*!< soft key label */
	char softkeynumber[6][16];      /*!< number dialed when the soft key is pressed */
	char softkeyicon[6];	    /*!< icon number */
	char softkeydevice[6][16];      /*!< name of the device monitored */
	struct unistim_device *sp[6];   /*!< pointer to the device monitored by this soft key */
	int height;							/*!< The number of lines the phone can display */
	char maintext0[25];		     /*!< when the phone is idle, display this string on line 0 */
	char maintext1[25];		     /*!< when the phone is idle, display this string on line 1 */
	char maintext2[25];		     /*!< when the phone is idle, display this string on line 2 */
	char titledefault[13];	  /*!< title (text before date/time) */
	char datetimeformat;	    /*!< format used for displaying time/date */
	char contrast;			  /*!< contrast */
	char country[3];			/*!< country used for dial tone frequency */
	struct ast_tone_zone *tz;	       /*!< Tone zone for res_indications (ring, busy, congestion) */
	char ringvolume;			/*!< Ring volume */
	char ringstyle;			 /*!< Ring melody */
	int rtp_port;			   /*!< RTP port used by the phone */
	int rtp_method;			 /*!< Select the unistim data used to establish a RTP session */
	int status_method;		      /*!< Select the unistim packet used for sending status text */
	char codec_number;		      /*!< The current codec used to make calls */
	int missed_call;			/*!< Number of call unanswered */
	int callhistory;			/*!< Allowed to record call history */
	char lst_cid[TEXT_LENGTH_MAX];  /*!< Last callerID received */
	char lst_cnm[TEXT_LENGTH_MAX];  /*!< Last callername recevied */
	char call_forward[AST_MAX_EXTENSION];   /*!< Forward number */
	int output;				     /*!< Handset, headphone or speaker */
	int previous_output;	    /*!< Previous output */
	int volume;				     /*!< Default volume */
	int mute;				       /*!< Mute mode */
	int moh;					/*!< Music on hold in progress */
	int nat;					/*!< Used by the obscure ast_rtp_setnat */
	enum autoprov_extn extension;   /*!< See ifdef EXTENSION for valid values */
	char extension_number[11];      /*!< Extension number entered by the user */
	char to_delete;			 /*!< Used in reload */
	time_t start_call_timestamp;    /*!< timestamp for the length calculation of the call */
	struct ast_silence_generator *silence_generator;
	struct unistim_line *lines;
	struct ast_ha *ha;
	struct unistimsession *session;
	struct unistim_device *next;
} *devices = NULL;

static struct unistimsession {
	ast_mutex_t lock;
	struct sockaddr_in sin;	 /*!< IP address of the phone */
	struct sockaddr_in sout;	/*!< IP address of server */
	int timeout;			    /*!< time-out in ticks : resend packet if no ack was received before the timeout occured */
	unsigned short seq_phone;       /*!< sequence number for the next packet (when we receive a request) */
	unsigned short seq_server;      /*!< sequence number for the next packet (when we send a request) */
	unsigned short last_seq_ack;    /*!< sequence number of the last ACK received */
	unsigned long tick_next_ping;   /*!< time for the next ping */
	int last_buf_available;	 /*!< number of a free slot */
	int nb_retransmit;		      /*!< number of retransmition */
	int state;				      /*!< state of the phone (see phone_state) */
	int size_buff_entry;	    /*!< size of the buffer used to enter datas */
	char buff_entry[16];	    /*!< Buffer for temporary datas */
	char macaddr[18];		       /*!< mac adress of the phone (not always available) */
	struct wsabuf wsabufsend[MAX_BUF_NUMBER];      /*!< Size of each paquet stored in the buffer array & pointer to this buffer */
	unsigned char buf[MAX_BUF_NUMBER][MAX_BUF_SIZE];	/*!< Buffer array used to keep the lastest non-acked paquets */
	struct unistim_device *device;
	struct unistimsession *next;
} *sessions = NULL;

/*!
 * \page Unistim datagram formats
 *
 * Format of datagrams :
 * bytes 0 & 1 : ffff for discovery packet, 0000 for everything else
 * byte 2 : sequence number (high part)
 * byte 3 : sequence number (low part)
 * byte 4 : 2 = ask question or send info, 1 = answer or ACK, 0 = retransmit request
 * byte 5 : direction, 1 = server to phone, 2 = phone to server arguments
 */

static const unsigned char packet_rcv_discovery[] =
	{ 0xff, 0xff, 0xff, 0xff, 0x02, 0x02, 0xff, 0xff, 0xff, 0xff, 0x9e, 0x03, 0x08 };
static const unsigned char packet_send_discovery_ack[] =
	{ 0x00, 0x00, /*Initial Seq (2 bytes) */ 0x00, 0x00, 0x00, 0x01 };

static const unsigned char packet_recv_firm_version[] =
	{ 0x00, 0x00, 0x00, 0x13, 0x9a, 0x0a, 0x02 };
static const unsigned char packet_recv_pressed_key[] =
	{ 0x00, 0x00, 0x00, 0x13, 0x99, 0x04, 0x00 };
static const unsigned char packet_recv_pick_up[] =
	{ 0x00, 0x00, 0x00, 0x13, 0x99, 0x03, 0x04 };
static const unsigned char packet_recv_hangup[] =
	{ 0x00, 0x00, 0x00, 0x13, 0x99, 0x03, 0x03 };
static const unsigned char packet_recv_r2[] = { 0x00, 0x00, 0x00, 0x13, 0x96, 0x03, 0x03 };

/*! TransportAdapter */
static const unsigned char packet_recv_resume_connection_with_server[] =
	{ 0xff, 0xff, 0xff, 0xff, 0x9e, 0x03, 0x08 };
static const unsigned char packet_recv_mac_addr[] =
	{ 0xff, 0xff, 0xff, 0xff, 0x9a, 0x0d, 0x07 /*MacAddr */  };

static const unsigned char packet_send_date_time3[] =
	{ 0x11, 0x09, 0x02, 0x02, /*Month */ 0x05, /*Day */ 0x06, /*Hour */ 0x07,
/*Minutes */ 0x08, 0x32
};
static const unsigned char packet_send_date_time[] =
	{ 0x11, 0x09, 0x02, 0x0a, /*Month */ 0x05, /*Day */ 0x06, /*Hour */ 0x07, /*Minutes */
0x08, 0x32, 0x17, 0x04, 0x24, 0x07, 0x19,
	0x04, 0x07, 0x00, 0x19, 0x05, 0x09, 0x3e, 0x0f, 0x16, 0x05, 0x00, 0x80, 0x00, 0x1e,
		0x05, 0x12, 0x00, 0x78
};

static const unsigned char packet_send_no_ring[] =
	{ 0x16, 0x04, 0x1a, 0x00, 0x16, 0x04, 0x11, 0x00 };
static const unsigned char packet_send_s4[] =
	{ 0x16, 0x04, 0x1a, 0x00, 0x16, 0x04, 0x11, 0x00, 0x16, 0x06, 0x32, 0xdf, 0x00, 0xff,
0x16, 0x05, 0x1c, 0x00, 0x00, 0x17, 0x05,
	0x0b, 0x00, 0x00, 0x19, 0x04, 0x00, 0x00, 0x19, 0x04, 0x00, 0x08, 0x19, 0x04, 0x00,
		0x10, 0x19, 0x04, 0x00, 0x18, 0x16, 0x05,
	0x31, 0x00, 0x00, 0x16, 0x05, 0x04, 0x00, 0x00
};
static const unsigned char packet_send_call[] =
	{ 0x16, 0x04, 0x1a, 0x00, 0x16, 0x04, 0x11, 0x00, 0x16, 0x06, 0x32, 0xdf,
	0x00, 0xff, 0x16, 0x05, 0x1c, 0x00, 0x00, 0x16, 0x0a, 0x38, 0x00, 0x12, 0xca, 0x03,
		0xc0, 0xc3, 0xc5, 0x16, 0x16, 0x30, 0x00,
	0x00, /*codec */ 0x12, 0x12, /* frames per packet */ 0x01, 0x5c, 0x00, /*port RTP */
		0x0f, 0xa0, /* port RTCP */ 0x9c, 0x41,
	/*port RTP */ 0x0f, 0xa0, /* port RTCP */ 0x9c, 0x41, /* IP Address */ 0x0a, 0x01,
		0x16, 0x66
};
static const unsigned char packet_send_stream_based_tone_off[] =
	{ 0x16, 0x05, 0x1c, 0x00, 0x00 };

/* static const unsigned char packet_send_Mute[] = { 0x16, 0x05, 0x04, 0x00, 0x00 };
static const unsigned char packet_send_CloseAudioStreamRX[] = { 0x16, 0x05, 0x31, 0x00, 0xff };
static const unsigned char packet_send_CloseAudioStreamTX[] = { 0x16, 0x05, 0x31, 0xff, 0x00 };*/
static const unsigned char packet_send_stream_based_tone_on[] =
	{ 0x16, 0x06, 0x1b, 0x00, 0x00, 0x05 };
static const unsigned char packet_send_stream_based_tone_single_freq[] =
	{ 0x16, 0x06, 0x1d, 0x00, 0x01, 0xb8 };
static const unsigned char packet_send_stream_based_tone_dial_freq[] =
	{ 0x16, 0x08, 0x1d, 0x00, 0x01, 0xb8, 0x01, 0x5e };
static const unsigned char packet_send_select_output[] =
	{ 0x16, 0x06, 0x32, 0xc0, 0x01, 0x00 };
static const unsigned char packet_send_ring[] =
	{ 0x16, 0x06, 0x32, 0xdf, 0x00, 0xff, 0x16, 0x05, 0x1c, 0x00, 0x00, 0x16,
	0x04, 0x1a, 0x01, 0x16, 0x05, 0x12, 0x13 /* Ring type 10 to 17 */ , 0x18, 0x16, 0x04, 0x18,     /* volume 00, 10, 20... */
	0x20, 0x16, 0x04, 0x10, 0x00
};
static const unsigned char packet_send_end_call[] =
	{ 0x16, 0x06, 0x32, 0xdf, 0x00, 0xff, 0x16, 0x05, 0x31, 0x00, 0x00, 0x19, 0x04, 0x00,
0x10, 0x19, 0x04, 0x00, 0x18, 0x16, 0x05,
	0x04, 0x00, 0x00, 0x16, 0x04, 0x37, 0x10
};
static const unsigned char packet_send_s9[] =
	{ 0x16, 0x06, 0x32, 0xdf, 0x00, 0xff, 0x19, 0x04, 0x00, 0x10, 0x16, 0x05, 0x1c, 0x00,
0x00 };
static const unsigned char packet_send_rtp_packet_size[] =
	{ 0x16, 0x08, 0x38, 0x00, 0x00, 0xe0, 0x00, 0xa0 };
static const unsigned char packet_send_jitter_buffer_conf[] =
	{ 0x16, 0x0e, 0x3a, 0x00, /* jitter */ 0x02, /* high water mark */ 0x04, 0x00, 0x00,
/* early packet resync 2 bytes */ 0x3e, 0x80,
	0x00, 0x00, /* late packet resync 2 bytes */ 0x3e, 0x80
};

/* Duration in ms div 2 (0x20 = 64ms, 0x08 = 16ms) 
static unsigned char packet_send_StreamBasedToneCad[] =
  { 0x16, 0x0a, 0x1e, 0x00, duration on  0x0a, duration off  0x0d, duration on 0x0a, duration off 0x0d, duration on 0x0a, duration off 0x2b }; */
static const unsigned char packet_send_open_audio_stream_rx[] =
	{ 0x16, 0x1a, 0x30, 0x00, 0xff, /* Codec */ 0x00, 0x00, 0x01, 0x00, 0xb8, 0xb8, 0x0e,
0x0e, 0x01, /* Port */ 0x14, 0x50, 0x00,
	0x00, /* Port */ 0x14, 0x50, 0x00, 0x00, /* Dest IP */ 0x0a, 0x93, 0x69, 0x05
};
static const unsigned char packet_send_open_audio_stream_tx[] =
	{ 0x16, 0x1a, 0x30, 0xff, 0x00, 0x00, /* Codec */ 0x00, 0x01, 0x00, 0xb8, 0xb8, 0x0e,
0x0e, 0x01, /* Local port */ 0x14, 0x50,
	0x00, 0x00, /* Rmt Port */ 0x14, 0x50, 0x00, 0x00, /* Dest IP */ 0x0a, 0x93, 0x69, 0x05
};

static const unsigned char packet_send_open_audio_stream_rx3[] =
	{ 0x16, 0x1a, 0x30, 0x00, 0xff, /* Codec */ 0x00, 0x00, 0x02, 0x01, 0xb8, 0xb8, 0x06,
0x06, 0x81, /* RTP Port */ 0x14, 0x50,
/* RTCP Port */ 0x14,
	0x51, /* RTP Port */ 0x14, 0x50, /* RTCP Port */ 0x00, 0x00, /* Dest IP */ 0x0a, 0x93,
		0x69, 0x05
};
static const unsigned char packet_send_open_audio_stream_tx3[] =
	{ 0x16, 0x1a, 0x30, 0xff, 0x00, 0x00, /* Codec */ 0x00, 0x02, 0x01, 0xb8, 0xb8, 0x06,
0x06, 0x81, /* RTP Local port */ 0x14, 0x50,
	/* RTCP Port */ 0x00, 0x00, /* RTP Rmt Port */ 0x14, 0x50, /* RTCP Port */ 0x00, 0x00,
		/* Dest IP */ 0x0a, 0x93, 0x69, 0x05
};

static const unsigned char packet_send_arrow[] = { 0x17, 0x04, 0x04, 0x00 };
static const unsigned char packet_send_blink_cursor[] = { 0x17, 0x04, 0x10, 0x86 };
static const unsigned char packet_send_date_time2[] = { 0x17, 0x04, 0x17, 0x3d, 0x11, 0x09, 0x02, 0x0a, /*Month */ 0x05,   /*Day */
	0x06, /*Hour */ 0x07, /*Minutes */ 0x08, 0x32
};
static const unsigned char packet_send_Contrast[] =
	{ 0x17, 0x04, 0x24, /*Contrast */ 0x08 };
static const unsigned char packet_send_StartTimer[] =
	{ 0x17, 0x05, 0x0b, 0x05, 0x00, 0x17, 0x08, 0x16, /* Text */ 0x44, 0x75, 0x72, 0xe9,
0x65 };
static const unsigned char packet_send_stop_timer[] = { 0x17, 0x05, 0x0b, 0x02, 0x00 };
static const unsigned char packet_send_icon[] = { 0x17, 0x05, 0x14, /*pos */ 0x00, /*icon */ 0x25 };      /* display an icon in front of the text zone */
static const unsigned char packet_send_S7[] = { 0x17, 0x06, 0x0f, 0x30, 0x07, 0x07 };
static const unsigned char packet_send_set_pos_cursor[] =
	{ 0x17, 0x06, 0x10, 0x81, 0x04, /*pos */ 0x20 };

/*static unsigned char packet_send_MonthLabelsDownload[] =
  { 0x17, 0x0a, 0x15,  Month (3 char)  0x46, 0x65, 0x62, 0x4d, 0xe4, 0x72, 0x20 }; */
static const unsigned char packet_send_favorite[] =
	{ 0x17, 0x0f, 0x19, 0x10, /*pos */ 0x01, /*name */ 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
0x20, 0x20, 0x20, 0x20, /*end_name */ 0x19,
	0x05, 0x0f, /*pos */ 0x01, /*icone */ 0x00
};
static const unsigned char packet_send_title[] =
	{ 0x17, 0x10, 0x19, 0x02, /*text */ 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
0x20, 0x20, 0x20, 0x20 /*end_text */  };
static const unsigned char packet_send_text[] =
	{ 0x17, 0x1e, 0x1b, 0x04, /*pos */ 0x00, /*inverse */ 0x25, /*text */ 0x20, 0x20,
0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
		/*end_text */ 0x17, 0x04, 0x10, 0x87
};
static const unsigned char packet_send_status[] =
	{ 0x17, 0x20, 0x19, 0x08, /*text */ 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20    /*end_text */
};
static const unsigned char packet_send_status2[] =
	{ 0x17, 0x0b, 0x19, /* pos [08|28|48|68] */ 0x00, /* text */ 0x20, 0x20, 0x20, 0x20,
0x20, 0x20, 0x20 /* end_text */  };

static const unsigned char packet_send_led_update[] = { 0x19, 0x04, 0x00, 0x00 };

static const unsigned char packet_send_query_basic_manager_04[] = { 0x1a, 0x04, 0x01, 0x04 };
static const unsigned char packet_send_query_mac_address[] = { 0x1a, 0x04, 0x01, 0x08 };
static const unsigned char packet_send_query_basic_manager_10[] = { 0x1a, 0x04, 0x01, 0x10 };
static const unsigned char packet_send_S1[] = { 0x1a, 0x07, 0x07, 0x00, 0x00, 0x00, 0x13 };

static unsigned char packet_send_ping[] =
	{ 0x1e, 0x05, 0x12, 0x00, /*Watchdog timer */ 0x78 };

#define BUFFSEND unsigned char buffsend[64] = { 0x00, 0x00, 0xaa, 0xbb, 0x02, 0x01 }

static const char tdesc[] = "UNISTIM Channel Driver";
static const char channel_type[] = "USTM";

/*! Protos */
static struct ast_channel *unistim_new(struct unistim_subchannel *sub, int state, const char *linkedid);
static int load_module(void);
static int reload(void);
static int unload_module(void);
static int reload_config(void);
static void show_main_page(struct unistimsession *pte);
static struct ast_channel *unistim_request(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, 
	void *data, int *cause);
static int unistim_call(struct ast_channel *ast, char *dest, int timeout);
static int unistim_hangup(struct ast_channel *ast);
static int unistim_answer(struct ast_channel *ast);
static struct ast_frame *unistim_read(struct ast_channel *ast);
static int unistim_write(struct ast_channel *ast, struct ast_frame *frame);
static int unistim_indicate(struct ast_channel *ast, int ind, const void *data,
	size_t datalen);
static int unistim_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static int unistim_senddigit_begin(struct ast_channel *ast, char digit);
static int unistim_senddigit_end(struct ast_channel *ast, char digit, 
	unsigned int duration);
static int unistim_sendtext(struct ast_channel *ast, const char *text);

static int write_entry_history(struct unistimsession *pte, FILE * f, char c, 
	char *line1);
static void change_callerid(struct unistimsession *pte, int type, char *callerid);

static struct ast_channel_tech unistim_tech = {
	.type = channel_type,
	.description = tdesc,
	.properties = AST_CHAN_TP_WANTSJITTER | AST_CHAN_TP_CREATESJITTER,
	.requester = unistim_request,
	.call = unistim_call,
	.hangup = unistim_hangup,
	.answer = unistim_answer,
	.read = unistim_read,
	.write = unistim_write,
	.indicate = unistim_indicate,
	.fixup = unistim_fixup,
	.send_digit_begin = unistim_senddigit_begin,
	.send_digit_end = unistim_senddigit_end,
	.send_text = unistim_sendtext,
	.bridge = ast_rtp_instance_bridge,
};

static void display_last_error(const char *sz_msg)
{
	time_t cur_time;
	
	time(&cur_time);

	/* Display the error message */
	ast_log(LOG_WARNING, "%s %s : (%u) %s\n", ctime(&cur_time), sz_msg, errno,
			strerror(errno));
}

static unsigned int get_tick_count(void)
{
	struct timeval now = ast_tvnow();

	return (now.tv_sec * 1000) + (now.tv_usec / 1000);
}

/* Send data to a phone without retransmit nor buffering */
static void send_raw_client(int size, const unsigned char *data, struct sockaddr_in *addr_to,
			    const struct sockaddr_in *addr_ourip)
{
#ifdef HAVE_PKTINFO
	struct iovec msg_iov;
	struct msghdr msg;
	char buffer[CMSG_SPACE(sizeof(struct in_pktinfo))];
	struct cmsghdr *ip_msg = (struct cmsghdr *) buffer;
	struct in_pktinfo *pki = (struct in_pktinfo *) CMSG_DATA(ip_msg);

	/* cast this to a non-const pointer, since the sendmsg() API
	 * does not provide read-only and write-only flavors of the
	 * structures used for its arguments, but in this case we know
	 * the data will not be modified
	 */
	msg_iov.iov_base = (char *) data;
	msg_iov.iov_len = size;

	msg.msg_name = addr_to;	 /* optional address */
	msg.msg_namelen = sizeof(struct sockaddr_in);   /* size of address */
	msg.msg_iov = &msg_iov;	 /* scatter/gather array */
	msg.msg_iovlen = 1;		     /* # elements in msg_iov */
	msg.msg_control = ip_msg;       /* ancillary data */
	msg.msg_controllen = sizeof(buffer);    /* ancillary data buffer len */
	msg.msg_flags = 0;		      /* flags on received message */

	ip_msg->cmsg_len = CMSG_LEN(sizeof(*pki));
	ip_msg->cmsg_level = IPPROTO_IP;
	ip_msg->cmsg_type = IP_PKTINFO;
	pki->ipi_ifindex = 0;	   /* Interface index, 0 = use interface specified in routing table */
	pki->ipi_spec_dst.s_addr = addr_ourip->sin_addr.s_addr; /* Local address */
	/* pki->ipi_addr = ;   Header Destination address - ignored by kernel */

#ifdef DUMP_PACKET
	if (unistimdebug) {
		int tmp;
		char iabuf[INET_ADDRSTRLEN];
		char iabuf2[INET_ADDRSTRLEN];
		ast_verb(0, "\n**> From %s sending %d bytes to %s ***\n",
					ast_inet_ntoa(addr_ourip->sin_addr), (int) size,
					ast_inet_ntoa(addr_to->sin_addr));
		for (tmp = 0; tmp < size; tmp++)
			ast_verb(0, "%.2x ", (unsigned char) data[tmp]);
		ast_verb(0, "\n******************************************\n");

	}
#endif

	if (sendmsg(unistimsock, &msg, 0) == -1)
		display_last_error("Error sending datas");
#else
	if (sendto(unistimsock, data, size, 0, (struct sockaddr *) addr_to, sizeof(*addr_to))
		== -1)
		display_last_error("Error sending datas");
#endif
}

static void send_client(int size, const unsigned char *data, struct unistimsession *pte)
{
	unsigned int tick;
	int buf_pos;
	unsigned short *sdata = (unsigned short *) data;

	ast_mutex_lock(&pte->lock);
	buf_pos = pte->last_buf_available;

	if (buf_pos >= MAX_BUF_NUMBER) {
		ast_log(LOG_WARNING, "Error : send queue overflow\n");
		ast_mutex_unlock(&pte->lock);
		return;
	}
	sdata[1] = ntohs(++(pte->seq_server));
	pte->wsabufsend[buf_pos].len = size;
	memcpy(pte->wsabufsend[buf_pos].buf, data, size);

	tick = get_tick_count();
	pte->timeout = tick + RETRANSMIT_TIMER;

/*#ifdef DUMP_PACKET */
	if (unistimdebug)
		ast_verb(6, "Sending datas with seq #0x%.4x Using slot #%d :\n", pte->seq_server, buf_pos);
/*#endif */
	send_raw_client(pte->wsabufsend[buf_pos].len, pte->wsabufsend[buf_pos].buf, &(pte->sin),
				  &(pte->sout));
	pte->last_buf_available++;
	ast_mutex_unlock(&pte->lock);
}

static void send_ping(struct unistimsession *pte)
{
	BUFFSEND;
	if (unistimdebug)
		ast_verb(6, "Sending ping\n");
	pte->tick_next_ping = get_tick_count() + unistim_keepalive;
	memcpy(buffsend + SIZE_HEADER, packet_send_ping, sizeof(packet_send_ping));
	send_client(SIZE_HEADER + sizeof(packet_send_ping), buffsend, pte);
}

static int get_to_address(int fd, struct sockaddr_in *toAddr)
{
#ifdef HAVE_PKTINFO
	int err;
	struct msghdr msg;
	struct {
		struct cmsghdr cm;
		int len;
		struct in_addr address;
	} ip_msg;

	/* Zero out the structures before we use them */
	/* This sets several key values to NULL */
	memset(&msg, 0, sizeof(msg));
	memset(&ip_msg, 0, sizeof(ip_msg));

	/* Initialize the message structure */
	msg.msg_control = &ip_msg;
	msg.msg_controllen = sizeof(ip_msg);
	/* Get info about the incoming packet */
	err = recvmsg(fd, &msg, MSG_PEEK);
	if (err == -1)
		ast_log(LOG_WARNING, "recvmsg returned an error: %s\n", strerror(errno));
	memcpy(&toAddr->sin_addr, &ip_msg.address, sizeof(struct in_addr));
	return err;
#else
	memcpy(&toAddr, &public_ip, sizeof(&toAddr));
	return 0;
#endif
}

/* Allocate memory & initialize structures for a new phone */
/* addr_from : ip address of the phone */
static struct unistimsession *create_client(const struct sockaddr_in *addr_from)
{
	int tmp;
	struct unistimsession *s;

	if (!(s = ast_calloc(1, sizeof(*s))))
		return NULL;

	memcpy(&s->sin, addr_from, sizeof(struct sockaddr_in));
	get_to_address(unistimsock, &s->sout);
	if (unistimdebug) {
		ast_verb(0, "Creating a new entry for the phone from %s received via server ip %s\n",
			 ast_inet_ntoa(addr_from->sin_addr), ast_inet_ntoa(s->sout.sin_addr));
	}
	ast_mutex_init(&s->lock);
	ast_mutex_lock(&sessionlock);
	s->next = sessions;
	sessions = s;

	s->timeout = get_tick_count() + RETRANSMIT_TIMER;
	s->seq_phone = (short) 0x0000;
	s->seq_server = (short) 0x0000;
	s->last_seq_ack = (short) 0x000;
	s->last_buf_available = 0;
	s->nb_retransmit = 0;
	s->state = STATE_INIT;
	s->tick_next_ping = get_tick_count() + unistim_keepalive;
	/* Initialize struct wsabuf  */
	for (tmp = 0; tmp < MAX_BUF_NUMBER; tmp++) {
		s->wsabufsend[tmp].buf = s->buf[tmp];
	}
	ast_mutex_unlock(&sessionlock);
	return s;
}

static void send_end_call(struct unistimsession *pte)
{
	BUFFSEND;
	if (unistimdebug)
		ast_verb(0, "Sending end call\n");
	memcpy(buffsend + SIZE_HEADER, packet_send_end_call, sizeof(packet_send_end_call));
	send_client(SIZE_HEADER + sizeof(packet_send_end_call), buffsend, pte);
}

static void set_ping_timer(struct unistimsession *pte)
{
	unsigned int tick = 0;	/* XXX what is this for, anyways */

	pte->timeout = pte->tick_next_ping;
	DEBUG_TIMER("tick = %u next ping at %u tick\n", tick, pte->timeout);
	return;
}

/* Checking if our send queue is empty,
 * if true, setting up a timer for keepalive */
static void check_send_queue(struct unistimsession *pte)
{
	/* Check if our send queue contained only one element */
	if (pte->last_buf_available == 1) {
		if (unistimdebug)
			ast_verb(6, "Our single packet was ACKed.\n");
		pte->last_buf_available--;
		set_ping_timer(pte);
		return;
	}
	/* Check if this ACK catch up our latest packet */
	else if (pte->last_seq_ack + 1 == pte->seq_server + 1) {
		if (unistimdebug)
			ast_verb(6, "Our send queue is completely ACKed.\n");
		pte->last_buf_available = 0;    /* Purge the send queue */
		set_ping_timer(pte);
		return;
	}
	if (unistimdebug)
		ast_verb(6, "We still have packets in our send queue\n");
	return;
}

static void send_start_timer(struct unistimsession *pte)
{
	BUFFSEND;
	if (unistimdebug)
		ast_verb(0, "Sending start timer\n");
	memcpy(buffsend + SIZE_HEADER, packet_send_StartTimer, sizeof(packet_send_StartTimer));
	send_client(SIZE_HEADER + sizeof(packet_send_StartTimer), buffsend, pte);
}

static void send_stop_timer(struct unistimsession *pte)
{
	BUFFSEND;
	if (unistimdebug)
		ast_verb(0, "Sending stop timer\n");
	memcpy(buffsend + SIZE_HEADER, packet_send_stop_timer, sizeof(packet_send_stop_timer));
	send_client(SIZE_HEADER + sizeof(packet_send_stop_timer), buffsend, pte);
}

static void Sendicon(unsigned char pos, unsigned char status, struct unistimsession *pte)
{
	BUFFSEND;
	if (unistimdebug)
		ast_verb(0, "Sending icon pos %d with status 0x%.2x\n", pos, status);
	memcpy(buffsend + SIZE_HEADER, packet_send_icon, sizeof(packet_send_icon));
	buffsend[9] = pos;
	buffsend[10] = status;
	send_client(SIZE_HEADER + sizeof(packet_send_icon), buffsend, pte);
}

static void send_tone(struct unistimsession *pte, uint16_t tone1, uint16_t tone2)
{
	BUFFSEND;
	if (!tone1) {
		if (unistimdebug)
			ast_verb(0, "Sending Stream Based Tone Off\n");
		memcpy(buffsend + SIZE_HEADER, packet_send_stream_based_tone_off,
			   sizeof(packet_send_stream_based_tone_off));
		send_client(SIZE_HEADER + sizeof(packet_send_stream_based_tone_off), buffsend, pte);
		return;
	}
	/* Since most of the world use a continuous tone, it's useless
	   if (unistimdebug)
	   ast_verb(0, "Sending Stream Based Tone Cadence Download\n");
	   memcpy (buffsend + SIZE_HEADER, packet_send_StreamBasedToneCad, sizeof (packet_send_StreamBasedToneCad));
	   send_client (SIZE_HEADER + sizeof (packet_send_StreamBasedToneCad), buffsend, pte); */
	if (unistimdebug)
		ast_verb(0, "Sending Stream Based Tone Frequency Component List Download %d %d\n", tone1, tone2);
	tone1 *= 8;
	if (!tone2) {
		memcpy(buffsend + SIZE_HEADER, packet_send_stream_based_tone_single_freq,
			   sizeof(packet_send_stream_based_tone_single_freq));
		buffsend[10] = (tone1 & 0xff00) >> 8;
		buffsend[11] = (tone1 & 0x00ff);
		send_client(SIZE_HEADER + sizeof(packet_send_stream_based_tone_single_freq), buffsend,
				   pte);
	} else {
		tone2 *= 8;
		memcpy(buffsend + SIZE_HEADER, packet_send_stream_based_tone_dial_freq,
			   sizeof(packet_send_stream_based_tone_dial_freq));
		buffsend[10] = (tone1 & 0xff00) >> 8;
		buffsend[11] = (tone1 & 0x00ff);
		buffsend[12] = (tone2 & 0xff00) >> 8;
		buffsend[13] = (tone2 & 0x00ff);
		send_client(SIZE_HEADER + sizeof(packet_send_stream_based_tone_dial_freq), buffsend,
				   pte);
	}

	if (unistimdebug)
		ast_verb(0, "Sending Stream Based Tone On\n");
	memcpy(buffsend + SIZE_HEADER, packet_send_stream_based_tone_on,
		   sizeof(packet_send_stream_based_tone_on));
	send_client(SIZE_HEADER + sizeof(packet_send_stream_based_tone_on), buffsend, pte);
}

/* Positions for favorites
 |--------------------|
 |  5	    2    |
 |  4	    1    |
 |  3	    0    |
*/

/* status (icons) : 00 = nothing, 2x/3x = see parser.h, 4x/5x = blink fast, 6x/7x = blink slow */
static void
send_favorite(unsigned char pos, unsigned char status, struct unistimsession *pte,
			 const char *text)
{
	BUFFSEND;
	int i;

	if (unistimdebug)
		ast_verb(0, "Sending favorite pos %d with status 0x%.2x\n", pos, status);
	memcpy(buffsend + SIZE_HEADER, packet_send_favorite, sizeof(packet_send_favorite));
	buffsend[10] = pos;
	buffsend[24] = pos;
	buffsend[25] = status;
	i = strlen(text);
	if (i > FAV_MAX_LENGTH)
		i = FAV_MAX_LENGTH;
	memcpy(buffsend + FAV_MAX_LENGTH + 1, text, i);
	send_client(SIZE_HEADER + sizeof(packet_send_favorite), buffsend, pte);
}

static void refresh_all_favorite(struct unistimsession *pte)
{
	int i = 0;

	if (unistimdebug)
		ast_verb(0, "Refreshing all favorite\n");
	for (i = 0; i < 6; i++) {
		if ((pte->device->softkeyicon[i] <= FAV_ICON_HEADPHONES_ONHOLD) &&
			(pte->device->softkeylinepos != i))
			send_favorite((unsigned char) i, pte->device->softkeyicon[i] + 1, pte,
						 pte->device->softkeylabel[i]);
		else
			send_favorite((unsigned char) i, pte->device->softkeyicon[i], pte,
						 pte->device->softkeylabel[i]);

	}
}

/* Change the status for this phone (pte) and update for each phones where pte is bookmarked
 * use FAV_ICON_*_BLACK constant in status parameters */
static void change_favorite_icon(struct unistimsession *pte, unsigned char status)
{
	struct unistim_device *d = devices;
	int i;
	/* Update the current phone */
	if (pte->state != STATE_CLEANING)
		send_favorite(pte->device->softkeylinepos, status, pte,
					 pte->device->softkeylabel[pte->device->softkeylinepos]);
	/* Notify other phones if we're in their bookmark */
	while (d) {
		for (i = 0; i < 6; i++) {
			if (d->sp[i] == pte->device) {  /* It's us ? */
				if (d->softkeyicon[i] != status) {      /* Avoid resending the same icon */
					d->softkeyicon[i] = status;
					if (d->session)
						send_favorite(i, status + 1, d->session, d->softkeylabel[i]);
				}
			}
		}
		d = d->next;
	}
}

static int RegisterExtension(const struct unistimsession *pte)
{
	if (unistimdebug)
		ast_verb(0, "Trying to register extension '%s' into context '%s' to %s\n",
					pte->device->extension_number, pte->device->lines->context,
					pte->device->lines->fullname);
	return ast_add_extension(pte->device->lines->context, 0,
							 pte->device->extension_number, 1, NULL, NULL, "Dial",
							 pte->device->lines->fullname, 0, "Unistim");
}

static int UnregisterExtension(const struct unistimsession *pte)
{
	if (unistimdebug)
		ast_verb(0, "Trying to unregister extension '%s' context '%s'\n",
					pte->device->extension_number, pte->device->lines->context);
	return ast_context_remove_extension(pte->device->lines->context,
										pte->device->extension_number, 1, "Unistim");
}

/* Free memory allocated for a phone */
static void close_client(struct unistimsession *s)
{
	struct unistim_subchannel *sub;
	struct unistimsession *cur, *prev = NULL;
	ast_mutex_lock(&sessionlock);
	cur = sessions;
	/* Looking for the session in the linked chain */
	while (cur) {
		if (cur == s)
			break;
		prev = cur;
		cur = cur->next;
	}
	if (cur) {				      /* Session found ? */
		if (cur->device) {	      /* This session was registered ? */
			s->state = STATE_CLEANING;
			if (unistimdebug)
				ast_verb(0, "close_client session %p device %p lines %p sub %p\n",
							s, s->device, s->device->lines,
							s->device->lines->subs[SUB_REAL]);
			change_favorite_icon(s, FAV_ICON_NONE);
			sub = s->device->lines->subs[SUB_REAL];
			if (sub) {
				if (sub->owner) {       /* Call in progress ? */
					if (unistimdebug)
						ast_verb(0, "Aborting call\n");
					ast_queue_hangup_with_cause(sub->owner, AST_CAUSE_NETWORK_OUT_OF_ORDER);
				}
			} else
				ast_log(LOG_WARNING, "Freeing a client with no subchannel !\n");
			if (!ast_strlen_zero(s->device->extension_number))
				UnregisterExtension(s);
			cur->device->session = NULL;
		} else {
			if (unistimdebug)
				ast_verb(0, "Freeing an unregistered client\n");
		}
		if (prev)
			prev->next = cur->next;
		else
			sessions = cur->next;
		ast_mutex_destroy(&s->lock);
		ast_free(s);
	} else
		ast_log(LOG_WARNING, "Trying to delete non-existent session %p?\n", s);
	ast_mutex_unlock(&sessionlock);
	return;
}

/* Return 1 if the session chained link was modified */
static int send_retransmit(struct unistimsession *pte)
{
	int i;

	ast_mutex_lock(&pte->lock);
	if (++pte->nb_retransmit >= NB_MAX_RETRANSMIT) {
		if (unistimdebug)
			ast_verb(0, "Too many retransmit - freeing client\n");
		ast_mutex_unlock(&pte->lock);
		close_client(pte);
		return 1;
	}
	pte->timeout = get_tick_count() + RETRANSMIT_TIMER;

	for (i = pte->last_buf_available - (pte->seq_server - pte->last_seq_ack);
		 i < pte->last_buf_available; i++) {
		if (i < 0) {
			ast_log(LOG_WARNING,
					"Asked to retransmit an ACKed slot ! last_buf_available=%d, seq_server = #0x%.4x last_seq_ack = #0x%.4x\n",
					pte->last_buf_available, pte->seq_server, pte->last_seq_ack);
			continue;
		}

		if (unistimdebug) {
			unsigned short *sbuf = (unsigned short *) pte->wsabufsend[i].buf;
			unsigned short seq;

			seq = ntohs(sbuf[1]);
			ast_verb(0, "Retransmit slot #%d (seq=#0x%.4x), last ack was #0x%.4x\n", i,
						seq, pte->last_seq_ack);
		}
		send_raw_client(pte->wsabufsend[i].len, pte->wsabufsend[i].buf, &pte->sin,
					  &pte->sout);
	}
	ast_mutex_unlock(&pte->lock);
	return 0;
}

/* inverse : TEXT_INVERSE : yes, TEXT_NORMAL  : no */
static void
send_text(unsigned char pos, unsigned char inverse, struct unistimsession *pte,
		 const char *text)
{
	int i;
	BUFFSEND;
	if (unistimdebug)
		ast_verb(0, "Sending text at pos %d, inverse flag %d\n", pos, inverse);
	memcpy(buffsend + SIZE_HEADER, packet_send_text, sizeof(packet_send_text));
	buffsend[10] = pos;
	buffsend[11] = inverse;
	i = strlen(text);
	if (i > TEXT_LENGTH_MAX)
		i = TEXT_LENGTH_MAX;
	memcpy(buffsend + 12, text, i);
	send_client(SIZE_HEADER + sizeof(packet_send_text), buffsend, pte);
}

static void send_text_status(struct unistimsession *pte, const char *text)
{
	BUFFSEND;
	int i;
	if (unistimdebug)
		ast_verb(0, "Sending status text\n");
	if (pte->device) {
		if (pte->device->status_method == 1) {  /* For new firmware and i2050 soft phone */
			int n = strlen(text);
			/* Must send individual button separately */
			int j;
			for (i = 0, j = 0; i < 4; i++, j += 7) {
				int pos = 0x08 + (i * 0x20);
				memcpy(buffsend + SIZE_HEADER, packet_send_status2,
					   sizeof(packet_send_status2));

				buffsend[9] = pos;
				memcpy(buffsend + 10, (j < n) ? (text + j) : "       ", 7);
				send_client(SIZE_HEADER + sizeof(packet_send_status2), buffsend, pte);
			}
			return;
		}
	}


	memcpy(buffsend + SIZE_HEADER, packet_send_status, sizeof(packet_send_status));
	i = strlen(text);
	if (i > STATUS_LENGTH_MAX)
		i = STATUS_LENGTH_MAX;
	memcpy(buffsend + 10, text, i);
	send_client(SIZE_HEADER + sizeof(packet_send_status), buffsend, pte);

}

/* led values in hexa : 0 = bar off, 1 = bar on, 2 = bar 1s on/1s off, 3 = bar 2.5s on/0.5s off
 * 4 = bar 0.6s on/0.3s off, 5 = bar 0.5s on/0.5s off, 6 = bar 2s on/0.5s off
 * 7 = bar off, 8 = speaker off, 9 = speaker on, 10 = headphone off, 11 = headphone on
 * 18 = mute off, 19 mute on */
static void send_led_update(struct unistimsession *pte, unsigned char led)
{
	BUFFSEND;
	if (unistimdebug)
		ast_verb(0, "Sending led_update (%x)\n", led);
	memcpy(buffsend + SIZE_HEADER, packet_send_led_update, sizeof(packet_send_led_update));
	buffsend[9] = led;
	send_client(SIZE_HEADER + sizeof(packet_send_led_update), buffsend, pte);
}

/* output = OUTPUT_HANDSET, OUTPUT_HEADPHONE or OUTPUT_SPEAKER
 * volume = VOLUME_LOW, VOLUME_NORMAL, VOLUME_INSANELY_LOUD
 * mute = MUTE_OFF, MUTE_ON */
static void
send_select_output(struct unistimsession *pte, unsigned char output, unsigned char volume,
				 unsigned char mute)
{
	BUFFSEND;
	if (unistimdebug)
		ast_verb(0, "Sending select output packet output=%x volume=%x mute=%x\n", output,
					volume, mute);
	memcpy(buffsend + SIZE_HEADER, packet_send_select_output,
		   sizeof(packet_send_select_output));
	buffsend[9] = output;
	if (output == OUTPUT_SPEAKER)
		volume = VOLUME_LOW_SPEAKER;
	else
		volume = VOLUME_LOW;
	buffsend[10] = volume;
	if (mute == MUTE_ON_DISCRET)
		buffsend[11] = MUTE_ON;
	else
		buffsend[11] = mute;
	send_client(SIZE_HEADER + sizeof(packet_send_select_output), buffsend, pte);
	if (mute == MUTE_OFF)
		send_led_update(pte, 0x18);
	else if (mute == MUTE_ON)
		send_led_update(pte, 0x19);
	pte->device->mute = mute;
	if (output == OUTPUT_HANDSET) {
		if (mute == MUTE_ON)
			change_favorite_icon(pte, FAV_ICON_ONHOLD_BLACK);
		else
			change_favorite_icon(pte, FAV_ICON_OFFHOOK_BLACK);
		send_led_update(pte, 0x08);
		send_led_update(pte, 0x10);
	} else if (output == OUTPUT_HEADPHONE) {
		if (mute == MUTE_ON)
			change_favorite_icon(pte, FAV_ICON_HEADPHONES_ONHOLD);
		else
			change_favorite_icon(pte, FAV_ICON_HEADPHONES);
		send_led_update(pte, 0x08);
		send_led_update(pte, 0x11);
	} else if (output == OUTPUT_SPEAKER) {
		send_led_update(pte, 0x10);
		send_led_update(pte, 0x09);
		if (pte->device->receiver_state == STATE_OFFHOOK) {
			if (mute == MUTE_ON)
				change_favorite_icon(pte, FAV_ICON_SPEAKER_ONHOLD_BLACK);
			else
				change_favorite_icon(pte, FAV_ICON_SPEAKER_ONHOOK_BLACK);
		} else {
			if (mute == MUTE_ON)
				change_favorite_icon(pte, FAV_ICON_SPEAKER_ONHOLD_BLACK);
			else
				change_favorite_icon(pte, FAV_ICON_SPEAKER_OFFHOOK_BLACK);
		}
	} else
		ast_log(LOG_WARNING, "Invalid output (%d)\n", output);
	if (output != pte->device->output)
		pte->device->previous_output = pte->device->output;
	pte->device->output = output;
}

static void send_ring(struct unistimsession *pte, char volume, char style)
{
	BUFFSEND;
	if (unistimdebug)
		ast_verb(0, "Sending ring packet\n");
	memcpy(buffsend + SIZE_HEADER, packet_send_ring, sizeof(packet_send_ring));
	buffsend[24] = style + 0x10;
	buffsend[29] = volume * 0x10;
	send_client(SIZE_HEADER + sizeof(packet_send_ring), buffsend, pte);
}

static void send_no_ring(struct unistimsession *pte)
{
	BUFFSEND;
	if (unistimdebug)
		ast_verb(0, "Sending no ring packet\n");
	memcpy(buffsend + SIZE_HEADER, packet_send_no_ring, sizeof(packet_send_no_ring));
	send_client(SIZE_HEADER + sizeof(packet_send_no_ring), buffsend, pte);
}

static void send_texttitle(struct unistimsession *pte, const char *text)
{
	BUFFSEND;
	int i;
	if (unistimdebug)
		ast_verb(0, "Sending title text\n");
	memcpy(buffsend + SIZE_HEADER, packet_send_title, sizeof(packet_send_title));
	i = strlen(text);
	if (i > 12)
		i = 12;
	memcpy(buffsend + 10, text, i);
	send_client(SIZE_HEADER + sizeof(packet_send_title), buffsend, pte);

}

static void send_date_time(struct unistimsession *pte)
{
	BUFFSEND;
	struct timeval now = ast_tvnow();
	struct ast_tm atm = { 0, };

	if (unistimdebug)
		ast_verb(0, "Sending Time & Date\n");
	memcpy(buffsend + SIZE_HEADER, packet_send_date_time, sizeof(packet_send_date_time));
	ast_localtime(&now, &atm, NULL);
	buffsend[10] = (unsigned char) atm.tm_mon + 1;
	buffsend[11] = (unsigned char) atm.tm_mday;
	buffsend[12] = (unsigned char) atm.tm_hour;
	buffsend[13] = (unsigned char) atm.tm_min;
	send_client(SIZE_HEADER + sizeof(packet_send_date_time), buffsend, pte);
}

static void send_date_time2(struct unistimsession *pte)
{
	BUFFSEND;
	struct timeval now = ast_tvnow();
	struct ast_tm atm = { 0, };

	if (unistimdebug)
		ast_verb(0, "Sending Time & Date #2\n");
	memcpy(buffsend + SIZE_HEADER, packet_send_date_time2, sizeof(packet_send_date_time2));
	ast_localtime(&now, &atm, NULL);
	if (pte->device)
		buffsend[9] = pte->device->datetimeformat;
	else
		buffsend[9] = 61;
	buffsend[14] = (unsigned char) atm.tm_mon + 1;
	buffsend[15] = (unsigned char) atm.tm_mday;
	buffsend[16] = (unsigned char) atm.tm_hour;
	buffsend[17] = (unsigned char) atm.tm_min;
	send_client(SIZE_HEADER + sizeof(packet_send_date_time2), buffsend, pte);
}

static void send_date_time3(struct unistimsession *pte)
{
	BUFFSEND;
	struct timeval now = ast_tvnow();
	struct ast_tm atm = { 0, };

	if (unistimdebug)
		ast_verb(0, "Sending Time & Date #3\n");
	memcpy(buffsend + SIZE_HEADER, packet_send_date_time3, sizeof(packet_send_date_time3));
	ast_localtime(&now, &atm, NULL);
	buffsend[10] = (unsigned char) atm.tm_mon + 1;
	buffsend[11] = (unsigned char) atm.tm_mday;
	buffsend[12] = (unsigned char) atm.tm_hour;
	buffsend[13] = (unsigned char) atm.tm_min;
	send_client(SIZE_HEADER + sizeof(packet_send_date_time3), buffsend, pte);
}

static void send_blink_cursor(struct unistimsession *pte)
{
	BUFFSEND;
	if (unistimdebug)
		ast_verb(0, "Sending set blink\n");
	memcpy(buffsend + SIZE_HEADER, packet_send_blink_cursor, sizeof(packet_send_blink_cursor));
	send_client(SIZE_HEADER + sizeof(packet_send_blink_cursor), buffsend, pte);
	return;
}

/* pos : 0xab (a=0/2/4 = line ; b = row) */
static void send_cursor_pos(struct unistimsession *pte, unsigned char pos)
{
	BUFFSEND;
	if (unistimdebug)
		ast_verb(0, "Sending set cursor position\n");
	memcpy(buffsend + SIZE_HEADER, packet_send_set_pos_cursor,
		   sizeof(packet_send_set_pos_cursor));
	buffsend[11] = pos;
	send_client(SIZE_HEADER + sizeof(packet_send_set_pos_cursor), buffsend, pte);
	return;
}

static void rcv_resume_connection_with_server(struct unistimsession *pte)
{
	BUFFSEND;
	if (unistimdebug) {
		ast_verb(0, "ResumeConnectionWithServer received\n");
		ast_verb(0, "Sending packet_send_query_mac_address\n");
	}
	memcpy(buffsend + SIZE_HEADER, packet_send_query_mac_address,
		   sizeof(packet_send_query_mac_address));
	send_client(SIZE_HEADER + sizeof(packet_send_query_mac_address), buffsend, pte);
	return;
}

static int unistim_register(struct unistimsession *s)
{
	struct unistim_device *d;

	ast_mutex_lock(&devicelock);
	d = devices;
	while (d) {
		if (!strcasecmp(s->macaddr, d->id)) {
			/* XXX Deal with IP authentication */
			s->device = d;
			d->session = s;
			d->codec_number = DEFAULT_CODEC;
			d->pos_fav = 0;
			d->missed_call = 0;
			d->receiver_state = STATE_ONHOOK;
			break;
		}
		d = d->next;
	}
	ast_mutex_unlock(&devicelock);

	if (!d)
		return 0;

	return 1;
}

static void unistim_line_copy(struct unistim_line *dst, struct unistim_line *src)
{
	struct ast_format_cap *tmp = src->cap;
	memcpy(dst, src, sizeof(*dst)); /* this over writes the cap ptr, so we have to reset it */
	src->cap = tmp;
	ast_format_cap_copy(src->cap, dst->cap);
}

static struct unistim_line *unistim_line_destroy(struct unistim_line *l)
{
	if (!l) {
		return NULL;
	}
	l->cap = ast_format_cap_destroy(l->cap);
	ast_free(l);
	return NULL;
}

static struct unistim_line *unistim_line_alloc(void)
{
	struct unistim_line *l;
	if (!(l = ast_calloc(1, sizeof(*l)))) {
		return NULL;
	}

	if (!(l->cap = ast_format_cap_alloc_nolock())) {
		ast_free(l);
		return NULL;
	}
	return l;
}

static int alloc_sub(struct unistim_line *l, int x)
{
	struct unistim_subchannel *sub;
	if (!(sub = ast_calloc(1, sizeof(*sub))))
		return 0;

	if (unistimdebug)
		ast_verb(3, "Allocating UNISTIM subchannel #%d on %s@%s ptr=%p\n", x, l->name, l->parent->name, sub);
	sub->parent = l;
	sub->subtype = x;
	l->subs[x] = sub;
	ast_mutex_init(&sub->lock);
	return 1;
}

static int unalloc_sub(struct unistim_line *p, int x)
{
	if (!x) {
		ast_log(LOG_WARNING, "Trying to unalloc the real channel %s@%s?!?\n", p->name,
				p->parent->name);
		return -1;
	}
	if (unistimdebug)
		ast_debug(1, "Released sub %d of channel %s@%s\n", x, p->name,
				p->parent->name);
	ast_mutex_destroy(&p->lock);
	ast_free(p->subs[x]);
	p->subs[x] = 0;
	return 0;
}

static void rcv_mac_addr(struct unistimsession *pte, const unsigned char *buf)
{
	BUFFSEND;
	int tmp, i = 0;
	char addrmac[19];
	int res = 0;
	if (unistimdebug)
		ast_verb(0, "Mac Address received : ");
	for (tmp = 15; tmp < 15 + SIZE_HEADER; tmp++) {
		sprintf(&addrmac[i], "%.2x", (unsigned char) buf[tmp]);
		i += 2;
	}
	if (unistimdebug)
		ast_verb(0, "%s\n", addrmac);
	strcpy(pte->macaddr, addrmac);
	res = unistim_register(pte);
	if (!res) {
		switch (autoprovisioning) {
		case AUTOPROVISIONING_NO:
			ast_log(LOG_WARNING, "No entry found for this phone : %s\n", addrmac);
			pte->state = STATE_AUTHDENY;
			break;
		case AUTOPROVISIONING_YES:
			{
				struct unistim_device *d, *newd;
				struct unistim_line *newl;
				if (unistimdebug)
					ast_verb(0, "New phone, autoprovisioning on\n");
				/* First : locate the [template] section */
				ast_mutex_lock(&devicelock);
				d = devices;
				while (d) {
					if (!strcasecmp(d->name, "template")) {
						/* Found, cloning this entry */
						if (!(newd = ast_malloc(sizeof(*newd)))) {
							ast_mutex_unlock(&devicelock);
							return;
						}

						memcpy(newd, d, sizeof(*newd));
						if (!(newl = unistim_line_alloc())) {
							ast_free(newd);
							ast_mutex_unlock(&devicelock);
							return;
						}

						unistim_line_copy(d->lines, newl);
						if (!alloc_sub(newl, SUB_REAL)) {
							ast_free(newd);
							unistim_line_destroy(newl);
							ast_mutex_unlock(&devicelock);
							return;
						}
						/* Ok, now updating some fields */
						ast_copy_string(newd->id, addrmac, sizeof(newd->id));
						ast_copy_string(newd->name, addrmac, sizeof(newd->name));
						if (newd->extension == EXTENSION_NONE)
							newd->extension = EXTENSION_ASK;
						newd->lines = newl;
						newd->receiver_state = STATE_ONHOOK;
						newd->session = pte;
						newd->to_delete = -1;
						pte->device = newd;
						newd->next = NULL;
						newl->parent = newd;
						strcpy(newl->name, d->lines->name);
						snprintf(d->lines->name, sizeof(d->lines->name), "%d",
								 atoi(d->lines->name) + 1);
						snprintf(newl->fullname, sizeof(newl->fullname), "USTM/%s@%s",
								 newl->name, newd->name);
						/* Go to the end of the linked chain */
						while (d->next) {
							d = d->next;
						}
						d->next = newd;
						d = newd;
						break;
					}
					d = d->next;
				}
				ast_mutex_unlock(&devicelock);
				if (!d) {
					ast_log(LOG_WARNING, "No entry [template] found in unistim.conf\n");
					pte->state = STATE_AUTHDENY;
				}
			}
			break;
		case AUTOPROVISIONING_TN:
			pte->state = STATE_AUTHDENY;
			break;
		case AUTOPROVISIONING_DB:
			ast_log(LOG_WARNING,
					"Autoprovisioning with database is not yet functional\n");
			break;
		default:
			ast_log(LOG_WARNING, "Internal error : unknown autoprovisioning value = %d\n",
					autoprovisioning);
		}
	}
	if (pte->state != STATE_AUTHDENY) {
		ast_verb(3, "Device '%s' successfuly registered\n", pte->device->name);
		switch (pte->device->extension) {
		case EXTENSION_NONE:
			pte->state = STATE_MAINPAGE;
			break;
		case EXTENSION_ASK:
			/* Checking if we already have an extension number */
			if (ast_strlen_zero(pte->device->extension_number))
				pte->state = STATE_EXTENSION;
			else {
				/* Yes, because of a phone reboot. We don't ask again for the TN */
				if (RegisterExtension(pte))
					pte->state = STATE_EXTENSION;
				else
					pte->state = STATE_MAINPAGE;
			}
			break;
		case EXTENSION_LINE:
			ast_copy_string(pte->device->extension_number, pte->device->lines->name,
							sizeof(pte->device->extension_number));
			if (RegisterExtension(pte))
				pte->state = STATE_EXTENSION;
			else
				pte->state = STATE_MAINPAGE;
			break;
		case EXTENSION_TN:
			/* If we are here, it's because of a phone reboot */
			pte->state = STATE_MAINPAGE;
			break;
		default:
			ast_log(LOG_WARNING, "Internal error, extension value unknown : %d\n",
					pte->device->extension);
			pte->state = STATE_AUTHDENY;
			break;
		}
	}
	if (pte->state == STATE_EXTENSION) {
		if (pte->device->extension != EXTENSION_TN)
			pte->device->extension = EXTENSION_ASK;
		pte->device->extension_number[0] = '\0';
	}
	if (unistimdebug)
		ast_verb(0, "\nSending S1\n");
	memcpy(buffsend + SIZE_HEADER, packet_send_S1, sizeof(packet_send_S1));
	send_client(SIZE_HEADER + sizeof(packet_send_S1), buffsend, pte);

	if (unistimdebug)
		ast_verb(0, "Sending query_basic_manager_04\n");
	memcpy(buffsend + SIZE_HEADER, packet_send_query_basic_manager_04,
		   sizeof(packet_send_query_basic_manager_04));
	send_client(SIZE_HEADER + sizeof(packet_send_query_basic_manager_04), buffsend, pte);

	if (unistimdebug)
		ast_verb(0, "Sending query_basic_manager_10\n");
	memcpy(buffsend + SIZE_HEADER, packet_send_query_basic_manager_10,
		   sizeof(packet_send_query_basic_manager_10));
	send_client(SIZE_HEADER + sizeof(packet_send_query_basic_manager_10), buffsend, pte);

	send_date_time(pte);
	return;
}

static int write_entry_history(struct unistimsession *pte, FILE * f, char c, char *line1)
{
	if (fwrite(&c, 1, 1, f) != 1) {
		display_last_error("Unable to write history log header.");
		return -1;
	}
	if (fwrite(line1, TEXT_LENGTH_MAX, 1, f) != 1) {
		display_last_error("Unable to write history entry - date.");
		return -1;
	}
	if (fwrite(pte->device->lst_cid, TEXT_LENGTH_MAX, 1, f) != 1) {
		display_last_error("Unable to write history entry - callerid.");
		return -1;
	}
	if (fwrite(pte->device->lst_cnm, TEXT_LENGTH_MAX, 1, f) != 1) {
		display_last_error("Unable to write history entry - callername.");
		return -1;
	}
	return 0;
}

static int write_history(struct unistimsession *pte, char way, char ismissed)
{
	char tmp[AST_CONFIG_MAX_PATH], tmp2[AST_CONFIG_MAX_PATH];
	char line1[TEXT_LENGTH_MAX + 1];
	char count = 0, *histbuf;
	int size;
	FILE *f, *f2;
	struct timeval now = ast_tvnow();
	struct ast_tm atm = { 0, };

	if (!pte->device)
		return -1;
	if (!pte->device->callhistory)
		return 0;
	if (strchr(pte->device->name, '/') || (pte->device->name[0] == '.')) {
		ast_log(LOG_WARNING, "Account code '%s' insecure for writing file\n",
				pte->device->name);
		return -1;
	}

	snprintf(tmp, sizeof(tmp), "%s/%s", ast_config_AST_LOG_DIR, USTM_LOG_DIR);
	if (ast_mkdir(tmp, 0770)) {
		if (errno != EEXIST) {
			display_last_error("Unable to create directory for history");
			return -1;
		}
	}

	ast_localtime(&now, &atm, NULL);
	if (ismissed) {
		if (way == 'i')
			strcpy(tmp2, "Miss");
		else
			strcpy(tmp2, "Fail");
	} else
		strcpy(tmp2, "Answ");
	snprintf(line1, sizeof(line1), "%04d/%02d/%02d %02d:%02d:%02d %s",
			 atm.tm_year + 1900, atm.tm_mon + 1, atm.tm_mday, atm.tm_hour,
			 atm.tm_min, atm.tm_sec, tmp2);

	snprintf(tmp, sizeof(tmp), "%s/%s/%s-%c.csv", ast_config_AST_LOG_DIR,
			 USTM_LOG_DIR, pte->device->name, way);
	if ((f = fopen(tmp, "r"))) {
		struct stat bufstat;

		if (stat(tmp, &bufstat)) {
			display_last_error("Unable to stat history log.");
			fclose(f);
			return -1;
		}
		size = 1 + (MAX_ENTRY_LOG * TEXT_LENGTH_MAX * 3);
		if (bufstat.st_size != size) {
			ast_log(LOG_WARNING,
					"History file %s has an incorrect size (%d instead of %d). It will be replaced by a new one.",
					tmp, (int) bufstat.st_size, size);
			fclose(f);
			f = NULL;
			count = 1;
		}
	}

	/* If we can't open the log file, we create a brand new one */
	if (!f) {
		char c = 1;
		int i;

		if ((errno != ENOENT) && (count == 0)) {
			display_last_error("Unable to open history log.");
			return -1;
		}
		f = fopen(tmp, "w");
		if (!f) {
			display_last_error("Unable to create history log.");
			return -1;
		}
		if (write_entry_history(pte, f, c, line1)) {
			fclose(f);
			return -1;
		}
		memset(line1, ' ', TEXT_LENGTH_MAX);
		for (i = 3; i < MAX_ENTRY_LOG * 3; i++) {
			if (fwrite(line1, TEXT_LENGTH_MAX, 1, f) != 1) {
				display_last_error("Unable to write history entry - stuffing.");
				fclose(f);
				return -1;
			}
		}
		if (fclose(f))
			display_last_error("Unable to close history - creation.");
		return 0;
	}
	/* We can open the log file, we create a temporary one, we add our entry and copy the rest */
	if (fread(&count, 1, 1, f) != 1) {
		display_last_error("Unable to read history header.");
		fclose(f);
		return -1;
	}
	if (count > MAX_ENTRY_LOG) {
		ast_log(LOG_WARNING, "Invalid count in history header of %s (%d max %d)\n", tmp,
				count, MAX_ENTRY_LOG);
		fclose(f);
		return -1;
	}
	snprintf(tmp2, sizeof(tmp2), "%s/%s/%s-%c.csv.tmp", ast_config_AST_LOG_DIR,
			 USTM_LOG_DIR, pte->device->name, way);
	if (!(f2 = fopen(tmp2, "w"))) {
		display_last_error("Unable to create temporary history log.");
		fclose(f);
		return -1;
	}

	if (++count > MAX_ENTRY_LOG)
		count = MAX_ENTRY_LOG;

	if (write_entry_history(pte, f2, count, line1)) {
		fclose(f);
		fclose(f2);
		return -1;
	}

	size = (MAX_ENTRY_LOG - 1) * TEXT_LENGTH_MAX * 3;
	if (!(histbuf = ast_malloc(size))) {
		fclose(f);
		fclose(f2);
		return -1;
	}

	if (fread(histbuf, size, 1, f) != 1) {
		ast_free(histbuf);
		fclose(f);
		fclose(f2);
		display_last_error("Unable to read previous history entries.");
		return -1;
	}
	if (fwrite(histbuf, size, 1, f2) != 1) {
		ast_free(histbuf);
		fclose(f);
		fclose(f2);
		display_last_error("Unable to write previous history entries.");
		return -1;
	}
	ast_free(histbuf);
	if (fclose(f))
		display_last_error("Unable to close history log.");
	if (fclose(f2))
		display_last_error("Unable to close temporary history log.");
	if (unlink(tmp))
		display_last_error("Unable to remove old history log.");
	if (rename(tmp2, tmp))
		display_last_error("Unable to rename new history log.");
	return 0;
}

static void cancel_dial(struct unistimsession *pte)
{
	send_no_ring(pte);
	pte->device->missed_call++;
	write_history(pte, 'i', 1);
	show_main_page(pte);
	return;
}

static void swap_subs(struct unistim_line *p, int a, int b)
{
/*  struct ast_channel *towner; */
	struct ast_rtp_instance *rtp;
	int fds;

	if (unistimdebug)
		ast_verb(0, "Swapping %d and %d\n", a, b);

	if ((!p->subs[a]->owner) || (!p->subs[b]->owner)) {
		ast_log(LOG_WARNING,
				"Attempted to swap subchannels with a null owner : sub #%d=%p sub #%d=%p\n",
				a, p->subs[a]->owner, b, p->subs[b]->owner);
		return;
	}
	rtp = p->subs[a]->rtp;
	p->subs[a]->rtp = p->subs[b]->rtp;
	p->subs[b]->rtp = rtp;

	fds = p->subs[a]->owner->fds[0];
	p->subs[a]->owner->fds[0] = p->subs[b]->owner->fds[0];
	p->subs[b]->owner->fds[0] = fds;

	fds = p->subs[a]->owner->fds[1];
	p->subs[a]->owner->fds[1] = p->subs[b]->owner->fds[1];
	p->subs[b]->owner->fds[1] = fds;
}

static int attempt_transfer(struct unistim_subchannel *p1, struct unistim_subchannel *p2)
{
	int res = 0;
	struct ast_channel
	 *chana = NULL, *chanb = NULL, *bridgea = NULL, *bridgeb = NULL, *peera =
		NULL, *peerb = NULL, *peerc = NULL;

	if (!p1->owner || !p2->owner) {
		ast_log(LOG_WARNING, "Transfer attempted without dual ownership?\n");
		return -1;
	}
	chana = p1->owner;
	chanb = p2->owner;
	bridgea = ast_bridged_channel(chana);
	bridgeb = ast_bridged_channel(chanb);

	if (bridgea) {
		peera = chana;
		peerb = chanb;
		peerc = bridgea;
	} else if (bridgeb) {
		peera = chanb;
		peerb = chana;
		peerc = bridgeb;
	}

	if (peera && peerb && peerc && (peerb != peerc)) {
		/*ast_quiet_chan(peera);
		   ast_quiet_chan(peerb);
		   ast_quiet_chan(peerc);
		   ast_quiet_chan(peerd); */

		if (peera->cdr && peerb->cdr) {
			peerb->cdr = ast_cdr_append(peerb->cdr, peera->cdr);
		} else if (peera->cdr) {
			peerb->cdr = peera->cdr;
		}
		peera->cdr = NULL;

		if (peerb->cdr && peerc->cdr) {
			peerb->cdr = ast_cdr_append(peerb->cdr, peerc->cdr);
		} else if (peerc->cdr) {
			peerb->cdr = peerc->cdr;
		}
		peerc->cdr = NULL;

		if (ast_channel_masquerade(peerb, peerc)) {
			ast_log(LOG_WARNING, "Failed to masquerade %s into %s\n", peerb->name,
					peerc->name);
			res = -1;
		}
		return res;
	} else {
		ast_log(LOG_NOTICE,
				"Transfer attempted with no appropriate bridged calls to transfer\n");
		if (chana)
			ast_softhangup_nolock(chana, AST_SOFTHANGUP_DEV);
		if (chanb)
			ast_softhangup_nolock(chanb, AST_SOFTHANGUP_DEV);
		return -1;
	}
	return 0;
}

void change_callerid(struct unistimsession *pte, int type, char *callerid)
{
	char *data;
	int size;

	if (type)
		data = pte->device->lst_cnm;
	else
		data = pte->device->lst_cid;

	/* This is very nearly strncpy(), except that the remaining buffer
	 * is padded with ' ', instead of '\0' */
	memset(data, ' ', TEXT_LENGTH_MAX);
	size = strlen(callerid);
	if (size > TEXT_LENGTH_MAX)
		size = TEXT_LENGTH_MAX;
	memcpy(data, callerid, size);
}

static void close_call(struct unistimsession *pte)
{
	struct unistim_subchannel *sub;
	struct unistim_line *l = pte->device->lines;

	sub = pte->device->lines->subs[SUB_REAL];
	send_stop_timer(pte);
	if (sub->owner) {
		sub->alreadygone = 1;
		if (l->subs[SUB_THREEWAY]) {
			l->subs[SUB_THREEWAY]->alreadygone = 1;
			if (attempt_transfer(sub, l->subs[SUB_THREEWAY]) < 0)
				ast_verb(0, "attempt_transfer failed.\n");
		} else
			ast_queue_hangup(sub->owner);
	} else {
		if (l->subs[SUB_THREEWAY]) {
			if (l->subs[SUB_THREEWAY]->owner)
				ast_queue_hangup_with_cause(l->subs[SUB_THREEWAY]->owner, AST_CAUSE_NORMAL_CLEARING);
			else
				ast_log(LOG_WARNING, "threeway sub without owner\n");
		} else
			ast_verb(0, "USTM(%s@%s-%d) channel already destroyed\n", sub->parent->name,
						sub->parent->parent->name, sub->subtype);
	}
	change_callerid(pte, 0, pte->device->redial_number);
	change_callerid(pte, 1, "");
	write_history(pte, 'o', pte->device->missed_call);
	pte->device->missed_call = 0;
	show_main_page(pte);
	return;
}

static void IgnoreCall(struct unistimsession *pte)
{
	send_no_ring(pte);
	return;
}

static void *unistim_ss(void *data)
{
	struct ast_channel *chan = data;
	struct unistim_subchannel *sub = chan->tech_pvt;
	struct unistim_line *l = sub->parent;
	struct unistimsession *s = l->parent->session;
	int res;

	ast_verb(3, "Starting switch on '%s@%s-%d' to %s\n", l->name, l->parent->name, sub->subtype, s->device->phone_number);
	ast_copy_string(chan->exten, s->device->phone_number, sizeof(chan->exten));
	ast_copy_string(s->device->redial_number, s->device->phone_number,
					sizeof(s->device->redial_number));
	ast_setstate(chan, AST_STATE_RING);
	res = ast_pbx_run(chan);
	if (res) {
		ast_log(LOG_WARNING, "PBX exited non-zero\n");
		send_tone(s, 1000, 0);;
	}
	return NULL;
}

static void start_rtp(struct unistim_subchannel *sub)
{
	BUFFSEND;
	struct sockaddr_in us = { 0, };
	struct sockaddr_in public = { 0, };
	struct sockaddr_in sin = { 0, };
	int codec;
	struct sockaddr_in sout = { 0, };
	struct ast_sockaddr us_tmp;
	struct ast_sockaddr sin_tmp;
	struct ast_sockaddr sout_tmp;

	/* Sanity checks */
	if (!sub) {
		ast_log(LOG_WARNING, "start_rtp with a null subchannel !\n");
		return;
	}
	if (!sub->parent) {
		ast_log(LOG_WARNING, "start_rtp with a null line !\n");
		return;
	}
	if (!sub->parent->parent) {
		ast_log(LOG_WARNING, "start_rtp with a null device !\n");
		return;
	}
	if (!sub->parent->parent->session) {
		ast_log(LOG_WARNING, "start_rtp with a null session !\n");
		return;
	}
	sout = sub->parent->parent->session->sout;

	ast_mutex_lock(&sub->lock);
	/* Allocate the RTP */
	if (unistimdebug)
		ast_verb(0, "Starting RTP. Bind on %s\n", ast_inet_ntoa(sout.sin_addr));
	ast_sockaddr_from_sin(&sout_tmp, &sout);
	sub->rtp = ast_rtp_instance_new("asterisk", sched, &sout_tmp, NULL);
	if (!sub->rtp) {
		ast_log(LOG_WARNING, "Unable to create RTP session: %s binaddr=%s\n",
				strerror(errno), ast_inet_ntoa(sout.sin_addr));
		ast_mutex_unlock(&sub->lock);
		return;
	}
	ast_rtp_instance_set_prop(sub->rtp, AST_RTP_PROPERTY_RTCP, 1);
	if (sub->owner) {
		sub->owner->fds[0] = ast_rtp_instance_fd(sub->rtp, 0);
		sub->owner->fds[1] = ast_rtp_instance_fd(sub->rtp, 1);
	}
	ast_rtp_instance_set_qos(sub->rtp, qos.tos_audio, qos.cos_audio, "UNISTIM RTP");
	ast_rtp_instance_set_prop(sub->rtp, AST_RTP_PROPERTY_NAT, sub->parent->parent->nat);

	/* Create the RTP connection */
	ast_rtp_instance_get_local_address(sub->rtp, &us_tmp);
	ast_sockaddr_to_sin(&us_tmp, &us);
	sin.sin_family = AF_INET;
	/* Setting up RTP for our side */
	memcpy(&sin.sin_addr, &sub->parent->parent->session->sin.sin_addr,
		   sizeof(sin.sin_addr));
	sin.sin_port = htons(sub->parent->parent->rtp_port);
	ast_sockaddr_from_sin(&sin_tmp, &sin);
	ast_rtp_instance_set_remote_address(sub->rtp, &sin_tmp);
	if (!(ast_format_cap_iscompatible(sub->owner->nativeformats, &sub->owner->readformat))) {
		struct ast_format tmpfmt;
		char tmp[256];
		ast_best_codec(sub->owner->nativeformats, &tmpfmt);
		ast_log(LOG_WARNING,
				"Our read/writeformat has been changed to something incompatible: %s, using %s best codec from %s\n",
				ast_getformatname(&sub->owner->readformat),
				ast_getformatname(&tmpfmt),
				ast_getformatname_multiple(tmp, sizeof(tmp), sub->owner->nativeformats));
		ast_format_copy(&sub->owner->readformat, &tmpfmt);
		ast_format_copy(&sub->owner->writeformat, &tmpfmt);
	}
	codec = ast_rtp_codecs_payload_code(ast_rtp_instance_get_codecs(sub->rtp), 1, &sub->owner->readformat, 0);
	/* Setting up RTP of the phone */
	if (public_ip.sin_family == 0)  /* NAT IP override ?   */
		memcpy(&public, &us, sizeof(public));   /* No defined, using IP from recvmsg  */
	else
		memcpy(&public, &public_ip, sizeof(public));    /* override  */
	if (unistimdebug) {
		ast_verb(0, "RTP started : Our IP/port is : %s:%hd with codec %s\n",
			 ast_inet_ntoa(us.sin_addr),
			 htons(us.sin_port), ast_getformatname(&sub->owner->readformat));
		ast_verb(0, "Starting phone RTP stack. Our public IP is %s\n",
					ast_inet_ntoa(public.sin_addr));
	}
	if ((sub->owner->readformat.id == AST_FORMAT_ULAW) ||
		(sub->owner->readformat.id == AST_FORMAT_ALAW)) {
		if (unistimdebug)
			ast_verb(0, "Sending packet_send_rtp_packet_size for codec %d\n", codec);
		memcpy(buffsend + SIZE_HEADER, packet_send_rtp_packet_size,
			   sizeof(packet_send_rtp_packet_size));
		buffsend[10] = (int) codec & 0xffffffffLL;
		send_client(SIZE_HEADER + sizeof(packet_send_rtp_packet_size), buffsend,
				   sub->parent->parent->session);
	}
	if (unistimdebug)
		ast_verb(0, "Sending Jitter Buffer Parameters Configuration\n");
	memcpy(buffsend + SIZE_HEADER, packet_send_jitter_buffer_conf,
		   sizeof(packet_send_jitter_buffer_conf));
	send_client(SIZE_HEADER + sizeof(packet_send_jitter_buffer_conf), buffsend,
			   sub->parent->parent->session);
	if (sub->parent->parent->rtp_method != 0) {
		uint16_t rtcpsin_port = htons(us.sin_port) + 1; /* RTCP port is RTP + 1 */

		if (unistimdebug)
			ast_verb(0, "Sending OpenAudioStreamTX using method #%d\n",
						sub->parent->parent->rtp_method);
		if (sub->parent->parent->rtp_method == 3)
			memcpy(buffsend + SIZE_HEADER, packet_send_open_audio_stream_tx3,
				   sizeof(packet_send_open_audio_stream_tx3));
		else
			memcpy(buffsend + SIZE_HEADER, packet_send_open_audio_stream_tx,
				   sizeof(packet_send_open_audio_stream_tx));
		if (sub->parent->parent->rtp_method != 2) {
			memcpy(buffsend + 28, &public.sin_addr, sizeof(public.sin_addr));
			buffsend[20] = (htons(sin.sin_port) & 0xff00) >> 8;
			buffsend[21] = (htons(sin.sin_port) & 0x00ff);
			buffsend[23] = (rtcpsin_port & 0x00ff);
			buffsend[22] = (rtcpsin_port & 0xff00) >> 8;
			buffsend[25] = (us.sin_port & 0xff00) >> 8;
			buffsend[24] = (us.sin_port & 0x00ff);
			buffsend[27] = (rtcpsin_port & 0x00ff);
			buffsend[26] = (rtcpsin_port & 0xff00) >> 8;
		} else {
			memcpy(buffsend + 23, &public.sin_addr, sizeof(public.sin_addr));
			buffsend[15] = (htons(sin.sin_port) & 0xff00) >> 8;
			buffsend[16] = (htons(sin.sin_port) & 0x00ff);
			buffsend[20] = (us.sin_port & 0xff00) >> 8;
			buffsend[19] = (us.sin_port & 0x00ff);
			buffsend[11] = codec;
		}
		buffsend[12] = codec;
		send_client(SIZE_HEADER + sizeof(packet_send_open_audio_stream_tx), buffsend,
				   sub->parent->parent->session);

		if (unistimdebug)
			ast_verb(0, "Sending OpenAudioStreamRX\n");
		if (sub->parent->parent->rtp_method == 3)
			memcpy(buffsend + SIZE_HEADER, packet_send_open_audio_stream_rx3,
				   sizeof(packet_send_open_audio_stream_rx3));
		else
			memcpy(buffsend + SIZE_HEADER, packet_send_open_audio_stream_rx,
				   sizeof(packet_send_open_audio_stream_rx));
		if (sub->parent->parent->rtp_method != 2) {
			memcpy(buffsend + 28, &public.sin_addr, sizeof(public.sin_addr));
			buffsend[20] = (htons(sin.sin_port) & 0xff00) >> 8;
			buffsend[21] = (htons(sin.sin_port) & 0x00ff);
			buffsend[23] = (rtcpsin_port & 0x00ff);
			buffsend[22] = (rtcpsin_port & 0xff00) >> 8;
			buffsend[25] = (us.sin_port & 0xff00) >> 8;
			buffsend[24] = (us.sin_port & 0x00ff);
			buffsend[27] = (rtcpsin_port & 0x00ff);
			buffsend[26] = (rtcpsin_port & 0xff00) >> 8;
		} else {
			memcpy(buffsend + 23, &public.sin_addr, sizeof(public.sin_addr));
			buffsend[15] = (htons(sin.sin_port) & 0xff00) >> 8;
			buffsend[16] = (htons(sin.sin_port) & 0x00ff);
			buffsend[20] = (us.sin_port & 0xff00) >> 8;
			buffsend[19] = (us.sin_port & 0x00ff);
			buffsend[12] = codec;
		}
		buffsend[11] = codec;
		send_client(SIZE_HEADER + sizeof(packet_send_open_audio_stream_rx), buffsend,
				   sub->parent->parent->session);
	} else {
		uint16_t rtcpsin_port = htons(us.sin_port) + 1; /* RTCP port is RTP + 1 */

		if (unistimdebug)
			ast_verb(0, "Sending packet_send_call default method\n");

		memcpy(buffsend + SIZE_HEADER, packet_send_call, sizeof(packet_send_call));
		memcpy(buffsend + 53, &public.sin_addr, sizeof(public.sin_addr));
		/* Destination port when sending RTP */
		buffsend[49] = (us.sin_port & 0x00ff);
		buffsend[50] = (us.sin_port & 0xff00) >> 8;
		/* Destination port when sending RTCP */
		buffsend[52] = (rtcpsin_port & 0x00ff);
		buffsend[51] = (rtcpsin_port & 0xff00) >> 8;
		/* Codec */
		buffsend[40] = codec;
		buffsend[41] = codec;
		if (sub->owner->readformat.id == AST_FORMAT_ULAW)
			buffsend[42] = 1;       /* 1 = 20ms (160 bytes), 2 = 40ms (320 bytes) */
		else if (sub->owner->readformat.id == AST_FORMAT_ALAW)
			buffsend[42] = 1;       /* 1 = 20ms (160 bytes), 2 = 40ms (320 bytes) */
		else if (sub->owner->readformat.id == AST_FORMAT_G723_1)
			buffsend[42] = 2;       /* 1 = 30ms (24 bytes), 2 = 60 ms (48 bytes) */
		else if (sub->owner->readformat.id == AST_FORMAT_G729A)
			buffsend[42] = 2;       /* 1 = 10ms (10 bytes), 2 = 20ms (20 bytes) */
		else
			ast_log(LOG_WARNING, "Unsupported codec %s!\n",
					ast_getformatname(&sub->owner->readformat));
		/* Source port for transmit RTP and Destination port for receiving RTP */
		buffsend[45] = (htons(sin.sin_port) & 0xff00) >> 8;
		buffsend[46] = (htons(sin.sin_port) & 0x00ff);
		buffsend[47] = (rtcpsin_port & 0xff00) >> 8;
		buffsend[48] = (rtcpsin_port & 0x00ff);
		send_client(SIZE_HEADER + sizeof(packet_send_call), buffsend,
				   sub->parent->parent->session);
	}
	ast_mutex_unlock(&sub->lock);
}

static void SendDialTone(struct unistimsession *pte)
{
	int i;
	/* No country defined ? Using US tone */
	if (ast_strlen_zero(pte->device->country)) {
		if (unistimdebug)
			ast_verb(0, "No country defined, using US tone\n");
		send_tone(pte, 350, 440);
		return;
	}
	if (strlen(pte->device->country) != 2) {
		if (unistimdebug)
			ast_verb(0, "Country code != 2 char, using US tone\n");
		send_tone(pte, 350, 440);
		return;
	}
	i = 0;
	while (frequency[i].freq1) {
		if ((frequency[i].country[0] == pte->device->country[0]) &&
			(frequency[i].country[1] == pte->device->country[1])) {
			if (unistimdebug)
				ast_verb(0, "Country code found (%s), freq1=%d freq2=%d\n",
							frequency[i].country, frequency[i].freq1, frequency[i].freq2);
			send_tone(pte, frequency[i].freq1, frequency[i].freq2);
		}
		i++;
	}
}

static void handle_dial_page(struct unistimsession *pte)
{
	pte->state = STATE_DIALPAGE;
	if (pte->device->call_forward[0] == -1) {
		send_text(TEXT_LINE0, TEXT_NORMAL, pte, "");
		send_text(TEXT_LINE1, TEXT_NORMAL, pte, "Enter forward");
		send_text_status(pte, "ForwardCancel BackSpcErase");
		if (pte->device->call_forward[1] != 0) {
			char tmp[TEXT_LENGTH_MAX + 1];

			ast_copy_string(pte->device->phone_number, pte->device->call_forward + 1,
							sizeof(pte->device->phone_number));
			pte->device->size_phone_number = strlen(pte->device->phone_number);
			if (pte->device->size_phone_number > 15)
				pte->device->size_phone_number = 15;
			strcpy(tmp, "Number : ...............");
			memcpy(tmp + 9, pte->device->phone_number, pte->device->size_phone_number);

			if (pte->device->height == 1) {
				send_text(TEXT_LINE0, TEXT_NORMAL, pte, tmp);
				send_blink_cursor(pte);
				send_cursor_pos(pte,
						  (unsigned char) (TEXT_LINE0 + 0x09 +
										   pte->device->size_phone_number));
			} else {
				send_text(TEXT_LINE2, TEXT_NORMAL, pte, tmp);
				send_blink_cursor(pte);
				send_cursor_pos(pte,
						  (unsigned char) (TEXT_LINE2 + 0x09 +
										   pte->device->size_phone_number));
			}

			send_led_update(pte, 0);
			return;
		}
	} else {
		if ((pte->device->output == OUTPUT_HANDSET) &&
			(pte->device->receiver_state == STATE_ONHOOK))
			send_select_output(pte, OUTPUT_SPEAKER, pte->device->volume, MUTE_OFF);
		else
			send_select_output(pte, pte->device->output, pte->device->volume, MUTE_OFF);
		SendDialTone(pte);

		if (pte->device->height > 1) {
			send_text(TEXT_LINE0, TEXT_NORMAL, pte, "Enter the number to dial");
			send_text(TEXT_LINE1, TEXT_NORMAL, pte, "and press Call");
		}
		send_text_status(pte, "Call   Redial BackSpcErase");
	}

	if (pte->device->height == 1) {
		send_text(TEXT_LINE0, TEXT_NORMAL, pte, "Number : ...............");
		send_blink_cursor(pte);
		send_cursor_pos(pte, TEXT_LINE0 + 0x09);
	} else {
		send_text(TEXT_LINE2, TEXT_NORMAL, pte, "Number : ...............");
		send_blink_cursor(pte);
		send_cursor_pos(pte, TEXT_LINE2 + 0x09);
	}
	pte->device->size_phone_number = 0;
	pte->device->phone_number[0] = 0;
	change_favorite_icon(pte, FAV_ICON_PHONE_BLACK);
	Sendicon(TEXT_LINE0, FAV_ICON_NONE, pte);
	pte->device->missed_call = 0;
	send_led_update(pte, 0);
	return;
}

/* Step 1 : Music On Hold for peer, Dialing screen for us */
static void TransferCallStep1(struct unistimsession *pte)
{
	struct unistim_subchannel *sub;
	struct unistim_line *p = pte->device->lines;

	sub = p->subs[SUB_REAL];

	if (!sub->owner) {
		ast_log(LOG_WARNING, "Unable to find subchannel for music on hold\n");
		return;
	}
	if (p->subs[SUB_THREEWAY]) {
		if (unistimdebug)
			ast_verb(0, "Transfer canceled, hangup our threeway channel\n");
		if (p->subs[SUB_THREEWAY]->owner)
			ast_queue_hangup_with_cause(p->subs[SUB_THREEWAY]->owner, AST_CAUSE_NORMAL_CLEARING);
		else
			ast_log(LOG_WARNING, "Canceling a threeway channel without owner\n");
		return;
	}
	/* Start music on hold if appropriate */
	if (pte->device->moh)
		ast_log(LOG_WARNING, "Transfer with peer already listening music on hold\n");
	else {
		if (ast_bridged_channel(p->subs[SUB_REAL]->owner)) {
			ast_moh_start(ast_bridged_channel(p->subs[SUB_REAL]->owner),
						  pte->device->lines->musicclass, NULL);
			pte->device->moh = 1;
		} else {
			ast_log(LOG_WARNING, "Unable to find peer subchannel for music on hold\n");
			return;
		}
	}
	/* Silence our channel */
	if (!pte->device->silence_generator) {
		pte->device->silence_generator =
			ast_channel_start_silence_generator(p->subs[SUB_REAL]->owner);
		if (pte->device->silence_generator == NULL)
			ast_log(LOG_WARNING, "Unable to start a silence generator.\n");
		else if (unistimdebug)
			ast_verb(0, "Starting silence generator\n");
	}
	handle_dial_page(pte);
}

/* From phone to PBX */
static void HandleCallOutgoing(struct unistimsession *s)
{
	struct ast_channel *c;
	struct unistim_subchannel *sub;
	pthread_t t;
	s->state = STATE_CALL;
	sub = s->device->lines->subs[SUB_REAL];
	if (!sub) {
		ast_log(LOG_NOTICE, "No available lines on: %s\n", s->device->name);
		return;
	}
	if (!sub->owner) {		      /* A call is already in progress ? */
		c = unistim_new(sub, AST_STATE_DOWN, NULL);   /* No, starting a new one */
		if (c) {
			/* Need to start RTP before calling ast_pbx_run */
			if (!sub->rtp)
				start_rtp(sub);
			send_select_output(s, s->device->output, s->device->volume, MUTE_OFF);

			if (s->device->height == 1) {
				send_text(TEXT_LINE0, TEXT_NORMAL, s, s->device->phone_number);
			} else {
				send_text(TEXT_LINE0, TEXT_NORMAL, s, "Calling :");
				send_text(TEXT_LINE1, TEXT_NORMAL, s, s->device->phone_number);
				send_text(TEXT_LINE2, TEXT_NORMAL, s, "Dialing...");
			}
			send_text_status(s, "Hangup");

			/* start switch */
			if (ast_pthread_create(&t, NULL, unistim_ss, c)) {
				display_last_error("Unable to create switch thread");
				ast_queue_hangup_with_cause(c, AST_CAUSE_SWITCH_CONGESTION);
			}
		} else
			ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n",
					sub->parent->name, s->device->name);
	} else {					/* We already have a call, so we switch in a threeway call */

		if (s->device->moh) {
			struct unistim_subchannel *subchannel;
			struct unistim_line *p = s->device->lines;
			subchannel = p->subs[SUB_REAL];

			if (!subchannel->owner) {
				ast_log(LOG_WARNING, "Unable to find subchannel for music on hold\n");
				return;
			}
			if (p->subs[SUB_THREEWAY]) {
				ast_log(LOG_WARNING,
						"Can't transfer while an another transfer is taking place\n");
				return;
			}
			if (!alloc_sub(p, SUB_THREEWAY)) {
				ast_log(LOG_WARNING, "Unable to allocate three-way subchannel\n");
				return;
			}
			/* Stop the silence generator */
			if (s->device->silence_generator) {
				if (unistimdebug)
					ast_verb(0, "Stopping silence generator\n");
				ast_channel_stop_silence_generator(subchannel->owner,
												   s->device->silence_generator);
				s->device->silence_generator = NULL;
			}
			send_tone(s, 0, 0);
			/* Make new channel */
			c = unistim_new(p->subs[SUB_THREEWAY], AST_STATE_DOWN, NULL);
			if (!c) {
				ast_log(LOG_WARNING, "Cannot allocate new structure on channel %p\n", p);
				return;
			}
			/* Swap things around between the three-way and real call */
			swap_subs(p, SUB_THREEWAY, SUB_REAL);
			send_select_output(s, s->device->output, s->device->volume, MUTE_OFF);

			if (s->device->height == 1) {
				send_text(TEXT_LINE0, TEXT_NORMAL, s, s->device->phone_number);
			} else {
				send_text(TEXT_LINE0, TEXT_NORMAL, s, "Calling (pre-transfer)");
				send_text(TEXT_LINE1, TEXT_NORMAL, s, s->device->phone_number);
				send_text(TEXT_LINE2, TEXT_NORMAL, s, "Dialing...");
			}
			send_text_status(s, "TransfrCancel");

			if (ast_pthread_create(&t, NULL, unistim_ss, p->subs[SUB_THREEWAY]->owner)) {
				ast_log(LOG_WARNING, "Unable to start simple switch on channel %p\n", p);
				ast_hangup(c);
				return;
			}
			if (unistimdebug)
				ast_verb(0, "Started three way call on channel %p (%s) subchan %d\n",
					 p->subs[SUB_THREEWAY]->owner, p->subs[SUB_THREEWAY]->owner->name,
					 p->subs[SUB_THREEWAY]->subtype);
		} else
			ast_debug(1, "Current sub [%s] already has owner\n", sub->owner->name);
	}
	return;
}

/* From PBX to phone */
static void HandleCallIncoming(struct unistimsession *s)
{
	struct unistim_subchannel *sub;
	s->state = STATE_CALL;
	s->device->missed_call = 0;
	send_no_ring(s);
	sub = s->device->lines->subs[SUB_REAL];
	if (!sub) {
		ast_log(LOG_NOTICE, "No available lines on: %s\n", s->device->name);
		return;
	} else if (unistimdebug)
		ast_verb(0, "Handle Call Incoming for %s@%s\n", sub->parent->name,
					s->device->name);
	start_rtp(sub);
	if (!sub->rtp)
		ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", sub->parent->name,
				s->device->name);
	ast_queue_control(sub->owner, AST_CONTROL_ANSWER);
	send_text(TEXT_LINE2, TEXT_NORMAL, s, "is on-line");
	send_text_status(s, "Hangup Transf");
	send_start_timer(s);

	if ((s->device->output == OUTPUT_HANDSET) &&
		(s->device->receiver_state == STATE_ONHOOK))
		send_select_output(s, OUTPUT_SPEAKER, s->device->volume, MUTE_OFF);
	else
		send_select_output(s, s->device->output, s->device->volume, MUTE_OFF);
	s->device->start_call_timestamp = time(0);
	write_history(s, 'i', 0);
	return;
}

static int unistim_do_senddigit(struct unistimsession *pte, char digit)
{
	struct ast_frame f = { .frametype = AST_FRAME_DTMF, .subclass.integer = digit, .src = "unistim" };
	struct unistim_subchannel *sub;
	sub = pte->device->lines->subs[SUB_REAL];
	if (!sub->owner || sub->alreadygone) {
		ast_log(LOG_WARNING, "Unable to find subchannel in dtmf senddigit\n");
		return -1;
	}

	/* Send DTMF indication _before_ playing sounds */
	ast_queue_frame(sub->owner, &f);

	if (unistimdebug)
		ast_verb(0, "Send Digit %c\n", digit);
	switch (digit) {
	case '0':
		send_tone(pte, 941, 1336);
		break;
	case '1':
		send_tone(pte, 697, 1209);
		break;
	case '2':
		send_tone(pte, 697, 1336);
		break;
	case '3':
		send_tone(pte, 697, 1477);
		break;
	case '4':
		send_tone(pte, 770, 1209);
		break;
	case '5':
		send_tone(pte, 770, 1336);
		break;
	case '6':
		send_tone(pte, 770, 1477);
		break;
	case '7':
		send_tone(pte, 852, 1209);
		break;
	case '8':
		send_tone(pte, 852, 1336);
		break;
	case '9':
		send_tone(pte, 852, 1477);
		break;
	case 'A':
		send_tone(pte, 697, 1633);
		break;
	case 'B':
		send_tone(pte, 770, 1633);
		break;
	case 'C':
		send_tone(pte, 852, 1633);
		break;
	case 'D':
		send_tone(pte, 941, 1633);
		break;
	case '*':
		send_tone(pte, 941, 1209);
		break;
	case '#':
		send_tone(pte, 941, 1477);
		break;
	default:
		send_tone(pte, 500, 2000);
	}
	usleep(150000);			 /* XXX Less than perfect, blocking an important thread is not a good idea */
	send_tone(pte, 0, 0);
	return 0;
}

static void key_call(struct unistimsession *pte, char keycode)
{
	if ((keycode >= KEY_0) && (keycode <= KEY_SHARP)) {
		if (keycode == KEY_SHARP)
			keycode = '#';
		else if (keycode == KEY_STAR)
			keycode = '*';
		else
			keycode -= 0x10;
		unistim_do_senddigit(pte, keycode);
		return;
	}
	switch (keycode) {
	case KEY_HANGUP:
	case KEY_FUNC1:
		close_call(pte);
		break;
	case KEY_FUNC2:
		TransferCallStep1(pte);
		break;
	case KEY_HEADPHN:
		if (pte->device->output == OUTPUT_HEADPHONE)
			send_select_output(pte, OUTPUT_HANDSET, pte->device->volume, MUTE_OFF);
		else
			send_select_output(pte, OUTPUT_HEADPHONE, pte->device->volume, MUTE_OFF);
		break;
	case KEY_LOUDSPK:
		if (pte->device->output != OUTPUT_SPEAKER)
			send_select_output(pte, OUTPUT_SPEAKER, pte->device->volume, MUTE_OFF);
		else
			send_select_output(pte, pte->device->previous_output, pte->device->volume,
							 MUTE_OFF);
		break;
	case KEY_MUTE:
		if (!pte->device->moh) {
			if (pte->device->mute == MUTE_ON)
				send_select_output(pte, pte->device->output, pte->device->volume, MUTE_OFF);
			else
				send_select_output(pte, pte->device->output, pte->device->volume, MUTE_ON);
			break;
		}
	case KEY_ONHOLD:
		{
			struct unistim_subchannel *sub;
			struct ast_channel *bridgepeer = NULL;
			sub = pte->device->lines->subs[SUB_REAL];
			if (!sub->owner) {
				ast_log(LOG_WARNING, "Unable to find subchannel for music on hold\n");
				return;
			}
			if ((bridgepeer = ast_bridged_channel(sub->owner))) {
				if (pte->device->moh) {
					ast_moh_stop(bridgepeer);
					pte->device->moh = 0;
					send_select_output(pte, pte->device->output, pte->device->volume,
									 MUTE_OFF);
				} else {
					ast_moh_start(bridgepeer, pte->device->lines->musicclass, NULL);
					pte->device->moh = 1;
					send_select_output(pte, pte->device->output, pte->device->volume,
									 MUTE_ON);
				}
			} else
				ast_log(LOG_WARNING,
						"Unable to find peer subchannel for music on hold\n");
			break;
		}
	}
	return;
}

static void key_ringing(struct unistimsession *pte, char keycode)
{
	if (keycode == KEY_FAV0 + pte->device->softkeylinepos) {
		HandleCallIncoming(pte);
		return;
	}
	switch (keycode) {
	case KEY_HANGUP:
	case KEY_FUNC4:
		IgnoreCall(pte);
		break;
	case KEY_FUNC1:
		HandleCallIncoming(pte);
		break;
	}
	return;
}

static void Keyfavorite(struct unistimsession *pte, char keycode)
{
	int fav;

	if ((keycode < KEY_FAV1) && (keycode > KEY_FAV5)) {
		ast_log(LOG_WARNING, "It's not a favorite key\n");
		return;
	}
	if (keycode == KEY_FAV0)
		return;
	fav = keycode - KEY_FAV0;
	if (pte->device->softkeyicon[fav] == 0)
		return;
	ast_copy_string(pte->device->phone_number, pte->device->softkeynumber[fav],
					sizeof(pte->device->phone_number));
	HandleCallOutgoing(pte);
	return;
}

static void key_dial_page(struct unistimsession *pte, char keycode)
{
	if (keycode == KEY_FUNC3) {
		if (pte->device->size_phone_number <= 1)
			keycode = KEY_FUNC4;
		else {
			pte->device->size_phone_number -= 2;
			keycode = pte->device->phone_number[pte->device->size_phone_number] + 0x10;
		}
	}
	if ((keycode >= KEY_0) && (keycode <= KEY_SHARP)) {
		char tmpbuf[] = "Number : ...............";
		int i = 0;

		if (pte->device->size_phone_number >= 15)
			return;
		if (pte->device->size_phone_number == 0)
			send_tone(pte, 0, 0);
		while (i < pte->device->size_phone_number) {
			tmpbuf[i + 9] = pte->device->phone_number[i];
			i++;
		}
		if (keycode == KEY_SHARP)
			keycode = '#';
		else if (keycode == KEY_STAR)
			keycode = '*';
		else
			keycode -= 0x10;
		tmpbuf[i + 9] = keycode;
		pte->device->phone_number[i] = keycode;
		pte->device->size_phone_number++;
		pte->device->phone_number[i + 1] = 0;
		if (pte->device->height == 1) {
			send_text(TEXT_LINE0, TEXT_NORMAL, pte, tmpbuf);
		} else {
			send_text(TEXT_LINE2, TEXT_NORMAL, pte, tmpbuf);
		}
		send_blink_cursor(pte);
		send_cursor_pos(pte, (unsigned char) (TEXT_LINE2 + 0x0a + i));
		return;
	}
	if (keycode == KEY_FUNC4) {

		pte->device->size_phone_number = 0;
		if (pte->device->height == 1) {
			send_text(TEXT_LINE0, TEXT_NORMAL, pte, "Number : ...............");
			send_blink_cursor(pte);
			send_cursor_pos(pte, TEXT_LINE0 + 0x09);
		} else {
			send_text(TEXT_LINE2, TEXT_NORMAL, pte, "Number : ...............");
			send_blink_cursor(pte);
			send_cursor_pos(pte, TEXT_LINE2 + 0x09);
		}
		return;
	}

	if (pte->device->call_forward[0] == -1) {
		if (keycode == KEY_FUNC1) {
			ast_copy_string(pte->device->call_forward, pte->device->phone_number,
							sizeof(pte->device->call_forward));
			show_main_page(pte);
		} else if ((keycode == KEY_FUNC2) || (keycode == KEY_HANGUP)) {
			pte->device->call_forward[0] = '\0';
			show_main_page(pte);
		}
		return;
	}
	switch (keycode) {
	case KEY_FUNC2:
		if (ast_strlen_zero(pte->device->redial_number))
			break;
		ast_copy_string(pte->device->phone_number, pte->device->redial_number,
						sizeof(pte->device->phone_number));
	case KEY_FUNC1:
		HandleCallOutgoing(pte);
		break;
	case KEY_HANGUP:
		if (pte->device->lines->subs[SUB_REAL]->owner) {
			/* Stop the silence generator */
			if (pte->device->silence_generator) {
				if (unistimdebug)
					ast_verb(0, "Stopping silence generator\n");
				ast_channel_stop_silence_generator(pte->device->lines->subs[SUB_REAL]->
												   owner, pte->device->silence_generator);
				pte->device->silence_generator = NULL;
			}
			send_tone(pte, 0, 0);
			ast_moh_stop(ast_bridged_channel(pte->device->lines->subs[SUB_REAL]->owner));
			pte->device->moh = 0;
			pte->state = STATE_CALL;

			if (pte->device->height == 1) {
				send_text(TEXT_LINE0, TEXT_NORMAL, pte, "Dial Cancel,back to priv. call.");
			} else {
				send_text(TEXT_LINE0, TEXT_NORMAL, pte, "Dialing canceled,");
				send_text(TEXT_LINE1, TEXT_NORMAL, pte, "switching back to");
				send_text(TEXT_LINE2, TEXT_NORMAL, pte, "previous call.");
			}
			send_text_status(pte, "Hangup Transf");
		} else
			show_main_page(pte);
		break;
	case KEY_FAV1:
	case KEY_FAV2:
	case KEY_FAV3:
	case KEY_FAV4:
	case KEY_FAV5:
		Keyfavorite(pte, keycode);
		break;
	case KEY_LOUDSPK:
		if (pte->device->output == OUTPUT_SPEAKER) {
			if (pte->device->receiver_state == STATE_OFFHOOK)
				send_select_output(pte, pte->device->previous_output, pte->device->volume,
								 MUTE_OFF);
			else
				show_main_page(pte);
		} else
			send_select_output(pte, OUTPUT_SPEAKER, pte->device->volume, MUTE_OFF);
		break;
	case KEY_HEADPHN:
		if (pte->device->output == OUTPUT_HEADPHONE) {
			if (pte->device->receiver_state == STATE_OFFHOOK)
				send_select_output(pte, OUTPUT_HANDSET, pte->device->volume, MUTE_OFF);
			else
				show_main_page(pte);
		} else
			send_select_output(pte, OUTPUT_HEADPHONE, pte->device->volume, MUTE_OFF);
		break;
	}
	return;
}

#define SELECTCODEC_START_ENTRY_POS 15
#define SELECTCODEC_MAX_LENGTH 2
#define SELECTCODEC_MSG "Codec number : .."
static void HandleSelectCodec(struct unistimsession *pte)
{
	char buf[30], buf2[5];

	pte->state = STATE_SELECTCODEC;
	strcpy(buf, "Using codec ");
	sprintf(buf2, "%d", pte->device->codec_number);
	strcat(buf, buf2);
	strcat(buf, " (G711u=0,");

	send_text(TEXT_LINE0, TEXT_NORMAL, pte, buf);
	send_text(TEXT_LINE1, TEXT_NORMAL, pte, "G723=4,G711a=8,G729A=18)");
	send_text(TEXT_LINE2, TEXT_INVERSE, pte, SELECTCODEC_MSG);
	send_blink_cursor(pte);
	send_cursor_pos(pte, TEXT_LINE2 + SELECTCODEC_START_ENTRY_POS);
	pte->size_buff_entry = 0;
	send_text_status(pte, "Select BackSpcErase  Cancel");
	return;
}

static void key_select_codec(struct unistimsession *pte, char keycode)
{
	if (keycode == KEY_FUNC2) {
		if (pte->size_buff_entry <= 1)
			keycode = KEY_FUNC3;
		else {
			pte->size_buff_entry -= 2;
			keycode = pte->buff_entry[pte->size_buff_entry] + 0x10;
		}
	}
	if ((keycode >= KEY_0) && (keycode <= KEY_9)) {
		char tmpbuf[] = SELECTCODEC_MSG;
		int i = 0;

		if (pte->size_buff_entry >= SELECTCODEC_MAX_LENGTH)
			return;

		while (i < pte->size_buff_entry) {
			tmpbuf[i + SELECTCODEC_START_ENTRY_POS] = pte->buff_entry[i];
			i++;
		}
		tmpbuf[i + SELECTCODEC_START_ENTRY_POS] = keycode - 0x10;
		pte->buff_entry[i] = keycode - 0x10;
		pte->size_buff_entry++;
		send_text(TEXT_LINE2, TEXT_INVERSE, pte, tmpbuf);
		send_blink_cursor(pte);
		send_cursor_pos(pte,
					  (unsigned char) (TEXT_LINE2 + SELECTCODEC_START_ENTRY_POS + 1 + i));
		return;
	}

	switch (keycode) {
	case KEY_FUNC1:
		if (pte->size_buff_entry == 1)
			pte->device->codec_number = pte->buff_entry[0] - 48;
		else if (pte->size_buff_entry == 2)
			pte->device->codec_number =
				((pte->buff_entry[0] - 48) * 10) + (pte->buff_entry[1] - 48);
		show_main_page(pte);
		break;
	case KEY_FUNC3:
		pte->size_buff_entry = 0;
		send_text(TEXT_LINE2, TEXT_INVERSE, pte, SELECTCODEC_MSG);
		send_blink_cursor(pte);
		send_cursor_pos(pte, TEXT_LINE2 + SELECTCODEC_START_ENTRY_POS);
		break;
	case KEY_HANGUP:
	case KEY_FUNC4:
		show_main_page(pte);
		break;
	}
	return;
}

#define SELECTEXTENSION_START_ENTRY_POS 0
#define SELECTEXTENSION_MAX_LENGTH 10
#define SELECTEXTENSION_MSG ".........."
static void ShowExtensionPage(struct unistimsession *pte)
{
	pte->state = STATE_EXTENSION;

	send_text(TEXT_LINE0, TEXT_NORMAL, pte, "Please enter a Terminal");
	send_text(TEXT_LINE1, TEXT_NORMAL, pte, "Number (TN) :");
	send_text(TEXT_LINE2, TEXT_NORMAL, pte, SELECTEXTENSION_MSG);
	send_blink_cursor(pte);
	send_cursor_pos(pte, TEXT_LINE2 + SELECTEXTENSION_START_ENTRY_POS);
	send_text_status(pte, "Enter  BackSpcErase");
	pte->size_buff_entry = 0;
	return;
}

static void key_select_extension(struct unistimsession *pte, char keycode)
{
	if (keycode == KEY_FUNC2) {
		if (pte->size_buff_entry <= 1)
			keycode = KEY_FUNC3;
		else {
			pte->size_buff_entry -= 2;
			keycode = pte->buff_entry[pte->size_buff_entry] + 0x10;
		}
	}
	if ((keycode >= KEY_0) && (keycode <= KEY_9)) {
		char tmpbuf[] = SELECTEXTENSION_MSG;
		int i = 0;

		if (pte->size_buff_entry >= SELECTEXTENSION_MAX_LENGTH)
			return;

		while (i < pte->size_buff_entry) {
			tmpbuf[i + SELECTEXTENSION_START_ENTRY_POS] = pte->buff_entry[i];
			i++;
		}
		tmpbuf[i + SELECTEXTENSION_START_ENTRY_POS] = keycode - 0x10;
		pte->buff_entry[i] = keycode - 0x10;
		pte->size_buff_entry++;
		send_text(TEXT_LINE2, TEXT_NORMAL, pte, tmpbuf);
		send_blink_cursor(pte);
		send_cursor_pos(pte,
					  (unsigned char) (TEXT_LINE2 + SELECTEXTENSION_START_ENTRY_POS + 1 +
									   i));
		return;
	}

	switch (keycode) {
	case KEY_FUNC1:
		if (pte->size_buff_entry < 1)
			return;
		if (autoprovisioning == AUTOPROVISIONING_TN) {
			struct unistim_device *d;

			/* First step : looking for this TN in our device list */
			ast_mutex_lock(&devicelock);
			d = devices;
			pte->buff_entry[pte->size_buff_entry] = '\0';
			while (d) {
				if (d->id[0] == 'T') {  /* It's a TN device ? */
					/* It's the TN we're looking for ? */
					if (!strcmp((d->id) + 1, pte->buff_entry)) {
						pte->device = d;
						d->session = pte;
						d->codec_number = DEFAULT_CODEC;
						d->pos_fav = 0;
						d->missed_call = 0;
						d->receiver_state = STATE_ONHOOK;
						strcpy(d->id, pte->macaddr);
						pte->device->extension_number[0] = 'T';
						pte->device->extension = EXTENSION_TN;
						ast_copy_string((pte->device->extension_number) + 1,
										pte->buff_entry, pte->size_buff_entry + 1);
						ast_mutex_unlock(&devicelock);
						show_main_page(pte);
						refresh_all_favorite(pte);
						return;
					}
				}
				d = d->next;
			}
			ast_mutex_unlock(&devicelock);
			send_text(TEXT_LINE0, TEXT_NORMAL, pte, "Invalid Terminal Number.");
			send_text(TEXT_LINE1, TEXT_NORMAL, pte, "Please try again :");
			send_cursor_pos(pte,
						  (unsigned char) (TEXT_LINE2 + SELECTEXTENSION_START_ENTRY_POS +
										   pte->size_buff_entry));
			send_blink_cursor(pte);
		} else {
			ast_copy_string(pte->device->extension_number, pte->buff_entry,
							pte->size_buff_entry + 1);
			if (RegisterExtension(pte)) {
				send_text(TEXT_LINE0, TEXT_NORMAL, pte, "Invalid extension.");
				send_text(TEXT_LINE1, TEXT_NORMAL, pte, "Please try again :");
				send_cursor_pos(pte,
							  (unsigned char) (TEXT_LINE2 +
											   SELECTEXTENSION_START_ENTRY_POS +
											   pte->size_buff_entry));
				send_blink_cursor(pte);
			} else
				show_main_page(pte);
		}
		break;
	case KEY_FUNC3:
		pte->size_buff_entry = 0;
		send_text(TEXT_LINE2, TEXT_NORMAL, pte, SELECTEXTENSION_MSG);
		send_blink_cursor(pte);
		send_cursor_pos(pte, TEXT_LINE2 + SELECTEXTENSION_START_ENTRY_POS);
		break;
	}
	return;
}

static int ReformatNumber(char *number)
{
	int pos = 0, i = 0, size = strlen(number);

	for (; i < size; i++) {
		if ((number[i] >= '0') && (number[i] <= '9')) {
			if (i == pos) {
				pos++;
				continue;
			}
			number[pos] = number[i];
			pos++;
		}
	}
	number[pos] = 0;
	return pos;
}

static void show_entry_history(struct unistimsession *pte, FILE ** f)
{
	char line[TEXT_LENGTH_MAX + 1], status[STATUS_LENGTH_MAX + 1], func1[10], func2[10],
		func3[10];

	if (fread(line, TEXT_LENGTH_MAX, 1, *f) != 1) {
		display_last_error("Can't read history date entry");
		fclose(*f);
		return;
	}
	line[sizeof(line) - 1] = '\0';
	send_text(TEXT_LINE0, TEXT_NORMAL, pte, line);
	if (fread(line, TEXT_LENGTH_MAX, 1, *f) != 1) {
		display_last_error("Can't read callerid entry");
		fclose(*f);
		return;
	}
	line[sizeof(line) - 1] = '\0';
	ast_copy_string(pte->device->lst_cid, line, sizeof(pte->device->lst_cid));
	send_text(TEXT_LINE1, TEXT_NORMAL, pte, line);
	if (fread(line, TEXT_LENGTH_MAX, 1, *f) != 1) {
		display_last_error("Can't read callername entry");
		fclose(*f);
		return;
	}
	line[sizeof(line) - 1] = '\0';
	send_text(TEXT_LINE2, TEXT_NORMAL, pte, line);
	fclose(*f);

	snprintf(line, sizeof(line), "Call %03d/%03d", pte->buff_entry[2],
			 pte->buff_entry[1]);
	send_texttitle(pte, line);

	if (pte->buff_entry[2] == 1)
		strcpy(func1, "       ");
	else
		strcpy(func1, "Prvious");
	if (pte->buff_entry[2] >= pte->buff_entry[1])
		strcpy(func2, "       ");
	else
		strcpy(func2, "Next   ");
	if (ReformatNumber(pte->device->lst_cid))
		strcpy(func3, "Redial ");
	else
		strcpy(func3, "       ");
	snprintf(status, sizeof(status), "%s%s%sCancel", func1, func2, func3);
	send_text_status(pte, status);
}

static char OpenHistory(struct unistimsession *pte, char way, FILE ** f)
{
	char tmp[AST_CONFIG_MAX_PATH];
	char count;

	snprintf(tmp, sizeof(tmp), "%s/%s/%s-%c.csv", ast_config_AST_LOG_DIR,
			 USTM_LOG_DIR, pte->device->name, way);
	*f = fopen(tmp, "r");
	if (!*f) {
		display_last_error("Unable to open history file");
		return 0;
	}
	if (fread(&count, 1, 1, *f) != 1) {
		display_last_error("Unable to read history header - display.");
		fclose(*f);
		return 0;
	}
	if (count > MAX_ENTRY_LOG) {
		ast_log(LOG_WARNING, "Invalid count in history header of %s (%d max %d)\n", tmp,
				count, MAX_ENTRY_LOG);
		fclose(*f);
		return 0;
	}
	return count;
}

static void show_history(struct unistimsession *pte, char way)
{
	FILE *f;
	char count;

	if (!pte->device)
		return;
	if (!pte->device->callhistory)
		return;
	count = OpenHistory(pte, way, &f);
	if (!count)
		return;
	pte->buff_entry[0] = way;
	pte->buff_entry[1] = count;
	pte->buff_entry[2] = 1;
	show_entry_history(pte, &f);
	pte->state = STATE_HISTORY;
}

static void show_main_page(struct unistimsession *pte)
{
	char tmpbuf[TEXT_LENGTH_MAX + 1];


	if ((pte->device->extension == EXTENSION_ASK) &&
		(ast_strlen_zero(pte->device->extension_number))) {
		ShowExtensionPage(pte);
		return;
	}

	pte->state = STATE_MAINPAGE;

	send_tone(pte, 0, 0);
	send_select_output(pte, pte->device->output, pte->device->volume, MUTE_ON_DISCRET);
	pte->device->lines->lastmsgssent = 0;
	send_favorite(pte->device->softkeylinepos, FAV_ICON_ONHOOK_BLACK, pte,
				 pte->device->softkeylabel[pte->device->softkeylinepos]);
	if (!ast_strlen_zero(pte->device->call_forward)) {
		if (pte->device->height == 1) {
			send_text(TEXT_LINE0, TEXT_NORMAL, pte, "Forwarding ON");
		} else {
			send_text(TEXT_LINE0, TEXT_NORMAL, pte, "Call forwarded to :");
			send_text(TEXT_LINE1, TEXT_NORMAL, pte, pte->device->call_forward);
		}
		Sendicon(TEXT_LINE0, FAV_ICON_REFLECT + FAV_BLINK_SLOW, pte);
		send_text_status(pte, "Dial   Redial NoForwd");
	} else {
		if ((pte->device->extension == EXTENSION_ASK) ||
			(pte->device->extension == EXTENSION_TN))
			send_text_status(pte, "Dial   Redial ForwardUnregis");
		else
			send_text_status(pte, "Dial   Redial Forward");

		send_text(TEXT_LINE1, TEXT_NORMAL, pte, pte->device->maintext1);
		if (pte->device->missed_call == 0)
			send_text(TEXT_LINE0, TEXT_NORMAL, pte, pte->device->maintext0);
		else {
			sprintf(tmpbuf, "%d unanswered call(s)", pte->device->missed_call);
			send_text(TEXT_LINE0, TEXT_NORMAL, pte, tmpbuf);
			Sendicon(TEXT_LINE0, FAV_ICON_CALL_CENTER + FAV_BLINK_SLOW, pte);
		}
	}
	if (ast_strlen_zero(pte->device->maintext2)) {
		strcpy(tmpbuf, "IP : ");
		strcat(tmpbuf, ast_inet_ntoa(pte->sin.sin_addr));
		send_text(TEXT_LINE2, TEXT_NORMAL, pte, tmpbuf);
	} else
		send_text(TEXT_LINE2, TEXT_NORMAL, pte, pte->device->maintext2);
	send_texttitle(pte, pte->device->titledefault);
	change_favorite_icon(pte, FAV_ICON_ONHOOK_BLACK);
}

static void key_main_page(struct unistimsession *pte, char keycode)
{
	if (pte->device->missed_call) {
		Sendicon(TEXT_LINE0, FAV_ICON_NONE, pte);
		pte->device->missed_call = 0;
	}
	if ((keycode >= KEY_0) && (keycode <= KEY_SHARP)) {
		handle_dial_page(pte);
		key_dial_page(pte, keycode);
		return;
	}
	switch (keycode) {
	case KEY_FUNC1:
		handle_dial_page(pte);
		break;
	case KEY_FUNC2:
		if (ast_strlen_zero(pte->device->redial_number))
			break;
		if ((pte->device->output == OUTPUT_HANDSET) &&
			(pte->device->receiver_state == STATE_ONHOOK))
			send_select_output(pte, OUTPUT_SPEAKER, pte->device->volume, MUTE_OFF);
		else
			send_select_output(pte, pte->device->output, pte->device->volume, MUTE_OFF);

		ast_copy_string(pte->device->phone_number, pte->device->redial_number,
						sizeof(pte->device->phone_number));
		HandleCallOutgoing(pte);
		break;
	case KEY_FUNC3:
		if (!ast_strlen_zero(pte->device->call_forward)) {
			/* Cancel call forwarding */
			memmove(pte->device->call_forward + 1, pte->device->call_forward,
					sizeof(pte->device->call_forward));
			pte->device->call_forward[0] = '\0';
			Sendicon(TEXT_LINE0, FAV_ICON_NONE, pte);
			pte->device->output = OUTPUT_HANDSET;   /* Seems to be reseted somewhere */
			show_main_page(pte);
			break;
		}
		pte->device->call_forward[0] = -1;
		handle_dial_page(pte);
		break;
	case KEY_FUNC4:
		if (pte->device->extension == EXTENSION_ASK) {
			UnregisterExtension(pte);
			pte->device->extension_number[0] = '\0';
			ShowExtensionPage(pte);
		} else if (pte->device->extension == EXTENSION_TN) {
			ast_mutex_lock(&devicelock);
			strcpy(pte->device->id, pte->device->extension_number);
			pte->buff_entry[0] = '\0';
			pte->size_buff_entry = 0;
			pte->device->session = NULL;
			pte->device = NULL;
			ast_mutex_unlock(&devicelock);
			ShowExtensionPage(pte);
		}
		break;
	case KEY_FAV0:
		handle_dial_page(pte);
		break;
	case KEY_FAV1:
	case KEY_FAV2:
	case KEY_FAV3:
	case KEY_FAV4:
	case KEY_FAV5:
		if ((pte->device->output == OUTPUT_HANDSET) &&
			(pte->device->receiver_state == STATE_ONHOOK))
			send_select_output(pte, OUTPUT_SPEAKER, pte->device->volume, MUTE_OFF);
		else
			send_select_output(pte, pte->device->output, pte->device->volume, MUTE_OFF);
		Keyfavorite(pte, keycode);
		break;
	case KEY_CONF:
		HandleSelectCodec(pte);
		break;
	case KEY_LOUDSPK:
		send_select_output(pte, OUTPUT_SPEAKER, pte->device->volume, MUTE_OFF);
		handle_dial_page(pte);
		break;
	case KEY_HEADPHN:
		send_select_output(pte, OUTPUT_HEADPHONE, pte->device->volume, MUTE_OFF);
		handle_dial_page(pte);
		break;
	case KEY_SNDHIST:
		show_history(pte, 'o');
		break;
	case KEY_RCVHIST:
		show_history(pte, 'i');
		break;
	}
	return;
}

static void key_history(struct unistimsession *pte, char keycode)
{
	FILE *f;
	char count;
	long offset;

	switch (keycode) {
	case KEY_UP:
	case KEY_LEFT:
	case KEY_FUNC1:
		if (pte->buff_entry[2] <= 1)
			return;
		pte->buff_entry[2]--;
		count = OpenHistory(pte, pte->buff_entry[0], &f);
		if (!count)
			return;
		offset = ((pte->buff_entry[2] - 1) * TEXT_LENGTH_MAX * 3);
		if (fseek(f, offset, SEEK_CUR)) {
			display_last_error("Unable to seek history entry.");
			fclose(f);
			return;
		}
		show_entry_history(pte, &f);
		break;
	case KEY_DOWN:
	case KEY_RIGHT:
	case KEY_FUNC2:
		if (pte->buff_entry[2] >= pte->buff_entry[1])
			return;
		pte->buff_entry[2]++;
		count = OpenHistory(pte, pte->buff_entry[0], &f);
		if (!count)
			return;
		offset = ((pte->buff_entry[2] - 1) * TEXT_LENGTH_MAX * 3);
		if (fseek(f, offset, SEEK_CUR)) {
			display_last_error("Unable to seek history entry.");
			fclose(f);
			return;
		}
		show_entry_history(pte, &f);
		break;
	case KEY_FUNC3:
		if (!ReformatNumber(pte->device->lst_cid))
			break;
		ast_copy_string(pte->device->redial_number, pte->device->lst_cid,
						sizeof(pte->device->redial_number));
		key_main_page(pte, KEY_FUNC2);
		break;
	case KEY_FUNC4:
	case KEY_HANGUP:
		show_main_page(pte);
		break;
	case KEY_SNDHIST:
		if (pte->buff_entry[0] == 'i')
			show_history(pte, 'o');
		else
			show_main_page(pte);
		break;
	case KEY_RCVHIST:
		if (pte->buff_entry[0] == 'i')
			show_main_page(pte);
		else
			show_history(pte, 'i');
		break;
	}
	return;
}

static void init_phone_step2(struct unistimsession *pte)
{
	BUFFSEND;
	if (unistimdebug)
		ast_verb(0, "Sending S4\n");
	memcpy(buffsend + SIZE_HEADER, packet_send_s4, sizeof(packet_send_s4));
	send_client(SIZE_HEADER + sizeof(packet_send_s4), buffsend, pte);
	send_date_time2(pte);
	send_date_time3(pte);
	if (unistimdebug)
		ast_verb(0, "Sending S7\n");
	memcpy(buffsend + SIZE_HEADER, packet_send_S7, sizeof(packet_send_S7));
	send_client(SIZE_HEADER + sizeof(packet_send_S7), buffsend, pte);
	if (unistimdebug)
		ast_verb(0, "Sending Contrast\n");
	memcpy(buffsend + SIZE_HEADER, packet_send_Contrast, sizeof(packet_send_Contrast));
	if (pte->device != NULL)
		buffsend[9] = pte->device->contrast;
	send_client(SIZE_HEADER + sizeof(packet_send_Contrast), buffsend, pte);

	if (unistimdebug)
		ast_verb(0, "Sending S9\n");
	memcpy(buffsend + SIZE_HEADER, packet_send_s9, sizeof(packet_send_s9));
	send_client(SIZE_HEADER + sizeof(packet_send_s9), buffsend, pte);
	send_no_ring(pte);

	if (unistimdebug)
		ast_verb(0, "Sending S7\n");
	memcpy(buffsend + SIZE_HEADER, packet_send_S7, sizeof(packet_send_S7));
	send_client(SIZE_HEADER + sizeof(packet_send_S7), buffsend, pte);
	send_led_update(pte, 0);
	send_ping(pte);
	if (pte->state < STATE_MAINPAGE) {
		if (autoprovisioning == AUTOPROVISIONING_TN) {
			ShowExtensionPage(pte);
			return;
		} else {
			int i;
			char tmp[30];

			for (i = 1; i < 6; i++)
				send_favorite(i, 0, pte, "");
			send_text(TEXT_LINE0, TEXT_NORMAL, pte, "Sorry, this phone is not");
			send_text(TEXT_LINE1, TEXT_NORMAL, pte, "registered in unistim.cfg");
			strcpy(tmp, "MAC = ");
			strcat(tmp, pte->macaddr);
			send_text(TEXT_LINE2, TEXT_NORMAL, pte, tmp);
			send_text_status(pte, "");
			send_texttitle(pte, "UNISTIM for*");
			return;
		}
	}
	show_main_page(pte);
	refresh_all_favorite(pte);
	if (unistimdebug)
		ast_verb(0, "Sending arrow\n");
	memcpy(buffsend + SIZE_HEADER, packet_send_arrow, sizeof(packet_send_arrow));
	send_client(SIZE_HEADER + sizeof(packet_send_arrow), buffsend, pte);
	return;
}

static void process_request(int size, unsigned char *buf, struct unistimsession *pte)
{
	char tmpbuf[255];
	if (memcmp
		(buf + SIZE_HEADER, packet_recv_resume_connection_with_server,
		 sizeof(packet_recv_resume_connection_with_server)) == 0) {
		rcv_resume_connection_with_server(pte);
		return;
	}
	if (memcmp(buf + SIZE_HEADER, packet_recv_firm_version, sizeof(packet_recv_firm_version)) ==
		0) {
		buf[size] = 0;
		if (unistimdebug)
			ast_verb(0, "Got the firmware version : '%s'\n", buf + 13);
		init_phone_step2(pte);
		return;
	}
	if (memcmp(buf + SIZE_HEADER, packet_recv_mac_addr, sizeof(packet_recv_mac_addr)) == 0) {
		rcv_mac_addr(pte, buf);
		return;
	}
	if (memcmp(buf + SIZE_HEADER, packet_recv_r2, sizeof(packet_recv_r2)) == 0) {
		if (unistimdebug)
			ast_verb(0, "R2 received\n");
		return;
	}

	if (pte->state < STATE_MAINPAGE) {
		if (unistimdebug)
			ast_verb(0, "Request not authorized in this state\n");
		return;
	}
	if (!memcmp(buf + SIZE_HEADER, packet_recv_pressed_key, sizeof(packet_recv_pressed_key))) {
		char keycode = buf[13];

		if (unistimdebug)
			ast_verb(0, "Key pressed : keycode = 0x%.2x - current state : %d\n", keycode,
						pte->state);

		switch (pte->state) {
		case STATE_INIT:
			if (unistimdebug)
				ast_verb(0, "No keys allowed in the init state\n");
			break;
		case STATE_AUTHDENY:
			if (unistimdebug)
				ast_verb(0, "No keys allowed in authdeny state\n");
			break;
		case STATE_MAINPAGE:
			key_main_page(pte, keycode);
			break;
		case STATE_DIALPAGE:
			key_dial_page(pte, keycode);
			break;
		case STATE_RINGING:
			key_ringing(pte, keycode);
			break;
		case STATE_CALL:
			key_call(pte, keycode);
			break;
		case STATE_EXTENSION:
			key_select_extension(pte, keycode);
			break;
		case STATE_SELECTCODEC:
			key_select_codec(pte, keycode);
			break;
		case STATE_HISTORY:
			key_history(pte, keycode);
			break;
		default:
			ast_log(LOG_WARNING, "Key : Unknown state\n");
		}
		return;
	}
	if (memcmp(buf + SIZE_HEADER, packet_recv_pick_up, sizeof(packet_recv_pick_up)) == 0) {
		if (unistimdebug)
			ast_verb(0, "Handset off hook\n");
		if (!pte->device)	       /* We are not yet registered (asking for a TN in AUTOPROVISIONING_TN) */
			return;
		pte->device->receiver_state = STATE_OFFHOOK;
		if (pte->device->output == OUTPUT_HEADPHONE)
			send_select_output(pte, OUTPUT_HEADPHONE, pte->device->volume, MUTE_OFF);
		else
			send_select_output(pte, OUTPUT_HANDSET, pte->device->volume, MUTE_OFF);
		if (pte->state == STATE_RINGING)
			HandleCallIncoming(pte);
		else if ((pte->state == STATE_DIALPAGE) || (pte->state == STATE_CALL))
			send_select_output(pte, OUTPUT_HANDSET, pte->device->volume, MUTE_OFF);
		else if (pte->state == STATE_EXTENSION) /* We must have a TN before calling */
			return;
		else {
			send_select_output(pte, OUTPUT_HANDSET, pte->device->volume, MUTE_OFF);
			handle_dial_page(pte);
		}
		return;
	}
	if (memcmp(buf + SIZE_HEADER, packet_recv_hangup, sizeof(packet_recv_hangup)) == 0) {
		if (unistimdebug)
			ast_verb(0, "Handset on hook\n");
		if (!pte->device)
			return;
		pte->device->receiver_state = STATE_ONHOOK;
		if (pte->state == STATE_CALL)
			close_call(pte);
		else if (pte->device->lines->subs[SUB_REAL]->owner)
			close_call(pte);
		else if (pte->state == STATE_EXTENSION)
			return;
		else
			show_main_page(pte);
		return;
	}
	strcpy(tmpbuf, ast_inet_ntoa(pte->sin.sin_addr));
	strcat(tmpbuf, " Unknown request packet\n");
	if (unistimdebug)
		ast_debug(1, "%s", tmpbuf);
	return;
}

static void parsing(int size, unsigned char *buf, struct unistimsession *pte,
	struct sockaddr_in *addr_from)
{
	unsigned short *sbuf = (unsigned short *) buf;
	unsigned short seq;
	char tmpbuf[255];

	strcpy(tmpbuf, ast_inet_ntoa(addr_from->sin_addr));

	if (size < 10) {
		if (size == 0) {
			ast_log(LOG_WARNING, "%s Read error\n", tmpbuf);
		} else {
			ast_log(LOG_NOTICE, "%s Packet too short - ignoring\n", tmpbuf);
		}
		return;
	}
	if (sbuf[0] == 0xffff) {	/* Starting with 0xffff ? *//* Yes, discovery packet ? */
		if (size != sizeof(packet_rcv_discovery)) {
			ast_log(LOG_NOTICE, "%s Invalid size of a discovery packet\n", tmpbuf);
		} else {
			if (memcmp(buf, packet_rcv_discovery, sizeof(packet_rcv_discovery)) == 0) {
				if (unistimdebug)
					ast_verb(0, "Discovery packet received - Sending Discovery ACK\n");
				if (pte) {	      /* A session was already active for this IP ? */
					if (pte->state == STATE_INIT) { /* Yes, but it's a dupe */
						if (unistimdebug)
							ast_verb(1, "Duplicated Discovery packet\n");
						send_raw_client(sizeof(packet_send_discovery_ack),
									  packet_send_discovery_ack, addr_from, &pte->sout);
						pte->seq_phone = (short) 0x0000;	/* reset sequence number */
					} else {	/* No, probably a reboot, phone side */
						close_client(pte);       /* Cleanup the previous session */
						if (create_client(addr_from))
							send_raw_client(sizeof(packet_send_discovery_ack),
										  packet_send_discovery_ack, addr_from, &pte->sout);
					}
				} else {
					/* Creating new entry in our phone list */
					if ((pte = create_client(addr_from)))
						send_raw_client(sizeof(packet_send_discovery_ack),
									  packet_send_discovery_ack, addr_from, &pte->sout);
				}
				return;
			}
			ast_log(LOG_NOTICE, "%s Invalid discovery packet\n", tmpbuf);
		}
		return;
	}
	if (!pte) {
		if (unistimdebug)
			ast_verb(0, "%s Not a discovery packet from an unknown source : ignoring\n",
						tmpbuf);
		return;
	}

	if (sbuf[0] != 0) {		     /* Starting with something else than 0x0000 ? */
		ast_log(LOG_NOTICE, "Unknown packet received - ignoring\n");
		return;
	}
	if (buf[5] != 2) {
		ast_log(LOG_NOTICE, "%s Wrong direction : got 0x%.2x expected 0x02\n", tmpbuf,
				buf[5]);
		return;
	}
	seq = ntohs(sbuf[1]);
	if (buf[4] == 1) {
		ast_mutex_lock(&pte->lock);
		if (unistimdebug)
			ast_verb(6, "ACK received for packet #0x%.4x\n", seq);
		pte->nb_retransmit = 0;

		if ((pte->last_seq_ack) + 1 == seq) {
			pte->last_seq_ack++;
			check_send_queue(pte);
			ast_mutex_unlock(&pte->lock);
			return;
		}
		if (pte->last_seq_ack > seq) {
			if (pte->last_seq_ack == 0xffff) {
				ast_verb(0, "ACK at 0xffff, restarting counter.\n");
				pte->last_seq_ack = 0;
			} else
				ast_log(LOG_NOTICE,
						"%s Warning : ACK received for an already ACKed packet : #0x%.4x we are at #0x%.4x\n",
						tmpbuf, seq, pte->last_seq_ack);
			ast_mutex_unlock(&pte->lock);
			return;
		}
		if (pte->seq_server < seq) {
			ast_log(LOG_NOTICE,
					"%s Error : ACK received for a non-existent packet : #0x%.4x\n",
					tmpbuf, pte->seq_server);
			ast_mutex_unlock(&pte->lock);
			return;
		}
		if (unistimdebug)
			ast_verb(0, "%s ACK gap : Received ACK #0x%.4x, previous was #0x%.4x\n",
						tmpbuf, seq, pte->last_seq_ack);
		pte->last_seq_ack = seq;
		check_send_queue(pte);
		ast_mutex_unlock(&pte->lock);
		return;
	}
	if (buf[4] == 2) {
		if (unistimdebug)
			ast_verb(0, "Request received\n");
		if (pte->seq_phone == seq) {
			/* Send ACK */
			buf[4] = 1;
			buf[5] = 1;
			send_raw_client(SIZE_HEADER, buf, addr_from, &pte->sout);
			pte->seq_phone++;

			process_request(size, buf, pte);
			return;
		}
		if (pte->seq_phone > seq) {
			ast_log(LOG_NOTICE,
					"%s Warning : received a retransmitted packet : #0x%.4x (we are at #0x%.4x)\n",
					tmpbuf, seq, pte->seq_phone);
			/* BUG ? pte->device->seq_phone = seq; */
			/* Send ACK */
			buf[4] = 1;
			buf[5] = 1;
			send_raw_client(SIZE_HEADER, buf, addr_from, &pte->sout);
			return;
		}
		ast_log(LOG_NOTICE,
				"%s Warning : we lost a packet : received #0x%.4x (we are at #0x%.4x)\n",
				tmpbuf, seq, pte->seq_phone);
		return;
	}
	if (buf[4] == 0) {
		ast_log(LOG_NOTICE, "%s Retransmit request for packet #0x%.4x\n", tmpbuf, seq);
		if (pte->last_seq_ack > seq) {
			ast_log(LOG_NOTICE,
					"%s Error : received a request for an already ACKed packet : #0x%.4x\n",
					tmpbuf, pte->last_seq_ack);
			return;
		}
		if (pte->seq_server < seq) {
			ast_log(LOG_NOTICE,
					"%s Error : received a request for a non-existent packet : #0x%.4x\n",
					tmpbuf, pte->seq_server);
			return;
		}
		send_retransmit(pte);
		return;
	}
	ast_log(LOG_NOTICE, "%s Unknown request : got 0x%.2x expected 0x00,0x01 or 0x02\n",
			tmpbuf, buf[4]);
	return;
}

static struct unistimsession *channel_to_session(struct ast_channel *ast)
{
	struct unistim_subchannel *sub;
	if (!ast) {
		ast_log(LOG_WARNING, "Unistim callback function called with a null channel\n");
		return NULL;
	}
	if (!ast->tech_pvt) {
		ast_log(LOG_WARNING, "Unistim callback function called without a tech_pvt\n");
		return NULL;
	}
	sub = ast->tech_pvt;

	if (!sub->parent) {
		ast_log(LOG_WARNING, "Unistim callback function called without a line\n");
		return NULL;
	}
	if (!sub->parent->parent) {
		ast_log(LOG_WARNING, "Unistim callback function called without a device\n");
		return NULL;
	}
	if (!sub->parent->parent->session) {
		ast_log(LOG_WARNING, "Unistim callback function called without a session\n");
		return NULL;
	}
	return sub->parent->parent->session;
}

/*--- unistim_call: Initiate UNISTIM call from PBX ---*/
/*      used from the dial() application      */
static int unistim_call(struct ast_channel *ast, char *dest, int timeout)
{
	int res = 0;
	struct unistim_subchannel *sub;
	struct unistimsession *session;

	session = channel_to_session(ast);
	if (!session) {
		ast_log(LOG_ERROR, "Device not registered, cannot call %s\n", dest);
		return -1;
	}

	sub = ast->tech_pvt;
	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "unistim_call called on %s, neither down nor reserved\n",
				ast->name);
		return -1;
	}

	if (unistimdebug)
		ast_verb(3, "unistim_call(%s)\n", ast->name);

	session->state = STATE_RINGING;
	Sendicon(TEXT_LINE0, FAV_ICON_NONE, session);

	if (sub->owner) {
		if (sub->owner->connected.id.number.valid
			&& sub->owner->connected.id.number.str) {
			if (session->device->height == 1) {
				send_text(TEXT_LINE0, TEXT_NORMAL, session, sub->owner->connected.id.number.str);
			} else {
				send_text(TEXT_LINE1, TEXT_NORMAL, session, sub->owner->connected.id.number.str);
			}
			change_callerid(session, 0, sub->owner->connected.id.number.str);
		} else {
			if (session->device->height == 1) {
				send_text(TEXT_LINE0, TEXT_NORMAL, session, DEFAULTCALLERID);
			} else {
				send_text(TEXT_LINE1, TEXT_NORMAL, session, DEFAULTCALLERID);
			}
			change_callerid(session, 0, DEFAULTCALLERID);
		}
		if (sub->owner->connected.id.name.valid
			&& sub->owner->connected.id.name.str) {
			send_text(TEXT_LINE0, TEXT_NORMAL, session, sub->owner->connected.id.name.str);
			change_callerid(session, 1, sub->owner->connected.id.name.str);
		} else {
			send_text(TEXT_LINE0, TEXT_NORMAL, session, DEFAULTCALLERNAME);
			change_callerid(session, 1, DEFAULTCALLERNAME);
		}
	}
	send_text(TEXT_LINE2, TEXT_NORMAL, session, "is calling you.");
	send_text_status(session, "Accept              Ignore");

	if (sub->ringstyle == -1)
		send_ring(session, session->device->ringvolume, session->device->ringstyle);
	else {
		if (sub->ringvolume == -1)
			send_ring(session, session->device->ringvolume, sub->ringstyle);
		else
			send_ring(session, sub->ringvolume, sub->ringstyle);
	}
	change_favorite_icon(session, FAV_ICON_SPEAKER_ONHOOK_BLACK + FAV_BLINK_FAST);

	ast_setstate(ast, AST_STATE_RINGING);
	ast_queue_control(ast, AST_CONTROL_RINGING);
	return res;
}

/*--- unistim_hangup: Hangup UNISTIM call */
static int unistim_hangup(struct ast_channel *ast)
{
	struct unistim_subchannel *sub;
	struct unistim_line *l;
	struct unistimsession *s;

	s = channel_to_session(ast);
	sub = ast->tech_pvt;
	if (!s) {
		ast_debug(1, "Asked to hangup channel not connected\n");
		ast_mutex_lock(&sub->lock);
		sub->owner = NULL;
		ast->tech_pvt = NULL;
		sub->alreadygone = 0;
		ast_mutex_unlock(&sub->lock);
		if (sub->rtp) {
			if (unistimdebug)
				ast_verb(0, "Destroying RTP session\n");
			ast_rtp_instance_destroy(sub->rtp);
			sub->rtp = NULL;
		}
		return 0;
	}
	l = sub->parent;
	if (unistimdebug)
		ast_verb(0, "unistim_hangup(%s) on %s@%s\n", ast->name, l->name, l->parent->name);

	if ((l->subs[SUB_THREEWAY]) && (sub->subtype == SUB_REAL)) {
		if (unistimdebug)
			ast_verb(0, "Real call disconnected while talking to threeway\n");
		sub->owner = NULL;
		ast->tech_pvt = NULL;
		return 0;
	}
	if ((l->subs[SUB_REAL]->owner) && (sub->subtype == SUB_THREEWAY) &&
		(sub->alreadygone == 0)) {
		if (unistimdebug)
			ast_verb(0, "threeway call disconnected, switching to real call\n");
		send_text(TEXT_LINE0, TEXT_NORMAL, s, "Three way call canceled,");
		send_text(TEXT_LINE1, TEXT_NORMAL, s, "switching back to");
		send_text(TEXT_LINE2, TEXT_NORMAL, s, "previous call.");
		send_text_status(s, "Hangup Transf");
		ast_moh_stop(ast_bridged_channel(l->subs[SUB_REAL]->owner));
		swap_subs(l, SUB_THREEWAY, SUB_REAL);
		l->parent->moh = 0;
		ast_mutex_lock(&sub->lock);
		sub->owner = NULL;
		ast->tech_pvt = NULL;
		ast_mutex_unlock(&sub->lock);
		unalloc_sub(l, SUB_THREEWAY);
		return 0;
	}
	ast_mutex_lock(&sub->lock);
	sub->owner = NULL;
	ast->tech_pvt = NULL;
	sub->alreadygone = 0;
	ast_mutex_unlock(&sub->lock);
	if (!s) {
		if (unistimdebug)
			ast_verb(0, "Asked to hangup channel not connected (no session)\n");
		if (sub->rtp) {
			if (unistimdebug)
				ast_verb(0, "Destroying RTP session\n");
			ast_rtp_instance_destroy(sub->rtp);
			sub->rtp = NULL;
		}
		return 0;
	}
	if (sub->subtype == SUB_REAL) {
		/* Stop the silence generator */
		if (s->device->silence_generator) {
			if (unistimdebug)
				ast_verb(0, "Stopping silence generator\n");
			if (sub->owner)
				ast_channel_stop_silence_generator(sub->owner,
												   s->device->silence_generator);
			else
				ast_log(LOG_WARNING,
						"Trying to stop silence generator on a null channel !\n");
			s->device->silence_generator = NULL;
		}
	}
	l->parent->moh = 0;
	send_no_ring(s);
	send_end_call(s);
	if (sub->rtp) {
		if (unistimdebug)
			ast_verb(0, "Destroying RTP session\n");
		ast_rtp_instance_destroy(sub->rtp);
		sub->rtp = NULL;
	} else if (unistimdebug)
		ast_verb(0, "No RTP session to destroy\n");
	if (l->subs[SUB_THREEWAY]) {
		if (unistimdebug)
			ast_verb(0, "Cleaning other subchannels\n");
		unalloc_sub(l, SUB_THREEWAY);
	}
	if (s->state == STATE_RINGING)
		cancel_dial(s);
	else if (s->state == STATE_CALL)
		close_call(s);

	return 0;
}

/*--- unistim_answer: Answer UNISTIM call */
static int unistim_answer(struct ast_channel *ast)
{
	int res = 0;
	struct unistim_subchannel *sub;
	struct unistim_line *l;
	struct unistimsession *s;

	s = channel_to_session(ast);
	if (!s) {
		ast_log(LOG_WARNING, "unistim_answer on a disconnected device ?\n");
		return -1;
	}
	sub = ast->tech_pvt;
	l = sub->parent;

	if ((!sub->rtp) && (!l->subs[SUB_THREEWAY]))
		start_rtp(sub);
	if (unistimdebug)
		ast_verb(0, "unistim_answer(%s) on %s@%s-%d\n", ast->name, l->name,
					l->parent->name, sub->subtype);
	send_text(TEXT_LINE2, TEXT_NORMAL, l->parent->session, "is now on-line");
	if (l->subs[SUB_THREEWAY])
		send_text_status(l->parent->session, "Transf Cancel");
	else
		send_text_status(l->parent->session, "Hangup Transf");
	send_start_timer(l->parent->session);
	if (ast->_state != AST_STATE_UP)
		ast_setstate(ast, AST_STATE_UP);
	return res;
}

/*--- unistimsock_read: Read data from UNISTIM socket ---*/
/*    Successful messages is connected to UNISTIM call and forwarded to parsing() */
static int unistimsock_read(int *id, int fd, short events, void *ignore)
{
	struct sockaddr_in addr_from = { 0, };
	struct unistimsession *cur = NULL;
	int found = 0;
	int tmp = 0;
	int dw_num_bytes_rcvd;
#ifdef DUMP_PACKET
	int dw_num_bytes_rcvdd;
	char iabuf[INET_ADDRSTRLEN];
#endif

	dw_num_bytes_rcvd =
		recvfrom(unistimsock, buff, SIZE_PAGE, 0, (struct sockaddr *) &addr_from,
				 &size_addr_from);
	if (dw_num_bytes_rcvd == -1) {
		if (errno == EAGAIN)
			ast_log(LOG_NOTICE, "UNISTIM: Received packet with bad UDP checksum\n");
		else if (errno != ECONNREFUSED)
			ast_log(LOG_WARNING, "Recv error %d (%s)\n", errno, strerror(errno));
		return 1;
	}

	/* Looking in the phone list if we already have a registration for him */
	ast_mutex_lock(&sessionlock);
	cur = sessions;
	while (cur) {
		if (cur->sin.sin_addr.s_addr == addr_from.sin_addr.s_addr) {
			found = 1;
			break;
		}
		tmp++;
		cur = cur->next;
	}
	ast_mutex_unlock(&sessionlock);

#ifdef DUMP_PACKET
	if (unistimdebug)
		ast_verb(0, "\n*** Dump %d bytes from %s - phone_table[%d] ***\n",
					dw_num_bytes_rcvd, ast_inet_ntoa(addr_from.sin_addr), tmp);
	for (dw_num_bytes_rcvdd = 0; dw_num_bytes_rcvdd < dw_num_bytes_rcvd;
		 dw_num_bytes_rcvdd++)
		ast_verb(0, "%.2x ", (unsigned char) buff[dw_num_bytes_rcvdd]);
	ast_verb(0, "\n******************************************\n");
#endif

	if (!found) {
		if (unistimdebug)
			ast_verb(0, "Received a packet from an unknown source\n");
		parsing(dw_num_bytes_rcvd, buff, NULL, (struct sockaddr_in *) &addr_from);

	} else
		parsing(dw_num_bytes_rcvd, buff, cur, (struct sockaddr_in *) &addr_from);

	return 1;
}

static struct ast_frame *unistim_rtp_read(const struct ast_channel *ast,
	const struct unistim_subchannel *sub)
{
	/* Retrieve audio/etc from channel.  Assumes sub->lock is already held. */
	struct ast_frame *f;

	if (!ast) {
		ast_log(LOG_WARNING, "Channel NULL while reading\n");
		return &ast_null_frame;
	}

	if (!sub->rtp) {
		ast_log(LOG_WARNING, "RTP handle NULL while reading on subchannel %d\n",
				sub->subtype);
		return &ast_null_frame;
	}

	switch (ast->fdno) {
	case 0:
		f = ast_rtp_instance_read(sub->rtp, 0);     /* RTP Audio */
		break;
	case 1:
		f = ast_rtp_instance_read(sub->rtp, 1);    /* RTCP Control Channel */
		break;
	default:
		f = &ast_null_frame;
	}

	if (sub->owner) {
		/* We already hold the channel lock */
		if (f->frametype == AST_FRAME_VOICE) {
			if (!(ast_format_cap_iscompatible(sub->owner->nativeformats, &f->subclass.format))) {
				char tmp[256];
				ast_debug(1,
						"Oooh, format changed from %s to %s\n",
						ast_getformatname_multiple(tmp, sizeof(tmp), sub->owner->nativeformats),
						ast_getformatname(&f->subclass.format));

				ast_format_cap_set(sub->owner->nativeformats, &f->subclass.format);
				ast_set_read_format(sub->owner, &sub->owner->readformat);
				ast_set_write_format(sub->owner, &sub->owner->writeformat);
			}
		}
	}

	return f;
}

static struct ast_frame *unistim_read(struct ast_channel *ast)
{
	struct ast_frame *fr;
	struct unistim_subchannel *sub = ast->tech_pvt;

	ast_mutex_lock(&sub->lock);
	fr = unistim_rtp_read(ast, sub);
	ast_mutex_unlock(&sub->lock);

	return fr;
}

static int unistim_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct unistim_subchannel *sub = ast->tech_pvt;
	int res = 0;

	if (frame->frametype != AST_FRAME_VOICE) {
		if (frame->frametype == AST_FRAME_IMAGE)
			return 0;
		else {
			ast_log(LOG_WARNING, "Can't send %d type frames with unistim_write\n",
					frame->frametype);
			return 0;
		}
	} else {
		if (!(ast_format_cap_iscompatible(ast->nativeformats, &frame->subclass.format))) {
			char tmp[256];
			ast_log(LOG_WARNING,
					"Asked to transmit frame type %s, while native formats is %s (read/write = (%s/%s)\n",
					ast_getformatname(&frame->subclass.format),
					ast_getformatname_multiple(tmp, sizeof(tmp), ast->nativeformats),
					ast_getformatname(&ast->readformat),
					ast_getformatname(&ast->writeformat));
			return -1;
		}
	}

	if (sub) {
		ast_mutex_lock(&sub->lock);
		if (sub->rtp) {
			res = ast_rtp_instance_write(sub->rtp, frame);
		}
		ast_mutex_unlock(&sub->lock);
	}

	return res;
}

static int unistim_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct unistim_subchannel *p = newchan->tech_pvt;
	struct unistim_line *l = p->parent;

	ast_mutex_lock(&p->lock);

	ast_debug(1, "New owner for channel USTM/%s@%s-%d is %s\n", l->name,
			l->parent->name, p->subtype, newchan->name);

	if (p->owner != oldchan) {
		ast_log(LOG_WARNING, "old channel wasn't %s (%p) but was %s (%p)\n",
				oldchan->name, oldchan, p->owner->name, p->owner);
		return -1;
	}

	p->owner = newchan;

	ast_mutex_unlock(&p->lock);

	return 0;

}

static char *control2str(int ind)
{
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

static void in_band_indication(struct ast_channel *ast, const struct ast_tone_zone *tz,
	const char *indication)
{
	struct ast_tone_zone_sound *ts = NULL;

	if ((ts = ast_get_indication_tone(tz, indication))) {
		ast_playtones_start(ast, 0, ts->data, 1);
		ts = ast_tone_zone_sound_unref(ts);
	} else {
		ast_log(LOG_WARNING, "Unable to get indication tone for %s\n", indication);
	}
}

static int unistim_indicate(struct ast_channel *ast, int ind, const void *data, 
	size_t datalen)
{
	struct unistim_subchannel *sub;
	struct unistim_line *l;
	struct unistimsession *s;

	if (unistimdebug) {
		ast_verb(3, "Asked to indicate '%s' condition on channel %s\n",
					control2str(ind), ast->name);
	}

	s = channel_to_session(ast);
	if (!s)
		return -1;

	sub = ast->tech_pvt;
	l = sub->parent;

	switch (ind) {
	case AST_CONTROL_RINGING:
		if (ast->_state != AST_STATE_UP) {
			send_text(TEXT_LINE2, TEXT_NORMAL, s, "Ringing...");
			in_band_indication(ast, l->parent->tz, "ring");
			s->device->missed_call = -1;
			break;
		}
		return -1;
	case AST_CONTROL_BUSY:
		if (ast->_state != AST_STATE_UP) {
			sub->alreadygone = 1;
			send_text(TEXT_LINE2, TEXT_NORMAL, s, "Busy");
			in_band_indication(ast, l->parent->tz, "busy");
			s->device->missed_call = -1;
			break;
		}
		return -1;
	case AST_CONTROL_CONGESTION:
		if (ast->_state != AST_STATE_UP) {
			sub->alreadygone = 1;
			send_text(TEXT_LINE2, TEXT_NORMAL, s, "Congestion");
			in_band_indication(ast, l->parent->tz, "congestion");
			s->device->missed_call = -1;
			break;
		}
		return -1;
	case AST_CONTROL_HOLD:
		ast_moh_start(ast, data, NULL);
		break;
	case AST_CONTROL_UNHOLD:
		ast_moh_stop(ast);
		break;
	case AST_CONTROL_PROGRESS:
	case AST_CONTROL_SRCUPDATE:
		break;
	case -1:
		ast_playtones_stop(ast);
		s->device->missed_call = 0;
		break;
	case AST_CONTROL_PROCEEDING:
		break;
	default:
		ast_log(LOG_WARNING, "Don't know how to indicate condition %d\n", ind);
		return -1;
	}

	return 0;
}

static struct unistim_subchannel *find_subchannel_by_name(const char *dest)
{
	struct unistim_line *l;
	struct unistim_device *d;
	char line[256];
	char *at;
	char *device;

	ast_copy_string(line, dest, sizeof(line));
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
	at = strchr(device, '/');       /* Extra options ? */
	if (at)
		*at = '\0';
	while (d) {
		if (!strcasecmp(d->name, device)) {
			if (unistimdebug)
				ast_verb(0, "Found device: %s\n", d->name);
			/* Found the device */
			l = d->lines;
			while (l) {
				/* Search for the right line */
				if (!strcasecmp(l->name, line)) {
					l->subs[SUB_REAL]->ringvolume = -1;
					l->subs[SUB_REAL]->ringstyle = -1;
					if (at) {       /* Other options ? */
						at++;   /* Skip slash */
						if (*at == 'r') {       /* distinctive ring */
							at++;
							if ((*at < '0') || (*at > '7')) /* ring style */
								ast_log(LOG_WARNING, "Invalid ring selection (%s)", at);
							else {
								char ring_volume = -1;
								char ring_style = *at - '0';
								at++;
								if ((*at >= '0') && (*at <= '3'))       /* ring volume */
									ring_volume = *at - '0';
								if (unistimdebug)
									ast_verb(0, "Distinctive ring : style #%d volume %d\n",
										 ring_style, ring_volume);
								l->subs[SUB_REAL]->ringvolume = ring_volume;
								l->subs[SUB_REAL]->ringstyle = ring_style;
							}
						}
					}
					ast_mutex_unlock(&devicelock);
					return l->subs[SUB_REAL];
				}
				l = l->next;
			}
		}
		d = d->next;
	}
	/* Device not found */
	ast_mutex_unlock(&devicelock);

	return NULL;
}

static int unistim_senddigit_begin(struct ast_channel *ast, char digit)
{
	struct unistimsession *pte = channel_to_session(ast);

	if (!pte)
		return -1;

	return unistim_do_senddigit(pte, digit);
}

static int unistim_senddigit_end(struct ast_channel *ast, char digit, unsigned int duration)
{
	struct unistimsession *pte = channel_to_session(ast);
	struct ast_frame f = { 0, };
	struct unistim_subchannel *sub;

	sub = pte->device->lines->subs[SUB_REAL];

	if (!sub->owner || sub->alreadygone) {
		ast_log(LOG_WARNING, "Unable to find subchannel in dtmf senddigit_end\n");
		return -1;
	}

	if (unistimdebug)
		ast_verb(0, "Send Digit off %c\n", digit);

	if (!pte)
		return -1;

	send_tone(pte, 0, 0);
	f.frametype = AST_FRAME_DTMF;
	f.subclass.integer = digit;
	f.src = "unistim";
	ast_queue_frame(sub->owner, &f);

	return 0;
}

/*--- unistim_sendtext: Display a text on the phone screen ---*/
/*      Called from PBX core text message functions */
static int unistim_sendtext(struct ast_channel *ast, const char *text)
{
	struct unistimsession *pte = channel_to_session(ast);
	int size;
	char tmp[TEXT_LENGTH_MAX + 1];

	if (unistimdebug)
		ast_verb(0, "unistim_sendtext called\n");

	if (!text) {
		ast_log(LOG_WARNING, "unistim_sendtext called with a null text\n");
		return 1;
	}

	size = strlen(text);
	if (text[0] == '@') {
		int pos = 0, i = 1, tok = 0, sz = 0;
		char label[11];
		char number[16];
		char icon = '\0';
		char cur = '\0';

		memset(label, 0, 11);
		memset(number, 0, 16);
		while (text[i]) {
			cur = text[i++];
			switch (tok) {
			case 0:
				if ((cur < '0') && (cur > '5')) {
					ast_log(LOG_WARNING,
							"sendtext failed : position must be a number beetween 0 and 5\n");
					return 1;
				}
				pos = cur - '0';
				tok = 1;
				continue;
			case 1:
				if (cur != '@') {
					ast_log(LOG_WARNING, "sendtext failed : invalid position\n");
					return 1;
				}
				tok = 2;
				continue;
			case 2:
				if ((cur < '3') && (cur > '6')) {
					ast_log(LOG_WARNING,
							"sendtext failed : icon must be a number beetween 32 and 63 (first digit invalid)\n");
					return 1;
				}
				icon = (cur - '0') * 10;
				tok = 3;
				continue;
			case 3:
				if ((cur < '0') && (cur > '9')) {
					ast_log(LOG_WARNING,
							"sendtext failed : icon must be a number beetween 32 and 63 (second digit invalid)\n");
					return 1;
				}
				icon += (cur - '0');
				tok = 4;
				continue;
			case 4:
				if (cur != '@') {
					ast_log(LOG_WARNING,
							"sendtext failed : icon must be a number beetween 32 and 63 (too many digits)\n");
					return 1;
				}
				tok = 5;
				continue;
			case 5:
				if (cur == '@') {
					tok = 6;
					sz = 0;
					continue;
				}
				if (sz > 10)
					continue;
				label[sz] = cur;
				sz++;
				continue;
			case 6:
				if (sz > 15) {
					ast_log(LOG_WARNING,
							"sendtext failed : extension too long = %d (15 car max)\n",
							sz);
					return 1;
				}
				number[sz] = cur;
				sz++;
				continue;
			}
		}
		if (tok != 6) {
			ast_log(LOG_WARNING, "sendtext failed : incomplet command\n");
			return 1;
		}
		if (!pte->device) {
			ast_log(LOG_WARNING, "sendtext failed : no device ?\n");
			return 1;
		}
		strcpy(pte->device->softkeylabel[pos], label);
		strcpy(pte->device->softkeynumber[pos], number);
		pte->device->softkeyicon[pos] = icon;
		send_favorite(pos, icon, pte, label);
		return 0;
	}

	if (size <= TEXT_LENGTH_MAX * 2) {
		if (pte->device->height == 1) {
			send_text(TEXT_LINE0, TEXT_NORMAL, pte, text);
		} else {
			send_text(TEXT_LINE0, TEXT_NORMAL, pte, "Message :");
			send_text(TEXT_LINE1, TEXT_NORMAL, pte, text);
		}
		if (size <= TEXT_LENGTH_MAX) {
			send_text(TEXT_LINE2, TEXT_NORMAL, pte, "");
			return 0;
		}
		memcpy(tmp, text + TEXT_LENGTH_MAX, TEXT_LENGTH_MAX);
		tmp[sizeof(tmp) - 1] = '\0';
		send_text(TEXT_LINE2, TEXT_NORMAL, pte, tmp);
		return 0;
	}
	send_text(TEXT_LINE0, TEXT_NORMAL, pte, text);
	memcpy(tmp, text + TEXT_LENGTH_MAX, TEXT_LENGTH_MAX);
	tmp[sizeof(tmp) - 1] = '\0';
	send_text(TEXT_LINE1, TEXT_NORMAL, pte, tmp);
	memcpy(tmp, text + TEXT_LENGTH_MAX * 2, TEXT_LENGTH_MAX);
	tmp[sizeof(tmp) - 1] = '\0';
	send_text(TEXT_LINE2, TEXT_NORMAL, pte, tmp);
	return 0;
}

/*--- unistim_send_mwi_to_peer: Send message waiting indication ---*/
static int unistim_send_mwi_to_peer(struct unistimsession *s, unsigned int tick)
{
	struct ast_event *event;
	int new;
	char *mailbox, *context;
	struct unistim_line *peer = s->device->lines;

	context = mailbox = ast_strdupa(peer->mailbox);
	strsep(&context, "@");
	if (ast_strlen_zero(context))
		context = "default";

	event = ast_event_get_cached(AST_EVENT_MWI,
		AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_STR, mailbox,
		AST_EVENT_IE_CONTEXT, AST_EVENT_IE_PLTYPE_STR, context,
		AST_EVENT_IE_END);

	if (event) {
		new = ast_event_get_ie_uint(event, AST_EVENT_IE_NEWMSGS);
		ast_event_destroy(event);
	} else { /* Fall back on checking the mailbox directly */
		new = ast_app_has_voicemail(peer->mailbox, "INBOX");
	}

	peer->nextmsgcheck = tick + TIMER_MWI;

	/* Return now if it's the same thing we told them last time */
	if (new == peer->lastmsgssent) {
		return 0;
	}

	peer->lastmsgssent = new;
	if (new == 0) {
		send_led_update(s, 0);
	} else {
		send_led_update(s, 1);
	}

	return 0;
}

/*--- unistim_new: Initiate a call in the UNISTIM channel */
/*      called from unistim_request (calls from the pbx ) */
static struct ast_channel *unistim_new(struct unistim_subchannel *sub, int state, const char *linkedid)
{
	struct ast_channel *tmp;
	struct unistim_line *l;
	struct ast_format tmpfmt;

	if (!sub) {
		ast_log(LOG_WARNING, "subchannel null in unistim_new\n");
		return NULL;
	}
	if (!sub->parent) {
		ast_log(LOG_WARNING, "no line for subchannel %p\n", sub);
		return NULL;
	}
	l = sub->parent;
	tmp = ast_channel_alloc(1, state, l->cid_num, NULL, l->accountcode, l->exten,
		l->context, linkedid, l->amaflags, "%s@%s-%d", l->name, l->parent->name, sub->subtype);
	if (unistimdebug)
		ast_verb(0, "unistim_new sub=%d (%p) chan=%p\n", sub->subtype, sub, tmp);
	if (!tmp) {
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
		return NULL;
	}

	ast_format_cap_copy(tmp->nativeformats, l->cap);
	if (ast_format_cap_is_empty(tmp->nativeformats))
		ast_format_cap_copy(tmp->nativeformats, global_cap);
	ast_best_codec(tmp->nativeformats, &tmpfmt);
	if (unistimdebug) {
		char tmp1[256], tmp2[256], tmp3[256];
		ast_verb(0, "Best codec = %s from nativeformats %s (line cap=%s global=%s)\n",
			ast_getformatname(&tmpfmt),
			ast_getformatname_multiple(tmp1, sizeof(tmp1), tmp->nativeformats),
			ast_getformatname_multiple(tmp2, sizeof(tmp2), l->cap),
			ast_getformatname_multiple(tmp3, sizeof(tmp3), global_cap));
	}
	if ((sub->rtp) && (sub->subtype == 0)) {
		if (unistimdebug)
			ast_verb(0, "New unistim channel with a previous rtp handle ?\n");
		tmp->fds[0] = ast_rtp_instance_fd(sub->rtp, 0);
		tmp->fds[1] = ast_rtp_instance_fd(sub->rtp, 1);
	}
	if (sub->rtp)
		ast_jb_configure(tmp, &global_jbconf);
		
/*      tmp->type = type; */
	ast_setstate(tmp, state);
	if (state == AST_STATE_RING)
		tmp->rings = 1;
	tmp->adsicpe = AST_ADSI_UNAVAILABLE;
	ast_format_copy(&tmp->writeformat, &tmpfmt);
	ast_format_copy(&tmp->rawwriteformat, &tmpfmt);
	ast_format_copy(&tmp->readformat, &tmpfmt);
	ast_format_copy(&tmp->rawreadformat, &tmpfmt);
	tmp->tech_pvt = sub;
	tmp->tech = &unistim_tech;
	if (!ast_strlen_zero(l->language))
		ast_string_field_set(tmp, language, l->language);
	sub->owner = tmp;
	ast_mutex_lock(&usecnt_lock);
	usecnt++;
	ast_mutex_unlock(&usecnt_lock);
	ast_update_use_count();
	tmp->callgroup = l->callgroup;
	tmp->pickupgroup = l->pickupgroup;
	ast_string_field_set(tmp, call_forward, l->parent->call_forward);
	if (!ast_strlen_zero(l->cid_num)) {
		char *name, *loc, *instr;
		instr = ast_strdup(l->cid_num);
		if (instr) {
			ast_callerid_parse(instr, &name, &loc);
			tmp->caller.id.number.valid = 1;
			ast_free(tmp->caller.id.number.str);
			tmp->caller.id.number.str = ast_strdup(loc);
			tmp->caller.id.name.valid = 1;
			ast_free(tmp->caller.id.name.str);
			tmp->caller.id.name.str = ast_strdup(name);
			ast_free(instr);
		}
	}
	tmp->priority = 1;
	if (state != AST_STATE_DOWN) {
		if (unistimdebug)
			ast_verb(0, "Starting pbx in unistim_new\n");
		if (ast_pbx_start(tmp)) {
			ast_log(LOG_WARNING, "Unable to start PBX on %s\n", tmp->name);
			ast_hangup(tmp);
			tmp = NULL;
		}
	}

	return tmp;
}

static void *do_monitor(void *data)
{
	struct unistimsession *cur = NULL;
	unsigned int dw_timeout = 0;
	unsigned int tick;
	int res;
	int reloading;

	/* Add an I/O event to our UDP socket */
	if (unistimsock > -1)
		ast_io_add(io, unistimsock, unistimsock_read, AST_IO_IN, NULL);

	/* This thread monitors our UDP socket and timers */
	for (;;) {
		/* This loop is executed at least every IDLE_WAITus (1s) or every time a packet is received */
		/* Looking for the smallest time-out value */
		tick = get_tick_count();
		dw_timeout = UINT_MAX;
		ast_mutex_lock(&sessionlock);
		cur = sessions;
		DEBUG_TIMER("checking timeout for session %p with tick = %u\n", cur, tick);
		while (cur) {
			DEBUG_TIMER("checking timeout for session %p timeout = %u\n", cur,
						cur->timeout);
			/* Check if we have miss something */
			if (cur->timeout <= tick) {
				DEBUG_TIMER("Event for session %p\n", cur);
				/* If the queue is empty, send a ping */
				if (cur->last_buf_available == 0)
					send_ping(cur);
				else {
					if (send_retransmit(cur)) {
						DEBUG_TIMER("The chained link was modified, restarting...\n");
						cur = sessions;
						dw_timeout = UINT_MAX;
						continue;
					}
				}
			}
			if (dw_timeout > cur->timeout - tick)
				dw_timeout = cur->timeout - tick;
			/* Checking if the phone is logged on for a new MWI */
			if (cur->device) {
				if ((!ast_strlen_zero(cur->device->lines->mailbox)) &&
					((tick >= cur->device->lines->nextmsgcheck))) {
					DEBUG_TIMER("Checking mailbox for MWI\n");
					unistim_send_mwi_to_peer(cur, tick);
					break;
				}
			}
			cur = cur->next;
		}
		ast_mutex_unlock(&sessionlock);
		DEBUG_TIMER("Waiting for %dus\n", dw_timeout);
		res = dw_timeout;
		/* We should not wait more than IDLE_WAIT */
		if ((res < 0) || (res > IDLE_WAIT))
			res = IDLE_WAIT;
		/* Wait for UDP messages for a maximum of res us */
		res = ast_io_wait(io, res);     /* This function will call unistimsock_read if a packet is received */
		/* Check for a reload request */
		ast_mutex_lock(&unistim_reload_lock);
		reloading = unistim_reloading;
		unistim_reloading = 0;
		ast_mutex_unlock(&unistim_reload_lock);
		if (reloading) {
			ast_verb(1, "Reloading unistim.conf...\n");
			reload_config();
		}
		pthread_testcancel();
	}
	/* Never reached */
	return NULL;
}

/*--- restart_monitor: Start the channel monitor thread ---*/
static int restart_monitor(void)
{
	pthread_attr_t attr;
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
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
		/* Start a new monitor */
		if (ast_pthread_create(&monitor_thread, &attr, do_monitor, NULL) < 0) {
			ast_mutex_unlock(&monlock);
			ast_log(LOG_ERROR, "Unable to start monitor thread.\n");
			return -1;
		}
	}
	ast_mutex_unlock(&monlock);
	return 0;
}

/*--- unistim_request: PBX interface function ---*/
/* UNISTIM calls initiated by the PBX arrive here */
static struct ast_channel *unistim_request(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, void *data,
										   int *cause)
{
	struct unistim_subchannel *sub;
	struct ast_channel *tmpc = NULL;
	char tmp[256];
	char tmp2[256];
	char *dest = data;

	if (!(ast_format_cap_has_joint(cap, global_cap))) {
		ast_log(LOG_NOTICE,
				"Asked to get a channel of unsupported format %s while capability is %s\n",
				ast_getformatname_multiple(tmp2, sizeof(tmp2), cap), ast_getformatname_multiple(tmp, sizeof(tmp), global_cap));
		return NULL;
	}

	ast_copy_string(tmp, dest, sizeof(tmp));
	if (ast_strlen_zero(tmp)) {
		ast_log(LOG_NOTICE, "Unistim channels require a device\n");
		return NULL;
	}

	sub = find_subchannel_by_name(tmp);
	if (!sub) {
		ast_log(LOG_NOTICE, "No available lines on: %s\n", dest);
		*cause = AST_CAUSE_CONGESTION;
		return NULL;
	}

	ast_verb(3, "unistim_request(%s)\n", tmp);
	/* Busy ? */
	if (sub->owner) {
		if (unistimdebug)
			ast_verb(0, "Can't create channel : Busy !\n");
		*cause = AST_CAUSE_BUSY;
		return NULL;
	}
	ast_format_cap_copy(sub->parent->cap, cap);
	tmpc = unistim_new(sub, AST_STATE_DOWN, requestor ? requestor->linkedid : NULL);
	if (!tmpc)
		ast_log(LOG_WARNING, "Unable to make channel for '%s'\n", tmp);
	if (unistimdebug)
		ast_verb(0, "unistim_request owner = %p\n", sub->owner);
	restart_monitor();

	/* and finish */
	return tmpc;
}

static char *unistim_info(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct unistim_device *device = devices;
	struct unistim_line *line;
	struct unistim_subchannel *sub;
	struct unistimsession *s;
	int i;
	struct ast_channel *tmp;

	switch (cmd) {
	case CLI_INIT:
		e->command = "unistim show info";
		e->usage =
			"Usage: unistim show info\n" 
			"       Dump internal structures.\n";
		return NULL;

	case CLI_GENERATE:
		return NULL;	/* no completion */
	}

	if (a->argc != e->args)
		return CLI_SHOWUSAGE;

	ast_cli(a->fd, "Dumping internal structures :\ndevice\n->line\n-->sub\n");
	while (device) {
		ast_cli(a->fd, "\nname=%s id=%s line=%p ha=%p sess=%p device=%p\n",
				device->name, device->id, device->lines, device->ha, device->session,
				device);
		line = device->lines;
		while (line) {
			char tmp2[256];
			ast_cli(a->fd,
					"->name=%s fullname=%s exten=%s callid=%s cap=%s device=%p line=%p\n",
					line->name, line->fullname, line->exten, line->cid_num,
					ast_getformatname_multiple(tmp2, sizeof(tmp2), line->cap), line->parent, line);
			for (i = 0; i < MAX_SUBS; i++) {
				sub = line->subs[i];
				if (!sub)
					continue;
				if (!sub->owner)
					tmp = (void *) -42;
				else
					tmp = sub->owner->_bridge;
				if (sub->subtype != i)
					ast_cli(a->fd, "Warning ! subchannel->subs[%d] have a subtype=%d\n", i,
							sub->subtype);
				ast_cli(a->fd,
						"-->subtype=%d chan=%p rtp=%p bridge=%p line=%p alreadygone=%d\n",
						sub->subtype, sub->owner, sub->rtp, tmp, sub->parent,
						sub->alreadygone);
			}
			line = line->next;
		}
		device = device->next;
	}
	ast_cli(a->fd, "\nSessions:\n");
	ast_mutex_lock(&sessionlock);
	s = sessions;
	while (s) {
		ast_cli(a->fd,
				"sin=%s timeout=%u state=%d macaddr=%s device=%p session=%p\n",
				ast_inet_ntoa(s->sin.sin_addr), s->timeout, s->state, s->macaddr,
				s->device, s);
		s = s->next;
	}
	ast_mutex_unlock(&sessionlock);

	return CLI_SUCCESS;
}

static char *unistim_sp(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	BUFFSEND;
	struct unistim_subchannel *sub;
	int i, j = 0, len;
	unsigned char c, cc;
	char tmp[256];

	switch (cmd) {
	case CLI_INIT:
		e->command = "unistim send packet";
		e->usage =
			"Usage: unistim send packet USTM/line@name hexa\n"
			"       unistim send packet USTM/1000@hans 19040004\n";
		return NULL;

	case CLI_GENERATE:
		return NULL;	/* no completion */
	}
	
	if (a->argc < 5)
		return CLI_SHOWUSAGE;

	if (strlen(a->argv[3]) < 9)
		return CLI_SHOWUSAGE;

	len = strlen(a->argv[4]);
	if (len % 2)
		return CLI_SHOWUSAGE;

	ast_copy_string(tmp, a->argv[3] + 5, sizeof(tmp));
	sub = find_subchannel_by_name(tmp);
	if (!sub) {
		ast_cli(a->fd, "Can't find '%s'\n", tmp);
		return CLI_SUCCESS;
	}
	if (!sub->parent->parent->session) {
		ast_cli(a->fd, "'%s' is not connected\n", tmp);
		return CLI_SUCCESS;
	}
	ast_cli(a->fd, "Sending '%s' to %s (%p)\n", a->argv[4], tmp, sub->parent->parent->session);
	for (i = 0; i < len; i++) {
		c = a->argv[4][i];
		if (c >= 'a')
			c -= 'a' - 10;
		else
			c -= '0';
		i++;
		cc = a->argv[4][i];
		if (cc >= 'a')
			cc -= 'a' - 10;
		else
			cc -= '0';
		tmp[j++] = (c << 4) | cc;
	}
	memcpy(buffsend + SIZE_HEADER, tmp, j);
	send_client(SIZE_HEADER + j, buffsend, sub->parent->parent->session);
	return CLI_SUCCESS;
}

static char *unistim_do_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "unistim set debug {on|off}";
		e->usage =
			"Usage: unistim set debug\n" 
			"       Display debug messages.\n";
		return NULL;

	case CLI_GENERATE:
		return NULL;	/* no completion */
	}

	if (a->argc != e->args)
		return CLI_SHOWUSAGE;

	if (!strcasecmp(a->argv[3], "on")) {
		unistimdebug = 1;
		ast_cli(a->fd, "UNISTIM Debugging Enabled\n");
	} else if (!strcasecmp(a->argv[3], "off")) {
		unistimdebug = 0;
		ast_cli(a->fd, "UNISTIM Debugging Disabled\n");
	} else
		return CLI_SHOWUSAGE;

	return CLI_SUCCESS;
}

/*! \brief --- unistim_reload: Force reload of module from cli ---
 * Runs in the asterisk main thread, so don't do anything useful
 * but setting a flag and waiting for do_monitor to do the job
 * in our thread */
static char *unistim_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "unistim reload";
		e->usage =
			"Usage: unistim reload\n" 
			"       Reloads UNISTIM configuration from unistim.conf\n";
		return NULL;

	case CLI_GENERATE:
		return NULL;	/* no completion */
	}

	if (e && a && a->argc != e->args)
		return CLI_SHOWUSAGE;

	if (unistimdebug)
		ast_verb(0, "reload unistim\n");

	ast_mutex_lock(&unistim_reload_lock);
	if (!unistim_reloading)
		unistim_reloading = 1;
	ast_mutex_unlock(&unistim_reload_lock);

	restart_monitor();

	return CLI_SUCCESS;
}

static struct ast_cli_entry unistim_cli[] = {
	AST_CLI_DEFINE(unistim_reload, "Reload UNISTIM configuration"),
	AST_CLI_DEFINE(unistim_info, "Show UNISTIM info"),
	AST_CLI_DEFINE(unistim_sp, "Send packet (for reverse engineering)"),
	AST_CLI_DEFINE(unistim_do_debug, "Toggle UNITSTIM debugging"),
};

static void unquote(char *out, const char *src, int maxlen)
{
	int len = strlen(src);
	if (!len)
		return;
	if ((len > 1) && src[0] == '\"') {
		/* This is a quoted string */
		src++;
		/* Don't take more than what's there */
		len--;
		if (maxlen > len - 1)
			maxlen = len - 1;
		memcpy(out, src, maxlen);
		((char *) out)[maxlen] = '\0';
	} else
		memcpy(out, src, maxlen);
	return;
}

static int ParseBookmark(const char *text, struct unistim_device *d)
{
	char line[256];
	char *at;
	char *number;
	char *icon;
	int p;
	int len = strlen(text);

	ast_copy_string(line, text, sizeof(line));
	/* Position specified ? */
	if ((len > 2) && (line[1] == '@')) {
		p = line[0];
		if ((p >= '0') && (p <= '5'))
			p -= '0';
		else {
			ast_log(LOG_WARNING,
					"Invalid position for bookmark : must be between 0 and 5\n");
			return 0;
		}
		if (d->softkeyicon[p] != 0) {
			ast_log(LOG_WARNING, "Invalid position %d for bookmark : already used\n:", p);
			return 0;
		}
		memmove(line, line + 2, sizeof(line));
	} else {
		/* No position specified, looking for a free slot */
		for (p = 0; p <= 5; p++) {
			if (!d->softkeyicon[p])
				break;
		}
		if (p > 5) {
			ast_log(LOG_WARNING, "No more free bookmark position\n");
			return 0;
		}
	}
	at = strchr(line, '@');
	if (!at) {
		ast_log(LOG_NOTICE, "Bookmark entry '%s' has no @ (at) sign!\n", text);
		return 0;
	}
	*at = '\0';
	at++;
	number = at;
	at = strchr(at, '@');
	if (ast_strlen_zero(number)) {
		ast_log(LOG_NOTICE, "Bookmark entry '%s' has no number\n", text);
		return 0;
	}
	if (ast_strlen_zero(line)) {
		ast_log(LOG_NOTICE, "Bookmark entry '%s' has no description\n", text);
		return 0;
	}

	at = strchr(number, '@');
	if (!at)
		d->softkeyicon[p] = FAV_ICON_SHARP;     /* default icon */
	else {
		*at = '\0';
		at++;
		icon = at;
		if (ast_strlen_zero(icon)) {
			ast_log(LOG_NOTICE, "Bookmark entry '%s' has no icon value\n", text);
			return 0;
		}
		if (strncmp(icon, "USTM/", 5))
			d->softkeyicon[p] = atoi(icon);
		else {
			d->softkeyicon[p] = 1;
			ast_copy_string(d->softkeydevice[p], icon + 5, sizeof(d->softkeydevice[p]));
		}
	}
	ast_copy_string(d->softkeylabel[p], line, sizeof(d->softkeylabel[p]));
	ast_copy_string(d->softkeynumber[p], number, sizeof(d->softkeynumber[p]));
	if (unistimdebug)
		ast_verb(0, "New bookmark at pos %d label='%s' number='%s' icon=%x\n",
					p, d->softkeylabel[p], d->softkeynumber[p], d->softkeyicon[p]);
	return 1;
}

/* Looking for dynamic icons entries in bookmarks */
static void finish_bookmark(void)
{
	struct unistim_device *d = devices;
	int i;
	while (d) {
		for (i = 0; i < 6; i++) {
			if (d->softkeyicon[i] == 1) {   /* Something for us */
				struct unistim_device *d2 = devices;
				while (d2) {
					if (!strcmp(d->softkeydevice[i], d2->name)) {
						d->sp[i] = d2;
						d->softkeyicon[i] = 0;
						break;
					}
					d2 = d2->next;
				}
				if (d->sp[i] == NULL)
					ast_log(LOG_NOTICE, "Bookmark entry with device %s not found\n",
							d->softkeydevice[i]);
			}
		}
		d = d->next;
	}
}


static struct unistim_device *build_device(const char *cat, const struct ast_variable *v)
{
	struct unistim_device *d;
	struct unistim_line *l = NULL;
	int create = 1;
	int nbsoftkey, dateformat, timeformat, callhistory;
	char linelabel[AST_MAX_EXTENSION];
	char context[AST_MAX_EXTENSION];
	char ringvolume, ringstyle;

	/* First, we need to know if we already have this name in our list */
	/* Get a lock for the device chained list */
	ast_mutex_lock(&devicelock);
	d = devices;
	while (d) {
		if (!strcmp(d->name, cat)) {
			/* Yep, we alreay have this one */
			if (unistimsock < 0) {
				/* It's a dupe */
				ast_log(LOG_WARNING, "Duplicate entry found (%s), ignoring.\n", cat);
				ast_mutex_unlock(&devicelock);
				return NULL;
			}
			/* we're reloading right now */
			create = 0;
			l = d->lines;
			break;
		}
		d = d->next;
	}
	ast_mutex_unlock(&devicelock);
	if (create) {
		if (!(d = ast_calloc(1, sizeof(*d))))
			return NULL;

		if (!(l = unistim_line_alloc())) {
			ast_free(d);
			return NULL;
		}
		ast_copy_string(d->name, cat, sizeof(d->name));
	}
	ast_copy_string(context, DEFAULTCONTEXT, sizeof(context));
	d->contrast = -1;
	d->output = OUTPUT_HANDSET;
	d->previous_output = OUTPUT_HANDSET;
	d->volume = VOLUME_LOW;
	d->mute = MUTE_OFF;
	d->height = DEFAULTHEIGHT;
	linelabel[0] = '\0';
	dateformat = 1;
	timeformat = 1;
	ringvolume = 2;
	callhistory = 1;
	ringstyle = 3;
	nbsoftkey = 0;
	while (v) {
		if (!strcasecmp(v->name, "rtp_port"))
			d->rtp_port = atoi(v->value);
		else if (!strcasecmp(v->name, "rtp_method"))
			d->rtp_method = atoi(v->value);
		else if (!strcasecmp(v->name, "status_method"))
			d->status_method = atoi(v->value);
		else if (!strcasecmp(v->name, "device"))
			ast_copy_string(d->id, v->value, sizeof(d->id));
		else if (!strcasecmp(v->name, "tn"))
			ast_copy_string(d->extension_number, v->value, sizeof(d->extension_number));
		else if (!strcasecmp(v->name, "permit") || !strcasecmp(v->name, "deny"))
			d->ha = ast_append_ha(v->name, v->value, d->ha, NULL);
		else if (!strcasecmp(v->name, "context"))
			ast_copy_string(context, v->value, sizeof(context));
		else if (!strcasecmp(v->name, "maintext0"))
			unquote(d->maintext0, v->value, sizeof(d->maintext0) - 1);
		else if (!strcasecmp(v->name, "maintext1"))
			unquote(d->maintext1, v->value, sizeof(d->maintext1) - 1);
		else if (!strcasecmp(v->name, "maintext2"))
			unquote(d->maintext2, v->value, sizeof(d->maintext2) - 1);
		else if (!strcasecmp(v->name, "titledefault"))
			unquote(d->titledefault, v->value, sizeof(d->titledefault) - 1);
		else if (!strcasecmp(v->name, "dateformat"))
			dateformat = atoi(v->value);
		else if (!strcasecmp(v->name, "timeformat"))
			timeformat = atoi(v->value);
		else if (!strcasecmp(v->name, "contrast")) {
			d->contrast = atoi(v->value);
			if ((d->contrast < 0) || (d->contrast > 15)) {
				ast_log(LOG_WARNING, "constrast must be beetween 0 and 15");
				d->contrast = 8;
			}
		} else if (!strcasecmp(v->name, "nat"))
			d->nat = ast_true(v->value);
		else if (!strcasecmp(v->name, "ringvolume"))
			ringvolume = atoi(v->value);
		else if (!strcasecmp(v->name, "ringstyle"))
			ringstyle = atoi(v->value);
		else if (!strcasecmp(v->name, "callhistory"))
			callhistory = atoi(v->value);
		else if (!strcasecmp(v->name, "callerid")) {
			if (!strcasecmp(v->value, "asreceived"))
				l->cid_num[0] = '\0';
			else
				ast_copy_string(l->cid_num, v->value, sizeof(l->cid_num));
		} else if (!strcasecmp(v->name, "language"))
			ast_copy_string(l->language, v->value, sizeof(l->language));
		else if (!strcasecmp(v->name, "country"))
			ast_copy_string(d->country, v->value, sizeof(d->country));
		else if (!strcasecmp(v->name, "accountcode"))
			ast_copy_string(l->accountcode, v->value, sizeof(l->accountcode));
		else if (!strcasecmp(v->name, "amaflags")) {
			int y;
			y = ast_cdr_amaflags2int(v->value);
			if (y < 0)
				ast_log(LOG_WARNING, "Invalid AMA flags: %s at line %d\n", v->value,
						v->lineno);
			else
				l->amaflags = y;
		} else if (!strcasecmp(v->name, "musiconhold"))
			ast_copy_string(l->musicclass, v->value, sizeof(l->musicclass));
		else if (!strcasecmp(v->name, "callgroup"))
			l->callgroup = ast_get_group(v->value);
		else if (!strcasecmp(v->name, "pickupgroup"))
			l->pickupgroup = ast_get_group(v->value);
		else if (!strcasecmp(v->name, "mailbox"))
			ast_copy_string(l->mailbox, v->value, sizeof(l->mailbox));
		else if (!strcasecmp(v->name, "parkinglot"))
			ast_copy_string(l->parkinglot, v->value, sizeof(l->parkinglot));
		else if (!strcasecmp(v->name, "linelabel"))
			unquote(linelabel, v->value, sizeof(linelabel) - 1);
		else if (!strcasecmp(v->name, "extension")) {
			if (!strcasecmp(v->value, "none"))
				d->extension = EXTENSION_NONE;
			else if (!strcasecmp(v->value, "ask"))
				d->extension = EXTENSION_ASK;
			else if (!strcasecmp(v->value, "line"))
				d->extension = EXTENSION_LINE;
			else
				ast_log(LOG_WARNING, "Unknown extension option.\n");
		} else if (!strcasecmp(v->name, "bookmark")) {
			if (nbsoftkey > 5)
				ast_log(LOG_WARNING,
						"More than 6 softkeys defined. Ignoring new entries.\n");
			else {
				if (ParseBookmark(v->value, d))
					nbsoftkey++;
			}
		} else if (!strcasecmp(v->name, "line")) {
			int len = strlen(linelabel);

			if (nbsoftkey) {
				ast_log(LOG_WARNING,
						"You must use bookmark AFTER line=>. Only one line is supported in this version\n");
				if (create) {
					ast_free(d);
					unistim_line_destroy(l);
				}
				return NULL;
			}
			if (create) {
				ast_mutex_init(&l->lock);
			} else {
				d->to_delete = 0;
				/* reset bookmarks */
				memset(d->softkeylabel, 0, sizeof(d->softkeylabel));
				memset(d->softkeynumber, 0, sizeof(d->softkeynumber));
				memset(d->softkeyicon, 0, sizeof(d->softkeyicon));
				memset(d->softkeydevice, 0, sizeof(d->softkeydevice));
				memset(d->sp, 0, sizeof(d->sp));
			}
			ast_copy_string(l->name, v->value, sizeof(l->name));
			snprintf(l->fullname, sizeof(l->fullname), "USTM/%s@%s", l->name, d->name);
			d->softkeyicon[0] = FAV_ICON_ONHOOK_BLACK;
			if (!len)		       /* label is undefined ? */
				ast_copy_string(d->softkeylabel[0], v->value, sizeof(d->softkeylabel[0]));
			else {
				if ((len > 2) && (linelabel[1] == '@')) {
					d->softkeylinepos = linelabel[0];
					if ((d->softkeylinepos >= '0') && (d->softkeylinepos <= '5')) {
						d->softkeylinepos -= '0';
						d->softkeyicon[0] = 0;
					} else {
						ast_log(LOG_WARNING,
								"Invalid position for linelabel : must be between 0 and 5\n");
						d->softkeylinepos = 0;
					}
					ast_copy_string(d->softkeylabel[d->softkeylinepos], linelabel + 2,
									sizeof(d->softkeylabel[d->softkeylinepos]));
					d->softkeyicon[d->softkeylinepos] = FAV_ICON_ONHOOK_BLACK;
				} else
					ast_copy_string(d->softkeylabel[0], linelabel,
									sizeof(d->softkeylabel[0]));
			}
			nbsoftkey++;
			ast_copy_string(l->context, context, sizeof(l->context));
			if (!ast_strlen_zero(l->mailbox)) {
				if (unistimdebug)
					ast_verb(3, "Setting mailbox '%s' on %s@%s\n", l->mailbox, d->name, l->name);
			}

			ast_format_cap_copy(l->cap, global_cap);
			l->parent = d;

			if (create) {
				if (!alloc_sub(l, SUB_REAL)) {
					ast_mutex_destroy(&l->lock);
					unistim_line_destroy(l);
					ast_free(d);
					return NULL;
				}
				l->next = d->lines;
				d->lines = l;
			}
		} else if (!strcasecmp(v->name, "height")) {
			/* Allow the user to lower the expected display lines on the phone
			 * For example the Nortal I2001 and I2002 only have one ! */
			d->height = atoi(v->value);
		} else
			ast_log(LOG_WARNING, "Don't know keyword '%s' at line %d\n", v->name,
					v->lineno);
		v = v->next;
	}
	d->ringvolume = ringvolume;
	d->ringstyle = ringstyle;
	d->callhistory = callhistory;
	d->tz = ast_get_indication_zone(d->country);
	if ((d->tz == NULL) && !ast_strlen_zero(d->country))
		ast_log(LOG_WARNING, "Country '%s' was not found in indications.conf\n",
				d->country);
	d->datetimeformat = 56 + (dateformat * 4);
	d->datetimeformat += timeformat;
	if (!d->lines) {
		ast_log(LOG_ERROR, "An Unistim device must have at least one line!\n");
		ast_mutex_destroy(&l->lock);
		unistim_line_destroy(l);
		if (d->tz) {
			d->tz = ast_tone_zone_unref(d->tz);
		}
		ast_free(d);
		return NULL;
	}
	if ((autoprovisioning == AUTOPROVISIONING_TN) &&
		(!ast_strlen_zero(d->extension_number))) {
		d->extension = EXTENSION_TN;
		if (!ast_strlen_zero(d->id))
			ast_log(LOG_WARNING,
					"tn= and device= can't be used together. Ignoring device= entry\n");
		d->id[0] = 'T';		 /* magic : this is a tn entry */
		ast_copy_string((d->id) + 1, d->extension_number, sizeof(d->id) - 1);
		d->extension_number[0] = '\0';
	} else if (ast_strlen_zero(d->id)) {
		if (strcmp(d->name, "template")) {
			ast_log(LOG_ERROR, "You must specify the mac address with device=\n");
			ast_mutex_destroy(&l->lock);
			unistim_line_destroy(l);
			if (d->tz) {
				d->tz = ast_tone_zone_unref(d->tz);
			}
			ast_free(d);
			return NULL;
		} else
			strcpy(d->id, "000000000000");
	}
	if (!d->rtp_port)
		d->rtp_port = 10000;
	if (d->contrast == -1)
		d->contrast = 8;
	if (ast_strlen_zero(d->maintext0))
		strcpy(d->maintext0, "Welcome");
	if (ast_strlen_zero(d->maintext1))
		strcpy(d->maintext1, d->name);
	if (ast_strlen_zero(d->titledefault)) {
		struct ast_tm tm = { 0, };
		struct timeval cur_time = ast_tvnow();

		if ((ast_localtime(&cur_time, &tm, 0)) == 0 || ast_strlen_zero(tm.tm_zone)) {
			display_last_error("Error in ast_localtime()");
			ast_copy_string(d->titledefault, "UNISTIM for*", 12);
		} else {
			if (strlen(tm.tm_zone) < 4) {
				strcpy(d->titledefault, "TimeZone ");
				strcat(d->titledefault, tm.tm_zone);
			} else if (strlen(tm.tm_zone) < 9) {
				strcpy(d->titledefault, "TZ ");
				strcat(d->titledefault, tm.tm_zone);
			} else
				ast_copy_string(d->titledefault, tm.tm_zone, 12);
		}
	}
	/* Update the chained link if it's a new device */
	if (create) {
		ast_mutex_lock(&devicelock);
		d->next = devices;
		devices = d;
		ast_mutex_unlock(&devicelock);
		ast_verb(3, "Added device '%s'\n", d->name);
	} else {
		ast_verb(3, "Device '%s' reloaded\n", d->name);
	}
	return d;
}

/*--- reload_config: Re-read unistim.conf config file ---*/
static int reload_config(void)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct ast_hostent ahp;
	struct hostent *hp;
	struct sockaddr_in bindaddr = { 0, };
	char *config = "unistim.conf";
	char *cat;
	struct unistim_device *d;
	const int reuseFlag = 1;
	struct unistimsession *s;
	struct ast_flags config_flags = { 0, };

	cfg = ast_config_load(config, config_flags);
	/* We *must* have a config file otherwise stop immediately */
	if (!cfg) {
		ast_log(LOG_ERROR, "Unable to load config %s\n", config);
		return -1;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file %s is in an invalid format.  Aborting.\n", config);
		return -1;
	}
	
	/* Copy the default jb config over global_jbconf */
	memcpy(&global_jbconf, &default_jbconf, sizeof(struct ast_jb_conf));

	unistim_keepalive = 120;
	unistim_port = 0;
	v = ast_variable_browse(cfg, "general");
	while (v) {
		/* handle jb conf */
		if (!ast_jb_read_conf(&global_jbconf, v->name, v->value))
			continue;	
	
		if (!strcasecmp(v->name, "keepalive"))
			unistim_keepalive = atoi(v->value);
		else if (!strcasecmp(v->name, "port"))
			unistim_port = atoi(v->value);
                else if (!strcasecmp(v->name, "tos")) {
                        if (ast_str2tos(v->value, &qos.tos))
                            ast_log(LOG_WARNING, "Invalid tos value at line %d, refer to QoS documentation\n", v->lineno);
                } else if (!strcasecmp(v->name, "tos_audio")) {
                        if (ast_str2tos(v->value, &qos.tos_audio))
                            ast_log(LOG_WARNING, "Invalid tos_audio value at line %d, refer to QoS documentation\n", v->lineno);
                } else if (!strcasecmp(v->name, "cos")) {
                        if (ast_str2cos(v->value, &qos.cos))
                            ast_log(LOG_WARNING, "Invalid cos value at line %d, refer to QoS documentation\n", v->lineno);
                } else if (!strcasecmp(v->name, "cos_audio")) {
                        if (ast_str2cos(v->value, &qos.cos_audio))
                            ast_log(LOG_WARNING, "Invalid cos_audio value at line %d, refer to QoS documentation\n", v->lineno);
		} else if (!strcasecmp(v->name, "autoprovisioning")) {
			if (!strcasecmp(v->value, "no"))
				autoprovisioning = AUTOPROVISIONING_NO;
			else if (!strcasecmp(v->value, "yes"))
				autoprovisioning = AUTOPROVISIONING_YES;
			else if (!strcasecmp(v->value, "db"))
				autoprovisioning = AUTOPROVISIONING_DB;
			else if (!strcasecmp(v->value, "tn"))
				autoprovisioning = AUTOPROVISIONING_TN;
			else
				ast_log(LOG_WARNING, "Unknown autoprovisioning option.\n");
		} else if (!strcasecmp(v->name, "public_ip")) {
			if (!ast_strlen_zero(v->value)) {
				if (!(hp = ast_gethostbyname(v->value, &ahp)))
					ast_log(LOG_WARNING, "Invalid address: %s\n", v->value);
				else {
					memcpy(&public_ip.sin_addr, hp->h_addr, sizeof(public_ip.sin_addr));
					public_ip.sin_family = AF_INET;
				}
			}
		}
		v = v->next;
	}
	if ((unistim_keepalive < 10) ||
		(unistim_keepalive >
		 255 - (((NB_MAX_RETRANSMIT + 1) * RETRANSMIT_TIMER) / 1000))) {
		ast_log(LOG_ERROR, "keepalive is invalid in %s\n", config);
		ast_config_destroy(cfg);
		return -1;
	}
	packet_send_ping[4] =
		unistim_keepalive + (((NB_MAX_RETRANSMIT + 1) * RETRANSMIT_TIMER) / 1000);
	if ((unistim_port < 1) || (unistim_port > 65535)) {
		ast_log(LOG_ERROR, "port is not set or invalid in %s\n", config);
		ast_config_destroy(cfg);
		return -1;
	}
	unistim_keepalive *= 1000;

	ast_mutex_lock(&devicelock);
	d = devices;
	while (d) {
		if (d->to_delete >= 0)
			d->to_delete = 1;
		d = d->next;
	}
	ast_mutex_unlock(&devicelock);
	/* load the device sections */
	cat = ast_category_browse(cfg, NULL);
	while (cat) {
		if (strcasecmp(cat, "general")) {
			d = build_device(cat, ast_variable_browse(cfg, cat));
		}
		cat = ast_category_browse(cfg, cat);
	}
	ast_mutex_lock(&devicelock);
	d = devices;
	while (d) {
		if (d->to_delete) {
			int i;

			if (unistimdebug)
				ast_verb(0, "Removing device '%s'\n", d->name);
			if (!d->lines) {
				ast_log(LOG_ERROR, "Device '%s' without a line !, aborting\n", d->name);
				ast_config_destroy(cfg);
				return 0;
			}
			if (!d->lines->subs[0]) {
				ast_log(LOG_ERROR, "Device '%s' without a subchannel !, aborting\n",
						d->name);
				ast_config_destroy(cfg);
				return 0;
			}
			if (d->lines->subs[0]->owner) {
				ast_log(LOG_WARNING,
						"Device '%s' was not deleted : a call is in progress. Try again later.\n",
						d->name);
				d = d->next;
				continue;
			}
			ast_mutex_destroy(&d->lines->subs[0]->lock);
			ast_free(d->lines->subs[0]);
			for (i = 1; i < MAX_SUBS; i++) {
				if (d->lines->subs[i]) {
					ast_log(LOG_WARNING,
							"Device '%s' with threeway call subchannels allocated, aborting.\n",
							d->name);
					break;
				}
			}
			if (i < MAX_SUBS) {
				d = d->next;
				continue;
			}
			ast_mutex_destroy(&d->lines->lock);
			ast_free(d->lines);
			if (d->session) {
				if (sessions == d->session)
					sessions = d->session->next;
				else {
					s = sessions;
					while (s) {
						if (s->next == d->session) {
							s->next = d->session->next;
							break;
						}
						s = s->next;
					}
				}
				ast_mutex_destroy(&d->session->lock);
				ast_free(d->session);
			}
			if (devices == d)
				devices = d->next;
			else {
				struct unistim_device *d2 = devices;
				while (d2) {
					if (d2->next == d) {
						d2->next = d->next;
						break;
					}
					d2 = d2->next;
				}
			}
			if (d->tz) {
				d->tz = ast_tone_zone_unref(d->tz);
			}
			ast_free(d);
			d = devices;
			continue;
		}
		d = d->next;
	}
	finish_bookmark();
	ast_mutex_unlock(&devicelock);
	ast_config_destroy(cfg);
	ast_mutex_lock(&sessionlock);
	s = sessions;
	while (s) {
		if (s->device)
			refresh_all_favorite(s);
		s = s->next;
	}
	ast_mutex_unlock(&sessionlock);
	/* We don't recreate a socket when reloading (locks would be necessary). */
	if (unistimsock > -1)
		return 0;
	bindaddr.sin_addr.s_addr = INADDR_ANY;
	bindaddr.sin_port = htons(unistim_port);
	bindaddr.sin_family = AF_INET;
	unistimsock = socket(AF_INET, SOCK_DGRAM, 0);
	if (unistimsock < 0) {
		ast_log(LOG_WARNING, "Unable to create UNISTIM socket: %s\n", strerror(errno));
		return -1;
	}
#ifdef HAVE_PKTINFO
	{
		const int pktinfoFlag = 1;
		setsockopt(unistimsock, IPPROTO_IP, IP_PKTINFO, &pktinfoFlag,
				   sizeof(pktinfoFlag));
	}
#else
	if (public_ip.sin_family == 0) {
		ast_log(LOG_WARNING,
				"Your OS does not support IP_PKTINFO, you must set public_ip.\n");
		unistimsock = -1;
		return -1;
	}
#endif
	setsockopt(unistimsock, SOL_SOCKET, SO_REUSEADDR, (const char *) &reuseFlag,
			   sizeof(reuseFlag));
	if (bind(unistimsock, (struct sockaddr *) &bindaddr, sizeof(bindaddr)) < 0) {
		ast_log(LOG_WARNING, "Failed to bind to %s:%d: %s\n",
				ast_inet_ntoa(bindaddr.sin_addr), htons(bindaddr.sin_port),
				strerror(errno));
		close(unistimsock);
		unistimsock = -1;
	} else {
		ast_verb(2, "UNISTIM Listening on %s:%d\n", ast_inet_ntoa(bindaddr.sin_addr), htons(bindaddr.sin_port));
		ast_netsock_set_qos(unistimsock, qos.tos, qos.cos, "UNISTIM");
	}
	return 0;
}

static enum ast_rtp_glue_result unistim_get_rtp_peer(struct ast_channel *chan, struct ast_rtp_instance **instance)
{
	struct unistim_subchannel *sub = chan->tech_pvt;

	ao2_ref(sub->rtp, +1);
	*instance = sub->rtp;

	return AST_RTP_GLUE_RESULT_LOCAL;
}

static struct ast_rtp_glue unistim_rtp_glue = {
	.type = channel_type,
	.get_rtp_info = unistim_get_rtp_peer,
};

/*--- load_module: PBX load module - initialization ---*/
int load_module(void)
{
	int res;
	struct ast_format tmpfmt;
	if (!(global_cap = ast_format_cap_alloc())) {
		goto buff_failed;
	}
	if (!(unistim_tech.capabilities = ast_format_cap_alloc())) {
		goto buff_failed;
	}

	ast_format_cap_add(global_cap, ast_format_set(&tmpfmt, AST_FORMAT_ULAW, 0));
	ast_format_cap_add(global_cap, ast_format_set(&tmpfmt, AST_FORMAT_ALAW, 0));
	ast_format_cap_copy(unistim_tech.capabilities, global_cap);
	if (!(buff = ast_malloc(SIZE_PAGE)))
		goto buff_failed;

	io = io_context_create();
	if (!io) {
		ast_log(LOG_ERROR, "Failed to allocate IO context\n");
		goto io_failed;
	}

	sched = ast_sched_context_create();
	if (!sched) {
		ast_log(LOG_ERROR, "Failed to allocate scheduler context\n");
		goto sched_failed;
	}

	res = reload_config();
	if (res)
		return AST_MODULE_LOAD_DECLINE;

	/* Make sure we can register our unistim channel type */
	if (ast_channel_register(&unistim_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel type '%s'\n", channel_type);
		goto chanreg_failed;
	} 

	ast_rtp_glue_register(&unistim_rtp_glue);

	ast_cli_register_multiple(unistim_cli, ARRAY_LEN(unistim_cli));

	restart_monitor();

	return AST_MODULE_LOAD_SUCCESS;

chanreg_failed:
	/*! XXX \todo Leaking anything allocated by reload_config() ... */
	ast_sched_context_destroy(sched);
	sched = NULL;
sched_failed:
	io_context_destroy(io);
	io = NULL;
io_failed:
	ast_free(buff);
	buff = NULL;
	global_cap = ast_format_cap_destroy(global_cap);
	unistim_tech.capabilities = ast_format_cap_destroy(unistim_tech.capabilities);
buff_failed:
	return AST_MODULE_LOAD_FAILURE;
}

static int unload_module(void)
{
	/* First, take us out of the channel loop */
	if (sched) {
		ast_sched_context_destroy(sched);
	}

	ast_cli_unregister_multiple(unistim_cli, ARRAY_LEN(unistim_cli));

	ast_channel_unregister(&unistim_tech);
	ast_rtp_glue_unregister(&unistim_rtp_glue);

	ast_mutex_lock(&monlock);
	if (monitor_thread && (monitor_thread != AST_PTHREADT_STOP) && (monitor_thread != AST_PTHREADT_NULL)) {
		pthread_cancel(monitor_thread);
		pthread_kill(monitor_thread, SIGURG);
		pthread_join(monitor_thread, NULL);
	}
	monitor_thread = AST_PTHREADT_STOP;
	ast_mutex_unlock(&monlock);

	if (buff)
		ast_free(buff);
	if (unistimsock > -1)
		close(unistimsock);

	global_cap = ast_format_cap_destroy(global_cap);
	unistim_tech.capabilities = ast_format_cap_destroy(unistim_tech.capabilities);

	return 0;
}

/*! reload: Part of Asterisk module interface ---*/
int reload(void)
{
	unistim_reload(NULL, 0, NULL);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "UNISTIM Protocol (USTM)",
    .load = load_module,
    .unload = unload_module,
    .reload = reload,
);
