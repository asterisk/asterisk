/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * chan_skinny was developed by Jeremy McNamara & Florian Overkamp
 * chan_skinny was heavily modified/fixed by North Antara
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
 * \brief Implementation of the Skinny protocol
 *
 * \author Jeremy McNamara & Florian Overkamp & North Antara
 * \ingroup channel_drivers
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/signal.h>
#include <signal.h>
#include <ctype.h>

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/sched.h"
#include "asterisk/io.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/netsock.h"
#include "asterisk/acl.h"
#include "asterisk/callerid.h"
#include "asterisk/cli.h"
#include "asterisk/manager.h"
#include "asterisk/say.h"
#include "asterisk/cdr.h"
#include "asterisk/astdb.h"
#include "asterisk/features.h"
#include "asterisk/app.h"
#include "asterisk/musiconhold.h"
#include "asterisk/utils.h"
#include "asterisk/dsp.h"
#include "asterisk/stringfields.h"
#include "asterisk/abstract_jb.h"
#include "asterisk/threadstorage.h"
#include "asterisk/devicestate.h"
#include "asterisk/event.h"
#include "asterisk/indications.h"
#include "asterisk/linkedlists.h"

/*** DOCUMENTATION
	<manager name="SKINNYdevices" language="en_US">
		<synopsis>
			List SKINNY devices (text format).
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
		</syntax>
		<description>
			<para>Lists Skinny devices in text format with details on current status.
			Devicelist will follow as separate events, followed by a final event called
			DevicelistComplete.</para>
		</description>
	</manager>
	<manager name="SKINNYshowdevice" language="en_US">
		<synopsis>
			Show SKINNY device (text format).
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Device" required="true">
				<para>The device name you want to check.</para>
			</parameter>
		</syntax>
		<description>
			<para>Show one SKINNY device with details on current status.</para>
		</description>
	</manager>
	<manager name="SKINNYlines" language="en_US">
		<synopsis>
			List SKINNY lines (text format).
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
		</syntax>
		<description>
			<para>Lists Skinny lines in text format with details on current status.
			Linelist will follow as separate events, followed by a final event called
			LinelistComplete.</para>
		</description>
	</manager>
	<manager name="SKINNYshowline" language="en_US">
		<synopsis>
			Show SKINNY line (text format).
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Line" required="true">
				<para>The line name you want to check.</para>
			</parameter>
		</syntax>
		<description>
			<para>Show one SKINNY line with details on current status.</para>
		</description>
	</manager>
 ***/

#ifdef SKINNY_DEVMODE
#define SKINNY_DEVONLY(code)	\
	code
#else
#define SKINNY_DEVONLY(code)
#endif

/*************************************
 * Skinny/Asterisk Protocol Settings *
 *************************************/
static const char tdesc[] = "Skinny Client Control Protocol (Skinny)";
static const char config[] = "skinny.conf";

static format_t default_capability = AST_FORMAT_ULAW | AST_FORMAT_ALAW;
static struct ast_codec_pref default_prefs;

enum skinny_codecs {
	SKINNY_CODEC_ALAW = 2,
	SKINNY_CODEC_ULAW = 4,
	SKINNY_CODEC_G723_1 = 9,
	SKINNY_CODEC_G729A = 12,
	SKINNY_CODEC_G726_32 = 82, /* XXX Which packing order does this translate to? */
	SKINNY_CODEC_H261 = 100,
	SKINNY_CODEC_H263 = 101
};

#define DEFAULT_SKINNY_PORT 2000
#define DEFAULT_SKINNY_BACKLOG 2
#define SKINNY_MAX_PACKET 1000
#define DEFAULT_AUTH_TIMEOUT 30
#define DEFAULT_AUTH_LIMIT 50

static struct {
	unsigned int tos;
	unsigned int tos_audio;
	unsigned int tos_video;
	unsigned int cos;
	unsigned int cos_audio;
	unsigned int cos_video;
} qos = { 0, 0, 0, 0, 0, 0 };

static int keep_alive = 120;
static int auth_timeout = DEFAULT_AUTH_TIMEOUT;
static int auth_limit = DEFAULT_AUTH_LIMIT;
static int unauth_sessions = 0;
static char global_vmexten[AST_MAX_EXTENSION];      /* Voicemail pilot number */
static char used_context[AST_MAX_EXTENSION]; /* placeholder to check if context are already used in regcontext */
static char regcontext[AST_MAX_CONTEXT];     /* Context for auto-extension */
static char date_format[6] = "D-M-Y";
static char version_id[16] = "P002F202";

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define letohl(x) (x)
#define letohs(x) (x)
#define htolel(x) (x)
#define htoles(x) (x)
#else
#if defined(HAVE_BYTESWAP_H)
#include <byteswap.h>
#define letohl(x) bswap_32(x)
#define letohs(x) bswap_16(x)
#define htolel(x) bswap_32(x)
#define htoles(x) bswap_16(x)
#elif defined(HAVE_SYS_ENDIAN_SWAP16)
#include <sys/endian.h>
#define letohl(x) __swap32(x)
#define letohs(x) __swap16(x)
#define htolel(x) __swap32(x)
#define htoles(x) __swap16(x)
#elif defined(HAVE_SYS_ENDIAN_BSWAP16)
#include <sys/endian.h>
#define letohl(x) bswap32(x)
#define letohs(x) bswap16(x)
#define htolel(x) bswap32(x)
#define htoles(x) bswap16(x)
#else
#define __bswap_16(x) \
	((((x) & 0xff00) >> 8) | \
	 (((x) & 0x00ff) << 8))
#define __bswap_32(x) \
	((((x) & 0xff000000) >> 24) | \
	 (((x) & 0x00ff0000) >>  8) | \
	 (((x) & 0x0000ff00) <<  8) | \
	 (((x) & 0x000000ff) << 24))
#define letohl(x) __bswap_32(x)
#define letohs(x) __bswap_16(x)
#define htolel(x) __bswap_32(x)
#define htoles(x) __bswap_16(x)
#endif
#endif

/*! Global jitterbuffer configuration - by default, jb is disabled
 *  \note Values shown here match the defaults shown in skinny.conf.sample */
static struct ast_jb_conf default_jbconf =
{
	.flags = 0,
	.max_size = 200,
	.resync_threshold = 1000,
	.impl = "fixed",
	.target_extra = 40,
};
static struct ast_jb_conf global_jbconf;

#ifdef SKINNY_DEVMODE
AST_THREADSTORAGE(message2str_threadbuf);
#define MESSAGE2STR_BUFSIZE   35
#endif

AST_THREADSTORAGE(device2str_threadbuf);
#define DEVICE2STR_BUFSIZE   15

AST_THREADSTORAGE(control2str_threadbuf);
#define CONTROL2STR_BUFSIZE   100

/*********************
 * Protocol Messages *
 *********************/
/* message types */
#define KEEP_ALIVE_MESSAGE 0x0000
/* no additional struct */

#define REGISTER_MESSAGE 0x0001
struct register_message {
	char name[16];
	uint32_t userId;
	uint32_t instance;
	uint32_t ip;
	uint32_t type;
	uint32_t maxStreams;
};

#define IP_PORT_MESSAGE 0x0002

#define KEYPAD_BUTTON_MESSAGE 0x0003
struct keypad_button_message {
	uint32_t button;
	uint32_t lineInstance;
	uint32_t callReference;
};


#define ENBLOC_CALL_MESSAGE 0x0004
struct enbloc_call_message {
	char calledParty[24];
};

#define STIMULUS_MESSAGE 0x0005
struct stimulus_message {
	uint32_t stimulus;
	uint32_t stimulusInstance;
	uint32_t callreference;
};

#define OFFHOOK_MESSAGE 0x0006
struct offhook_message {
	uint32_t instance;
	uint32_t reference;
};

#define ONHOOK_MESSAGE 0x0007
struct onhook_message {
	uint32_t instance;
	uint32_t reference;
};

#define CAPABILITIES_RES_MESSAGE 0x0010
struct station_capabilities {
	uint32_t codec;
	uint32_t frames;
	union {
		char res[8];
		uint32_t rate;
	} payloads;
};

#define SKINNY_MAX_CAPABILITIES 18

struct capabilities_res_message {
	uint32_t count;
	struct station_capabilities caps[SKINNY_MAX_CAPABILITIES];
};

#define SPEED_DIAL_STAT_REQ_MESSAGE 0x000A
struct speed_dial_stat_req_message {
	uint32_t speedDialNumber;
};

#define LINE_STATE_REQ_MESSAGE 0x000B
struct line_state_req_message {
	uint32_t lineNumber;
};

#define TIME_DATE_REQ_MESSAGE 0x000D
#define BUTTON_TEMPLATE_REQ_MESSAGE 0x000E
#define VERSION_REQ_MESSAGE 0x000F
#define SERVER_REQUEST_MESSAGE 0x0012

#define ALARM_MESSAGE 0x0020
struct alarm_message {
	uint32_t alarmSeverity;
	char displayMessage[80];
	uint32_t alarmParam1;
	uint32_t alarmParam2;
};

#define OPEN_RECEIVE_CHANNEL_ACK_MESSAGE 0x0022
struct open_receive_channel_ack_message {
	uint32_t status;
	uint32_t ipAddr;
	uint32_t port;
	uint32_t passThruId;
};

#define SOFT_KEY_SET_REQ_MESSAGE 0x0025

#define SOFT_KEY_EVENT_MESSAGE 0x0026
struct soft_key_event_message {
	uint32_t softKeyEvent;
	uint32_t instance;
	uint32_t callreference;
};

#define UNREGISTER_MESSAGE 0x0027
#define SOFT_KEY_TEMPLATE_REQ_MESSAGE 0x0028
#define HEADSET_STATUS_MESSAGE 0x002B
#define REGISTER_AVAILABLE_LINES_MESSAGE 0x002D

#define REGISTER_ACK_MESSAGE 0x0081
struct register_ack_message {
	uint32_t keepAlive;
	char dateTemplate[6];
	char res[2];
	uint32_t secondaryKeepAlive;
	char res2[4];
};

#define START_TONE_MESSAGE 0x0082
struct start_tone_message {
	uint32_t tone;
	uint32_t space;
	uint32_t instance;
	uint32_t reference;
};

#define STOP_TONE_MESSAGE 0x0083
struct stop_tone_message {
	uint32_t instance;
	uint32_t reference;
};

#define SET_RINGER_MESSAGE 0x0085
struct set_ringer_message {
	uint32_t ringerMode;
	uint32_t unknown1; /* See notes in transmit_ringer_mode */
	uint32_t unknown2;
	uint32_t space[2];
};

#define SET_LAMP_MESSAGE 0x0086
struct set_lamp_message {
	uint32_t stimulus;
	uint32_t stimulusInstance;
	uint32_t deviceStimulus;
};

#define SET_SPEAKER_MESSAGE 0x0088
struct set_speaker_message {
	uint32_t mode;
};

/* XXX When do we need to use this? */
#define SET_MICROPHONE_MESSAGE 0x0089
struct set_microphone_message {
	uint32_t mode;
};

#define START_MEDIA_TRANSMISSION_MESSAGE 0x008A
struct media_qualifier {
	uint32_t precedence;
	uint32_t vad;
	uint16_t packets;
	uint32_t bitRate;
};

struct start_media_transmission_message {
	uint32_t conferenceId;
	uint32_t passThruPartyId;
	uint32_t remoteIp;
	uint32_t remotePort;
	uint32_t packetSize;
	uint32_t payloadType;
	struct media_qualifier qualifier;
	uint32_t space[16];
};

#define STOP_MEDIA_TRANSMISSION_MESSAGE 0x008B
struct stop_media_transmission_message {
	uint32_t conferenceId;
	uint32_t passThruPartyId;
	uint32_t space[3];
};

#define CALL_INFO_MESSAGE 0x008F
struct call_info_message {
	char callingPartyName[40];
	char callingParty[24];
	char calledPartyName[40];
	char calledParty[24];
	uint32_t instance;
	uint32_t reference;
	uint32_t type;
	char originalCalledPartyName[40];
	char originalCalledParty[24];
	char lastRedirectingPartyName[40];
	char lastRedirectingParty[24];
	uint32_t originalCalledPartyRedirectReason;
	uint32_t lastRedirectingReason;
	char callingPartyVoiceMailbox[24];
	char calledPartyVoiceMailbox[24];
	char originalCalledPartyVoiceMailbox[24];
	char lastRedirectingVoiceMailbox[24];
	uint32_t space[3];
};

#define FORWARD_STAT_MESSAGE 0x0090
struct forward_stat_message {
	uint32_t activeforward;
	uint32_t lineNumber;
	uint32_t fwdall;
	char fwdallnum[24];
	uint32_t fwdbusy;
	char fwdbusynum[24];
	uint32_t fwdnoanswer;
	char fwdnoanswernum[24];
};

#define SPEED_DIAL_STAT_RES_MESSAGE 0x0091
struct speed_dial_stat_res_message {
	uint32_t speedDialNumber;
	char speedDialDirNumber[24];
	char speedDialDisplayName[40];
};

#define LINE_STAT_RES_MESSAGE 0x0092
struct line_stat_res_message {
	uint32_t lineNumber;
	char lineDirNumber[24];
	char lineDisplayName[24];
	uint32_t space[15];
};

#define DEFINETIMEDATE_MESSAGE 0x0094
struct definetimedate_message {
	uint32_t year; /* since 1900 */
	uint32_t month;
	uint32_t dayofweek; /* monday = 1 */
	uint32_t day;
	uint32_t hour;
	uint32_t minute;
	uint32_t seconds;
	uint32_t milliseconds;
	uint32_t timestamp;
};

#define BUTTON_TEMPLATE_RES_MESSAGE 0x0097
struct button_definition {
	uint8_t instanceNumber;
	uint8_t buttonDefinition;
};

struct button_definition_template {
	uint8_t buttonDefinition;
	/* for now, anything between 0xB0 and 0xCF is custom */
	/*int custom;*/
};

#define STIMULUS_REDIAL 0x01
#define STIMULUS_SPEEDDIAL 0x02
#define STIMULUS_HOLD 0x03
#define STIMULUS_TRANSFER 0x04
#define STIMULUS_FORWARDALL 0x05
#define STIMULUS_FORWARDBUSY 0x06
#define STIMULUS_FORWARDNOANSWER 0x07
#define STIMULUS_DISPLAY 0x08
#define STIMULUS_LINE 0x09
#define STIMULUS_VOICEMAIL 0x0F
#define STIMULUS_AUTOANSWER 0x11
#define STIMULUS_DND 0x3F
#define STIMULUS_CONFERENCE 0x7D
#define STIMULUS_CALLPARK 0x7E
#define STIMULUS_CALLPICKUP 0x7F
#define STIMULUS_NONE 0xFF

/* Button types */
#define BT_REDIAL STIMULUS_REDIAL
#define BT_SPEEDDIAL STIMULUS_SPEEDDIAL
#define BT_HOLD STIMULUS_HOLD
#define BT_TRANSFER STIMULUS_TRANSFER
#define BT_FORWARDALL STIMULUS_FORWARDALL
#define BT_FORWARDBUSY STIMULUS_FORWARDBUSY
#define BT_FORWARDNOANSWER STIMULUS_FORWARDNOANSWER
#define BT_DISPLAY STIMULUS_DISPLAY
#define BT_LINE STIMULUS_LINE
#define BT_VOICEMAIL STIMULUS_VOICEMAIL
#define BT_AUTOANSWER STIMULUS_AUTOANSWER
#define BT_DND STIMULUS_DND
#define BT_CONFERENCE STIMULUS_CONFERENCE
#define BT_CALLPARK STIMULUS_CALLPARK
#define BT_CALLPICKUP STIMULUS_CALLPICKUP
#define BT_NONE 0x00

/* Custom button types - add our own between 0xB0 and 0xCF.
   This may need to be revised in the future,
   if stimuluses are ever added in this range. */
#define BT_CUST_LINESPEEDDIAL 0xB0 /* line or speeddial with/without hint */
#define BT_CUST_LINE 0xB1          /* line or speeddial with hint only */

struct button_template_res_message {
	uint32_t buttonOffset;
	uint32_t buttonCount;
	uint32_t totalButtonCount;
	struct button_definition definition[42];
};

#define VERSION_RES_MESSAGE 0x0098
struct version_res_message {
	char version[16];
};

#define DISPLAYTEXT_MESSAGE 0x0099
struct displaytext_message {
	char text[40];
};

#define CLEAR_NOTIFY_MESSAGE  0x0115
#define CLEAR_DISPLAY_MESSAGE 0x009A

#define CAPABILITIES_REQ_MESSAGE 0x009B

#define REGISTER_REJ_MESSAGE 0x009D
struct register_rej_message {
	char errMsg[33];
};

#define SERVER_RES_MESSAGE 0x009E
struct server_identifier {
	char serverName[48];
};

struct server_res_message {
	struct server_identifier server[5];
	uint32_t serverListenPort[5];
	uint32_t serverIpAddr[5];
};

#define RESET_MESSAGE 0x009F
struct reset_message {
	uint32_t resetType;
};

#define KEEP_ALIVE_ACK_MESSAGE 0x0100

#define OPEN_RECEIVE_CHANNEL_MESSAGE 0x0105
struct open_receive_channel_message {
	uint32_t conferenceId;
	uint32_t partyId;
	uint32_t packets;
	uint32_t capability;
	uint32_t echo;
	uint32_t bitrate;
	uint32_t space[16];
};

#define CLOSE_RECEIVE_CHANNEL_MESSAGE 0x0106
struct close_receive_channel_message {
	uint32_t conferenceId;
	uint32_t partyId;
	uint32_t space[2];
};

#define SOFT_KEY_TEMPLATE_RES_MESSAGE 0x0108

struct soft_key_template_definition {
	char softKeyLabel[16];
	uint32_t softKeyEvent;
};

#define KEYDEF_ONHOOK 0
#define KEYDEF_CONNECTED 1
#define KEYDEF_ONHOLD 2
#define KEYDEF_RINGIN 3
#define KEYDEF_OFFHOOK 4
#define KEYDEF_CONNWITHTRANS 5
#define KEYDEF_DADFD 6 /* Digits After Dialing First Digit */
#define KEYDEF_CONNWITHCONF 7
#define KEYDEF_RINGOUT 8
#define KEYDEF_OFFHOOKWITHFEAT 9
#define KEYDEF_UNKNOWN 10

#define SOFTKEY_NONE 0x00
#define SOFTKEY_REDIAL 0x01
#define SOFTKEY_NEWCALL 0x02
#define SOFTKEY_HOLD 0x03
#define SOFTKEY_TRNSFER 0x04
#define SOFTKEY_CFWDALL 0x05
#define SOFTKEY_CFWDBUSY 0x06
#define SOFTKEY_CFWDNOANSWER 0x07
#define SOFTKEY_BKSPC 0x08
#define SOFTKEY_ENDCALL 0x09
#define SOFTKEY_RESUME 0x0A
#define SOFTKEY_ANSWER 0x0B
#define SOFTKEY_INFO 0x0C
#define SOFTKEY_CONFRN 0x0D
#define SOFTKEY_PARK 0x0E
#define SOFTKEY_JOIN 0x0F
#define SOFTKEY_MEETME 0x10
#define SOFTKEY_PICKUP 0x11
#define SOFTKEY_GPICKUP 0x12
#define SOFTKEY_DND 0x13
#define SOFTKEY_IDIVERT 0x14

static struct soft_key_template_definition soft_key_template_default[] = {
	{ "\200\001", SOFTKEY_REDIAL },
	{ "\200\002", SOFTKEY_NEWCALL },
	{ "\200\003", SOFTKEY_HOLD },
	{ "\200\004", SOFTKEY_TRNSFER },
	{ "\200\005", SOFTKEY_CFWDALL },
	{ "\200\006", SOFTKEY_CFWDBUSY },
	{ "\200\007", SOFTKEY_CFWDNOANSWER },
	{ "\200\010", SOFTKEY_BKSPC },
	{ "\200\011", SOFTKEY_ENDCALL },
	{ "\200\012", SOFTKEY_RESUME },
	{ "\200\013", SOFTKEY_ANSWER },
	{ "\200\014", SOFTKEY_INFO },
	{ "\200\015", SOFTKEY_CONFRN },
	{ "\200\016", SOFTKEY_PARK },
	{ "\200\017", SOFTKEY_JOIN },
	{ "\200\020", SOFTKEY_MEETME },
	{ "\200\021", SOFTKEY_PICKUP },
	{ "\200\022", SOFTKEY_GPICKUP },
	{ "\200\077", SOFTKEY_DND },
	{ "\200\120", SOFTKEY_IDIVERT },
};

/* Localized message "codes" (in octal)
   Below is en_US (taken from a 7970)

   \200\xxx
       \000: ???
       \001: Redial
       \002: New Call
       \003: Hold
       \004: Transfer
       \005: CFwdALL
       \006: CFwdBusy
       \007: CFwdNoAnswer
       \010: <<
       \011: EndCall
       \012: Resume
       \013: Answer
       \014: Info
       \015: Confrn
       \016: Park
       \017: Join
       \020: MeetMe
       \021: PickUp
       \022: GPickUp
       \023: Your current options
       \024: Off Hook
       \025: On Hook
       \026: Ring out
       \027: From
       \030: Connected
       \031: Busy
       \032: Line In Use
       \033: Call Waiting
       \034: Call Transfer
       \035: Call Park
       \036: Call Proceed
       \037: In Use Remote
       \040: Enter number
       \041: Call park At
       \042: Primary Only
       \043: Temp Fail
       \044: You Have VoiceMail
       \045: Forwarded to
       \046: Can Not Complete Conference
       \047: No Conference Bridge
       \050: Can Not Hold Primary Control
       \051: Invalid Conference Participant
       \052: In Conference Already
       \053: No Participant Info
       \054: Exceed Maximum Parties
       \055: Key Is Not Active
       \056: Error No License
       \057: Error DBConfig
       \060: Error Database
       \061: Error Pass Limit
       \062: Error Unknown
       \063: Error Mismatch
       \064: Conference
       \065: Park Number
       \066: Private
       \067: Not Enough Bandwidth
       \070: Unknown Number
       \071: RmLstC
       \072: Voicemail
       \073: ImmDiv
       \074: Intrcpt
       \075: SetWtch
       \076: TrnsfVM
       \077: DND
       \100: DivAll
       \101: CallBack
       \102: Network congestion,rerouting
       \103: Barge
       \104: Failed to setup Barge
       \105: Another Barge exists
       \106: Incompatible device type
       \107: No Park Number Available
       \110: CallPark Reversion
       \111: Service is not Active
       \112: High Traffic Try Again Later
       \113: QRT
       \114: MCID
       \115: DirTrfr
       \116: Select
       \117: ConfList
       \120: iDivert
       \121: cBarge
       \122: Can Not Complete Transfer
       \123: Can Not Join Calls
       \124: Mcid Successful
       \125: Number Not Configured
       \126: Security Error
       \127: Video Bandwidth Unavailable
       \130: VidMode
       \131: Max Call Duration Timeout
       \132: Max Hold Duration Timeout
       \133: OPickUp
       \134: ???
       \135: ???
       \136: ???
       \137: ???
       \140: ???
       \141: External Transfer Restricted
       \142: ???
       \143: ???
       \144: ???
       \145: Mac Address
       \146: Host Name
       \147: Domain Name
       \150: IP Address
       \151: Subnet Mask
       \152: TFTP Server 1
       \153: Default Router 1
       \154: Default Router 2
       \155: Default Router 3
       \156: Default Router 4
       \157: Default Router 5
       \160: DNS Server 1
       \161: DNS Server 2
       \162: DNS Server 3
       \163: DNS Server 4
       \164: DNS Server 5
       \165: Operational VLAN Id
       \166: Admin. VLAN Id
       \167: CallManager 1
       \170: CallManager 2
       \171: CallManager 3
       \172: CallManager 4
       \173: CallManager 5
       \174: Information URL
       \175: Directories URL
       \176: Messages URL
       \177: Services URL
 */

struct soft_key_definitions {
	const uint8_t mode;
	const uint8_t *defaults;
	const int count;
};

static const uint8_t soft_key_default_onhook[] = {
	SOFTKEY_REDIAL,
	SOFTKEY_NEWCALL,
	SOFTKEY_CFWDALL,
	SOFTKEY_CFWDBUSY,
	SOFTKEY_DND,
	/*SOFTKEY_GPICKUP,
	SOFTKEY_CONFRN,*/
};

static const uint8_t soft_key_default_connected[] = {
	SOFTKEY_HOLD,
	SOFTKEY_ENDCALL,
	SOFTKEY_TRNSFER,
	SOFTKEY_PARK,
	SOFTKEY_CFWDALL,
	SOFTKEY_CFWDBUSY,
};

static const uint8_t soft_key_default_onhold[] = {
	SOFTKEY_RESUME,
	SOFTKEY_NEWCALL,
	SOFTKEY_ENDCALL,
	SOFTKEY_TRNSFER,
};

static const uint8_t soft_key_default_ringin[] = {
	SOFTKEY_ANSWER,
	SOFTKEY_ENDCALL,
	SOFTKEY_TRNSFER,
};

static const uint8_t soft_key_default_offhook[] = {
	SOFTKEY_REDIAL,
	SOFTKEY_ENDCALL,
	SOFTKEY_CFWDALL,
	SOFTKEY_CFWDBUSY,
	/*SOFTKEY_GPICKUP,*/
};

static const uint8_t soft_key_default_connwithtrans[] = {
	SOFTKEY_HOLD,
	SOFTKEY_ENDCALL,
	SOFTKEY_TRNSFER,
	SOFTKEY_PARK,
	SOFTKEY_CFWDALL,
	SOFTKEY_CFWDBUSY,
};

static const uint8_t soft_key_default_dadfd[] = {
	SOFTKEY_BKSPC,
	SOFTKEY_ENDCALL,
};

static const uint8_t soft_key_default_connwithconf[] = {
	SOFTKEY_NONE,
};

static const uint8_t soft_key_default_ringout[] = {
	SOFTKEY_NONE,
	SOFTKEY_ENDCALL,
};

static const uint8_t soft_key_default_offhookwithfeat[] = {
	SOFTKEY_REDIAL,
	SOFTKEY_ENDCALL,
	SOFTKEY_TRNSFER,
};

static const uint8_t soft_key_default_unknown[] = {
	SOFTKEY_NONE,
};

static const struct soft_key_definitions soft_key_default_definitions[] = {
	{KEYDEF_ONHOOK, soft_key_default_onhook, sizeof(soft_key_default_onhook) / sizeof(uint8_t)},
	{KEYDEF_CONNECTED, soft_key_default_connected, sizeof(soft_key_default_connected) / sizeof(uint8_t)},
	{KEYDEF_ONHOLD, soft_key_default_onhold, sizeof(soft_key_default_onhold) / sizeof(uint8_t)},
	{KEYDEF_RINGIN, soft_key_default_ringin, sizeof(soft_key_default_ringin) / sizeof(uint8_t)},
	{KEYDEF_OFFHOOK, soft_key_default_offhook, sizeof(soft_key_default_offhook) / sizeof(uint8_t)},
	{KEYDEF_CONNWITHTRANS, soft_key_default_connwithtrans, sizeof(soft_key_default_connwithtrans) / sizeof(uint8_t)},
	{KEYDEF_DADFD, soft_key_default_dadfd, sizeof(soft_key_default_dadfd) / sizeof(uint8_t)},
	{KEYDEF_CONNWITHCONF, soft_key_default_connwithconf, sizeof(soft_key_default_connwithconf) / sizeof(uint8_t)},
	{KEYDEF_RINGOUT, soft_key_default_ringout, sizeof(soft_key_default_ringout) / sizeof(uint8_t)},
	{KEYDEF_OFFHOOKWITHFEAT, soft_key_default_offhookwithfeat, sizeof(soft_key_default_offhookwithfeat) / sizeof(uint8_t)},
	{KEYDEF_UNKNOWN, soft_key_default_unknown, sizeof(soft_key_default_unknown) / sizeof(uint8_t)}
};

struct soft_key_template_res_message {
	uint32_t softKeyOffset;
	uint32_t softKeyCount;
	uint32_t totalSoftKeyCount;
	struct soft_key_template_definition softKeyTemplateDefinition[32];
};

#define SOFT_KEY_SET_RES_MESSAGE 0x0109

struct soft_key_set_definition {
	uint8_t softKeyTemplateIndex[16];
	uint16_t softKeyInfoIndex[16];
};

struct soft_key_set_res_message {
	uint32_t softKeySetOffset;
	uint32_t softKeySetCount;
	uint32_t totalSoftKeySetCount;
	struct soft_key_set_definition softKeySetDefinition[16];
	uint32_t res;
};

#define SELECT_SOFT_KEYS_MESSAGE 0x0110
struct select_soft_keys_message {
	uint32_t instance;
	uint32_t reference;
	uint32_t softKeySetIndex;
	uint32_t validKeyMask;
};

#define CALL_STATE_MESSAGE 0x0111
struct call_state_message {
	uint32_t callState;
	uint32_t lineInstance;
	uint32_t callReference;
	uint32_t space[3];
};

#define DISPLAY_PROMPT_STATUS_MESSAGE 0x0112
struct display_prompt_status_message {
	uint32_t messageTimeout;
	char promptMessage[32];
	uint32_t lineInstance;
	uint32_t callReference;
	uint32_t space[3];
};

#define CLEAR_PROMPT_MESSAGE  0x0113
struct clear_prompt_message {
	uint32_t lineInstance;
	uint32_t callReference;
};

#define DISPLAY_NOTIFY_MESSAGE 0x0114
struct display_notify_message {
	uint32_t displayTimeout;
	char displayMessage[100];
};

#define ACTIVATE_CALL_PLANE_MESSAGE 0x0116
struct activate_call_plane_message {
	uint32_t lineInstance;
};

#define DIALED_NUMBER_MESSAGE 0x011D
struct dialed_number_message {
	char dialedNumber[24];
	uint32_t lineInstance;
	uint32_t callReference;
};

union skinny_data {
	struct alarm_message alarm;
	struct speed_dial_stat_req_message speeddialreq;
	struct register_message reg;
	struct register_ack_message regack;
	struct register_rej_message regrej;
	struct capabilities_res_message caps;
	struct version_res_message version;
	struct button_template_res_message buttontemplate;
	struct displaytext_message displaytext;
	struct display_prompt_status_message displaypromptstatus;
	struct clear_prompt_message clearpromptstatus;
	struct definetimedate_message definetimedate;
	struct start_tone_message starttone;
	struct stop_tone_message stoptone;
	struct speed_dial_stat_res_message speeddial;
	struct line_state_req_message line;
	struct line_stat_res_message linestat;
	struct soft_key_set_res_message softkeysets;
	struct soft_key_template_res_message softkeytemplate;
	struct server_res_message serverres;
	struct reset_message reset;
	struct set_lamp_message setlamp;
	struct set_ringer_message setringer;
	struct call_state_message callstate;
	struct keypad_button_message keypad;
	struct select_soft_keys_message selectsoftkey;
	struct activate_call_plane_message activatecallplane;
	struct stimulus_message stimulus;
	struct offhook_message offhook;
	struct onhook_message onhook;
	struct set_speaker_message setspeaker;
	struct set_microphone_message setmicrophone;
	struct call_info_message callinfo;
	struct start_media_transmission_message startmedia;
	struct stop_media_transmission_message stopmedia;
	struct open_receive_channel_message openreceivechannel;
	struct open_receive_channel_ack_message openreceivechannelack;
	struct close_receive_channel_message closereceivechannel;
	struct display_notify_message displaynotify;
	struct dialed_number_message dialednumber;
	struct soft_key_event_message softkeyeventmessage;
	struct enbloc_call_message enbloccallmessage;
	struct forward_stat_message forwardstat;
};

/* packet composition */
struct skinny_req {
	int len;
	int res;
	int e;
	union skinny_data data;
};

/* XXX This is the combined size of the variables above.  (len, res, e)
   If more are added, this MUST change.
   (sizeof(skinny_req) - sizeof(skinny_data)) DOES NOT WORK on all systems (amd64?). */
static int skinny_header_size = 12;

/*****************************
 * Asterisk specific globals *
 *****************************/

static int skinnydebug = 0;
static int skinnyreload = 0;

/* a hostname, portnumber, socket and such is usefull for VoIP protocols */
static struct sockaddr_in bindaddr;
static char ourhost[256];
static int ourport;
static struct in_addr __ourip;
static struct ast_hostent ahp;
static struct hostent *hp;
static int skinnysock = -1;
static pthread_t accept_t;
static int callnums = 1;

#define SKINNY_DEVICE_UNKNOWN -1
#define SKINNY_DEVICE_NONE 0
#define SKINNY_DEVICE_30SPPLUS 1
#define SKINNY_DEVICE_12SPPLUS 2
#define SKINNY_DEVICE_12SP 3
#define SKINNY_DEVICE_12 4
#define SKINNY_DEVICE_30VIP 5
#define SKINNY_DEVICE_7910 6
#define SKINNY_DEVICE_7960 7
#define SKINNY_DEVICE_7940 8
#define SKINNY_DEVICE_7935 9
#define SKINNY_DEVICE_ATA186 12 /* Cisco ATA-186 */
#define SKINNY_DEVICE_7941 115
#define SKINNY_DEVICE_7971 119
#define SKINNY_DEVICE_7914 124 /* Expansion module */
#define SKINNY_DEVICE_7985 302
#define SKINNY_DEVICE_7911 307
#define SKINNY_DEVICE_7961GE 308
#define SKINNY_DEVICE_7941GE 309
#define SKINNY_DEVICE_7931 348
#define SKINNY_DEVICE_7921 365
#define SKINNY_DEVICE_7906 369
#define SKINNY_DEVICE_7962 404 /* Not found */
#define SKINNY_DEVICE_7937 431
#define SKINNY_DEVICE_7942 434
#define SKINNY_DEVICE_7945 435
#define SKINNY_DEVICE_7965 436
#define SKINNY_DEVICE_7975 437
#define SKINNY_DEVICE_7905 20000
#define SKINNY_DEVICE_7920 30002
#define SKINNY_DEVICE_7970 30006
#define SKINNY_DEVICE_7912 30007
#define SKINNY_DEVICE_7902 30008
#define SKINNY_DEVICE_CIPC 30016 /* Cisco IP Communicator */
#define SKINNY_DEVICE_7961 30018
#define SKINNY_DEVICE_7936 30019
#define SKINNY_DEVICE_SCCPGATEWAY_AN 30027 /* Analog gateway */
#define SKINNY_DEVICE_SCCPGATEWAY_BRI 30028 /* BRI gateway */

#define SKINNY_SPEAKERON 1
#define SKINNY_SPEAKEROFF 2

#define SKINNY_MICON 1
#define SKINNY_MICOFF 2

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
#define SKINNY_CALLREMOTEMULTILINE 13
#define SKINNY_INVALID 14

#define SKINNY_SILENCE 0x00		/* Note sure this is part of the protocol, remove? */
#define SKINNY_DIALTONE 0x21
#define SKINNY_BUSYTONE 0x23
#define SKINNY_ALERT 0x24
#define SKINNY_REORDER 0x25
#define SKINNY_CALLWAITTONE 0x2D
#define SKINNY_NOTONE 0x7F

#define SKINNY_LAMP_OFF 1
#define SKINNY_LAMP_ON 2
#define SKINNY_LAMP_WINK 3
#define SKINNY_LAMP_FLASH 4
#define SKINNY_LAMP_BLINK 5

#define SKINNY_RING_OFF 1
#define SKINNY_RING_INSIDE 2
#define SKINNY_RING_OUTSIDE 3
#define SKINNY_RING_FEATURE 4

#define SKINNY_CFWD_ALL       (1 << 0)
#define SKINNY_CFWD_BUSY      (1 << 1)
#define SKINNY_CFWD_NOANSWER  (1 << 2)

/* Skinny rtp stream modes. Do we really need this? */
#define SKINNY_CX_SENDONLY 0
#define SKINNY_CX_RECVONLY 1
#define SKINNY_CX_SENDRECV 2
#define SKINNY_CX_CONF 3
#define SKINNY_CX_CONFERENCE 3
#define SKINNY_CX_MUTE 4
#define SKINNY_CX_INACTIVE 4

#if 0
static const char * const skinny_cxmodes[] = {
	"sendonly",
	"recvonly",
	"sendrecv",
	"confrnce",
	"inactive"
};
#endif

/* driver scheduler */
static struct sched_context *sched = NULL;
static struct io_context *io;

/* Protect the monitoring thread, so only one process can kill or start it, and not
   when it's doing something critical. */
AST_MUTEX_DEFINE_STATIC(monlock);
/* Protect the network socket */
AST_MUTEX_DEFINE_STATIC(netlock);

/* This is the thread for the monitor which checks for input on the channels
   which are not currently in use. */
static pthread_t monitor_thread = AST_PTHREADT_NULL;

/* Wait up to 16 seconds for first digit */
static int firstdigittimeout = 16000;

/* How long to wait for following digits */
static int gendigittimeout = 8000;

/* How long to wait for an extra digit, if there is an ambiguous match */
static int matchdigittimeout = 3000;

struct skinny_subchannel {
	ast_mutex_t lock;
	struct ast_channel *owner;
	struct ast_rtp_instance *rtp;
	struct ast_rtp_instance *vrtp;
	unsigned int callid;
	/* time_t lastouttime; */ /* Unused */
	int progress;
	int ringing;
	int onhold;
	/* int lastout; */ /* Unused */
	int cxmode;
	int nat;
	int outgoing;
	int alreadygone;
	int blindxfer;
	int xferor;


	AST_LIST_ENTRY(skinny_subchannel) list;
	struct skinny_subchannel *related;
	struct skinny_line *parent;
};

#define SKINNY_LINE_OPTIONS				\
	char name[80];					\
	char label[24];					\
	char accountcode[AST_MAX_ACCOUNT_CODE];		\
	char exten[AST_MAX_EXTENSION];			\
	char context[AST_MAX_CONTEXT];			\
	char language[MAX_LANGUAGE];			\
	char cid_num[AST_MAX_EXTENSION]; 		\
	char cid_name[AST_MAX_EXTENSION]; 		\
	char lastcallerid[AST_MAX_EXTENSION]; 		\
	int cfwdtype;					\
	char call_forward_all[AST_MAX_EXTENSION];	\
	char call_forward_busy[AST_MAX_EXTENSION];	\
	char call_forward_noanswer[AST_MAX_EXTENSION];	\
	char mailbox[AST_MAX_EXTENSION];		\
	char vmexten[AST_MAX_EXTENSION];		\
	char regexten[AST_MAX_EXTENSION];		\
	char regcontext[AST_MAX_CONTEXT];		\
	char parkinglot[AST_MAX_CONTEXT];		\
	char mohinterpret[MAX_MUSICCLASS];		\
	char mohsuggest[MAX_MUSICCLASS];		\
	char lastnumberdialed[AST_MAX_EXTENSION];	\
	int curtone;					\
	ast_group_t callgroup;				\
	ast_group_t pickupgroup;			\
	int callwaiting;				\
	int transfer;					\
	int threewaycalling;				\
	int mwiblink;					\
	int cancallforward;				\
	int getforward;					\
	int callreturn;					\
	int dnd;					\
	int hascallerid;				\
	int hidecallerid;				\
	int amaflags;					\
	int type;					\
	int instance;					\
	int group;					\
	int needdestroy;				\
	format_t confcapability;				\
	struct ast_codec_pref confprefs;		\
	format_t capability;					\
	struct ast_codec_pref prefs;			\
	int nonCodecCapability;				\
	int onhooktime;					\
	int msgstate;					\
	int immediate;					\
	int hookstate;					\
	int nat;					\
	int directmedia;				\
	int prune;

struct skinny_line {
	SKINNY_LINE_OPTIONS
	ast_mutex_t lock;
	struct ast_event_sub *mwi_event_sub; /* Event based MWI */
	struct skinny_subchannel *activesub;
	AST_LIST_HEAD(, skinny_subchannel) sub;
	AST_LIST_ENTRY(skinny_line) list;
	AST_LIST_ENTRY(skinny_line) all;
	struct skinny_device *device;
	struct ast_variable *chanvars; /*!< Channel variables to set for inbound call */
	int newmsgs;
};

static struct skinny_line_options{
	SKINNY_LINE_OPTIONS
} default_line_struct = {
 	.callwaiting = 1,
	.transfer = 1,
 	.mwiblink = 0,
 	.dnd = 0,
 	.hidecallerid = 0,
	.amaflags = 0,
 	.instance = 0,
 	.directmedia = 0,
 	.nat = 0,
 	.confcapability = AST_FORMAT_ULAW | AST_FORMAT_ALAW,
 	.capability = 0,
	.getforward = 0,
 	.needdestroy = 0,
	.prune = 0,
	.hookstate = SKINNY_ONHOOK,
};
static struct skinny_line_options *default_line = &default_line_struct;

static AST_LIST_HEAD_STATIC(lines, skinny_line);

struct skinny_speeddial {
	ast_mutex_t lock;
	char label[42];
	char context[AST_MAX_CONTEXT];
	char exten[AST_MAX_EXTENSION];
	int instance;
	int stateid;
	int laststate;
	int isHint;

	AST_LIST_ENTRY(skinny_speeddial) list;
	struct skinny_device *parent;
};

struct skinny_addon {
	ast_mutex_t lock;
	char type[10];
	AST_LIST_ENTRY(skinny_addon) list;
	struct skinny_device *parent;
};

#define SKINNY_DEVICE_OPTIONS					\
	char name[80];						\
	char id[16];						\
	char version_id[16];					\
	char exten[AST_MAX_EXTENSION];				\
	char vmexten[AST_MAX_EXTENSION];			\
	int type;						\
	int registered;						\
	int lastlineinstance;					\
	int lastcallreference;					\
	format_t confcapability;					\
	struct ast_codec_pref confprefs;			\
	format_t capability;						\
	int earlyrtp;						\
	int transfer;						\
	int callwaiting;					\
	int mwiblink;						\
	int dnd;						\
	int prune;

struct skinny_device {
	SKINNY_DEVICE_OPTIONS
	struct type *first;
	struct type *last;
	ast_mutex_t lock;
	struct sockaddr_in addr;
	struct in_addr ourip;
	struct ast_ha *ha;
	struct skinnysession *session;
	struct skinny_line *activeline;
	AST_LIST_HEAD(, skinny_line) lines;
	AST_LIST_HEAD(, skinny_speeddial) speeddials;
	AST_LIST_HEAD(, skinny_addon) addons;
	AST_LIST_ENTRY(skinny_device) list;
};

static struct skinny_device_options {
	SKINNY_DEVICE_OPTIONS
} default_device_struct = {
	.transfer = 1,
 	.earlyrtp = 1,
 	.callwaiting = 1,
 	.mwiblink = 0,
 	.dnd = 0,
 	.confcapability = AST_FORMAT_ULAW | AST_FORMAT_ALAW,
 	.capability = 0,
	.prune = 0,
};
static struct skinny_device_options *default_device = &default_device_struct;
	
static AST_LIST_HEAD_STATIC(devices, skinny_device);

struct skinnysession {
	pthread_t t;
	ast_mutex_t lock;
	time_t start;
	struct sockaddr_in sin;
	int fd;
	char inbuf[SKINNY_MAX_PACKET];
	char outbuf[SKINNY_MAX_PACKET];
	struct skinny_device *device;
	AST_LIST_ENTRY(skinnysession) list;
};

static struct ast_channel *skinny_request(const char *type, format_t format, const struct ast_channel *requestor, void *data, int *cause);
static AST_LIST_HEAD_STATIC(sessions, skinnysession);

static int skinny_devicestate(void *data);
static int skinny_call(struct ast_channel *ast, char *dest, int timeout);
static int skinny_hangup(struct ast_channel *ast);
static int skinny_answer(struct ast_channel *ast);
static struct ast_frame *skinny_read(struct ast_channel *ast);
static int skinny_write(struct ast_channel *ast, struct ast_frame *frame);
static int skinny_indicate(struct ast_channel *ast, int ind, const void *data, size_t datalen);
static int skinny_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static int skinny_senddigit_begin(struct ast_channel *ast, char digit);
static int skinny_senddigit_end(struct ast_channel *ast, char digit, unsigned int duration);
static void mwi_event_cb(const struct ast_event *event, void *userdata);
static int skinny_reload(void);

static const struct ast_channel_tech skinny_tech = {
	.type = "Skinny",
	.description = tdesc,
	.capabilities = AST_FORMAT_AUDIO_MASK,
	.properties = AST_CHAN_TP_WANTSJITTER | AST_CHAN_TP_CREATESJITTER,
	.requester = skinny_request,
	.devicestate = skinny_devicestate,
	.call = skinny_call,
	.hangup = skinny_hangup,
	.answer = skinny_answer,
	.read = skinny_read,
	.write = skinny_write,
	.indicate = skinny_indicate,
	.fixup = skinny_fixup,
	.send_digit_begin = skinny_senddigit_begin,
	.send_digit_end = skinny_senddigit_end,
	.bridge = ast_rtp_instance_bridge, 
};

static int skinny_extensionstate_cb(char *context, char* exten, int state, void *data);
static int skinny_transfer(struct skinny_subchannel *sub);

static void *get_button_template(struct skinnysession *s, struct button_definition_template *btn)
{
	struct skinny_device *d = s->device;
	struct skinny_addon *a;
	int i;

	switch (d->type) {
		case SKINNY_DEVICE_30SPPLUS:
		case SKINNY_DEVICE_30VIP:
			/* 13 rows, 2 columns */
			for (i = 0; i < 4; i++)
				(btn++)->buttonDefinition = BT_CUST_LINE;
			(btn++)->buttonDefinition = BT_REDIAL;
			(btn++)->buttonDefinition = BT_VOICEMAIL;
			(btn++)->buttonDefinition = BT_CALLPARK;
			(btn++)->buttonDefinition = BT_FORWARDALL;
			(btn++)->buttonDefinition = BT_CONFERENCE;
			for (i = 0; i < 4; i++)
				(btn++)->buttonDefinition = BT_NONE;
			for (i = 0; i < 13; i++)
				(btn++)->buttonDefinition = BT_SPEEDDIAL;
			
			break;
		case SKINNY_DEVICE_12SPPLUS:
		case SKINNY_DEVICE_12SP:
		case SKINNY_DEVICE_12:
			/* 6 rows, 2 columns */
			for (i = 0; i < 2; i++)
				(btn++)->buttonDefinition = BT_CUST_LINE;
			for (i = 0; i < 4; i++)
				(btn++)->buttonDefinition = BT_SPEEDDIAL;
			(btn++)->buttonDefinition = BT_HOLD;
			(btn++)->buttonDefinition = BT_REDIAL;
			(btn++)->buttonDefinition = BT_TRANSFER;
			(btn++)->buttonDefinition = BT_FORWARDALL;
			(btn++)->buttonDefinition = BT_CALLPARK;
			(btn++)->buttonDefinition = BT_VOICEMAIL;
			break;
		case SKINNY_DEVICE_7910:
			(btn++)->buttonDefinition = BT_LINE;
			(btn++)->buttonDefinition = BT_HOLD;
			(btn++)->buttonDefinition = BT_TRANSFER;
			(btn++)->buttonDefinition = BT_DISPLAY;
			(btn++)->buttonDefinition = BT_VOICEMAIL;
			(btn++)->buttonDefinition = BT_CONFERENCE;
			(btn++)->buttonDefinition = BT_FORWARDALL;
			for (i = 0; i < 2; i++)
				(btn++)->buttonDefinition = BT_SPEEDDIAL;
			(btn++)->buttonDefinition = BT_REDIAL;
			break;
		case SKINNY_DEVICE_7960:
		case SKINNY_DEVICE_7961:
		case SKINNY_DEVICE_7961GE:
		case SKINNY_DEVICE_7962:
		case SKINNY_DEVICE_7965:
			for (i = 0; i < 6; i++)
				(btn++)->buttonDefinition = BT_CUST_LINESPEEDDIAL;
			break;
		case SKINNY_DEVICE_7940:
		case SKINNY_DEVICE_7941:
		case SKINNY_DEVICE_7941GE:
		case SKINNY_DEVICE_7942:
		case SKINNY_DEVICE_7945:
			for (i = 0; i < 2; i++)
				(btn++)->buttonDefinition = BT_CUST_LINESPEEDDIAL;
			break;
		case SKINNY_DEVICE_7935:
		case SKINNY_DEVICE_7936:
			for (i = 0; i < 2; i++)
				(btn++)->buttonDefinition = BT_LINE;
			break;
		case SKINNY_DEVICE_ATA186:
			(btn++)->buttonDefinition = BT_LINE;
			break;
		case SKINNY_DEVICE_7970:
		case SKINNY_DEVICE_7971:
		case SKINNY_DEVICE_7975:
		case SKINNY_DEVICE_CIPC:
			for (i = 0; i < 8; i++)
				(btn++)->buttonDefinition = BT_CUST_LINESPEEDDIAL;
			break;
		case SKINNY_DEVICE_7985:
			/* XXX I have no idea what the buttons look like on these. */
			ast_log(LOG_WARNING, "Unsupported device type '%d (7985)' found.\n", d->type);
			break;
		case SKINNY_DEVICE_7912:
		case SKINNY_DEVICE_7911:
		case SKINNY_DEVICE_7905:
			(btn++)->buttonDefinition = BT_LINE;
			(btn++)->buttonDefinition = BT_HOLD;
			break;
		case SKINNY_DEVICE_7920:
			/* XXX I don't know if this is right. */
			for (i = 0; i < 4; i++)
				(btn++)->buttonDefinition = BT_CUST_LINESPEEDDIAL;
			break;
		case SKINNY_DEVICE_7921:
			for (i = 0; i < 6; i++)
				(btn++)->buttonDefinition = BT_CUST_LINESPEEDDIAL;
			break;
		case SKINNY_DEVICE_7902:
			ast_log(LOG_WARNING, "Unsupported device type '%d (7902)' found.\n", d->type);
			break;
		case SKINNY_DEVICE_7906:
			ast_log(LOG_WARNING, "Unsupported device type '%d (7906)' found.\n", d->type);
			break;
		case SKINNY_DEVICE_7931:
			ast_log(LOG_WARNING, "Unsupported device type '%d (7931)' found.\n", d->type);
			break;
		case SKINNY_DEVICE_7937:
			ast_log(LOG_WARNING, "Unsupported device type '%d (7937)' found.\n", d->type);
			break;
		case SKINNY_DEVICE_7914:
			ast_log(LOG_WARNING, "Unsupported device type '%d (7914)' found.  Expansion module registered by itself?\n", d->type);
			break;
		case SKINNY_DEVICE_SCCPGATEWAY_AN:
		case SKINNY_DEVICE_SCCPGATEWAY_BRI:
			ast_log(LOG_WARNING, "Unsupported device type '%d (SCCP gateway)' found.\n", d->type);
			break;
		default:
			ast_log(LOG_WARNING, "Unknown device type '%d' found.\n", d->type);
			break;
	}

	AST_LIST_LOCK(&d->addons);
	AST_LIST_TRAVERSE(&d->addons, a, list) {
		if (!strcasecmp(a->type, "7914")) {
			for (i = 0; i < 14; i++)
				(btn++)->buttonDefinition = BT_CUST_LINESPEEDDIAL;
		} else {
			ast_log(LOG_WARNING, "Unknown addon type '%s' found.  Skipping.\n", a->type);
		}
	}
	AST_LIST_UNLOCK(&d->addons);

	return btn;
}

static struct skinny_req *req_alloc(size_t size, int response_message)
{
	struct skinny_req *req;

	if (!(req = ast_calloc(1, skinny_header_size + size + 4)))
		return NULL;

	req->len = htolel(size+4);
	req->e = htolel(response_message);

	return req;
}

static struct skinny_line *find_line_by_instance(struct skinny_device *d, int instance)
{
	struct skinny_line *l;

	/*Dialing from on hook or on a 7920 uses instance 0 in requests
	  but we need to start looking at instance 1 */

	if (!instance)
		instance = 1;

	AST_LIST_TRAVERSE(&d->lines, l, list){
		if (l->instance == instance)
			break;
	}

	if (!l) {
		ast_log(LOG_WARNING, "Could not find line with instance '%d' on device '%s'\n", instance, d->name);
	}
	return l;
}

static struct skinny_line *find_line_by_name(const char *dest)
{
	struct skinny_line *l;
	struct skinny_line *tmpl = NULL;
	struct skinny_device *d;
	char line[256];
	char *at;
	char *device;
	int checkdevice = 0;

	ast_copy_string(line, dest, sizeof(line));
	at = strchr(line, '@');
	if (at)
		*at++ = '\0';
	device = at;

	if (!ast_strlen_zero(device))
		checkdevice = 1;

	AST_LIST_LOCK(&devices);
	AST_LIST_TRAVERSE(&devices, d, list){
		if (checkdevice && tmpl)
			break;
		else if (!checkdevice) {
			/* This is a match, since we're checking for line on every device. */
		} else if (!strcasecmp(d->name, device)) {
			if (skinnydebug)
				ast_verb(2, "Found device: %s\n", d->name);
		} else
			continue;

		/* Found the device (or we don't care which device) */
		AST_LIST_TRAVERSE(&d->lines, l, list){
			/* Search for the right line */
			if (!strcasecmp(l->name, line)) {
				if (tmpl) {
					ast_verb(2, "Ambiguous line name: %s\n", line);
					AST_LIST_UNLOCK(&devices);
					return NULL;
				} else
					tmpl = l;
			}
		}
	}
	AST_LIST_UNLOCK(&devices);
	return tmpl;
}

/*!
 * implement the setvar config line
 */
static struct ast_variable *add_var(const char *buf, struct ast_variable *list)
{
	struct ast_variable *tmpvar = NULL;
	char *varname = ast_strdupa(buf), *varval = NULL;

	if ((varval = strchr(varname,'='))) {
		*varval++ = '\0';
		if ((tmpvar = ast_variable_new(varname, varval, ""))) {
			tmpvar->next = list;
			list = tmpvar;
		}
	}
	return list;
}

/* It's quicker/easier to find the subchannel when we know the instance number too */
static struct skinny_subchannel *find_subchannel_by_instance_reference(struct skinny_device *d, int instance, int reference)
{
	struct skinny_line *l = find_line_by_instance(d, instance);
	struct skinny_subchannel *sub;

	if (!l) {
		return NULL;
	}

	/* 7920 phones set call reference to 0, so use the first
	   sub-channel on the list.
           This MIGHT need more love to be right */
	if (!reference)
		sub = AST_LIST_FIRST(&l->sub);
	else {
		AST_LIST_TRAVERSE(&l->sub, sub, list) {
			if (sub->callid == reference)
				break;
		}
	}
	if (!sub) {
		ast_log(LOG_WARNING, "Could not find subchannel with reference '%d' on '%s'\n", reference, d->name);
	}
	return sub;
}

/* Find the subchannel when we only have the callid - this shouldn't happen often */
static struct skinny_subchannel *find_subchannel_by_reference(struct skinny_device *d, int reference)
{
	struct skinny_line *l;
	struct skinny_subchannel *sub = NULL;

	AST_LIST_TRAVERSE(&d->lines, l, list){
		AST_LIST_TRAVERSE(&l->sub, sub, list){
			if (sub->callid == reference)
				break;
		}
		if (sub)
			break;
	}

	if (!l) {
		ast_log(LOG_WARNING, "Could not find any lines that contained a subchannel with reference '%d' on device '%s'\n", reference, d->name);
	} else {
		if (!sub) {
			ast_log(LOG_WARNING, "Could not find subchannel with reference '%d' on '%s@%s'\n", reference, l->name, d->name);
		}
	}
	return sub;
}

static struct skinny_speeddial *find_speeddial_by_instance(struct skinny_device *d, int instance, int isHint)
{
	struct skinny_speeddial *sd;

	AST_LIST_TRAVERSE(&d->speeddials, sd, list) {
		if (sd->isHint == isHint && sd->instance == instance)
			break;
	}

	if (!sd) {
		ast_log(LOG_WARNING, "Could not find speeddial with instance '%d' on device '%s'\n", instance, d->name);
	}
	return sd;
}

static format_t codec_skinny2ast(enum skinny_codecs skinnycodec)
{
	switch (skinnycodec) {
	case SKINNY_CODEC_ALAW:
		return AST_FORMAT_ALAW;
	case SKINNY_CODEC_ULAW:
		return AST_FORMAT_ULAW;
	case SKINNY_CODEC_G723_1:
		return AST_FORMAT_G723_1;
	case SKINNY_CODEC_G729A:
		return AST_FORMAT_G729A;
	case SKINNY_CODEC_G726_32:
		return AST_FORMAT_G726_AAL2; /* XXX Is this right? */
	case SKINNY_CODEC_H261:
		return AST_FORMAT_H261;
	case SKINNY_CODEC_H263:
		return AST_FORMAT_H263;
	default:
		return 0;
	}
}

static int codec_ast2skinny(format_t astcodec)
{
	switch (astcodec) {
	case AST_FORMAT_ALAW:
		return SKINNY_CODEC_ALAW;
	case AST_FORMAT_ULAW:
		return SKINNY_CODEC_ULAW;
	case AST_FORMAT_G723_1:
		return SKINNY_CODEC_G723_1;
	case AST_FORMAT_G729A:
		return SKINNY_CODEC_G729A;
	case AST_FORMAT_G726_AAL2: /* XXX Is this right? */
		return SKINNY_CODEC_G726_32;
	case AST_FORMAT_H261:
		return SKINNY_CODEC_H261;
	case AST_FORMAT_H263:
		return SKINNY_CODEC_H263;
	default:
		return 0;
	}
}

static int set_callforwards(struct skinny_line *l, const char *cfwd, int cfwdtype)
{
	if (!l)
		return 0;

	if (!ast_strlen_zero(cfwd)) {
		if (cfwdtype & SKINNY_CFWD_ALL) {
			l->cfwdtype |= SKINNY_CFWD_ALL;
			ast_copy_string(l->call_forward_all, cfwd, sizeof(l->call_forward_all));
		}
		if (cfwdtype & SKINNY_CFWD_BUSY) {
			l->cfwdtype |= SKINNY_CFWD_BUSY;
			ast_copy_string(l->call_forward_busy, cfwd, sizeof(l->call_forward_busy));
		}
		if (cfwdtype & SKINNY_CFWD_NOANSWER) {
			l->cfwdtype |= SKINNY_CFWD_NOANSWER;
			ast_copy_string(l->call_forward_noanswer, cfwd, sizeof(l->call_forward_noanswer));
		}
	} else {
		if (cfwdtype & SKINNY_CFWD_ALL) {
			l->cfwdtype &= ~SKINNY_CFWD_ALL;
			memset(l->call_forward_all, 0, sizeof(l->call_forward_all));
		}
		if (cfwdtype & SKINNY_CFWD_BUSY) {
			l->cfwdtype &= ~SKINNY_CFWD_BUSY;
			memset(l->call_forward_busy, 0, sizeof(l->call_forward_busy));
		}
		if (cfwdtype & SKINNY_CFWD_NOANSWER) {
			l->cfwdtype &= ~SKINNY_CFWD_NOANSWER;
			memset(l->call_forward_noanswer, 0, sizeof(l->call_forward_noanswer));
		}
	}
	return l->cfwdtype;
}

static void cleanup_stale_contexts(char *new, char *old)
{
	char *oldcontext, *newcontext, *stalecontext, *stringp, newlist[AST_MAX_CONTEXT];

	while ((oldcontext = strsep(&old, "&"))) {
		stalecontext = '\0';
		ast_copy_string(newlist, new, sizeof(newlist));
		stringp = newlist;
		while ((newcontext = strsep(&stringp, "&"))) {
			if (strcmp(newcontext, oldcontext) == 0) {
				/* This is not the context you're looking for */
				stalecontext = '\0';
				break;
			} else if (strcmp(newcontext, oldcontext)) {
				stalecontext = oldcontext;
			}
			
		}
		if (stalecontext)
			ast_context_destroy(ast_context_find(stalecontext), "Skinny");
	}
}

static void register_exten(struct skinny_line *l)
{
	char multi[256];
	char *stringp, *ext, *context;

	if (ast_strlen_zero(regcontext))
		return;

	ast_copy_string(multi, S_OR(l->regexten, l->name), sizeof(multi));
	stringp = multi;
	while ((ext = strsep(&stringp, "&"))) {
		if ((context = strchr(ext, '@'))) {
			*context++ = '\0'; /* split ext@context */
			if (!ast_context_find(context)) {
				ast_log(LOG_WARNING, "Context %s must exist in regcontext= in skinny.conf!\n", context);
				continue;
			}
		} else {
			context = regcontext;
		}
		ast_add_extension(context, 1, ext, 1, NULL, NULL, "Noop",
			 ast_strdup(l->name), ast_free_ptr, "Skinny");
	}
}

static void unregister_exten(struct skinny_line *l)
{
	char multi[256];
	char *stringp, *ext, *context;

	if (ast_strlen_zero(regcontext))
		return;

	ast_copy_string(multi, S_OR(l->regexten, l->name), sizeof(multi));
	stringp = multi;
	while ((ext = strsep(&stringp, "&"))) {
		if ((context = strchr(ext, '@'))) {
			*context++ = '\0'; /* split ext@context */
			if (!ast_context_find(context)) {
				ast_log(LOG_WARNING, "Context %s must exist in regcontext= in skinny.conf!\n", context);
				continue;
			}
		} else {
			context = regcontext;
		}
		ast_context_remove_extension(context, ext, 1, NULL);
	}
}

static int skinny_register(struct skinny_req *req, struct skinnysession *s)
{
	struct skinny_device *d;
	struct skinny_line *l;
	struct skinny_speeddial *sd;
	struct sockaddr_in sin;
	socklen_t slen;
	int instance;

	AST_LIST_LOCK(&devices);
	AST_LIST_TRAVERSE(&devices, d, list){
		struct ast_sockaddr addr;
		ast_sockaddr_from_sin(&addr, &s->sin);
		if (!strcasecmp(req->data.reg.name, d->id)
				&& ast_apply_ha(d->ha, &addr)) {
			s->device = d;
			d->type = letohl(req->data.reg.type);
			if (ast_strlen_zero(d->version_id)) {
				ast_copy_string(d->version_id, version_id, sizeof(d->version_id));
			}
			d->registered = 1;
			d->session = s;

			slen = sizeof(sin);
			if (getsockname(s->fd, (struct sockaddr *)&sin, &slen)) {
				ast_log(LOG_WARNING, "Cannot get socket name\n");
				sin.sin_addr = __ourip;
			}
			d->ourip = sin.sin_addr;

			AST_LIST_TRAVERSE(&d->speeddials, sd, list) {
				sd->stateid = ast_extension_state_add(sd->context, sd->exten, skinny_extensionstate_cb, sd);
			}
			instance = 0;
			AST_LIST_TRAVERSE(&d->lines, l, list) {
				instance++;
			}
			AST_LIST_TRAVERSE(&d->lines, l, list) {
				/* FIXME: All sorts of issues will occur if this line is already connected to a device */
				if (l->device) {
					manager_event(EVENT_FLAG_SYSTEM, "PeerStatus", "ChannelType: Skinny\r\nPeer: Skinny/%s@%s\r\nPeerStatus: Rejected\r\nCause: LINE_ALREADY_CONNECTED\r\n", l->name, l->device->name); 
					ast_verb(1, "Line %s already connected to %s. Not connecting to %s.\n", l->name, l->device->name, d->name);
				} else {
					l->device = d;
					l->capability = l->confcapability & d->capability;
					l->prefs = l->confprefs;
					if (!l->prefs.order[0]) {
						l->prefs = d->confprefs;
					}
					/* l->capability = d->capability;
					l->prefs = d->prefs; */
					l->instance = instance;
					l->newmsgs = ast_app_has_voicemail(l->mailbox, NULL);
					set_callforwards(l, NULL, 0);
					manager_event(EVENT_FLAG_SYSTEM, "PeerStatus", "ChannelType: Skinny\r\nPeer: Skinny/%s@%s\r\nPeerStatus: Registered\r\n", l->name, d->name);
					register_exten(l);
					/* initialize MWI on line and device */
					mwi_event_cb(0, l);
					ast_devstate_changed(AST_DEVICE_NOT_INUSE, "Skinny/%s@%s", l->name, d->name);
				}
				--instance;
			}
			break;
		}
	}
	AST_LIST_UNLOCK(&devices);
	if (!d) {
		return 0;
	}
	return 1;
}

static int skinny_unregister(struct skinny_req *req, struct skinnysession *s)
{
	struct skinny_device *d;
	struct skinny_line *l;
	struct skinny_speeddial *sd;

	d = s->device;

	if (d) {
		d->session = NULL;
		d->registered = 0;

		AST_LIST_TRAVERSE(&d->speeddials, sd, list) {
			if (sd->stateid > -1)
				ast_extension_state_del(sd->stateid, NULL);
		}
		AST_LIST_TRAVERSE(&d->lines, l, list) {
			if (l->device == d) {
				l->device = NULL;
				l->capability = 0;
				ast_parse_allow_disallow(&l->prefs, &l->capability, "all", 0);			
				l->instance = 0;
				manager_event(EVENT_FLAG_SYSTEM, "PeerStatus", "ChannelType: Skinny\r\nPeer: Skinny/%s@%s\r\nPeerStatus: Unregistered\r\n", l->name, d->name);
				unregister_exten(l);
				ast_devstate_changed(AST_DEVICE_UNAVAILABLE, "Skinny/%s@%s", l->name, d->name);
			}
		}
	}

	return -1; /* main loop will destroy the session */
}

#ifdef SKINNY_DEVMODE
static char *message2str(int type)
{
	char *tmp;

	switch (letohl(type)) {
	case KEEP_ALIVE_MESSAGE:
		return "KEEP_ALIVE_MESSAGE";
	case REGISTER_MESSAGE:
		return "REGISTER_MESSAGE";
	case IP_PORT_MESSAGE:
		return "IP_PORT_MESSAGE";
	case KEYPAD_BUTTON_MESSAGE:
		return "KEYPAD_BUTTON_MESSAGE";
	case ENBLOC_CALL_MESSAGE:
		return "ENBLOC_CALL_MESSAGE";
	case STIMULUS_MESSAGE:
		return "STIMULUS_MESSAGE";
	case OFFHOOK_MESSAGE:
		return "OFFHOOK_MESSAGE";
	case ONHOOK_MESSAGE:
		return "ONHOOK_MESSAGE";
	case CAPABILITIES_RES_MESSAGE:
		return "CAPABILITIES_RES_MESSAGE";
	case SPEED_DIAL_STAT_REQ_MESSAGE:
		return "SPEED_DIAL_STAT_REQ_MESSAGE";
	case LINE_STATE_REQ_MESSAGE:
		return "LINE_STATE_REQ_MESSAGE";
	case TIME_DATE_REQ_MESSAGE:
		return "TIME_DATE_REQ_MESSAGE";
	case BUTTON_TEMPLATE_REQ_MESSAGE:
		return "BUTTON_TEMPLATE_REQ_MESSAGE";
	case VERSION_REQ_MESSAGE:
		return "VERSION_REQ_MESSAGE";
	case SERVER_REQUEST_MESSAGE:
		return "SERVER_REQUEST_MESSAGE";
	case ALARM_MESSAGE:
		return "ALARM_MESSAGE";
	case OPEN_RECEIVE_CHANNEL_ACK_MESSAGE:
		return "OPEN_RECEIVE_CHANNEL_ACK_MESSAGE";
	case SOFT_KEY_SET_REQ_MESSAGE:
		return "SOFT_KEY_SET_REQ_MESSAGE";
	case SOFT_KEY_EVENT_MESSAGE:
		return "SOFT_KEY_EVENT_MESSAGE";
	case UNREGISTER_MESSAGE:
		return "UNREGISTER_MESSAGE";
	case SOFT_KEY_TEMPLATE_REQ_MESSAGE:
		return "SOFT_KEY_TEMPLATE_REQ_MESSAGE";
	case HEADSET_STATUS_MESSAGE:
		return "HEADSET_STATUS_MESSAGE";
	case REGISTER_AVAILABLE_LINES_MESSAGE:
		return "REGISTER_AVAILABLE_LINES_MESSAGE";
	case REGISTER_ACK_MESSAGE:
		return "REGISTER_ACK_MESSAGE";
	case START_TONE_MESSAGE:
		return "START_TONE_MESSAGE";
	case STOP_TONE_MESSAGE:
		return "STOP_TONE_MESSAGE";
	case SET_RINGER_MESSAGE:
		return "SET_RINGER_MESSAGE";
	case SET_LAMP_MESSAGE:
		return "SET_LAMP_MESSAGE";
	case SET_SPEAKER_MESSAGE:
		return "SET_SPEAKER_MESSAGE";
	case SET_MICROPHONE_MESSAGE:
		return "SET_MICROPHONE_MESSAGE";
	case START_MEDIA_TRANSMISSION_MESSAGE:
		return "START_MEDIA_TRANSMISSION_MESSAGE";
	case STOP_MEDIA_TRANSMISSION_MESSAGE:
		return "STOP_MEDIA_TRANSMISSION_MESSAGE";
	case CALL_INFO_MESSAGE:
		return "CALL_INFO_MESSAGE";
	case FORWARD_STAT_MESSAGE:
		return "FORWARD_STAT_MESSAGE";
	case SPEED_DIAL_STAT_RES_MESSAGE:
		return "SPEED_DIAL_STAT_RES_MESSAGE";
	case LINE_STAT_RES_MESSAGE:
		return "LINE_STAT_RES_MESSAGE";
	case DEFINETIMEDATE_MESSAGE:
		return "DEFINETIMEDATE_MESSAGE";
	case BUTTON_TEMPLATE_RES_MESSAGE:
		return "BUTTON_TEMPLATE_RES_MESSAGE";
	case VERSION_RES_MESSAGE:
		return "VERSION_RES_MESSAGE";
	case DISPLAYTEXT_MESSAGE:
		return "DISPLAYTEXT_MESSAGE";
	case CLEAR_NOTIFY_MESSAGE:
		return "CLEAR_NOTIFY_MESSAGE";
	case CLEAR_DISPLAY_MESSAGE:
		return "CLEAR_DISPLAY_MESSAGE";
	case CAPABILITIES_REQ_MESSAGE:
		return "CAPABILITIES_REQ_MESSAGE";
	case REGISTER_REJ_MESSAGE:
		return "REGISTER_REJ_MESSAGE";
	case SERVER_RES_MESSAGE:
		return "SERVER_RES_MESSAGE";
	case RESET_MESSAGE:
		return "RESET_MESSAGE";
	case KEEP_ALIVE_ACK_MESSAGE:
		return "KEEP_ALIVE_ACK_MESSAGE";
	case OPEN_RECEIVE_CHANNEL_MESSAGE:
		return "OPEN_RECEIVE_CHANNEL_MESSAGE";
	case CLOSE_RECEIVE_CHANNEL_MESSAGE:
		return "CLOSE_RECEIVE_CHANNEL_MESSAGE";
	case SOFT_KEY_TEMPLATE_RES_MESSAGE:
		return "SOFT_KEY_TEMPLATE_RES_MESSAGE";
	case SOFT_KEY_SET_RES_MESSAGE:
		return "SOFT_KEY_SET_RES_MESSAGE";
	case SELECT_SOFT_KEYS_MESSAGE:
		return "SELECT_SOFT_KEYS_MESSAGE";
	case CALL_STATE_MESSAGE:
		return "CALL_STATE_MESSAGE";
	case DISPLAY_PROMPT_STATUS_MESSAGE:
		return "DISPLAY_PROMPT_STATUS_MESSAGE";
	case CLEAR_PROMPT_MESSAGE:
		return "CLEAR_PROMPT_MESSAGE";
	case DISPLAY_NOTIFY_MESSAGE:
		return "DISPLAY_NOTIFY_MESSAGE";
	case ACTIVATE_CALL_PLANE_MESSAGE:
		return "ACTIVATE_CALL_PLANE_MESSAGE";
	case DIALED_NUMBER_MESSAGE:
		return "DIALED_NUMBER_MESSAGE";
	default:
		if (!(tmp = ast_threadstorage_get(&message2str_threadbuf, MESSAGE2STR_BUFSIZE)))
			return "Unknown";
		snprintf(tmp, MESSAGE2STR_BUFSIZE, "UNKNOWN_MESSAGE-%d", type);
		return tmp;
	}
}
#endif

static int transmit_response(struct skinny_device *d, struct skinny_req *req)
{
	struct skinnysession *s = d->session;
	int res = 0;

	if (!s) {
		ast_log(LOG_WARNING, "Asked to transmit to a non-existent session!\n");
		return -1;
	}

	ast_mutex_lock(&s->lock);

	SKINNY_DEVONLY(if (skinnydebug>1) ast_verb(4, "Transmitting %s to %s\n", message2str(req->e), d->name);)

	if ((letohl(req->len) > SKINNY_MAX_PACKET) || (letohl(req->len) < 0)) {
		ast_log(LOG_WARNING, "transmit_response: the length of the request (%d) is out of bounds (%d)\n", letohl(req->len), SKINNY_MAX_PACKET);
		ast_mutex_unlock(&s->lock);
		return -1;
	}

	memset(s->outbuf, 0, sizeof(s->outbuf));
	memcpy(s->outbuf, req, skinny_header_size);
	memcpy(s->outbuf+skinny_header_size, &req->data, letohl(req->len));

	res = write(s->fd, s->outbuf, letohl(req->len)+8);
	
	if (res != letohl(req->len)+8) {
		ast_log(LOG_WARNING, "Transmit: write only sent %d out of %d bytes: %s\n", res, letohl(req->len)+8, strerror(errno));
		if (res == -1) {
			if (skinnydebug)
				ast_log(LOG_WARNING, "Transmit: Skinny Client was lost, unregistering\n");
			skinny_unregister(NULL, s);
		}
		
	}
	
	ast_free(req);
	ast_mutex_unlock(&s->lock);
	return 1;
}

static void transmit_speaker_mode(struct skinny_device *d, int mode)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct set_speaker_message), SET_SPEAKER_MESSAGE)))
		return;

	req->data.setspeaker.mode = htolel(mode);
	transmit_response(d, req);
}
/*
static void transmit_microphone_mode(struct skinny_device *d, int mode)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct set_microphone_message), SET_MICROPHONE_MESSAGE)))
		return;

	req->data.setmicrophone.mode = htolel(mode);
	transmit_response(d, req);
}
*/

static void transmit_callinfo(struct skinny_device *d, const char *fromname, const char *fromnum, const char *toname, const char *tonum, int instance, int callid, int calltype)
{
	struct skinny_req *req;

	/* We should not be able to get here without a device */
	if (!d)
		return;

	if (!(req = req_alloc(sizeof(struct call_info_message), CALL_INFO_MESSAGE)))
		return;

	if (skinnydebug)
			ast_verb(1, "Setting Callinfo to %s(%s) from %s(%s) on %s(%d)\n", fromname, fromnum, toname, tonum, d->name, instance);

	if (fromname) {
		ast_copy_string(req->data.callinfo.callingPartyName, fromname, sizeof(req->data.callinfo.callingPartyName));
	}
	if (fromnum) {
		ast_copy_string(req->data.callinfo.callingParty, fromnum, sizeof(req->data.callinfo.callingParty));
	}
	if (toname) {
		ast_copy_string(req->data.callinfo.calledPartyName, toname, sizeof(req->data.callinfo.calledPartyName));
	}
	if (tonum) {
		ast_copy_string(req->data.callinfo.calledParty, tonum, sizeof(req->data.callinfo.calledParty));
	}
	req->data.callinfo.instance = htolel(instance);
	req->data.callinfo.reference = htolel(callid);
	req->data.callinfo.type = htolel(calltype);
	transmit_response(d, req);
}

static void transmit_connect(struct skinny_device *d, struct skinny_subchannel *sub)
{
	struct skinny_req *req;
	struct skinny_line *l = sub->parent;
	struct ast_format_list fmt;

	if (!(req = req_alloc(sizeof(struct open_receive_channel_message), OPEN_RECEIVE_CHANNEL_MESSAGE)))
		return;

	fmt = ast_codec_pref_getsize(&l->prefs, ast_best_codec(l->capability));

	req->data.openreceivechannel.conferenceId = htolel(sub->callid);
	req->data.openreceivechannel.partyId = htolel(sub->callid);
	req->data.openreceivechannel.packets = htolel(fmt.cur_ms);
	req->data.openreceivechannel.capability = htolel(codec_ast2skinny(fmt.bits));
	req->data.openreceivechannel.echo = htolel(0);
	req->data.openreceivechannel.bitrate = htolel(0);
	transmit_response(d, req);
}

static void transmit_start_tone(struct skinny_device *d, int tone, int instance, int reference)
{
	struct skinny_req *req;
	if (!(req = req_alloc(sizeof(struct start_tone_message), START_TONE_MESSAGE)))
		return;
	req->data.starttone.tone = htolel(tone);
	req->data.starttone.instance = htolel(instance);
	req->data.starttone.reference = htolel(reference);
	transmit_response(d, req);
}

static void transmit_stop_tone(struct skinny_device *d, int instance, int reference)
{
	struct skinny_req *req;
	if (!(req = req_alloc(sizeof(struct stop_tone_message), STOP_TONE_MESSAGE)))
		return;
	req->data.stoptone.instance = htolel(instance);
	req->data.stoptone.reference = htolel(reference);
	transmit_response(d, req);
}

static void transmit_selectsoftkeys(struct skinny_device *d, int instance, int callid, int softkey)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct select_soft_keys_message), SELECT_SOFT_KEYS_MESSAGE)))
		return;

	req->data.selectsoftkey.instance = htolel(instance);
	req->data.selectsoftkey.reference = htolel(callid);
	req->data.selectsoftkey.softKeySetIndex = htolel(softkey);
	req->data.selectsoftkey.validKeyMask = htolel(0xFFFFFFFF);
	transmit_response(d, req);
}

static void transmit_lamp_indication(struct skinny_device *d, int stimulus, int instance, int indication)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct set_lamp_message), SET_LAMP_MESSAGE)))
		return;

	req->data.setlamp.stimulus = htolel(stimulus);
	req->data.setlamp.stimulusInstance = htolel(instance);
	req->data.setlamp.deviceStimulus = htolel(indication);
	transmit_response(d, req);
}

static void transmit_ringer_mode(struct skinny_device *d, int mode)
{
	struct skinny_req *req;

	if (skinnydebug)
		ast_verb(1, "Setting ringer mode to '%d'.\n", mode);

	if (!(req = req_alloc(sizeof(struct set_ringer_message), SET_RINGER_MESSAGE)))
		return;

	req->data.setringer.ringerMode = htolel(mode);
	/* XXX okay, I don't quite know what this is, but here's what happens (on a 7960).
	   Note: The phone will always show as ringing on the display.

	   1: phone will audibly ring over and over
	   2: phone will audibly ring only once
	   any other value, will NOT cause the phone to audibly ring
	*/
	req->data.setringer.unknown1 = htolel(1);
	/* XXX the value here doesn't seem to change anything.  Must be higher than 0.
	   Perhaps a packet capture can shed some light on this. */
	req->data.setringer.unknown2 = htolel(1);
	transmit_response(d, req);
}

static void transmit_clear_display_message(struct skinny_device *d, int instance, int reference)
{
	struct skinny_req *req;
	if (!(req = req_alloc(0, CLEAR_DISPLAY_MESSAGE)))
		return;

	//what do we want hear CLEAR_DISPLAY_MESSAGE or CLEAR_PROMPT_STATUS???
	//if we are clearing the display, it appears there is no instance and refernece info (size 0)
	//req->data.clearpromptstatus.lineInstance = instance;
	//req->data.clearpromptstatus.callReference = reference;

	if (skinnydebug)
		ast_verb(1, "Clearing Display\n");
	transmit_response(d, req);
}

/* This function is not currently used, but will be (wedhorn)*/
/* static void transmit_display_message(struct skinny_device *d, const char *text, int instance, int reference)
{
	struct skinny_req *req;

	if (text == 0) {
		ast_verb(1, "Bug, Asked to display empty message\n");
		return;
	}

	if (!(req = req_alloc(sizeof(struct displaytext_message), DISPLAYTEXT_MESSAGE)))
		return;

	ast_copy_string(req->data.displaytext.text, text, sizeof(req->data.displaytext.text));
	if (skinnydebug)
		ast_verb(1, "Displaying message '%s'\n", req->data.displaytext.text);
	transmit_response(d, req);
} */

static void transmit_displaynotify(struct skinny_device *d, const char *text, int t)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct display_notify_message), DISPLAY_NOTIFY_MESSAGE)))
		return;

	ast_copy_string(req->data.displaynotify.displayMessage, text, sizeof(req->data.displaynotify.displayMessage));
	req->data.displaynotify.displayTimeout = htolel(t);

	if (skinnydebug)
		ast_verb(1, "Displaying notify '%s'\n", text);

	transmit_response(d, req);
}

static void transmit_displaypromptstatus(struct skinny_device *d, const char *text, int t, int instance, int callid)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct display_prompt_status_message), DISPLAY_PROMPT_STATUS_MESSAGE)))
		return;

	ast_copy_string(req->data.displaypromptstatus.promptMessage, text, sizeof(req->data.displaypromptstatus.promptMessage));
	req->data.displaypromptstatus.messageTimeout = htolel(t);
	req->data.displaypromptstatus.lineInstance = htolel(instance);
	req->data.displaypromptstatus.callReference = htolel(callid);

	if (skinnydebug)
		ast_verb(1, "Displaying Prompt Status '%s'\n", text);

	transmit_response(d, req);
}

static void transmit_clearpromptmessage(struct skinny_device *d, int instance, int callid)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct clear_prompt_message), CLEAR_PROMPT_MESSAGE)))
		return;

	req->data.clearpromptstatus.lineInstance = htolel(instance);
	req->data.clearpromptstatus.callReference = htolel(callid);

	if (skinnydebug)
		ast_verb(1, "Clearing Prompt\n");

	transmit_response(d, req);
}

static void transmit_dialednumber(struct skinny_device *d, const char *text, int instance, int callid)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct dialed_number_message), DIALED_NUMBER_MESSAGE)))
		return;

	ast_copy_string(req->data.dialednumber.dialedNumber, text, sizeof(req->data.dialednumber.dialedNumber));
	req->data.dialednumber.lineInstance = htolel(instance);
	req->data.dialednumber.callReference = htolel(callid);

	transmit_response(d, req);
}

static void transmit_closereceivechannel(struct skinny_device *d, struct skinny_subchannel *sub)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct close_receive_channel_message), CLOSE_RECEIVE_CHANNEL_MESSAGE)))
		return;

	req->data.closereceivechannel.conferenceId = htolel(0);
	req->data.closereceivechannel.partyId = htolel(sub->callid);
	transmit_response(d, req);
}

static void transmit_stopmediatransmission(struct skinny_device *d, struct skinny_subchannel *sub)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct stop_media_transmission_message), STOP_MEDIA_TRANSMISSION_MESSAGE)))
		return;

	req->data.stopmedia.conferenceId = htolel(0);
	req->data.stopmedia.passThruPartyId = htolel(sub->callid);
	transmit_response(d, req);
}

static void transmit_startmediatransmission(struct skinny_device *d, struct skinny_subchannel *sub, struct sockaddr_in dest, struct ast_format_list fmt)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct start_media_transmission_message), START_MEDIA_TRANSMISSION_MESSAGE)))
		return;

	req->data.startmedia.conferenceId = htolel(sub->callid);
	req->data.startmedia.passThruPartyId = htolel(sub->callid);
	req->data.startmedia.remoteIp = dest.sin_addr.s_addr;
	req->data.startmedia.remotePort = htolel(ntohs(dest.sin_port));
	req->data.startmedia.packetSize = htolel(fmt.cur_ms);
	req->data.startmedia.payloadType = htolel(codec_ast2skinny(fmt.bits));
	req->data.startmedia.qualifier.precedence = htolel(127);
	req->data.startmedia.qualifier.vad = htolel(0);
	req->data.startmedia.qualifier.packets = htolel(0);
	req->data.startmedia.qualifier.bitRate = htolel(0);
	transmit_response(d, req);
}

static void transmit_activatecallplane(struct skinny_device *d, struct skinny_line *l)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct activate_call_plane_message), ACTIVATE_CALL_PLANE_MESSAGE)))
		return;

	req->data.activatecallplane.lineInstance = htolel(l->instance);
	transmit_response(d, req);
}

static void transmit_callstate(struct skinny_device *d, int buttonInstance, unsigned callid, int state)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct call_state_message), CALL_STATE_MESSAGE)))
		return;

	req->data.callstate.callState = htolel(state);
	req->data.callstate.lineInstance = htolel(buttonInstance);
	req->data.callstate.callReference = htolel(callid);
	transmit_response(d, req);
}

static void transmit_cfwdstate(struct skinny_device *d, struct skinny_line *l)
{
	struct skinny_req *req;
	int anyon = 0;

	if (!(req = req_alloc(sizeof(struct forward_stat_message), FORWARD_STAT_MESSAGE)))
		return;

	if (l->cfwdtype & SKINNY_CFWD_ALL) {
		if (!ast_strlen_zero(l->call_forward_all)) {
			ast_copy_string(req->data.forwardstat.fwdallnum, l->call_forward_all, sizeof(req->data.forwardstat.fwdallnum));
			req->data.forwardstat.fwdall = htolel(1);
			anyon++;
		} else {
			req->data.forwardstat.fwdall = htolel(0);
		}
	}
	if (l->cfwdtype & SKINNY_CFWD_BUSY) {
		if (!ast_strlen_zero(l->call_forward_busy)) {
			ast_copy_string(req->data.forwardstat.fwdbusynum, l->call_forward_busy, sizeof(req->data.forwardstat.fwdbusynum));
			req->data.forwardstat.fwdbusy = htolel(1);
			anyon++;
		} else {
			req->data.forwardstat.fwdbusy = htolel(0);
		}
	}
	if (l->cfwdtype & SKINNY_CFWD_NOANSWER) {
		if (!ast_strlen_zero(l->call_forward_noanswer)) {
			ast_copy_string(req->data.forwardstat.fwdnoanswernum, l->call_forward_noanswer, sizeof(req->data.forwardstat.fwdnoanswernum));
			req->data.forwardstat.fwdnoanswer = htolel(1);
			anyon++;
		} else {
			req->data.forwardstat.fwdnoanswer = htolel(0);
		}
	}
	req->data.forwardstat.lineNumber = htolel(l->instance);
	if (anyon)
		req->data.forwardstat.activeforward = htolel(7);
	else
		req->data.forwardstat.activeforward = htolel(0);

	transmit_response(d, req);
}

static void transmit_speeddialstatres(struct skinny_device *d, struct skinny_speeddial *sd)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct speed_dial_stat_res_message), SPEED_DIAL_STAT_RES_MESSAGE)))
		return;

	req->data.speeddialreq.speedDialNumber = htolel(sd->instance);
	ast_copy_string(req->data.speeddial.speedDialDirNumber, sd->exten, sizeof(req->data.speeddial.speedDialDirNumber));
	ast_copy_string(req->data.speeddial.speedDialDisplayName, sd->label, sizeof(req->data.speeddial.speedDialDisplayName));

	transmit_response(d, req);
}

static void transmit_linestatres(struct skinny_device *d, struct skinny_line *l)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct line_stat_res_message), LINE_STAT_RES_MESSAGE)))
		return;

	req->data.linestat.lineNumber = letohl(l->instance);
	memcpy(req->data.linestat.lineDirNumber, l->name, sizeof(req->data.linestat.lineDirNumber));
	memcpy(req->data.linestat.lineDisplayName, l->label, sizeof(req->data.linestat.lineDisplayName));
	transmit_response(d, req);
}

static void transmit_definetimedate(struct skinny_device *d)
{
	struct skinny_req *req;
	struct timeval now = ast_tvnow();
	struct ast_tm cmtime;

	if (!(req = req_alloc(sizeof(struct definetimedate_message), DEFINETIMEDATE_MESSAGE)))
		return;

	ast_localtime(&now, &cmtime, NULL);
	req->data.definetimedate.year = htolel(cmtime.tm_year+1900);
	req->data.definetimedate.month = htolel(cmtime.tm_mon+1);
	req->data.definetimedate.dayofweek = htolel(cmtime.tm_wday);
	req->data.definetimedate.day = htolel(cmtime.tm_mday);
	req->data.definetimedate.hour = htolel(cmtime.tm_hour);
	req->data.definetimedate.minute = htolel(cmtime.tm_min);
	req->data.definetimedate.seconds = htolel(cmtime.tm_sec);
	req->data.definetimedate.milliseconds = htolel(cmtime.tm_usec / 1000);
	req->data.definetimedate.timestamp = htolel(now.tv_sec);
	transmit_response(d, req);
}

static void transmit_versionres(struct skinny_device *d)
{
	struct skinny_req *req;
	if (!(req = req_alloc(sizeof(struct version_res_message), VERSION_RES_MESSAGE)))
		return;

	ast_copy_string(req->data.version.version, d->version_id, sizeof(req->data.version.version));
	transmit_response(d, req);
}

static void transmit_serverres(struct skinny_device *d)
{
	struct skinny_req *req;
	if (!(req = req_alloc(sizeof(struct server_res_message), SERVER_RES_MESSAGE)))
		return;

	memcpy(req->data.serverres.server[0].serverName, ourhost,
			sizeof(req->data.serverres.server[0].serverName));
	req->data.serverres.serverListenPort[0] = htolel(ourport);
	req->data.serverres.serverIpAddr[0] = htolel(d->ourip.s_addr);
	transmit_response(d, req);
}

static void transmit_softkeysetres(struct skinny_device *d)
{
	struct skinny_req *req;
	int i;
	int x;
	int y;
	const struct soft_key_definitions *softkeymode = soft_key_default_definitions;

	if (!(req = req_alloc(sizeof(struct soft_key_set_res_message), SOFT_KEY_SET_RES_MESSAGE)))
		return;

	req->data.softkeysets.softKeySetOffset = htolel(0);
	req->data.softkeysets.softKeySetCount = htolel(11);
	req->data.softkeysets.totalSoftKeySetCount = htolel(11);
	for (x = 0; x < sizeof(soft_key_default_definitions) / sizeof(struct soft_key_definitions); x++) {
		const uint8_t *defaults = softkeymode->defaults;
		/* XXX I wanted to get the size of the array dynamically, but that wasn't wanting to work.
		   This will have to do for now. */
		for (y = 0; y < softkeymode->count; y++) {
			for (i = 0; i < (sizeof(soft_key_template_default) / sizeof(struct soft_key_template_definition)); i++) {
				if (defaults[y] == i+1) {
					req->data.softkeysets.softKeySetDefinition[softkeymode->mode].softKeyTemplateIndex[y] = (i+1);
					req->data.softkeysets.softKeySetDefinition[softkeymode->mode].softKeyInfoIndex[y] = htoles(i+301);
				        if (skinnydebug)	
						ast_verbose("softKeySetDefinition : softKeyTemplateIndex: %d softKeyInfoIndex: %d\n", i+1, i+301);
				}
			}
		}
		softkeymode++;
	}
	transmit_response(d, req);
}

static void transmit_softkeytemplateres(struct skinny_device *d)
{
	struct skinny_req *req;
	if (!(req = req_alloc(sizeof(struct soft_key_template_res_message), SOFT_KEY_TEMPLATE_RES_MESSAGE)))
		return;

	req->data.softkeytemplate.softKeyOffset = htolel(0);
	req->data.softkeytemplate.softKeyCount = htolel(sizeof(soft_key_template_default) / sizeof(struct soft_key_template_definition));
	req->data.softkeytemplate.totalSoftKeyCount = htolel(sizeof(soft_key_template_default) / sizeof(struct soft_key_template_definition));
	memcpy(req->data.softkeytemplate.softKeyTemplateDefinition,
		soft_key_template_default,
		sizeof(soft_key_template_default));
	transmit_response(d, req);
}


static int skinny_extensionstate_cb(char *context, char *exten, int state, void *data)
{
	struct skinny_speeddial *sd = data;
	struct skinny_device *d = sd->parent;
	char hint[AST_MAX_EXTENSION];

	if (ast_get_hint(hint, sizeof(hint), NULL, 0, NULL, sd->context, sd->exten)) {
		/* If they are not registered, we will override notification and show no availability */
		if (ast_device_state(hint) == AST_DEVICE_UNAVAILABLE) {
			transmit_lamp_indication(d, STIMULUS_LINE, sd->instance, SKINNY_LAMP_FLASH);
			transmit_callstate(d, sd->instance, SKINNY_ONHOOK, 0);
		}
	} else {
		switch (state) {
		case AST_EXTENSION_DEACTIVATED: /* Retry after a while */
		case AST_EXTENSION_REMOVED:     /* Extension is gone */
			ast_verb(2, "Extension state: Watcher for hint %s %s. Notify Device %s\n", exten, state == AST_EXTENSION_DEACTIVATED ? "deactivated" : "removed", d->name);
			sd->stateid = -1;
			transmit_lamp_indication(d, STIMULUS_LINE, sd->instance, SKINNY_LAMP_OFF);
			transmit_callstate(d, sd->instance, SKINNY_ONHOOK, 0);
			break;
		case AST_EXTENSION_RINGING:
		case AST_EXTENSION_UNAVAILABLE:
			transmit_lamp_indication(d, STIMULUS_LINE, sd->instance, SKINNY_LAMP_BLINK);
			transmit_callstate(d, sd->instance, SKINNY_RINGIN, 0);
			break;
		case AST_EXTENSION_BUSY: /* callstate = SKINNY_BUSY wasn't wanting to work - I'll settle for this */
		case AST_EXTENSION_INUSE:
			transmit_lamp_indication(d, STIMULUS_LINE, sd->instance, SKINNY_LAMP_ON);
			transmit_callstate(d, sd->instance, SKINNY_CALLREMOTEMULTILINE, 0);
			break;
		case AST_EXTENSION_ONHOLD:
			transmit_lamp_indication(d, STIMULUS_LINE, sd->instance, SKINNY_LAMP_WINK);
			transmit_callstate(d, sd->instance, SKINNY_HOLD, 0);
			break;
		case AST_EXTENSION_NOT_INUSE:
		default:
			transmit_lamp_indication(d, STIMULUS_LINE, sd->instance, SKINNY_LAMP_OFF);
			transmit_callstate(d, sd->instance, SKINNY_ONHOOK, 0);
			break;
		}
	}

	sd->laststate = state;

	return 0;
}

static void update_connectedline(struct skinny_subchannel *sub, const void *data, size_t datalen)
{
	struct ast_channel *c = sub->owner;
	struct skinny_line *l = sub->parent;
	struct skinny_device *d = l->device;

	if (!c->caller.id.number.valid
		|| ast_strlen_zero(c->caller.id.number.str)
		|| !c->connected.id.number.valid
		|| ast_strlen_zero(c->connected.id.number.str))
		return;

	if (sub->owner->_state == AST_STATE_UP) {
		transmit_callstate(d, l->instance, sub->callid, SKINNY_CONNECTED);
		transmit_displaypromptstatus(d, "Connected", 0, l->instance, sub->callid);
		if (sub->outgoing)
			transmit_callinfo(d,
				S_COR(c->connected.id.name.valid, c->connected.id.name.str, ""),
				c->connected.id.number.str,
				l->cid_name, l->cid_num, l->instance, sub->callid, 1);
		else
			transmit_callinfo(d, l->cid_name, l->cid_num,
				S_COR(c->connected.id.name.valid, c->connected.id.name.str, ""),
				c->connected.id.number.str,
				l->instance, sub->callid, 2);
	} else {
		if (sub->outgoing) {
			transmit_callstate(d, l->instance, sub->callid, SKINNY_RINGIN);
			transmit_displaypromptstatus(d, "Ring-In", 0, l->instance, sub->callid);
			transmit_callinfo(d,
				S_COR(c->connected.id.name.valid, c->connected.id.name.str, ""),
				c->connected.id.number.str,
				l->cid_name, l->cid_num, l->instance, sub->callid, 1);
		} else {
			if (!sub->ringing) {
				transmit_callstate(d, l->instance, sub->callid, SKINNY_RINGOUT);
				transmit_displaypromptstatus(d, "Ring-Out", 0, l->instance, sub->callid);
				sub->ringing = 1;
			} else {
				transmit_callstate(d, l->instance, sub->callid, SKINNY_PROGRESS);
				transmit_displaypromptstatus(d, "Call Progress", 0, l->instance, sub->callid);
				sub->progress = 1;
			}

			transmit_callinfo(d, l->cid_name, l->cid_num,
				S_COR(c->connected.id.name.valid, c->connected.id.name.str, ""),
				c->connected.id.number.str,
				l->instance, sub->callid, 2);
		}
	}
}

static void mwi_event_cb(const struct ast_event *event, void *userdata)
{
	struct skinny_line *l = userdata;
	struct skinny_device *d = l->device;
	if (d) {
		struct skinnysession *s = d->session;
		struct skinny_line *l2;
		int new_msgs = 0;
		int dev_msgs = 0;

		if (s) {
			if (event) {
				l->newmsgs = ast_event_get_ie_uint(event, AST_EVENT_IE_NEWMSGS);
			}

			if (l->newmsgs) {
				transmit_lamp_indication(d, STIMULUS_VOICEMAIL, l->instance, l->mwiblink?SKINNY_LAMP_BLINK:SKINNY_LAMP_ON);
			} else {
				transmit_lamp_indication(d, STIMULUS_VOICEMAIL, l->instance, SKINNY_LAMP_OFF);
			}

			/* find out wether the device lamp should be on or off */
			AST_LIST_TRAVERSE(&d->lines, l2, list) {
				if (l2->newmsgs) {
					dev_msgs++;
				}
			}

			if (dev_msgs) {
				transmit_lamp_indication(d, STIMULUS_VOICEMAIL, 0, d->mwiblink?SKINNY_LAMP_BLINK:SKINNY_LAMP_ON);
			} else {
				transmit_lamp_indication(d, STIMULUS_VOICEMAIL, 0, SKINNY_LAMP_OFF);
			}
			ast_verb(3, "Skinny mwi_event_cb found %d new messages\n", new_msgs);
		}
	}
}

/* I do not believe skinny can deal with video.
   Anyone know differently? */
/* Yes, it can.  Currently 7985 and Cisco VT Advantage do video. */
static enum ast_rtp_glue_result skinny_get_vrtp_peer(struct ast_channel *c, struct ast_rtp_instance **instance)
{
	struct skinny_subchannel *sub = NULL;

	if (!(sub = c->tech_pvt) || !(sub->vrtp))
		return AST_RTP_GLUE_RESULT_FORBID;

	ao2_ref(sub->vrtp, +1);
	*instance = sub->vrtp;

	return AST_RTP_GLUE_RESULT_REMOTE;
}

static enum ast_rtp_glue_result skinny_get_rtp_peer(struct ast_channel *c, struct ast_rtp_instance **instance)
{
	struct skinny_subchannel *sub = NULL;
	struct skinny_line *l;
	enum ast_rtp_glue_result res = AST_RTP_GLUE_RESULT_REMOTE;

	if (skinnydebug)
		ast_verb(1, "skinny_get_rtp_peer() Channel = %s\n", c->name);


	if (!(sub = c->tech_pvt))
		return AST_RTP_GLUE_RESULT_FORBID;

	ast_mutex_lock(&sub->lock);

	if (!(sub->rtp)){
		ast_mutex_unlock(&sub->lock);
		return AST_RTP_GLUE_RESULT_FORBID;
	}

	ao2_ref(sub->rtp, +1);
	*instance = sub->rtp;

	l = sub->parent;

	if (!l->directmedia || l->nat){
		res = AST_RTP_GLUE_RESULT_LOCAL;
		if (skinnydebug)
			ast_verb(1, "skinny_get_rtp_peer() Using AST_RTP_GLUE_RESULT_LOCAL \n");
	}

	ast_mutex_unlock(&sub->lock);

	return res;

}

static int skinny_set_rtp_peer(struct ast_channel *c, struct ast_rtp_instance *rtp, struct ast_rtp_instance *vrtp, struct ast_rtp_instance *trtp, format_t codecs, int nat_active)
{
	struct skinny_subchannel *sub;
	struct skinny_line *l;
	struct skinny_device *d;
	struct ast_format_list fmt;
	struct sockaddr_in us = { 0, };
	struct sockaddr_in them = { 0, };
	struct ast_sockaddr them_tmp;
	struct ast_sockaddr us_tmp;
	
	sub = c->tech_pvt;

	if (c->_state != AST_STATE_UP)
		return 0;

	if (!sub) {
		return -1;
	}

	l = sub->parent;
	d = l->device;

	if (rtp){
		ast_rtp_instance_get_remote_address(rtp, &them_tmp);
		ast_sockaddr_to_sin(&them_tmp, &them);

		/* Shutdown any early-media or previous media on re-invite */
		transmit_stopmediatransmission(d, sub);
		
		if (skinnydebug)
			ast_verb(1, "Peerip = %s:%d\n", ast_inet_ntoa(them.sin_addr), ntohs(them.sin_port));

		fmt = ast_codec_pref_getsize(&l->prefs, ast_best_codec(l->capability));

		if (skinnydebug)
			ast_verb(1, "Setting payloadType to '%s' (%d ms)\n", ast_getformatname(fmt.bits), fmt.cur_ms);

		if (!(l->directmedia) || (l->nat)){
			ast_rtp_instance_get_local_address(rtp, &us_tmp);
			ast_sockaddr_to_sin(&us_tmp, &us);
			us.sin_addr.s_addr = us.sin_addr.s_addr ? us.sin_addr.s_addr : d->ourip.s_addr;
			transmit_startmediatransmission(d, sub, us, fmt);
		} else {
			transmit_startmediatransmission(d, sub, them, fmt);
		}

		return 0;
	}
	/* Need a return here to break the bridge */
	return 0;
}

static struct ast_rtp_glue skinny_rtp_glue = {
	.type = "Skinny",
	.get_rtp_info = skinny_get_rtp_peer,
	.get_vrtp_info = skinny_get_vrtp_peer,
	.update_peer = skinny_set_rtp_peer,
};

static char *handle_skinny_set_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
#ifdef SKINNY_DEVMODE
		e->command = "skinny set debug {off|on|packet}";
		e->usage =
			"Usage: skinny set debug {off|on|packet}\n"
			"       Enables/Disables dumping of Skinny packets for debugging purposes\n";
#else
		e->command = "skinny set debug {off|on}";
		e->usage =
			"Usage: skinny set debug {off|on}\n"
			"       Enables/Disables dumping of Skinny packets for debugging purposes\n";
#endif
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	
	if (a->argc != e->args)
		return CLI_SHOWUSAGE;

	if (!strncasecmp(a->argv[e->args - 1], "on", 2)) {
		skinnydebug = 1;
		ast_cli(a->fd, "Skinny Debugging Enabled\n");
		return CLI_SUCCESS;
	} else if (!strncasecmp(a->argv[e->args - 1], "off", 3)) {
		skinnydebug = 0;
		ast_cli(a->fd, "Skinny Debugging Disabled\n");
		return CLI_SUCCESS;
#ifdef SKINNY_DEVMODE
	} else if (!strncasecmp(a->argv[e->args - 1], "packet", 6)) {
		skinnydebug = 2;
		ast_cli(a->fd, "Skinny Debugging Enabled including Packets\n");
		return CLI_SUCCESS;
#endif
	} else {
		return CLI_SHOWUSAGE;
	}
}

static char *handle_skinny_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "skinny reload";
		e->usage =
			"Usage: skinny reload\n"
			"       Reloads the chan_skinny configuration\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	
	if (a->argc != e->args)
		return CLI_SHOWUSAGE;

	skinny_reload();
	return CLI_SUCCESS;

}

static char *complete_skinny_devices(const char *word, int state)
{
	struct skinny_device *d;
	char *result = NULL;
	int wordlen = strlen(word), which = 0;

	AST_LIST_TRAVERSE(&devices, d, list) {
		if (!strncasecmp(word, d->id, wordlen) && ++which > state)
			result = ast_strdup(d->id);
	}

	return result;
}

static char *complete_skinny_show_device(const char *line, const char *word, int pos, int state)
{
	return (pos == 3 ? ast_strdup(complete_skinny_devices(word, state)) : NULL);
}

static char *complete_skinny_reset(const char *line, const char *word, int pos, int state)
{
	return (pos == 2 ? ast_strdup(complete_skinny_devices(word, state)) : NULL);
}

static char *complete_skinny_show_line(const char *line, const char *word, int pos, int state)
{
	struct skinny_device *d;
	struct skinny_line *l;
	char *result = NULL;
	int wordlen = strlen(word), which = 0;

	if (pos != 3)
		return NULL;
	
	AST_LIST_TRAVERSE(&devices, d, list) {
		AST_LIST_TRAVERSE(&d->lines, l, list) {
			if (!strncasecmp(word, l->name, wordlen) && ++which > state)
				result = ast_strdup(l->name);
		}
	}

	return result;
}

static char *handle_skinny_reset(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct skinny_device *d;
	struct skinny_req *req;

	switch (cmd) {
	case CLI_INIT:
		e->command = "skinny reset";
		e->usage =
			"Usage: skinny reset <DeviceId|DeviceName|all> [restart]\n"
			"       Causes a Skinny device to reset itself, optionally with a full restart\n";
		return NULL;
	case CLI_GENERATE:
		return complete_skinny_reset(a->line, a->word, a->pos, a->n);
	}

	if (a->argc < 3 || a->argc > 4)
		return CLI_SHOWUSAGE;

	AST_LIST_LOCK(&devices);
	AST_LIST_TRAVERSE(&devices, d, list) {
		int fullrestart = 0;
		if (!strcasecmp(a->argv[2], d->id) || !strcasecmp(a->argv[2], d->name) || !strcasecmp(a->argv[2], "all")) {
			if (!(d->session))
				continue;

			if (!(req = req_alloc(sizeof(struct reset_message), RESET_MESSAGE)))
				continue;

			if (a->argc == 4 && !strcasecmp(a->argv[3], "restart"))
				fullrestart = 1;

			if (fullrestart)
				req->data.reset.resetType = 2;
			else
				req->data.reset.resetType = 1;

			ast_verb(3, "%s device %s.\n", (fullrestart) ? "Restarting" : "Resetting", d->id);
			transmit_response(d, req);
		}
	}
	AST_LIST_UNLOCK(&devices);
	return CLI_SUCCESS;
}

static char *device2str(int type)
{
	char *tmp;

	switch (type) {
	case SKINNY_DEVICE_NONE:
		return "No Device";
	case SKINNY_DEVICE_30SPPLUS:
		return "30SP Plus";
	case SKINNY_DEVICE_12SPPLUS:
		return "12SP Plus";
	case SKINNY_DEVICE_12SP:
		return "12SP";
	case SKINNY_DEVICE_12:
		return "12";
	case SKINNY_DEVICE_30VIP:
		return "30VIP";
	case SKINNY_DEVICE_7910:
		return "7910";
	case SKINNY_DEVICE_7960:
		return "7960";
	case SKINNY_DEVICE_7940:
		return "7940";
	case SKINNY_DEVICE_7935:
		return "7935";
	case SKINNY_DEVICE_ATA186:
		return "ATA186";
	case SKINNY_DEVICE_7941:
		return "7941";
	case SKINNY_DEVICE_7971:
		return "7971";
	case SKINNY_DEVICE_7914:
		return "7914";
	case SKINNY_DEVICE_7985:
		return "7985";
	case SKINNY_DEVICE_7911:
		return "7911";
	case SKINNY_DEVICE_7961GE:
		return "7961GE";
	case SKINNY_DEVICE_7941GE:
		return "7941GE";
	case SKINNY_DEVICE_7931:
		return "7931";
	case SKINNY_DEVICE_7921:
		return "7921";
	case SKINNY_DEVICE_7906:
		return "7906";
	case SKINNY_DEVICE_7962:
		return "7962";
	case SKINNY_DEVICE_7937:
		return "7937";
	case SKINNY_DEVICE_7942:
		return "7942";
	case SKINNY_DEVICE_7945:
		return "7945";
	case SKINNY_DEVICE_7965:
		return "7965";
	case SKINNY_DEVICE_7975:
		return "7975";
	case SKINNY_DEVICE_7905:
		return "7905";
	case SKINNY_DEVICE_7920:
		return "7920";
	case SKINNY_DEVICE_7970:
		return "7970";
	case SKINNY_DEVICE_7912:
		return "7912";
	case SKINNY_DEVICE_7902:
		return "7902";
	case SKINNY_DEVICE_CIPC:
		return "IP Communicator";
	case SKINNY_DEVICE_7961:
		return "7961";
	case SKINNY_DEVICE_7936:
		return "7936";
	case SKINNY_DEVICE_SCCPGATEWAY_AN:
		return "SCCPGATEWAY_AN";
	case SKINNY_DEVICE_SCCPGATEWAY_BRI:
		return "SCCPGATEWAY_BRI";
	case SKINNY_DEVICE_UNKNOWN:
		return "Unknown";
	default:
		if (!(tmp = ast_threadstorage_get(&device2str_threadbuf, DEVICE2STR_BUFSIZE)))
			return "Unknown";
		snprintf(tmp, DEVICE2STR_BUFSIZE, "UNKNOWN-%d", type);
		return tmp;
	}
}

/*! \brief Print codec list from preference to CLI/manager */
static void print_codec_to_cli(int fd, struct ast_codec_pref *pref)
{
	int x, codec;

	for(x = 0; x < 32 ; x++) {
		codec = ast_codec_pref_index(pref, x);
		if (!codec)
			break;
		ast_cli(fd, "%s", ast_getformatname(codec));
		ast_cli(fd, ":%d", pref->framing[x]);
		if (x < 31 && ast_codec_pref_index(pref, x + 1))
			ast_cli(fd, ",");
	}
	if (!x)
		ast_cli(fd, "none");
}

static char *_skinny_show_devices(int fd, int *total, struct mansession *s, const struct message *m, int argc, const char *argv[])
{
	struct skinny_device *d;
	struct skinny_line *l;
	const char *id;
	char idtext[256] = "";
	int total_devices = 0;

	if (s) {	/* Manager - get ActionID */
		id = astman_get_header(m, "ActionID");
		if (!ast_strlen_zero(id))
			snprintf(idtext, sizeof(idtext), "ActionID: %s\r\n", id);
	}

	switch (argc) {
	case 3:
		break;
	default:
		return CLI_SHOWUSAGE;
	}

	if (!s) {
		ast_cli(fd, "Name                 DeviceId         IP              Type            R NL\n");
		ast_cli(fd, "-------------------- ---------------- --------------- --------------- - --\n");
	}

	AST_LIST_LOCK(&devices);
	AST_LIST_TRAVERSE(&devices, d, list) {
		int numlines = 0;
		total_devices++;
		AST_LIST_TRAVERSE(&d->lines, l, list) {
			numlines++;
		}
		if (!s) {
			ast_cli(fd, "%-20s %-16s %-15s %-15s %c %2d\n",
				d->name,
				d->id,
				d->session?ast_inet_ntoa(d->session->sin.sin_addr):"",
				device2str(d->type),
				d->registered?'Y':'N',
				numlines);
		} else {
			astman_append(s,
				"Event: DeviceEntry\r\n%s"
				"Channeltype: SKINNY\r\n"
				"ObjectName: %s\r\n"
				"ChannelObjectType: device\r\n"
				"DeviceId: %s\r\n"
				"IPaddress: %s\r\n"
				"Type: %s\r\n"
				"Devicestatus: %s\r\n"
				"NumberOfLines: %d\r\n",
				idtext,
				d->name,
				d->id,
				d->session?ast_inet_ntoa(d->session->sin.sin_addr):"-none-",
				device2str(d->type),
				d->registered?"registered":"unregistered",
				numlines);
		}
	}
	AST_LIST_UNLOCK(&devices);

	if (total)
		*total = total_devices;
	
	return CLI_SUCCESS;
}

/*! \brief  Show SKINNY devices in the manager API */
/*    Inspired from chan_sip */
static int manager_skinny_show_devices(struct mansession *s, const struct message *m)
{
	const char *id = astman_get_header(m, "ActionID");
	const char *a[] = {"skinny", "show", "devices"};
	char idtext[256] = "";
	int total = 0;

	if (!ast_strlen_zero(id))
		snprintf(idtext, sizeof(idtext), "ActionID: %s\r\n", id);

	astman_send_listack(s, m, "Device status list will follow", "start");
	/* List the devices in separate manager events */
	_skinny_show_devices(-1, &total, s, m, 3, a);
	/* Send final confirmation */
	astman_append(s,
	"Event: DevicelistComplete\r\n"
	"EventList: Complete\r\n"
	"ListItems: %d\r\n"
	"%s"
	"\r\n", total, idtext);
	return 0;
}

static char *handle_skinny_show_devices(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{

	switch (cmd) {
	case CLI_INIT:
		e->command = "skinny show devices";
		e->usage =
			"Usage: skinny show devices\n"
			"       Lists all devices known to the Skinny subsystem.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	return _skinny_show_devices(a->fd, NULL, NULL, NULL, a->argc, (const char **) a->argv);
}

static char *_skinny_show_device(int type, int fd, struct mansession *s, const struct message *m, int argc, const char *argv[])
{
	struct skinny_device *d;
	struct skinny_line *l;
	struct skinny_speeddial *sd;
	struct skinny_addon *sa;
	char codec_buf[512];

	if (argc < 4) {
		return CLI_SHOWUSAGE;
	}

	AST_LIST_LOCK(&devices);
	AST_LIST_TRAVERSE(&devices, d, list) {
		if (!strcasecmp(argv[3], d->id) || !strcasecmp(argv[3], d->name)) {
			int numlines = 0, numaddons = 0, numspeeddials = 0;

			AST_LIST_TRAVERSE(&d->lines, l, list){
				numlines++;
			}

			AST_LIST_TRAVERSE(&d->addons, sa, list) {
				numaddons++;
			}

			AST_LIST_TRAVERSE(&d->speeddials, sd, list) {
				numspeeddials++;
			}

			if (type == 0) { /* CLI */
				ast_cli(fd, "Name:        %s\n", d->name);
				ast_cli(fd, "Id:          %s\n", d->id);
				ast_cli(fd, "version:     %s\n", S_OR(d->version_id, "Unknown"));
				ast_cli(fd, "Ip address:  %s\n", (d->session ? ast_inet_ntoa(d->session->sin.sin_addr) : "Unknown"));
				ast_cli(fd, "Port:        %d\n", (d->session ? ntohs(d->session->sin.sin_port) : 0));
				ast_cli(fd, "Device Type: %s\n", device2str(d->type));
				ast_cli(fd, "Conf Codecs:");
				ast_getformatname_multiple(codec_buf, sizeof(codec_buf) - 1, d->confcapability);
				ast_cli(fd, "%s\n", codec_buf);
				ast_cli(fd, "Neg Codecs: ");
				ast_getformatname_multiple(codec_buf, sizeof(codec_buf) - 1, d->capability);
				ast_cli(fd, "%s\n", codec_buf);
				ast_cli(fd, "Registered:  %s\n", (d->registered ? "Yes" : "No"));
				ast_cli(fd, "Lines:       %d\n", numlines);
				AST_LIST_TRAVERSE(&d->lines, l, list) {
					ast_cli(fd, "  %s (%s)\n", l->name, l->label);
				}
				AST_LIST_TRAVERSE(&d->addons, sa, list) {
					numaddons++;
				}	
				ast_cli(fd, "Addons:      %d\n", numaddons);
				AST_LIST_TRAVERSE(&d->addons, sa, list) {
					ast_cli(fd, "  %s\n", sa->type);
				}
				AST_LIST_TRAVERSE(&d->speeddials, sd, list) {
					numspeeddials++;
				}
				ast_cli(fd, "Speeddials:  %d\n", numspeeddials);
				AST_LIST_TRAVERSE(&d->speeddials, sd, list) {
					ast_cli(fd, "  %s (%s) ishint: %d\n", sd->exten, sd->label, sd->isHint);
				}
			} else { /* manager */
				astman_append(s, "Channeltype: SKINNY\r\n");
				astman_append(s, "ObjectName: %s\r\n", d->name);
				astman_append(s, "ChannelObjectType: device\r\n");
				astman_append(s, "Id: %s\r\n", d->id);
				astman_append(s, "version: %s\r\n", S_OR(d->version_id, "Unknown"));
				astman_append(s, "Ipaddress: %s\r\n", (d->session ? ast_inet_ntoa(d->session->sin.sin_addr) : "Unknown"));
				astman_append(s, "Port: %d\r\n", (d->session ? ntohs(d->session->sin.sin_port) : 0));
				astman_append(s, "DeviceType: %s\r\n", device2str(d->type));
				astman_append(s, "Codecs: ");
				ast_getformatname_multiple(codec_buf, sizeof(codec_buf) -1, d->confcapability);
				astman_append(s, "%s\r\n", codec_buf);
				astman_append(s, "CodecOrder: ");
				ast_getformatname_multiple(codec_buf, sizeof(codec_buf) -1, d->capability);
				astman_append(s, "%s\r\n", codec_buf);
				astman_append(s, "Devicestatus: %s\r\n", (d->registered?"registered":"unregistered"));
				astman_append(s, "NumberOfLines: %d\r\n", numlines);
				AST_LIST_TRAVERSE(&d->lines, l, list) {
					astman_append(s, "Line: %s (%s)\r\n", l->name, l->label);
				}
				astman_append(s, "NumberOfAddons: %d\r\n", numaddons);
				AST_LIST_TRAVERSE(&d->addons, sa, list) {
					astman_append(s, "Addon: %s\r\n", sa->type);
				}
				astman_append(s, "NumberOfSpeeddials: %d\r\n", numspeeddials);
				AST_LIST_TRAVERSE(&d->speeddials, sd, list) {
					astman_append(s, "Speeddial: %s (%s) ishint: %d\r\n", sd->exten, sd->label, sd->isHint);
				}
			}
		}
	}
	AST_LIST_UNLOCK(&devices);
	return CLI_SUCCESS;
}

static int manager_skinny_show_device(struct mansession *s, const struct message *m)
{
	const char *a[4];
	const char *device;

	device = astman_get_header(m, "Device");
	if (ast_strlen_zero(device)) {
		astman_send_error(s, m, "Device: <name> missing.");
		return 0;
	}
	a[0] = "skinny";
	a[1] = "show";
	a[2] = "device";
	a[3] = device;

	_skinny_show_device(1, -1, s, m, 4, a);
	astman_append(s, "\r\n\r\n" );
	return 0;
}

/*! \brief Show device information */
static char *handle_skinny_show_device(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "skinny show device";
		e->usage =
			"Usage: skinny show device <DeviceId|DeviceName>\n"
			"       Lists all deviceinformation of a specific device known to the Skinny subsystem.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_skinny_show_device(a->line, a->word, a->pos, a->n);
	}

	return _skinny_show_device(0, a->fd, NULL, NULL, a->argc, (const char **) a->argv);
}

static char *_skinny_show_lines(int fd, int *total, struct mansession *s, const struct message *m, int argc, const char *argv[])
{
	struct skinny_line *l;
	struct skinny_subchannel *sub;
	int total_lines = 0;
	int verbose = 0;
	const char *id;
	char idtext[256] = "";

	if (s) {	/* Manager - get ActionID */
		id = astman_get_header(m, "ActionID");
		if (!ast_strlen_zero(id))
			snprintf(idtext, sizeof(idtext), "ActionID: %s\r\n", id);
	}

	switch (argc) {
	case 4:
		verbose = 1;
		break;
	case 3:
		verbose = 0;
		break;
	default:
		return CLI_SHOWUSAGE;
	}

	if (!s) {
	 	ast_cli(fd, "Name                 Device Name          Instance Label               \n");
		ast_cli(fd, "-------------------- -------------------- -------- --------------------\n");
	}
	AST_LIST_LOCK(&lines);
	AST_LIST_TRAVERSE(&lines, l, all) {
		total_lines++;
		if (!s) {
			ast_cli(fd, "%-20s %-20s %8d %-20s\n",
				l->name,
				(l->device ? l->device->name : "Not connected"),
				l->instance,
				l->label);
			if (verbose) {
				AST_LIST_TRAVERSE(&l->sub, sub, list) {
					ast_cli(fd, "  %s> %s to %s\n",
						(sub == l->activesub?"Active  ":"Inactive"),
						sub->owner->name,
						(ast_bridged_channel(sub->owner)?ast_bridged_channel(sub->owner)->name:"")
					);
				}
			}
		} else {
			astman_append(s,
				"Event: LineEntry\r\n%s"
				"Channeltype: SKINNY\r\n"
				"ObjectName: %s\r\n"
				"ChannelObjectType: line\r\n"
				"Device: %s\r\n"
				"Instance: %d\r\n"
				"Label: %s\r\n",
				idtext,
				l->name,
				(l->device?l->device->name:"None"),
				l->instance,
				l->label);
		}
	}
	AST_LIST_UNLOCK(&lines);

	if (total) {
		*total = total_lines;
	}

	return CLI_SUCCESS;
}

/*! \brief  Show Skinny lines in the manager API */
/*    Inspired from chan_sip */
static int manager_skinny_show_lines(struct mansession *s, const struct message *m)
{
	const char *id = astman_get_header(m, "ActionID");
	const char *a[] = {"skinny", "show", "lines"};
	char idtext[256] = "";
	int total = 0;

	if (!ast_strlen_zero(id))
		snprintf(idtext, sizeof(idtext), "ActionID: %s\r\n", id);

	astman_send_listack(s, m, "Line status list will follow", "start");
	/* List the lines in separate manager events */
	_skinny_show_lines(-1, &total, s, m, 3, a);
	/* Send final confirmation */
	astman_append(s,
	"Event: LinelistComplete\r\n"
	"EventList: Complete\r\n"
	"ListItems: %d\r\n"
	"%s"
	"\r\n", total, idtext);
	return 0;
}

static char *handle_skinny_show_lines(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "skinny show lines [verbose]";
		e->usage =
			"Usage: skinny show lines\n"
			"       Lists all lines known to the Skinny subsystem.\n"
			"       If 'verbose' is specified, the output includes\n"
			"       information about subs for each line.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc == e->args) {
		if (strcasecmp(a->argv[e->args-1], "verbose")) {
			return CLI_SHOWUSAGE;
		}
	} else if (a->argc != e->args - 1) {
		return CLI_SHOWUSAGE;
	}

	return _skinny_show_lines(a->fd, NULL, NULL, NULL, a->argc, (const char **) a->argv);
}

static char *_skinny_show_line(int type, int fd, struct mansession *s, const struct message *m, int argc, const char *argv[])
{
	struct skinny_device *d;
	struct skinny_line *l;
	struct ast_codec_pref *pref;
	int x = 0, codec = 0;
	char codec_buf[512];
	char group_buf[256];
	char cbuf[256];

	switch (argc) {
	case 4:
		break;
	case 6:
		break;
	default:
		return CLI_SHOWUSAGE;
	}

	AST_LIST_LOCK(&devices);

	/* Show all lines matching the one supplied */
	AST_LIST_TRAVERSE(&devices, d, list) {
		if (argc == 6 && (strcasecmp(argv[5], d->id) && strcasecmp(argv[5], d->name))) {
			continue;
		}
		AST_LIST_TRAVERSE(&d->lines, l, list) {
			if (strcasecmp(argv[3], l->name)) {
				continue;
			}
			if (type == 0) { /* CLI */
				ast_cli(fd, "Line:             %s\n", l->name);
				ast_cli(fd, "On Device:        %s\n", d->name);
				ast_cli(fd, "Line Label:       %s\n", l->label);
				ast_cli(fd, "Extension:        %s\n", S_OR(l->exten, "<not set>"));
				ast_cli(fd, "Context:          %s\n", l->context);
				ast_cli(fd, "CallGroup:        %s\n", ast_print_group(group_buf, sizeof(group_buf), l->callgroup));
				ast_cli(fd, "PickupGroup:      %s\n", ast_print_group(group_buf, sizeof(group_buf), l->pickupgroup));
				ast_cli(fd, "Language:         %s\n", S_OR(l->language, "<not set>"));
				ast_cli(fd, "Accountcode:      %s\n", S_OR(l->accountcode, "<not set>"));
				ast_cli(fd, "AmaFlag:          %s\n", ast_cdr_flags2str(l->amaflags));
				ast_cli(fd, "CallerId Number:  %s\n", S_OR(l->cid_num, "<not set>"));
				ast_cli(fd, "CallerId Name:    %s\n", S_OR(l->cid_name, "<not set>"));
				ast_cli(fd, "Hide CallerId:    %s\n", (l->hidecallerid ? "Yes" : "No"));
				ast_cli(fd, "CFwdAll:          %s\n", S_COR((l->cfwdtype & SKINNY_CFWD_ALL), l->call_forward_all, "<not set>"));
				ast_cli(fd, "CFwdBusy:         %s\n", S_COR((l->cfwdtype & SKINNY_CFWD_BUSY), l->call_forward_busy, "<not set>"));
				ast_cli(fd, "CFwdNoAnswer:     %s\n", S_COR((l->cfwdtype & SKINNY_CFWD_NOANSWER), l->call_forward_noanswer, "<not set>"));
				ast_cli(fd, "VoicemailBox:     %s\n", S_OR(l->mailbox, "<not set>"));
				ast_cli(fd, "VoicemailNumber:  %s\n", S_OR(l->vmexten, "<not set>"));
				ast_cli(fd, "MWIblink:         %d\n", l->mwiblink);
				ast_cli(fd, "Regextension:     %s\n", S_OR(l->regexten, "<not set>"));
				ast_cli(fd, "Regcontext:       %s\n", S_OR(l->regcontext, "<not set>"));
				ast_cli(fd, "MoHInterpret:     %s\n", S_OR(l->mohinterpret, "<not set>"));
				ast_cli(fd, "MoHSuggest:       %s\n", S_OR(l->mohsuggest, "<not set>"));
				ast_cli(fd, "Last dialed nr:   %s\n", S_OR(l->lastnumberdialed, "<no calls made yet>"));
				ast_cli(fd, "Last CallerID:    %s\n", S_OR(l->lastcallerid, "<not set>"));
				ast_cli(fd, "Transfer enabled: %s\n", (l->transfer ? "Yes" : "No"));
				ast_cli(fd, "Callwaiting:      %s\n", (l->callwaiting ? "Yes" : "No"));
				ast_cli(fd, "3Way Calling:     %s\n", (l->threewaycalling ? "Yes" : "No"));
				ast_cli(fd, "Can forward:      %s\n", (l->cancallforward ? "Yes" : "No"));
				ast_cli(fd, "Do Not Disturb:   %s\n", (l->dnd ? "Yes" : "No"));
				ast_cli(fd, "NAT:              %s\n", (l->nat ? "Yes" : "No"));
				ast_cli(fd, "immediate:        %s\n", (l->immediate ? "Yes" : "No"));
				ast_cli(fd, "Group:            %d\n", l->group);
				ast_cli(fd, "Parkinglot:       %s\n", S_OR(l->parkinglot, "<not set>"));
				ast_cli(fd, "Conf Codecs:      ");
				ast_getformatname_multiple(codec_buf, sizeof(codec_buf) - 1, l->confcapability);
				ast_cli(fd, "%s\n", codec_buf);
				ast_cli(fd, "Neg Codecs:       ");
				ast_getformatname_multiple(codec_buf, sizeof(codec_buf) - 1, l->capability);
				ast_cli(fd, "%s\n", codec_buf);
				ast_cli(fd, "Codec Order:      (");
				print_codec_to_cli(fd, &l->prefs);
				ast_cli(fd, ")\n");
				ast_cli(fd, "\n");
			} else { /* manager */
				astman_append(s, "Channeltype: SKINNY\r\n");
				astman_append(s, "ObjectName: %s\r\n", l->name);
				astman_append(s, "ChannelObjectType: line\r\n");
				astman_append(s, "Device: %s\r\n", d->name);
				astman_append(s, "LineLabel: %s\r\n", l->label);
				astman_append(s, "Extension: %s\r\n", S_OR(l->exten, "<not set>"));
				astman_append(s, "Context: %s\r\n", l->context);
				astman_append(s, "CallGroup: %s\r\n", ast_print_group(group_buf, sizeof(group_buf), l->callgroup));
				astman_append(s, "PickupGroup: %s\r\n", ast_print_group(group_buf, sizeof(group_buf), l->pickupgroup));
				astman_append(s, "Language: %s\r\n", S_OR(l->language, "<not set>"));
				astman_append(s, "Accountcode: %s\r\n", S_OR(l->accountcode, "<not set>"));
				astman_append(s, "AMAflags: %s\r\n", ast_cdr_flags2str(l->amaflags));
				astman_append(s, "Callerid: %s\r\n", ast_callerid_merge(cbuf, sizeof(cbuf), l->cid_name, l->cid_num, ""));
				astman_append(s, "HideCallerId: %s\r\n", (l->hidecallerid ? "Yes" : "No"));
				astman_append(s, "CFwdAll: %s\r\n", S_COR((l->cfwdtype & SKINNY_CFWD_ALL), l->call_forward_all, "<not set>"));
				astman_append(s, "CFwdBusy: %s\r\n", S_COR((l->cfwdtype & SKINNY_CFWD_BUSY), l->call_forward_busy, "<not set>"));
				astman_append(s, "CFwdNoAnswer: %s\r\n", S_COR((l->cfwdtype & SKINNY_CFWD_NOANSWER), l->call_forward_noanswer, "<not set>"));
				astman_append(s, "VoicemailBox: %s\r\n", S_OR(l->mailbox, "<not set>"));
				astman_append(s, "VoicemailNumber: %s\r\n", S_OR(l->vmexten, "<not set>"));
				astman_append(s, "MWIblink: %d\r\n", l->mwiblink);
				astman_append(s, "RegExtension: %s\r\n", S_OR(l->regexten, "<not set>"));
				astman_append(s, "Regcontext: %s\r\n", S_OR(l->regcontext, "<not set>"));
				astman_append(s, "MoHInterpret: %s\r\n", S_OR(l->mohinterpret, "<not set>"));
				astman_append(s, "MoHSuggest: %s\r\n", S_OR(l->mohsuggest, "<not set>"));
				astman_append(s, "LastDialedNr: %s\r\n", S_OR(l->lastnumberdialed, "<no calls made yet>"));
				astman_append(s, "LastCallerID: %s\r\n", S_OR(l->lastcallerid, "<not set>"));
				astman_append(s, "Transfer: %s\r\n", (l->transfer ? "Yes" : "No"));
				astman_append(s, "Callwaiting: %s\r\n", (l->callwaiting ? "Yes" : "No"));
				astman_append(s, "3WayCalling: %s\r\n", (l->threewaycalling ? "Yes" : "No"));
				astman_append(s, "CanForward: %s\r\n", (l->cancallforward ? "Yes" : "No"));
				astman_append(s, "DoNotDisturb: %s\r\n", (l->dnd ? "Yes" : "No"));
				astman_append(s, "NAT: %s\r\n", (l->nat ? "Yes" : "No"));
				astman_append(s, "immediate: %s\r\n", (l->immediate ? "Yes" : "No"));
				astman_append(s, "Group: %d\r\n", l->group);
				astman_append(s, "Parkinglot: %s\r\n", S_OR(l->parkinglot, "<not set>"));
				ast_getformatname_multiple(codec_buf, sizeof(codec_buf) - 1, l->confcapability);
				astman_append(s, "Codecs: %s\r\n", codec_buf);
				astman_append(s, "CodecOrder: ");
				pref = &l->prefs;
				for(x = 0; x < 32 ; x++) {
					codec = ast_codec_pref_index(pref, x);
					if (!codec)
						break;
					astman_append(s, "%s", ast_getformatname(codec));
					if (x < 31 && ast_codec_pref_index(pref, x+1))
						astman_append(s, ",");
				}
				astman_append(s, "\r\n");
			}
		}
	}
	
	AST_LIST_UNLOCK(&devices);
	return CLI_SUCCESS;
}

static int manager_skinny_show_line(struct mansession *s, const struct message *m)
{
	const char *a[4];
	const char *line;

	line = astman_get_header(m, "Line");
	if (ast_strlen_zero(line)) {
		astman_send_error(s, m, "Line: <name> missing.");
		return 0;
	}
	a[0] = "skinny";
	a[1] = "show";
	a[2] = "line";
	a[3] = line;

	_skinny_show_line(1, -1, s, m, 4, a);
	astman_append(s, "\r\n\r\n" );
	return 0;
}

/*! \brief List line information. */
static char *handle_skinny_show_line(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "skinny show line";
		e->usage =
			"Usage: skinny show line <Line> [ on <DeviceID|DeviceName> ]\n"
			"       List all lineinformation of a specific line known to the Skinny subsystem.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_skinny_show_line(a->line, a->word, a->pos, a->n);
	}

	return _skinny_show_line(0, a->fd, NULL, NULL, a->argc, (const char **) a->argv);
}

/*! \brief List global settings for the Skinny subsystem. */
static char *handle_skinny_show_settings(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "skinny show settings";
		e->usage =
			"Usage: skinny show settings\n"
			"       Lists all global configuration settings of the Skinny subsystem.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}	

	if (a->argc != 3)
		return CLI_SHOWUSAGE;

	ast_cli(a->fd, "\nGlobal Settings:\n");
	ast_cli(a->fd, "  Skinny Port:            %d\n", ntohs(bindaddr.sin_port));
	ast_cli(a->fd, "  Bindaddress:            %s\n", ast_inet_ntoa(bindaddr.sin_addr));
	ast_cli(a->fd, "  KeepAlive:              %d\n", keep_alive);
	ast_cli(a->fd, "  Date Format:            %s\n", date_format);
	ast_cli(a->fd, "  Voice Mail Extension:   %s\n", S_OR(global_vmexten, "(not set)"));
	ast_cli(a->fd, "  Reg. context:           %s\n", S_OR(regcontext, "(not set)"));
	ast_cli(a->fd, "  Jitterbuffer enabled:   %s\n", AST_CLI_YESNO(ast_test_flag(&global_jbconf, AST_JB_ENABLED)));
	 if (ast_test_flag(&global_jbconf, AST_JB_ENABLED)) {
		ast_cli(a->fd, "  Jitterbuffer forced:    %s\n", AST_CLI_YESNO(ast_test_flag(&global_jbconf, AST_JB_FORCED)));
		ast_cli(a->fd, "  Jitterbuffer max size:  %ld\n", global_jbconf.max_size);
		ast_cli(a->fd, "  Jitterbuffer resync:    %ld\n", global_jbconf.resync_threshold);
		ast_cli(a->fd, "  Jitterbuffer impl:      %s\n", global_jbconf.impl);
		if (!strcasecmp(global_jbconf.impl, "adaptive")) {
			ast_cli(a->fd, "  Jitterbuffer tgt extra: %ld\n", global_jbconf.target_extra);
		}
		ast_cli(a->fd, "  Jitterbuffer log:       %s\n", AST_CLI_YESNO(ast_test_flag(&global_jbconf, AST_JB_LOG)));
	}

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_skinny[] = {
	AST_CLI_DEFINE(handle_skinny_show_devices, "List defined Skinny devices"),
	AST_CLI_DEFINE(handle_skinny_show_device, "List Skinny device information"),
	AST_CLI_DEFINE(handle_skinny_show_lines, "List defined Skinny lines per device"),
	AST_CLI_DEFINE(handle_skinny_show_line, "List Skinny line information"),
	AST_CLI_DEFINE(handle_skinny_show_settings, "List global Skinny settings"),
	AST_CLI_DEFINE(handle_skinny_set_debug, "Enable/Disable Skinny debugging"),
	AST_CLI_DEFINE(handle_skinny_reset, "Reset Skinny device(s)"),
	AST_CLI_DEFINE(handle_skinny_reload, "Reload Skinny config"),
};

static void start_rtp(struct skinny_subchannel *sub)
{
	struct skinny_line *l = sub->parent;
	struct skinny_device *d = l->device;
	int hasvideo = 0;
	struct ast_sockaddr bindaddr_tmp;

	ast_mutex_lock(&sub->lock);
	/* Allocate the RTP */
	ast_sockaddr_from_sin(&bindaddr_tmp, &bindaddr);
	sub->rtp = ast_rtp_instance_new("asterisk", sched, &bindaddr_tmp, NULL);
	if (hasvideo)
		sub->vrtp = ast_rtp_instance_new("asterisk", sched, &bindaddr_tmp, NULL);

	if (sub->rtp) {
		ast_rtp_instance_set_prop(sub->rtp, AST_RTP_PROPERTY_RTCP, 1);
	}
	if (sub->vrtp) {
		ast_rtp_instance_set_prop(sub->vrtp, AST_RTP_PROPERTY_RTCP, 1);
	}

	if (sub->rtp && sub->owner) {
		ast_channel_set_fd(sub->owner, 0, ast_rtp_instance_fd(sub->rtp, 0));
		ast_channel_set_fd(sub->owner, 1, ast_rtp_instance_fd(sub->rtp, 1));
	}
	if (hasvideo && sub->vrtp && sub->owner) {
		ast_channel_set_fd(sub->owner, 2, ast_rtp_instance_fd(sub->vrtp, 0));
		ast_channel_set_fd(sub->owner, 3, ast_rtp_instance_fd(sub->vrtp, 1));
	}
	if (sub->rtp) {
		ast_rtp_instance_set_qos(sub->rtp, qos.tos_audio, qos.cos_audio, "Skinny RTP");
		ast_rtp_instance_set_prop(sub->rtp, AST_RTP_PROPERTY_NAT, l->nat);
	}
	if (sub->vrtp) {
		ast_rtp_instance_set_qos(sub->vrtp, qos.tos_video, qos.cos_video, "Skinny VRTP");
		ast_rtp_instance_set_prop(sub->vrtp, AST_RTP_PROPERTY_NAT, l->nat);
	}
	/* Set Frame packetization */
	if (sub->rtp)
		ast_rtp_codecs_packetization_set(ast_rtp_instance_get_codecs(sub->rtp), sub->rtp, &l->prefs);

	/* Create the RTP connection */
	transmit_connect(d, sub);
	ast_mutex_unlock(&sub->lock);
}

static void *skinny_newcall(void *data)
{
	struct ast_channel *c = data;
	struct skinny_subchannel *sub = c->tech_pvt;
	struct skinny_line *l = sub->parent;
	struct skinny_device *d = l->device;
	int res = 0;

	ast_copy_string(l->lastnumberdialed, c->exten, sizeof(l->lastnumberdialed));
	ast_set_callerid(c,
		l->hidecallerid ? "" : l->cid_num,
		l->hidecallerid ? "" : l->cid_name,
		c->caller.ani.number.valid ? NULL : l->cid_num);
#if 1	/* XXX This code is probably not necessary */
	ast_party_number_free(&c->connected.id.number);
	ast_party_number_init(&c->connected.id.number);
	c->connected.id.number.valid = 1;
	c->connected.id.number.str = ast_strdup(c->exten);
	ast_party_name_free(&c->connected.id.name);
	ast_party_name_init(&c->connected.id.name);
#endif
	ast_setstate(c, AST_STATE_RING);
	if (!sub->rtp) {
		start_rtp(sub);
	}
	res = ast_pbx_run(c);
	if (res) {
		ast_log(LOG_WARNING, "PBX exited non-zero\n");
		transmit_start_tone(d, SKINNY_REORDER, l->instance, sub->callid);
	}
	return NULL;
}

static void *skinny_ss(void *data)
{
	struct ast_channel *c = data;
	struct skinny_subchannel *sub = c->tech_pvt;
	struct skinny_line *l = sub->parent;
	struct skinny_device *d = l->device;
	int len = 0;
	int timeout = firstdigittimeout;
	int res = 0;
	int loop_pause = 100;

	ast_verb(3, "Starting simple switch on '%s@%s'\n", l->name, d->name);

	len = strlen(d->exten);

	while (len < AST_MAX_EXTENSION-1) {
		res = 1;  /* Assume that we will get a digit */
		while (strlen(d->exten) == len){
			ast_safe_sleep(c, loop_pause);
			timeout -= loop_pause;
			if ( (timeout -= loop_pause) <= 0){
				 res = 0;
				 break;
			}
		res = 1;
		}

		timeout = 0;
		len = strlen(d->exten);

		if (!ast_ignore_pattern(c->context, d->exten)) {
			transmit_stop_tone(d, l->instance, sub->callid);
		}
		if (ast_exists_extension(c, c->context, d->exten, 1, l->cid_num)) {
			if (!res || !ast_matchmore_extension(c, c->context, d->exten, 1, l->cid_num)) {
				if (l->getforward) {
					/* Record this as the forwarding extension */
					set_callforwards(l, d->exten, l->getforward);
					ast_verb(3, "Setting call forward (%d) to '%s' on channel %s\n",
							l->cfwdtype, d->exten, c->name);
					transmit_start_tone(d, SKINNY_DIALTONE, l->instance, sub->callid);
					transmit_lamp_indication(d, STIMULUS_FORWARDALL, 1, SKINNY_LAMP_ON);
					transmit_displaynotify(d, "CFwd enabled", 10);
					transmit_cfwdstate(d, l);
					ast_safe_sleep(c, 500);
					ast_indicate(c, -1);
					ast_safe_sleep(c, 1000);
					memset(d->exten, 0, sizeof(d->exten));
					len = 0;
					l->getforward = 0;
					if (sub->owner && sub->owner->_state != AST_STATE_UP) {
						ast_indicate(c, -1);
						ast_hangup(c);
					}
					return NULL;
				} else {
					ast_copy_string(c->exten, d->exten, sizeof(c->exten));
					ast_copy_string(l->lastnumberdialed, d->exten, sizeof(l->lastnumberdialed));
					memset(d->exten, 0, sizeof(d->exten));
					skinny_newcall(c);
					return NULL;
				}
			} else {
				/* It's a match, but they just typed a digit, and there is an ambiguous match,
				   so just set the timeout to matchdigittimeout and wait some more */
				timeout = matchdigittimeout;
			}
		} else if (res == 0) {
			ast_debug(1, "Not enough digits (%s) (and no ambiguous match)...\n", d->exten);
			memset(d->exten, 0, sizeof(d->exten));
			if (l->hookstate == SKINNY_OFFHOOK) {
				transmit_start_tone(d, SKINNY_REORDER, l->instance, sub->callid);
			}
			if (sub->owner && sub->owner->_state != AST_STATE_UP) {
				ast_indicate(c, -1);
				ast_hangup(c);
			}
			return NULL;
		} else if (!ast_canmatch_extension(c, c->context, d->exten, 1,
			S_COR(c->caller.id.number.valid, c->caller.id.number.str, NULL))
			&& ((d->exten[0] != '*') || (!ast_strlen_zero(d->exten) > 2))) {
			ast_log(LOG_WARNING, "Can't match [%s] from '%s' in context %s\n", d->exten,
				S_COR(c->caller.id.number.valid, c->caller.id.number.str, "<Unknown Caller>"),
				c->context);
			memset(d->exten, 0, sizeof(d->exten));
			if (l->hookstate == SKINNY_OFFHOOK) {
				transmit_start_tone(d, SKINNY_REORDER, l->instance, sub->callid);
				/* hang out for 3 seconds to let congestion play */
				ast_safe_sleep(c, 3000);
			}
			break;
		}
		if (!timeout) {
			timeout = gendigittimeout;
		}
		if (len && !ast_ignore_pattern(c->context, d->exten)) {
			ast_indicate(c, -1);
		}
	}
	if (c)
		ast_hangup(c);
	memset(d->exten, 0, sizeof(d->exten));
	return NULL;
}



static int skinny_call(struct ast_channel *ast, char *dest, int timeout)
{
	int res = 0;
	struct skinny_subchannel *sub = ast->tech_pvt;
	struct skinny_line *l = sub->parent;
	struct skinny_device *d = l->device;

	if (!d->registered) {
		ast_log(LOG_ERROR, "Device not registered, cannot call %s\n", dest);
		return -1;
	}

	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "skinny_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}

	if (skinnydebug)
		ast_verb(3, "skinny_call(%s)\n", ast->name);

	if (l->dnd) {
		ast_queue_control(ast, AST_CONTROL_BUSY);
		return -1;
	}

	if (AST_LIST_NEXT(sub,list) && !l->callwaiting) {
		ast_queue_control(ast, AST_CONTROL_BUSY);
		return -1;
	}
	
	switch (l->hookstate) {
	case SKINNY_OFFHOOK:
		break;
	case SKINNY_ONHOOK:
		l->activesub = sub;
		break;
	default:
		ast_log(LOG_ERROR, "Don't know how to deal with hookstate %d\n", l->hookstate);
		break;
	}

	transmit_callstate(d, sub->parent->instance, sub->callid, SKINNY_RINGIN);
	transmit_selectsoftkeys(d, l->instance, sub->callid, KEYDEF_RINGIN);
	transmit_displaypromptstatus(d, "Ring-In", 0, l->instance, sub->callid);
	transmit_callinfo(d,
		S_COR(ast->connected.id.name.valid, ast->connected.id.name.str, ""),
		S_COR(ast->connected.id.number.valid, ast->connected.id.number.str, ""),
		l->cid_name, l->cid_num, l->instance, sub->callid, 1);
	transmit_lamp_indication(d, STIMULUS_LINE, l->instance, SKINNY_LAMP_BLINK);
	transmit_ringer_mode(d, SKINNY_RING_INSIDE);

	ast_setstate(ast, AST_STATE_RINGING);
	ast_queue_control(ast, AST_CONTROL_RINGING);
	sub->outgoing = 1;
	return res;
}

static int skinny_hangup(struct ast_channel *ast)
{
	struct skinny_subchannel *sub = ast->tech_pvt;
	struct skinny_line *l;
	struct skinny_device *d;

	if (!sub) {
		ast_debug(1, "Asked to hangup channel not connected\n");
		return 0;
	}

	l = sub->parent;
	d = l->device;

	if (skinnydebug)
		ast_verb(3,"Hanging up %s/%d\n",d->name,sub->callid);

	AST_LIST_REMOVE(&l->sub, sub, list);

	if (d->registered) {
		/* Ignoring l->type, doesn't seem relevant and previous code 
		   assigned rather than tested, ie always true */
		if (!AST_LIST_EMPTY(&l->sub)) {
			if (sub->related) {
				sub->related->related = NULL;

			}
			if (sub == l->activesub) {      /* we are killing the active sub, but there are other subs on the line*/
				ast_verb(4,"Killing active sub %d\n", sub->callid);
				if (sub->related) {
					l->activesub = sub->related;
				} else {
					if (AST_LIST_NEXT(sub, list)) {
						l->activesub = AST_LIST_NEXT(sub, list);
					} else {
						l->activesub = AST_LIST_FIRST(&l->sub);
					}
				}
				//transmit_callstate(d, l->instance, SKINNY_ONHOOK, sub->callid);
				transmit_activatecallplane(d, l);
				transmit_closereceivechannel(d, sub);
				transmit_stopmediatransmission(d, sub);
				transmit_lamp_indication(d, STIMULUS_LINE, l->instance, SKINNY_LAMP_BLINK);
				transmit_stop_tone(d, l->instance, sub->callid);
			} else {    /* we are killing a background sub on the line with other subs*/
				ast_verb(4,"Killing inactive sub %d\n", sub->callid);
				if (AST_LIST_NEXT(sub, list)) {
					transmit_lamp_indication(d, STIMULUS_LINE, l->instance, SKINNY_LAMP_BLINK);
				} else {
					transmit_lamp_indication(d, STIMULUS_LINE, l->instance, SKINNY_LAMP_ON);
				}
			}
		} else {                                                /* no more subs on line so make idle */
			ast_verb(4,"Killing only sub %d\n", sub->callid);
			l->hookstate = SKINNY_ONHOOK;
			transmit_closereceivechannel(d, sub);
			transmit_stopmediatransmission(d, sub);
			transmit_speaker_mode(d, SKINNY_SPEAKEROFF);
			transmit_clearpromptmessage(d, l->instance, sub->callid);
			transmit_callstate(d, l->instance, sub->callid, SKINNY_ONHOOK);
			transmit_selectsoftkeys(d, 0, 0, KEYDEF_ONHOOK);
			transmit_activatecallplane(d, l);
			l->activesub = NULL;
			transmit_lamp_indication(d, STIMULUS_LINE, l->instance, SKINNY_LAMP_OFF);
			if (sub->parent == d->activeline) {
				transmit_activatecallplane(d, l);
				transmit_closereceivechannel(d, sub);
				transmit_stopmediatransmission(d, sub);
				transmit_speaker_mode(d, SKINNY_SPEAKEROFF);
				transmit_ringer_mode(d, SKINNY_RING_OFF);
				transmit_clear_display_message(d, l->instance, sub->callid);
				transmit_stop_tone(d, l->instance, sub->callid);
				/* we should check to see if we can start the ringer if another line is ringing */
			}
		}
	}
	ast_mutex_lock(&sub->lock);
	sub->owner = NULL;
	ast->tech_pvt = NULL;
	sub->alreadygone = 0;
	sub->outgoing = 0;
	if (sub->rtp) {
		ast_rtp_instance_destroy(sub->rtp);
		sub->rtp = NULL;
	}
	ast_mutex_unlock(&sub->lock);
	ast_free(sub);
	ast_module_unref(ast_module_info->self);
	return 0;
}

static int skinny_answer(struct ast_channel *ast)
{
	int res = 0;
	struct skinny_subchannel *sub = ast->tech_pvt;
	struct skinny_line *l = sub->parent;
	struct skinny_device *d = l->device;

	if (sub->blindxfer) {
		if (skinnydebug)
			ast_debug(1, "skinny_answer(%s) on %s@%s-%d with BlindXFER, transferring\n",
				ast->name, l->name, d->name, sub->callid);
		ast_setstate(ast, AST_STATE_UP);
		skinny_transfer(sub);
		return 0;
	}

	sub->cxmode = SKINNY_CX_SENDRECV;
	if (!sub->rtp) {
		start_rtp(sub);
	}
	if (skinnydebug)
		ast_verb(1, "skinny_answer(%s) on %s@%s-%d\n", ast->name, l->name, d->name, sub->callid);
	if (ast->_state != AST_STATE_UP) {
		ast_setstate(ast, AST_STATE_UP);
	}

	transmit_stop_tone(d, l->instance, sub->callid);
	/* order matters here...
	   for some reason, transmit_callinfo must be before transmit_callstate,
	   or you won't get keypad messages in some situations. */
	transmit_callinfo(d,
		S_COR(ast->connected.id.name.valid, ast->connected.id.name.str, ""),
		S_COR(ast->connected.id.number.valid, ast->connected.id.number.str, ""),
		l->lastnumberdialed, l->lastnumberdialed, l->instance, sub->callid, 2);
	transmit_callstate(d, sub->parent->instance, sub->callid, SKINNY_CONNECTED);
	transmit_selectsoftkeys(d, l->instance, sub->callid, KEYDEF_CONNECTED);
	transmit_dialednumber(d, l->lastnumberdialed, l->instance, sub->callid);
	transmit_displaypromptstatus(d, "Connected", 0, l->instance, sub->callid);
	l->activesub = sub;
	return res;
}

/* Retrieve audio/etc from channel.  Assumes sub->lock is already held. */
static struct ast_frame *skinny_rtp_read(struct skinny_subchannel *sub)
{
	struct ast_channel *ast = sub->owner;
	struct ast_frame *f;

	if (!sub->rtp) {
		/* We have no RTP allocated for this channel */
		return &ast_null_frame;
	}

	switch(ast->fdno) {
	case 0:
		f = ast_rtp_instance_read(sub->rtp, 0); /* RTP Audio */
		break;
	case 1:
		f = ast_rtp_instance_read(sub->rtp, 1); /* RTCP Control Channel */
		break;
	case 2:
		f = ast_rtp_instance_read(sub->vrtp, 0); /* RTP Video */
		break;
	case 3:
		f = ast_rtp_instance_read(sub->vrtp, 1); /* RTCP Control Channel for video */
		break;
#if 0
	case 5:
		/* Not yet supported */
		f = ast_udptl_read(sub->udptl); /* UDPTL for T.38 */
		break;
#endif
	default:
		f = &ast_null_frame;
	}

	if (ast) {
		/* We already hold the channel lock */
		if (f->frametype == AST_FRAME_VOICE) {
			if (f->subclass.codec != ast->nativeformats) {
				ast_debug(1, "Oooh, format changed to %s\n", ast_getformatname(f->subclass.codec));
				ast->nativeformats = f->subclass.codec;
				ast_set_read_format(ast, ast->readformat);
				ast_set_write_format(ast, ast->writeformat);
			}
		}
	}
	return f;
}

static struct ast_frame *skinny_read(struct ast_channel *ast)
{
	struct ast_frame *fr;
	struct skinny_subchannel *sub = ast->tech_pvt;
	ast_mutex_lock(&sub->lock);
	fr = skinny_rtp_read(sub);
	ast_mutex_unlock(&sub->lock);
	return fr;
}

static int skinny_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct skinny_subchannel *sub = ast->tech_pvt;
	int res = 0;
	if (frame->frametype != AST_FRAME_VOICE) {
		if (frame->frametype == AST_FRAME_IMAGE) {
			return 0;
		} else {
			ast_log(LOG_WARNING, "Can't send %d type frames with skinny_write\n", frame->frametype);
			return 0;
		}
	} else {
		if (!(frame->subclass.codec & ast->nativeformats)) {
			char buf[256];
			ast_log(LOG_WARNING, "Asked to transmit frame type %s, while native formats is %s (read/write = %s/%s)\n",
				ast_getformatname(frame->subclass.codec),
				ast_getformatname_multiple(buf, sizeof(buf), ast->nativeformats),
				ast_getformatname(ast->readformat),
				ast_getformatname(ast->writeformat));
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

static int skinny_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct skinny_subchannel *sub = newchan->tech_pvt;
	ast_log(LOG_NOTICE, "skinny_fixup(%s, %s)\n", oldchan->name, newchan->name);
	if (sub->owner != oldchan) {
		ast_log(LOG_WARNING, "old channel wasn't %p but was %p\n", oldchan, sub->owner);
		return -1;
	}
	sub->owner = newchan;
	return 0;
}

static int skinny_senddigit_begin(struct ast_channel *ast, char digit)
{
	return -1; /* Start inband indications */
}

static int skinny_senddigit_end(struct ast_channel *ast, char digit, unsigned int duration)
{
#if 0
	struct skinny_subchannel *sub = ast->tech_pvt;
	struct skinny_line *l = sub->parent;
	struct skinny_device *d = l->device;
	int tmp;
	/* not right */
	sprintf(tmp, "%d", digit);
	//transmit_tone(d, digit, l->instance, sub->callid);
#endif
	return -1; /* Stop inband indications */
}

static int get_devicestate(struct skinny_line *l)
{
	struct skinny_subchannel *sub;
	int res = AST_DEVICE_UNKNOWN;

	if (!l)
		res = AST_DEVICE_INVALID;
	else if (!l->device)
		res = AST_DEVICE_UNAVAILABLE;
	else if (l->dnd)
		res = AST_DEVICE_BUSY;
	else {
		if (l->hookstate == SKINNY_ONHOOK) {
			res = AST_DEVICE_NOT_INUSE;
		} else {
			res = AST_DEVICE_INUSE;
		}

		AST_LIST_TRAVERSE(&l->sub, sub, list) {
			if (sub->onhold) {
				res = AST_DEVICE_ONHOLD;
				break;
			}
		}
	}

	return res;
}

static char *control2str(int ind) {
	char *tmp;

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
	case AST_CONTROL_PROGRESS:
		return "Remote end is making Progress";
	case AST_CONTROL_PROCEEDING:
		return "Remote end is proceeding";
	case AST_CONTROL_HOLD:
		return "Hold";
	case AST_CONTROL_UNHOLD:
		return "Unhold";
	case AST_CONTROL_SRCUPDATE:
		return "Media Source Update";
	case AST_CONTROL_CONNECTED_LINE:
		return "Connected Line";
	case AST_CONTROL_REDIRECTING:
		return "Redirecting";
	case -1:
		return "Stop tone";
	default:
		if (!(tmp = ast_threadstorage_get(&control2str_threadbuf, CONTROL2STR_BUFSIZE)))
                        return "Unknown";
		snprintf(tmp, CONTROL2STR_BUFSIZE, "UNKNOWN-%d", ind);
		return tmp;
	}
}

static int skinny_transfer(struct skinny_subchannel *sub)
{
	struct skinny_subchannel *xferor; /* the sub doing the transferring */
	struct skinny_subchannel *xferee; /* the sub being transferred */
	struct ast_tone_zone_sound *ts = NULL;
		
	if (ast_bridged_channel(sub->owner) || ast_bridged_channel(sub->related->owner)) {
		if (sub->xferor) {
			xferor = sub;
			xferee = sub->related;
		} else {
			xferor = sub;
			xferee = sub->related;
		}
		
		if (skinnydebug) {
			ast_debug(1, "Transferee channels (local/remote): %s and %s\n",
				xferee->owner->name, ast_bridged_channel(xferee->owner)?ast_bridged_channel(xferee->owner)->name:"");
			ast_debug(1, "Transferor channels (local/remote): %s and %s\n",
				xferor->owner->name, ast_bridged_channel(xferor->owner)?ast_bridged_channel(xferor->owner)->name:"");
		}
		if (ast_bridged_channel(xferor->owner)) {
			if (ast_bridged_channel(xferee->owner)) {
				ast_queue_control(xferee->owner, AST_CONTROL_UNHOLD);
			}
			if (xferor->owner->_state == AST_STATE_RING) {
				/* play ringing inband */
				if ((ts = ast_get_indication_tone(xferor->owner->zone, "ring"))) {
					ast_playtones_start(xferor->owner, 0, ts->data, 1);
					ts = ast_tone_zone_sound_unref(ts);
				}
			}
			if (skinnydebug)
				ast_debug(1, "Transfer Masquerading %s to %s\n",
					xferee->owner->name, ast_bridged_channel(xferor->owner)?ast_bridged_channel(xferor->owner)->name:"");
			if (ast_channel_masquerade(xferee->owner, ast_bridged_channel(xferor->owner))) {
				ast_log(LOG_WARNING, "Unable to masquerade %s as %s\n",
					ast_bridged_channel(xferor->owner)->name, xferee->owner->name);
				return -1;
			}
		} else if (ast_bridged_channel(xferee->owner)) {
			ast_queue_control(xferee->owner, AST_CONTROL_UNHOLD);
			if (xferor->owner->_state == AST_STATE_RING) {
				/* play ringing inband */
				if ((ts = ast_get_indication_tone(xferor->owner->zone, "ring"))) {
					ast_playtones_start(xferor->owner, 0, ts->data, 1);
					ts = ast_tone_zone_sound_unref(ts);
				}
			}
			if (skinnydebug)
				ast_debug(1, "Transfer Masquerading %s to %s\n",
					xferor->owner->name, ast_bridged_channel(xferee->owner)?ast_bridged_channel(xferee->owner)->name:"");
			if (ast_channel_masquerade(xferor->owner, ast_bridged_channel(xferee->owner))) {
				ast_log(LOG_WARNING, "Unable to masquerade %s as %s\n",
					ast_bridged_channel(xferee->owner)->name, xferor->owner->name);
				return -1;
			}
			return 0;
		} else {
			if (option_debug)
				ast_log(LOG_DEBUG, "Neither %s nor %s are in a bridge, nothing to transfer\n",
					xferor->owner->name, xferee->owner->name);
		}
	}
	return 0;
}

static int skinny_indicate(struct ast_channel *ast, int ind, const void *data, size_t datalen)
{
	struct skinny_subchannel *sub = ast->tech_pvt;
	struct skinny_line *l = sub->parent;
	struct skinny_device *d = l->device;
	struct skinnysession *s = d->session;

	if (!s) {
		ast_log(LOG_NOTICE, "Asked to indicate '%s' condition on channel %s, but session does not exist.\n", control2str(ind), ast->name);
		return -1;
	}

	if (skinnydebug)
		ast_verb(3, "Asked to indicate '%s' condition on channel %s\n", control2str(ind), ast->name);
	switch(ind) {
	case AST_CONTROL_RINGING:
		if (sub->blindxfer) {
			if (skinnydebug)
				ast_debug(1, "Channel %s set up for Blind Xfer, so Xfer rather than ring device\n", ast->name);
			skinny_transfer(sub);
			break;
		}
		if (ast->_state != AST_STATE_UP) {
			if (!sub->progress) {
				if (!d->earlyrtp) {
					transmit_start_tone(d, SKINNY_ALERT, l->instance, sub->callid);
				}
				transmit_callstate(d, sub->parent->instance, sub->callid, SKINNY_RINGOUT);
				transmit_dialednumber(d, l->lastnumberdialed, l->instance, sub->callid);
				transmit_displaypromptstatus(d, "Ring Out", 0, l->instance, sub->callid);
				transmit_callinfo(d,
					S_COR(ast->caller.id.name.valid, ast->caller.id.name.str, ""),
					S_COR(ast->caller.id.number.valid, ast->caller.id.number.str, ""),
					S_COR(ast->connected.id.name.valid, ast->connected.id.name.str, l->lastnumberdialed),
					S_COR(ast->connected.id.number.valid, ast->connected.id.number.str, l->lastnumberdialed),
					l->instance, sub->callid, 2); /* 2 = outgoing from phone */
				sub->ringing = 1;
				if (!d->earlyrtp) {
					break;
				}
			}
		}
		return -1; /* Tell asterisk to provide inband signalling */
	case AST_CONTROL_BUSY:
		if (ast->_state != AST_STATE_UP) {
			if (!d->earlyrtp) {
				transmit_start_tone(d, SKINNY_BUSYTONE, l->instance, sub->callid);
			}
			transmit_callstate(d, sub->parent->instance, sub->callid, SKINNY_BUSY);
			sub->alreadygone = 1;
			ast_softhangup_nolock(ast, AST_SOFTHANGUP_DEV);
			if (!d->earlyrtp) {
				break;
			}
		}
		return -1; /* Tell asterisk to provide inband signalling */
	case AST_CONTROL_CONGESTION:
		if (ast->_state != AST_STATE_UP) {
			if (!d->earlyrtp) {
				transmit_start_tone(d, SKINNY_REORDER, l->instance, sub->callid);
			}
			transmit_callstate(d, sub->parent->instance, sub->callid, SKINNY_CONGESTION);
			sub->alreadygone = 1;
			ast_softhangup_nolock(ast, AST_SOFTHANGUP_DEV);
			if (!d->earlyrtp) {
				break;
			}
		}
		return -1; /* Tell asterisk to provide inband signalling */
	case AST_CONTROL_PROGRESS:
		if ((ast->_state != AST_STATE_UP) && !sub->progress && !sub->outgoing) {
			if (!d->earlyrtp) {
				transmit_start_tone(d, SKINNY_ALERT, l->instance, sub->callid);
			}
			transmit_callstate(d, sub->parent->instance, sub->callid, SKINNY_PROGRESS);
			transmit_displaypromptstatus(d, "Call Progress", 0, l->instance, sub->callid);
			transmit_callinfo(d,
				S_COR(ast->caller.id.name.valid, ast->caller.id.name.str, ""),
				S_COR(ast->caller.id.number.valid, ast->caller.id.number.str, ""),
				S_COR(ast->connected.id.name.valid, ast->connected.id.name.str, l->lastnumberdialed),
				S_COR(ast->connected.id.number.valid, ast->connected.id.number.str, l->lastnumberdialed),
				l->instance, sub->callid, 2); /* 2 = outgoing from phone */
			sub->progress = 1;
			if (!d->earlyrtp) {
				break;
			}
		}
		return -1; /* Tell asterisk to provide inband signalling */
	case -1:  /* STOP_TONE */
		transmit_stop_tone(d, l->instance, sub->callid);
		break;
	case AST_CONTROL_HOLD:
		ast_moh_start(ast, data, l->mohinterpret);
		break;
	case AST_CONTROL_UNHOLD:
		ast_moh_stop(ast);
		break;
	case AST_CONTROL_PROCEEDING:
		break;
	case AST_CONTROL_SRCUPDATE:
		ast_rtp_instance_update_source(sub->rtp);
		break;
	case AST_CONTROL_SRCCHANGE:
		ast_rtp_instance_change_source(sub->rtp);
		break;
	case AST_CONTROL_CONNECTED_LINE:
		update_connectedline(sub, data, datalen);
		break;
	default:
		ast_log(LOG_WARNING, "Don't know how to indicate condition %d\n", ind);
		return -1; /* Tell asterisk to provide inband signalling */
	}
	return 0;
}

static struct ast_channel *skinny_new(struct skinny_line *l, int state, const char *linkedid)
{
	struct ast_channel *tmp;
	struct skinny_subchannel *sub;
	struct skinny_device *d = l->device;
	struct ast_variable *v = NULL;
	int fmt;

	if (!l->device) {
		ast_log(LOG_WARNING, "Device for line %s is not registered.\n", l->name);
		return NULL;
	}

	tmp = ast_channel_alloc(1, state, l->cid_num, l->cid_name, l->accountcode, l->exten, l->context, linkedid, l->amaflags, "Skinny/%s@%s-%d", l->name, d->name, callnums);
	if (!tmp) {
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
		return NULL;
	} else {
		sub = ast_calloc(1, sizeof(*sub));
		if (!sub) {
			ast_log(LOG_WARNING, "Unable to allocate Skinny subchannel\n");
			return NULL;
		} else {
			ast_mutex_init(&sub->lock);

			sub->owner = tmp;
			sub->callid = callnums++;
			d->lastlineinstance = l->instance;
			d->lastcallreference = sub->callid;
			sub->cxmode = SKINNY_CX_INACTIVE;
			sub->nat = l->nat;
			sub->parent = l;
			sub->onhold = 0;
			sub->blindxfer = 0;
			sub->xferor = 0;
			sub->related = NULL;

			AST_LIST_INSERT_HEAD(&l->sub, sub, list);
			//l->activesub = sub;
		}
		tmp->tech = &skinny_tech;
		tmp->tech_pvt = sub;
		tmp->nativeformats = l->capability;
		if (!tmp->nativeformats)
			// Should throw an error
			tmp->nativeformats = default_capability;
		fmt = ast_best_codec(tmp->nativeformats);
		if (skinnydebug) {
			char buf[256];
			ast_verb(1, "skinny_new: tmp->nativeformats=%s fmt=%s\n",
				ast_getformatname_multiple(buf, sizeof(buf), tmp->nativeformats),
				ast_getformatname(fmt));
		}
		if (sub->rtp) {
			ast_channel_set_fd(tmp, 0, ast_rtp_instance_fd(sub->rtp, 0));
		}
		if (state == AST_STATE_RING) {
			tmp->rings = 1;
		}
		tmp->writeformat = fmt;
		tmp->rawwriteformat = fmt;
		tmp->readformat = fmt;
		tmp->rawreadformat = fmt;
		if (!ast_strlen_zero(l->language))
			ast_string_field_set(tmp, language, l->language);
		if (!ast_strlen_zero(l->accountcode))
			ast_string_field_set(tmp, accountcode, l->accountcode);
		if (!ast_strlen_zero(l->parkinglot))
			ast_string_field_set(tmp, parkinglot, l->parkinglot);
		if (l->amaflags)
			tmp->amaflags = l->amaflags;

		ast_module_ref(ast_module_info->self);
		tmp->callgroup = l->callgroup;
		tmp->pickupgroup = l->pickupgroup;

		/* XXX Need to figure out how to handle CFwdNoAnswer */
		if (l->cfwdtype & SKINNY_CFWD_ALL) {
			ast_string_field_set(tmp, call_forward, l->call_forward_all);
		} else if (l->cfwdtype & SKINNY_CFWD_BUSY) {
			if (get_devicestate(l) != AST_DEVICE_NOT_INUSE) {
				ast_string_field_set(tmp, call_forward, l->call_forward_busy);
			}
		}

		ast_copy_string(tmp->context, l->context, sizeof(tmp->context));
		ast_copy_string(tmp->exten, l->exten, sizeof(tmp->exten));

		/* Don't use ast_set_callerid() here because it will
		 * generate a needless NewCallerID event */
		if (!ast_strlen_zero(l->cid_num)) {
			tmp->caller.ani.number.valid = 1;
			tmp->caller.ani.number.str = ast_strdup(l->cid_num);
		}

		tmp->priority = 1;
		tmp->adsicpe = AST_ADSI_UNAVAILABLE;

		if (sub->rtp)
			ast_jb_configure(tmp, &global_jbconf);

		/* Set channel variables for this call from configuration */
		for (v = l->chanvars ; v ; v = v->next)
			pbx_builtin_setvar_helper(tmp, v->name, v->value);

		if (state != AST_STATE_DOWN) {
			if (ast_pbx_start(tmp)) {
				ast_log(LOG_WARNING, "Unable to start PBX on %s\n", tmp->name);
				ast_hangup(tmp);
				tmp = NULL;
			}
		}
	}
	return tmp;
}

static int skinny_hold(struct skinny_subchannel *sub)
{
	struct skinny_line *l = sub->parent;
	struct skinny_device *d = l->device;

	/* Don't try to hold a channel that doesn't exist */
	if (!sub || !sub->owner)
		return 0;

	/* Channel needs to be put on hold */
	if (skinnydebug)
		ast_verb(1, "Putting on Hold(%d)\n", l->instance);

	ast_queue_control_data(sub->owner, AST_CONTROL_HOLD,
		S_OR(l->mohsuggest, NULL),
		!ast_strlen_zero(l->mohsuggest) ? strlen(l->mohsuggest) + 1 : 0);

	transmit_activatecallplane(d, l);
	transmit_closereceivechannel(d, sub);
	transmit_stopmediatransmission(d, sub);

	transmit_callstate(d, sub->parent->instance, sub->callid, SKINNY_HOLD);
	transmit_lamp_indication(d, STIMULUS_LINE, l->instance, SKINNY_LAMP_WINK);
	sub->onhold = 1;
	return 1;
}

static int skinny_unhold(struct skinny_subchannel *sub)
{
	struct skinny_line *l = sub->parent;
	struct skinny_device *d = l->device;

	/* Don't try to unhold a channel that doesn't exist */
	if (!sub || !sub->owner)
		return 0;

	/* Channel is on hold, so we will unhold */
	if (skinnydebug)
		ast_verb(1, "Taking off Hold(%d)\n", l->instance);

	ast_queue_control(sub->owner, AST_CONTROL_UNHOLD);

	transmit_activatecallplane(d, l);

	transmit_connect(d, sub);
	transmit_callstate(d, sub->parent->instance, sub->callid, SKINNY_CONNECTED);
	transmit_lamp_indication(d, STIMULUS_LINE, l->instance, SKINNY_LAMP_ON);
	l->hookstate = SKINNY_OFFHOOK;
	sub->onhold = 0;
	return 1;
}

static int handle_hold_button(struct skinny_subchannel *sub)
{
	if (!sub)
		return -1;
	if (sub->related) {
		skinny_hold(sub);
		skinny_unhold(sub->related);
		sub->parent->activesub = sub->related;
	} else {
		if (sub->onhold) {
			skinny_unhold(sub);
			transmit_selectsoftkeys(sub->parent->device, sub->parent->instance, sub->callid, KEYDEF_CONNECTED);
		} else {
			skinny_hold(sub);
			transmit_selectsoftkeys(sub->parent->device, sub->parent->instance, sub->callid, KEYDEF_ONHOLD);
		}
	}
	return 1;
}

static int handle_transfer_button(struct skinny_subchannel *sub)
{
	struct skinny_line *l;
	struct skinny_device *d;
	struct skinny_subchannel *newsub;
	struct ast_channel *c;
	pthread_t t;

	if (!sub) {
		ast_verbose("Transfer: No subchannel to transfer\n");
		return -1;
	}

	l = sub->parent;
	d = l->device;

	if (!sub->related) {
		/* Another sub has not been created so this must be first XFER press */
		if (!sub->onhold) {
			skinny_hold(sub);
		}
		c = skinny_new(l, AST_STATE_DOWN, NULL);
		if (c) {
			newsub = c->tech_pvt;
			/* point the sub and newsub at each other so we know they are related */
			newsub->related = sub;
			sub->related = newsub;
			newsub->xferor = 1;
			l->activesub = newsub;
			transmit_callstate(d, l->instance, sub->callid, SKINNY_OFFHOOK);
			transmit_activatecallplane(d, l);
			transmit_clear_display_message(d, l->instance, newsub->callid);
			transmit_start_tone(d, SKINNY_DIALTONE, l->instance, newsub->callid);
			transmit_selectsoftkeys(d, l->instance, newsub->callid, KEYDEF_OFFHOOKWITHFEAT);
			/* start the switch thread */
			if (ast_pthread_create(&t, NULL, skinny_ss, c)) {
				ast_log(LOG_WARNING, "Unable to create switch thread: %s\n", strerror(errno));
				ast_hangup(c);
			}
		} else {
			ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
		}
	} else {
		/* We already have a related sub so we can either complete XFER or go into BLINDXFER (or cancel BLINDXFER */
		if (sub->blindxfer) {
			/* toggle blindxfer off */
			sub->blindxfer = 0;
			sub->related->blindxfer = 0;
			/* we really need some indications */
		} else {
			/* We were doing attended transfer */
			if (sub->owner->_state == AST_STATE_DOWN || sub->related->owner->_state == AST_STATE_DOWN) {
				/* one of the subs so we cant transfer yet, toggle blindxfer on */
				sub->blindxfer = 1;
				sub->related->blindxfer = 1;
			} else {
				/* big assumption we have two channels, lets transfer */
				skinny_transfer(sub);
			}
		}
	}
	return 0;
}

static int handle_keep_alive_message(struct skinny_req *req, struct skinnysession *s)
{
	if (!(req = req_alloc(0, KEEP_ALIVE_ACK_MESSAGE)))
		return -1;

	transmit_response(s->device, req);
	return 1;
}

static int handle_register_message(struct skinny_req *req, struct skinnysession *s)
{
	struct skinny_device *d = NULL;
	char name[16];
	int res;

	memcpy(&name, req->data.reg.name, sizeof(name));

	res = skinny_register(req, s);
	if (!res) {
		ast_log(LOG_ERROR, "Rejecting Device %s: Device not found\n", name);
		if (!(req = req_alloc(sizeof(struct register_rej_message), REGISTER_REJ_MESSAGE)))
			return -1;

		snprintf(req->data.regrej.errMsg, sizeof(req->data.regrej.errMsg), "No Authority: %s", name);

		/* transmit_respons in line as we don't have a valid d */
		ast_mutex_lock(&s->lock);

		if (letohl(req->len) > SKINNY_MAX_PACKET || letohl(req->len) < 0) {
			ast_log(LOG_WARNING, "transmit_response: the length (%d) of the request is out of bounds (%d) \n",  letohl(req->len), SKINNY_MAX_PACKET);
			ast_mutex_unlock(&s->lock);
			return -1;
		}

		memset(s->outbuf, 0, sizeof(s->outbuf));
		memcpy(s->outbuf, req, skinny_header_size);
		memcpy(s->outbuf+skinny_header_size, &req->data, letohl(req->len));

		res = write(s->fd, s->outbuf, letohl(req->len)+8);

		if (res != letohl(req->len)+8) {
			ast_log(LOG_WARNING, "Transmit: write only sent %d out of %d bytes: %s\n", res, letohl(req->len)+8, strerror(errno));
		}
	
		ast_mutex_unlock(&s->lock);

		return 0;
	}
	ast_atomic_fetchadd_int(&unauth_sessions, -1);

	ast_verb(3, "Device '%s' successfully registered\n", name);

	d = s->device;
	
	if (!(req = req_alloc(sizeof(struct register_ack_message), REGISTER_ACK_MESSAGE)))
		return -1;

	req->data.regack.res[0] = '0';
	req->data.regack.res[1] = '\0';
	req->data.regack.keepAlive = htolel(keep_alive);
	memcpy(req->data.regack.dateTemplate, date_format, sizeof(req->data.regack.dateTemplate));
	req->data.regack.res2[0] = '0';
	req->data.regack.res2[1] = '\0';
	req->data.regack.secondaryKeepAlive = htolel(keep_alive);
	transmit_response(d, req);
	if (skinnydebug)
		ast_verb(1, "Requesting capabilities\n");

	if (!(req = req_alloc(0, CAPABILITIES_REQ_MESSAGE)))
		return -1;

	transmit_response(d, req);

	return res;
}

static int handle_callforward_button(struct skinny_subchannel *sub, int cfwdtype)
{
	struct skinny_line *l = sub->parent;
	struct skinny_device *d = l->device;
	struct ast_channel *c = sub->owner;
	pthread_t t;

	if (l->hookstate == SKINNY_ONHOOK) {
		l->hookstate = SKINNY_OFFHOOK;
		transmit_speaker_mode(d, SKINNY_SPEAKERON);
		transmit_callstate(d, l->instance, sub->callid, SKINNY_OFFHOOK);
		transmit_activatecallplane(d, l);
	}
	transmit_clear_display_message(d, l->instance, sub->callid);

	if (l->cfwdtype & cfwdtype) {
		set_callforwards(l, NULL, cfwdtype);
		ast_safe_sleep(c, 500);
		transmit_speaker_mode(d, SKINNY_SPEAKEROFF);
		transmit_closereceivechannel(d, sub);
		transmit_stopmediatransmission(d, sub);
		transmit_speaker_mode(d, SKINNY_SPEAKEROFF);
		transmit_clearpromptmessage(d, l->instance, sub->callid);
		transmit_callstate(d, l->instance, sub->callid, SKINNY_ONHOOK);
		transmit_selectsoftkeys(d, 0, 0, KEYDEF_ONHOOK);
		transmit_activatecallplane(d, l);
		transmit_displaynotify(d, "CFwd disabled", 10);
		if (sub->owner && sub->owner->_state != AST_STATE_UP) {
			ast_indicate(c, -1);
			ast_hangup(c);
		}
		transmit_cfwdstate(d, l);
	} else {
		l->getforward = cfwdtype;
		transmit_start_tone(d, SKINNY_DIALTONE, l->instance, sub->callid);
		transmit_selectsoftkeys(d, l->instance, sub->callid, KEYDEF_RINGOUT);
		if (ast_pthread_create(&t, NULL, skinny_ss, c)) {
			ast_log(LOG_WARNING, "Unable to create switch thread: %s\n", strerror(errno));
			ast_hangup(c);
		}
	}
	return 0;
}
static int handle_ip_port_message(struct skinny_req *req, struct skinnysession *s)
{
	/* no response necessary */
	return 1;
}

static int handle_keypad_button_message(struct skinny_req *req, struct skinnysession *s)
{
	struct skinny_subchannel *sub = NULL;
	struct skinny_line *l;
	struct skinny_device *d = s->device;
	struct ast_frame f = { 0, };
	char dgt;
	int digit;
	int lineInstance;
	int callReference;

	digit = letohl(req->data.keypad.button);
	lineInstance = letohl(req->data.keypad.lineInstance);
	callReference = letohl(req->data.keypad.callReference);

	if (digit == 14) {
		dgt = '*';
	} else if (digit == 15) {
		dgt = '#';
	} else if (digit >= 0 && digit <= 9) {
		dgt = '0' + digit;
	} else {
		/* digit=10-13 (A,B,C,D ?), or
		 * digit is bad value
		 *
		 * probably should not end up here, but set
		 * value for backward compatibility, and log
		 * a warning.
		 */
		dgt = '0' + digit;
		ast_log(LOG_WARNING, "Unsupported digit %d\n", digit);
	}

	f.subclass.integer = dgt;

	f.src = "skinny";

	if (lineInstance && callReference)
		sub = find_subchannel_by_instance_reference(d, lineInstance, callReference);
	else
		sub = d->activeline->activesub;
		//sub = find_subchannel_by_instance_reference(d, d->lastlineinstance, d->lastcallreference);

	if (!sub)
		return 0;

	l = sub->parent;
	if (sub->owner) {
		if (sub->owner->_state == 0) {
			f.frametype = AST_FRAME_DTMF_BEGIN;
			ast_queue_frame(sub->owner, &f);
		}
		/* XXX MUST queue this frame to all lines in threeway call if threeway call is active */
		f.frametype = AST_FRAME_DTMF_END;
		ast_queue_frame(sub->owner, &f);
		/* XXX This seriously needs to be fixed */
		if (AST_LIST_NEXT(sub, list) && AST_LIST_NEXT(sub, list)->owner) {
			if (sub->owner->_state == 0) {
				f.frametype = AST_FRAME_DTMF_BEGIN;
				ast_queue_frame(AST_LIST_NEXT(sub, list)->owner, &f);
			}
			f.frametype = AST_FRAME_DTMF_END;
			ast_queue_frame(AST_LIST_NEXT(sub, list)->owner, &f);
		}
	} else {
		if (skinnydebug)
			ast_verb(1, "No owner: %s\n", l->name);
	}
	return 1;
}

static int handle_stimulus_message(struct skinny_req *req, struct skinnysession *s)
{
	struct skinny_device *d = s->device;
	struct skinny_line *l;
	struct skinny_subchannel *sub;
	/*struct skinny_speeddial *sd;*/
	struct ast_channel *c;
	pthread_t t;
	int event;
	int instance;
	int callreference;
	/*int res = 0;*/

	event = letohl(req->data.stimulus.stimulus);
	instance = letohl(req->data.stimulus.stimulusInstance);
	callreference = letohl(req->data.stimulus.callreference); 
	if (skinnydebug)
		ast_verb(1, "callreference in handle_stimulus_message is '%d'\n", callreference);

	/*  Note that this call should be using the passed in instance and callreference */
	sub = find_subchannel_by_instance_reference(d, d->lastlineinstance, d->lastcallreference);

	if (!sub) {
		l = find_line_by_instance(d, d->lastlineinstance);
		if (!l) {
			return 0;
		}
		sub = l->activesub;
	} else {
		l = sub->parent;
	}

	switch(event) {
	case STIMULUS_REDIAL:
		if (skinnydebug)
			ast_verb(1, "Received Stimulus: Redial(%d/%d)\n", instance, callreference);

		if (ast_strlen_zero(l->lastnumberdialed)) {
			ast_log(LOG_WARNING, "Attempted redial, but no previously dialed number found.\n");
			l->hookstate = SKINNY_ONHOOK;
			transmit_speaker_mode(d, SKINNY_SPEAKEROFF);
			transmit_closereceivechannel(d, sub);
			transmit_stopmediatransmission(d, sub);
			transmit_speaker_mode(d, SKINNY_SPEAKEROFF);
			transmit_clearpromptmessage(d, l->instance, sub->callid);
			transmit_callstate(d, l->instance, sub->callid, SKINNY_ONHOOK);
			transmit_selectsoftkeys(d, 0, 0, KEYDEF_ONHOOK);
			transmit_activatecallplane(d, l);
			break;
		}

		c = skinny_new(l, AST_STATE_DOWN, NULL);
		if (!c) {
			ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
		} else {
			sub = c->tech_pvt;
			l = sub->parent;
			l->activesub = sub;
			if (l->hookstate == SKINNY_ONHOOK) {
				l->hookstate = SKINNY_OFFHOOK;
				transmit_callstate(d, l->instance, sub->callid, SKINNY_OFFHOOK);
				transmit_activatecallplane(d, l);
			}
			transmit_clear_display_message(d, l->instance, sub->callid);
			transmit_start_tone(d, SKINNY_DIALTONE, l->instance, sub->callid);
			transmit_selectsoftkeys(d, l->instance, sub->callid, KEYDEF_RINGOUT);

			if (!ast_ignore_pattern(c->context, l->lastnumberdialed)) {
				transmit_stop_tone(d, l->instance, sub->callid);
			}
			ast_copy_string(c->exten, l->lastnumberdialed, sizeof(c->exten));
			if (ast_pthread_create(&t, NULL, skinny_newcall, c)) {
				ast_log(LOG_WARNING, "Unable to create new call thread: %s\n", strerror(errno));
				ast_hangup(c);
			}
		}
		break;
	case STIMULUS_SPEEDDIAL:
	    {
		struct skinny_speeddial *sd;

		if (skinnydebug)
			ast_verb(1, "Received Stimulus: SpeedDial(%d/%d)\n", instance, callreference);
		if (!(sd = find_speeddial_by_instance(d, instance, 0))) {
			return 0;
		}

		if (!sub || !sub->owner)
			c = skinny_new(l, AST_STATE_DOWN, NULL);
		else
			c = sub->owner;

		if (!c) {
			ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
		} else {
			sub = c->tech_pvt;
			l = sub->parent;
			l->activesub = sub;
			if (l->hookstate == SKINNY_ONHOOK) {
				l->hookstate = SKINNY_OFFHOOK;
				transmit_speaker_mode(d, SKINNY_SPEAKERON);
				transmit_callstate(d, l->instance, sub->callid, SKINNY_OFFHOOK);
				transmit_activatecallplane(d, l);
			}
			transmit_clear_display_message(d, l->instance, sub->callid);
			transmit_start_tone(d, SKINNY_DIALTONE, l->instance, sub->callid);
			transmit_selectsoftkeys(d, l->instance, sub->callid, KEYDEF_RINGOUT);

			if (!ast_ignore_pattern(c->context, sd->exten)) {
				transmit_stop_tone(d, l->instance, sub->callid);
			}
			if (ast_exists_extension(c, c->context, sd->exten, 1, l->cid_num)) {
				ast_copy_string(c->exten, sd->exten, sizeof(c->exten));
				ast_copy_string(l->lastnumberdialed, sd->exten, sizeof(l->lastnumberdialed));

				if (ast_pthread_create(&t, NULL, skinny_newcall, c)) {
					ast_log(LOG_WARNING, "Unable to create new call thread: %s\n", strerror(errno));
					ast_hangup(c);
				}
				break;
			}
		}
	    }
		break;
	case STIMULUS_HOLD:
		if (skinnydebug)
			ast_verb(1, "Received Stimulus: Hold(%d/%d)\n", instance, callreference);
		handle_hold_button(sub);
		break;
	case STIMULUS_TRANSFER:
		if (skinnydebug)
			ast_verb(1, "Received Stimulus: Transfer(%d/%d)\n", instance, callreference);
		if (l->transfer)
			handle_transfer_button(sub);
		else
			transmit_displaynotify(d, "Transfer disabled", 10);
		break;
	case STIMULUS_CONFERENCE:
		if (skinnydebug)
			ast_verb(1, "Received Stimulus: Conference(%d/%d)\n", instance, callreference);
		/* XXX determine the best way to pull off a conference.  Meetme? */
		break;
	case STIMULUS_VOICEMAIL:
		if (skinnydebug)
			ast_verb(1, "Received Stimulus: Voicemail(%d/%d)\n", instance, callreference);

		if (!sub || !sub->owner) {
			c = skinny_new(l, AST_STATE_DOWN, NULL);
		} else {
			c = sub->owner;
		}
		if (!c) {
			ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
		} else {
			sub = c->tech_pvt;
			l = sub->parent;
			l->activesub = sub;

			if (ast_strlen_zero(l->vmexten))  /* Exit the call if no VM pilot */
				break;

			if (l->hookstate == SKINNY_ONHOOK){
				l->hookstate = SKINNY_OFFHOOK;
				transmit_speaker_mode(d, SKINNY_SPEAKERON);
				transmit_callstate(d, l->instance, sub->callid, SKINNY_OFFHOOK);
				transmit_activatecallplane(d, l);
			}

			transmit_clear_display_message(d, l->instance, sub->callid);
			transmit_start_tone(d, SKINNY_DIALTONE, l->instance, sub->callid);
			transmit_selectsoftkeys(d, l->instance, sub->callid, KEYDEF_RINGOUT);

			if (!ast_ignore_pattern(c->context, l->vmexten)) {
				transmit_stop_tone(d, l->instance, sub->callid);
			}

			if (ast_exists_extension(c, c->context, l->vmexten, 1, l->cid_num)) {
				ast_copy_string(c->exten, l->vmexten, sizeof(c->exten));
				ast_copy_string(l->lastnumberdialed, l->vmexten, sizeof(l->lastnumberdialed));
				if (ast_pthread_create(&t, NULL, skinny_newcall, c)) {
					ast_log(LOG_WARNING, "Unable to create new call thread: %s\n", strerror(errno));
					ast_hangup(c);
				}
				break;
			}
		}
		break;
	case STIMULUS_CALLPARK:
		{
		int extout;
		char message[32];

		if (skinnydebug)
			ast_verb(1, "Received Stimulus: Park Call(%d/%d)\n", instance, callreference);

		if ((sub && sub->owner) && (sub->owner->_state ==  AST_STATE_UP)){
			c = sub->owner;
			if (ast_bridged_channel(c)) {
				if (!ast_masq_park_call(ast_bridged_channel(c), c, 0, &extout)) {
					snprintf(message, sizeof(message), "Call Parked at: %d", extout);
					transmit_displaynotify(d, message, 10);
				} else {
					transmit_displaynotify(d, "Call Park failed", 10);
				}
			} else {
				transmit_displaynotify(d, "Call Park not available", 10);
			}
		} else {
			transmit_displaynotify(d, "Call Park not available", 10);
		}
		break;
		}
	case STIMULUS_DND:
		if (skinnydebug)
			ast_verb(1, "Received Stimulus: DND (%d/%d)\n", instance, callreference);

		/* Do not disturb */
		if (l->dnd != 0){
			ast_verb(3, "Disabling DND on %s@%s\n", l->name, d->name);
			l->dnd = 0;
			transmit_lamp_indication(d, STIMULUS_DND, 1, SKINNY_LAMP_ON);
			transmit_displaynotify(d, "DnD disabled", 10);
		} else {
			ast_verb(3, "Enabling DND on %s@%s\n", l->name, d->name);
			l->dnd = 1;
			transmit_lamp_indication(d, STIMULUS_DND, 1, SKINNY_LAMP_OFF);
			transmit_displaynotify(d, "DnD enabled", 10);
		}
		break;
	case STIMULUS_FORWARDALL:
		if (skinnydebug)
			ast_verb(1, "Received Stimulus: Forward All(%d/%d)\n", instance, callreference);

		if (!sub || !sub->owner) {
			c = skinny_new(l, AST_STATE_DOWN, NULL);
		} else {
			c = sub->owner;
		}

		if (!c) {
			ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
		} else {
			sub = c->tech_pvt;
			handle_callforward_button(sub, SKINNY_CFWD_ALL);
		}
		break;
	case STIMULUS_FORWARDBUSY:
		if (skinnydebug)
			ast_verb(1, "Received Stimulus: Forward Busy (%d/%d)\n", instance, callreference);

		if (!sub || !sub->owner) {
			c = skinny_new(l, AST_STATE_DOWN, NULL);
		} else {
			c = sub->owner;
		}

		if (!c) {
			ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
		} else {
			sub = c->tech_pvt;
			handle_callforward_button(sub, SKINNY_CFWD_BUSY);
		}
		break;
	case STIMULUS_FORWARDNOANSWER:
		if (skinnydebug)
			ast_verb(1, "Received Stimulus: Forward No Answer (%d/%d)\n", instance, callreference);

#if 0 /* Not sure how to handle this yet */
		if (!sub || !sub->owner) {
			c = skinny_new(l, AST_STATE_DOWN, NULL);
		} else {
			c = sub->owner;
		}

		if (!c) {
			ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
		} else {
			sub = c->tech_pvt;
			handle_callforward_button(sub, SKINNY_CFWD_NOANSWER);
		}
#endif
		break;
	case STIMULUS_DISPLAY:
		/* Not sure what this is */
		if (skinnydebug)
			ast_verb(1, "Received Stimulus: Display(%d/%d)\n", instance, callreference);
		break;
	case STIMULUS_LINE:
		if (skinnydebug)
			ast_verb(1, "Received Stimulus: Line(%d/%d)\n", instance, callreference);

		l = find_line_by_instance(d, instance);

		if (!l) {
			return 0;
		}

		d->activeline = l;

		/* turn the speaker on */
		transmit_speaker_mode(d, SKINNY_SPEAKERON);
		transmit_ringer_mode(d, SKINNY_RING_OFF);
		transmit_lamp_indication(d, STIMULUS_LINE, l->instance, SKINNY_LAMP_ON);

		l->hookstate = SKINNY_OFFHOOK;

		if (sub && sub->outgoing) {
			/* We're answering a ringing call */
			ast_queue_control(sub->owner, AST_CONTROL_ANSWER);
			transmit_callstate(d, l->instance, sub->callid, SKINNY_OFFHOOK);
			transmit_activatecallplane(d, l);
			transmit_stop_tone(d, l->instance, sub->callid);
			transmit_callstate(d, sub->parent->instance, sub->callid, SKINNY_CONNECTED);
			transmit_displaypromptstatus(d, "Connected", 0, l->instance, sub->callid);
			transmit_selectsoftkeys(d, l->instance, sub->callid, KEYDEF_CONNECTED);
			start_rtp(sub);
			ast_setstate(sub->owner, AST_STATE_UP);
		} else {
			if (sub && sub->owner) {
				ast_debug(1, "Current subchannel [%s] already has owner\n", sub->owner->name);
			} else {
				c = skinny_new(l, AST_STATE_DOWN, NULL);
				if (c) {
					sub = c->tech_pvt;
					l->activesub = sub;
					transmit_callstate(d, l->instance, sub->callid, SKINNY_OFFHOOK);
					transmit_activatecallplane(d, l);
					transmit_clear_display_message(d, l->instance, sub->callid);
					transmit_start_tone(d, SKINNY_DIALTONE, l->instance, sub->callid);
					transmit_selectsoftkeys(d, l->instance, sub->callid, KEYDEF_OFFHOOK);

					/* start the switch thread */
					if (ast_pthread_create(&t, NULL, skinny_ss, c)) {
						ast_log(LOG_WARNING, "Unable to create switch thread: %s\n", strerror(errno));
						ast_hangup(c);
					}
				} else {
					ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
				}
			}
		}
		break;
	default:
		if (skinnydebug)
			ast_verb(1, "RECEIVED UNKNOWN STIMULUS:  %d(%d/%d)\n", event, instance, callreference);
		break;
	}
	ast_devstate_changed(AST_DEVICE_UNKNOWN, "Skinny/%s@%s", l->name, d->name);

	return 1;
}

static int handle_offhook_message(struct skinny_req *req, struct skinnysession *s)
{
	struct skinny_device *d = s->device;
	struct skinny_line *l;
	struct skinny_subchannel *sub;
	struct ast_channel *c;
	struct skinny_line *tmp;
	pthread_t t;
	int instance;

	/* if any line on a device is offhook, than the device must be offhook, 
	   unless we have shared lines CCM seems that it would never get here, 
	   but asterisk does, so we may need to do more work.  Ugly, we should 
	   probably move hookstate from line to device, afterall, it's actually
	    a device that changes hookstates */

	AST_LIST_TRAVERSE(&d->lines, tmp, list) {
		if (tmp->hookstate == SKINNY_OFFHOOK) {
			ast_verbose(VERBOSE_PREFIX_3 "Got offhook message when device (%s@%s) already offhook\n", tmp->name, d->name);
			return 0;
		}
	}

	instance = letohl(req->data.offhook.instance);

	if (instance) {
		sub = find_subchannel_by_instance_reference(d, d->lastlineinstance, d->lastcallreference);
		if (!sub) {
			l = find_line_by_instance(d, d->lastlineinstance);
			if (!l) {
				return 0;
			}
		} else {
			l = sub->parent;
		}
	} else {
		l = d->activeline;
		sub = l->activesub;
	}

	/* Not ideal, but let's send updated time at onhook and offhook, as it clears the display */
	transmit_definetimedate(d);
	
	transmit_ringer_mode(d, SKINNY_RING_OFF);
	l->hookstate = SKINNY_OFFHOOK;

	ast_devstate_changed(AST_DEVICE_INUSE, "Skinny/%s@%s", l->name, d->name);

	if (sub && sub->onhold) {
		return 1;
	}

	transmit_lamp_indication(d, STIMULUS_LINE, l->instance, SKINNY_LAMP_ON);

	if (sub && sub->outgoing) {
		/* We're answering a ringing call */
		ast_queue_control(sub->owner, AST_CONTROL_ANSWER);
		transmit_callstate(d, l->instance, sub->callid, SKINNY_OFFHOOK);
		transmit_activatecallplane(d, l);
		transmit_stop_tone(d, l->instance, sub->callid);
		transmit_callstate(d, sub->parent->instance, sub->callid, SKINNY_CONNECTED);
		transmit_selectsoftkeys(d, l->instance, sub->callid, KEYDEF_CONNECTED);
		start_rtp(sub);
		ast_setstate(sub->owner, AST_STATE_UP);
	} else {
		if (sub && sub->owner) {
			ast_debug(1, "Current sub [%s] already has owner\n", sub->owner->name);
		} else {
			c = skinny_new(l, AST_STATE_DOWN, NULL);
			if (c) {
				sub = c->tech_pvt;
				l->activesub = sub;
				transmit_callstate(d, l->instance, sub->callid, SKINNY_OFFHOOK);
				transmit_activatecallplane(d, l);
				transmit_clear_display_message(d, l->instance, sub->callid);
				transmit_start_tone(d, SKINNY_DIALTONE, l->instance, sub->callid);
				transmit_selectsoftkeys(d, l->instance, sub->callid, KEYDEF_OFFHOOK);

				/* start the switch thread */
				if (ast_pthread_create(&t, NULL, skinny_ss, c)) {
					ast_log(LOG_WARNING, "Unable to create switch thread: %s\n", strerror(errno));
					ast_hangup(c);
				}
			} else {
				ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
			}
		}
	}
	return 1;
}

static int handle_onhook_message(struct skinny_req *req, struct skinnysession *s)
{
	struct skinny_device *d = s->device;
	struct skinny_line *l;
	struct skinny_subchannel *sub;
	int instance;
	int reference;
	int onlysub = 0;

	instance = letohl(req->data.onhook.instance);
	reference = letohl(req->data.onhook.reference);

	if (instance && reference) {
		sub = find_subchannel_by_instance_reference(d, instance, reference);
		if (!sub) {
			return 0;
		}
		l = sub->parent;
	} else {
		l = d->activeline;
		sub = l->activesub;
		if (!sub) {
			return 0;
		}
	}

	if (l->hookstate == SKINNY_ONHOOK) {
		/* Something else already put us back on hook */
		return 0;
	}

	ast_devstate_changed(AST_DEVICE_NOT_INUSE, "Skinny/%s@%s", l->name, d->name);

	if (sub->onhold) {
		return 0;
	}

	if (!AST_LIST_NEXT(sub, list)) {
		onlysub = 1;
	} else {
		AST_LIST_REMOVE(&l->sub, sub, list);
	}

	sub->cxmode = SKINNY_CX_RECVONLY;
	if (onlysub || sub->xferor){  /* is this the only call to this device? */
		l->hookstate = SKINNY_ONHOOK;
		if (skinnydebug)
			ast_debug(1, "Skinny %s@%s-%d went on hook\n", l->name, d->name, reference);
	}

	if (l->hookstate == SKINNY_ONHOOK) {
		transmit_closereceivechannel(d, sub);
		transmit_stopmediatransmission(d, sub);
		transmit_speaker_mode(d, SKINNY_SPEAKEROFF);
		transmit_clearpromptmessage(d, instance, sub->callid);
		transmit_callstate(d, l->instance, sub->callid, SKINNY_ONHOOK);
		transmit_selectsoftkeys(d, 0, 0, KEYDEF_ONHOOK);
		transmit_activatecallplane(d, l);
	} else if (l->hookstate == SKINNY_OFFHOOK) {
		transmit_callstate(d, l->instance, sub->callid, SKINNY_OFFHOOK);
		transmit_activatecallplane(d, l);
	} else {
		transmit_callstate(d, l->instance, sub->callid, l->hookstate);
	}

	if (l->transfer && sub->xferor && sub->owner->_state >= AST_STATE_RING) {
		/* We're allowed to transfer, we have two active calls and
		   we made at least one of the calls.  Let's try and transfer */
		handle_transfer_button(sub);
	} else {
		/* Hangup the current call */
		/* If there is another active call, skinny_hangup will ring the phone with the other call */
		if (sub->xferor && sub->related){
			sub->related->related = NULL;
			sub->related->blindxfer = 0;
		}

		if (sub->owner) {
			sub->alreadygone = 1;
			ast_queue_hangup(sub->owner);
		} else {
			ast_log(LOG_WARNING, "Skinny(%s@%s-%d) channel already destroyed\n",
				l->name, d->name, sub->callid);
		}
		/* Not ideal, but let's send updated time at onhook and offhook, as it clears the display */
		transmit_definetimedate(d);
	}
	return 1;
}

static int handle_capabilities_res_message(struct skinny_req *req, struct skinnysession *s)
{
	struct skinny_device *d = s->device;
	struct skinny_line *l;
	uint32_t count = 0;
	format_t codecs = 0;
	int i;
	char buf[256];

	count = letohl(req->data.caps.count);
	if (count > SKINNY_MAX_CAPABILITIES) {
		count = SKINNY_MAX_CAPABILITIES;
		ast_log(LOG_WARNING, "Received more capabilities than we can handle (%d).  Ignoring the rest.\n", SKINNY_MAX_CAPABILITIES);
	}

	for (i = 0; i < count; i++) {
		format_t acodec = 0;
		int scodec = 0;
		scodec = letohl(req->data.caps.caps[i].codec);
		acodec = codec_skinny2ast(scodec);
		if (skinnydebug)
			ast_verb(1, "Adding codec capability '%" PRId64 " (%d)'\n", acodec, scodec);
		codecs |= acodec;
	}

	d->capability = d->confcapability & codecs;
	ast_verb(0, "Device capability set to '%s'\n", ast_getformatname_multiple(buf, sizeof(buf), d->capability));
	AST_LIST_TRAVERSE(&d->lines, l, list) {
		ast_mutex_lock(&l->lock);
		l->capability = l->confcapability & d->capability;
		ast_mutex_unlock(&l->lock);
	}

	return 1;
}

static int handle_button_template_req_message(struct skinny_req *req, struct skinnysession *s)
{
	struct skinny_device *d = s->device;
	struct skinny_line *l;
	int i;

	struct skinny_speeddial *sd;
	struct button_definition_template btn[42];
	int lineInstance = 1;
	int speeddialInstance = 1;
	int buttonCount = 0;

	if (!(req = req_alloc(sizeof(struct button_template_res_message), BUTTON_TEMPLATE_RES_MESSAGE)))
		return -1;

	memset(&btn, 0, sizeof(btn));

	get_button_template(s, btn);

	for (i=0; i<42; i++) {
		int btnSet = 0;
		switch (btn[i].buttonDefinition) {
			case BT_CUST_LINE:
				/* assume failure */
				req->data.buttontemplate.definition[i].buttonDefinition = BT_NONE;
				req->data.buttontemplate.definition[i].instanceNumber = 0;

				AST_LIST_TRAVERSE(&d->lines, l, list) {
					if (l->instance == lineInstance) {
						ast_verb(0, "Adding button: %d, %d\n", BT_LINE, lineInstance);
						req->data.buttontemplate.definition[i].buttonDefinition = BT_LINE;
						req->data.buttontemplate.definition[i].instanceNumber = lineInstance;
						lineInstance++;
						buttonCount++;
						btnSet = 1;
						break;
					}
				}

				if (!btnSet) {
					AST_LIST_TRAVERSE(&d->speeddials, sd, list) {
						if (sd->isHint && sd->instance == lineInstance) {
							ast_verb(0, "Adding button: %d, %d\n", BT_LINE, lineInstance);
							req->data.buttontemplate.definition[i].buttonDefinition = BT_LINE;
							req->data.buttontemplate.definition[i].instanceNumber = lineInstance;
							lineInstance++;
							buttonCount++;
							btnSet = 1;
							break;
						}
					}
				}
				break;
			case BT_CUST_LINESPEEDDIAL:
				/* assume failure */
				req->data.buttontemplate.definition[i].buttonDefinition = BT_NONE;
				req->data.buttontemplate.definition[i].instanceNumber = 0;

				AST_LIST_TRAVERSE(&d->lines, l, list) {
					if (l->instance == lineInstance) {
						ast_verb(0, "Adding button: %d, %d\n", BT_LINE, lineInstance);
						req->data.buttontemplate.definition[i].buttonDefinition = BT_LINE;
						req->data.buttontemplate.definition[i].instanceNumber = lineInstance;
						lineInstance++;
						buttonCount++;
						btnSet = 1;
						break;
					}
				}

				if (!btnSet) {
					AST_LIST_TRAVERSE(&d->speeddials, sd, list) {
						if (sd->isHint && sd->instance == lineInstance) {
							ast_verb(0, "Adding button: %d, %d\n", BT_LINE, lineInstance);
							req->data.buttontemplate.definition[i].buttonDefinition = BT_LINE;
							req->data.buttontemplate.definition[i].instanceNumber = lineInstance;
							lineInstance++;
							buttonCount++;
							btnSet = 1;
							break;
						} else if (!sd->isHint && sd->instance == speeddialInstance) {
							ast_verb(0, "Adding button: %d, %d\n", BT_SPEEDDIAL, speeddialInstance);
							req->data.buttontemplate.definition[i].buttonDefinition = BT_SPEEDDIAL;
							req->data.buttontemplate.definition[i].instanceNumber = speeddialInstance;
							speeddialInstance++;
							buttonCount++;
							btnSet = 1;
							break;
						}
					}
				}
				break;
			case BT_LINE:
				req->data.buttontemplate.definition[i].buttonDefinition = htolel(BT_NONE);
				req->data.buttontemplate.definition[i].instanceNumber = htolel(0);

				AST_LIST_TRAVERSE(&d->lines, l, list) {
					if (l->instance == lineInstance) {
						ast_verb(0, "Adding button: %d, %d\n", BT_LINE, lineInstance);
						req->data.buttontemplate.definition[i].buttonDefinition = BT_LINE;
						req->data.buttontemplate.definition[i].instanceNumber = lineInstance;
						lineInstance++;
						buttonCount++;
						btnSet = 1;
						break;
					}
				}
				break;
			case BT_SPEEDDIAL:
				req->data.buttontemplate.definition[i].buttonDefinition = BT_NONE;
				req->data.buttontemplate.definition[i].instanceNumber = 0;

				AST_LIST_TRAVERSE(&d->speeddials, sd, list) {
					if (!sd->isHint && sd->instance == speeddialInstance) {
						ast_verb(0, "Adding button: %d, %d\n", BT_SPEEDDIAL, speeddialInstance);
						req->data.buttontemplate.definition[i].buttonDefinition = BT_SPEEDDIAL;
						req->data.buttontemplate.definition[i].instanceNumber = speeddialInstance - 1;
						speeddialInstance++;
						buttonCount++;
						btnSet = 1;
						break;
					}
				}
				break;
			case BT_NONE:
				break;
			default:
				ast_verb(0, "Adding button: %d, %d\n", btn[i].buttonDefinition, 0);
				req->data.buttontemplate.definition[i].buttonDefinition = htolel(btn[i].buttonDefinition);
				req->data.buttontemplate.definition[i].instanceNumber = 0;
				buttonCount++;
				btnSet = 1;
				break;
		}
	}

	req->data.buttontemplate.buttonOffset = 0;
	req->data.buttontemplate.buttonCount = htolel(buttonCount);
	req->data.buttontemplate.totalButtonCount = htolel(buttonCount);

	if (skinnydebug)
		ast_verb(1, "Sending %d template to %s\n",
					d->type,
					d->name);
	transmit_response(d, req);
	return 1;
}

static int handle_open_receive_channel_ack_message(struct skinny_req *req, struct skinnysession *s)
{
	struct skinny_device *d = s->device;
	struct skinny_line *l;
	struct skinny_subchannel *sub;
	struct ast_format_list fmt;
	struct sockaddr_in sin = { 0, };
	struct sockaddr_in us = { 0, };
	struct ast_sockaddr sin_tmp;
	struct ast_sockaddr us_tmp;
	uint32_t addr;
	int port;
	int status;
	int passthruid;

	status = letohl(req->data.openreceivechannelack.status);
	if (status) {
		ast_log(LOG_ERROR, "Open Receive Channel Failure\n");
		return 0;
	}
	addr = req->data.openreceivechannelack.ipAddr;
	port = letohl(req->data.openreceivechannelack.port);
	passthruid = letohl(req->data.openreceivechannelack.passThruId);

	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = addr;
	sin.sin_port = htons(port);

	sub = find_subchannel_by_reference(d, passthruid);

	if (!sub)
		return 0;

	l = sub->parent;

	if (sub->rtp) {
		ast_sockaddr_from_sin(&sin_tmp, &sin);
		ast_rtp_instance_set_remote_address(sub->rtp, &sin_tmp);
		ast_rtp_instance_get_local_address(sub->rtp, &us_tmp);
		ast_sockaddr_to_sin(&us_tmp, &us);
		us.sin_addr.s_addr = us.sin_addr.s_addr ? us.sin_addr.s_addr : d->ourip.s_addr;
	} else {
		ast_log(LOG_ERROR, "No RTP structure, this is very bad\n");
		return 0;
	}

	if (skinnydebug) {
		ast_verb(1, "device ipaddr = %s:%d\n", ast_inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
		ast_verb(1, "asterisk ipaddr = %s:%d\n", ast_inet_ntoa(us.sin_addr), ntohs(us.sin_port));
	}

	fmt = ast_codec_pref_getsize(&l->prefs, ast_best_codec(l->capability));

	if (skinnydebug)
		ast_verb(1, "Setting payloadType to '%s' (%d ms)\n", ast_getformatname(fmt.bits), fmt.cur_ms);

	transmit_startmediatransmission(d, sub, us, fmt);

	return 1;
}

static int handle_enbloc_call_message(struct skinny_req *req, struct skinnysession *s)
{
	struct skinny_device *d = s->device;
	struct skinny_line *l;
	struct skinny_subchannel *sub = NULL;
	struct ast_channel *c;
	pthread_t t;

	if (skinnydebug)
		ast_verb(1, "Received Enbloc Call: %s\n", req->data.enbloccallmessage.calledParty);

	sub = find_subchannel_by_instance_reference(d, d->lastlineinstance, d->lastcallreference);

	if (!sub) {
		l = find_line_by_instance(d, d->lastlineinstance);
		if (!l) {
			return 0;
		}
	} else {
		l = sub->parent;
	}

	c = skinny_new(l, AST_STATE_DOWN, NULL);

	if(!c) {
		ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
	} else {
		l->hookstate = SKINNY_OFFHOOK;

		sub = c->tech_pvt;
		l->activesub = sub;
		transmit_callstate(d, l->instance, sub->callid, SKINNY_OFFHOOK);
		transmit_activatecallplane(d, l);
		transmit_clear_display_message(d, l->instance, sub->callid);
		transmit_start_tone(d, SKINNY_DIALTONE, l->instance, sub->callid);

		if (!ast_ignore_pattern(c->context, req->data.enbloccallmessage.calledParty)) {
			transmit_stop_tone(d, l->instance, sub->callid);
		}
		ast_copy_string(c->exten, req->data.enbloccallmessage.calledParty, sizeof(c->exten));
		if (ast_pthread_create(&t, NULL, skinny_newcall, c)) {
			ast_log(LOG_WARNING, "Unable to create new call thread: %s\n", strerror(errno));
			ast_hangup(c);
		}
	}
	
	return 1;
}


static int handle_soft_key_event_message(struct skinny_req *req, struct skinnysession *s)
{
	struct skinny_device *d = s->device;
	struct skinny_line *l;
	struct skinny_subchannel *sub = NULL;
	struct ast_channel *c;
	pthread_t t;
	int event;
	int instance;
	int callreference;

	event = letohl(req->data.softkeyeventmessage.softKeyEvent);
	instance = letohl(req->data.softkeyeventmessage.instance);
	callreference = letohl(req->data.softkeyeventmessage.callreference);

	if (instance) {
		l = find_line_by_instance(d, instance);
		if (callreference) {
			sub = find_subchannel_by_instance_reference(d, instance, callreference);
		} else {
			sub = find_subchannel_by_instance_reference(d, instance, d->lastcallreference);
		}
	} else {
		l = find_line_by_instance(d, d->lastlineinstance);
	}

	if (!l) {
		if (skinnydebug)
			ast_verb(1, "Received Softkey Event: %d(%d/%d)\n", event, instance, callreference);
		return 0;
	}

	ast_devstate_changed(AST_DEVICE_INUSE, "Skinny/%s@%s", l->name, d->name);

	switch(event) {
	case SOFTKEY_NONE:
		if (skinnydebug)
			ast_verb(1, "Received Softkey Event: None(%d/%d)\n", instance, callreference);
		break;
	case SOFTKEY_REDIAL:
		if (skinnydebug)
			ast_verb(1, "Received Softkey Event: Redial(%d/%d)\n", instance, callreference);

		if (ast_strlen_zero(l->lastnumberdialed)) {
			ast_log(LOG_WARNING, "Attempted redial, but no previously dialed number found. Ignoring button.\n");
			break;
		}

		if (!sub || !sub->owner) {
			c = skinny_new(l, AST_STATE_DOWN, NULL);
		} else {
			c = sub->owner;
		}

		if (!c) {
			ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
		} else {
			sub = c->tech_pvt;
			l->activesub = sub;
			if (l->hookstate == SKINNY_ONHOOK) {
				l->hookstate = SKINNY_OFFHOOK;
				transmit_speaker_mode(d, SKINNY_SPEAKERON);
				transmit_callstate(d, l->instance, sub->callid, SKINNY_OFFHOOK);
				transmit_activatecallplane(d, l);
			}
			transmit_clear_display_message(d, l->instance, sub->callid);
			transmit_start_tone(d, SKINNY_DIALTONE, l->instance, sub->callid);
			transmit_selectsoftkeys(d, l->instance, sub->callid, KEYDEF_RINGOUT);

			if (!ast_ignore_pattern(c->context, l->lastnumberdialed)) {
				transmit_stop_tone(d, l->instance, sub->callid);
			}
			ast_copy_string(c->exten, l->lastnumberdialed, sizeof(c->exten));
			if (ast_pthread_create(&t, NULL, skinny_newcall, c)) {
				ast_log(LOG_WARNING, "Unable to create new call thread: %s\n", strerror(errno));
				ast_hangup(c);
			}
		}
		break;
	case SOFTKEY_NEWCALL:  /* Actually the DIAL softkey */
		if (skinnydebug)
			ast_verb(1, "Received Softkey Event: New Call(%d/%d)\n", instance, callreference);

		/* New Call ALWAYS gets a new sub-channel */
		c = skinny_new(l, AST_STATE_DOWN, NULL);
		sub = c->tech_pvt;
	
		/* transmit_ringer_mode(d, SKINNY_RING_OFF);
		transmit_lamp_indication(d, STIMULUS_LINE, l->instance, SKINNY_LAMP_ON); */

		/* l->hookstate = SKINNY_OFFHOOK; */

		if (!c) {
			ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
		} else {
			sub = c->tech_pvt;
			l->activesub = sub;
			if (l->hookstate == SKINNY_ONHOOK) {
				l->hookstate = SKINNY_OFFHOOK;
				transmit_speaker_mode(d, SKINNY_SPEAKERON);
			}
			ast_verb(1, "Call-id: %d\n", sub->callid);

			transmit_callstate(d, l->instance, sub->callid, SKINNY_OFFHOOK);
			transmit_activatecallplane(d, l);

			transmit_clear_display_message(d, l->instance, sub->callid);
			transmit_start_tone(d, SKINNY_DIALTONE, l->instance, sub->callid);
			transmit_selectsoftkeys(d, l->instance, sub->callid, KEYDEF_OFFHOOK);

			/* start the switch thread */
			if (ast_pthread_create(&t, NULL, skinny_ss, c)) {
				ast_log(LOG_WARNING, "Unable to create switch thread: %s\n", strerror(errno));
				ast_hangup(c);
			}
		}
		break;
	case SOFTKEY_HOLD:
		if (skinnydebug)
			ast_verb(1, "Received Softkey Event: Hold(%d/%d)\n", instance, callreference);
		handle_hold_button(sub);	
		break;
	case SOFTKEY_TRNSFER:
		if (skinnydebug)
			ast_verb(1, "Received Softkey Event: Transfer(%d/%d)\n", instance, callreference);
		if (l->transfer)
			handle_transfer_button(sub);
		else
			transmit_displaynotify(d, "Transfer disabled", 10);

		break;
	case SOFTKEY_DND:
		if (skinnydebug)
			ast_verb(1, "Received Softkey Event: DND(%d/%d)\n", instance, callreference);

		/* Do not disturb */
		if (l->dnd != 0){
			ast_verb(3, "Disabling DND on %s@%s\n", l->name, d->name);
			l->dnd = 0;
			transmit_lamp_indication(d, STIMULUS_DND, 1, SKINNY_LAMP_ON);
			transmit_displaynotify(d, "DnD disabled", 10);
		} else {
			ast_verb(3, "Enabling DND on %s@%s\n", l->name, d->name);
			l->dnd = 1;
			transmit_lamp_indication(d, STIMULUS_DND, 1, SKINNY_LAMP_OFF);
			transmit_displaynotify(d, "DnD enabled", 10);
		}
		break;
	case SOFTKEY_CFWDALL:
		if (skinnydebug)
			ast_verb(1, "Received Softkey Event: Forward All(%d/%d)\n", instance, callreference);

		if (!sub || !sub->owner) {
			c = skinny_new(l, AST_STATE_DOWN, NULL);
		} else {
			c = sub->owner;
		}

		if (!c) {
			ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
		} else {
			sub = c->tech_pvt;
			l->activesub = sub;
			handle_callforward_button(sub, SKINNY_CFWD_ALL);
		}
		break;
	case SOFTKEY_CFWDBUSY:
		if (skinnydebug)
			ast_verb(1, "Received Softkey Event: Forward Busy (%d/%d)\n", instance, callreference);

		if (!sub || !sub->owner) {
			c = skinny_new(l, AST_STATE_DOWN, NULL);
		} else {
			c = sub->owner;
		}

		if (!c) {
			ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
		} else {
			sub = c->tech_pvt;
			l->activesub = sub;
			handle_callforward_button(sub, SKINNY_CFWD_BUSY);
		}
		break;
	case SOFTKEY_CFWDNOANSWER:
		if (skinnydebug)
			ast_verb(1, "Received Softkey Event: Forward No Answer (%d/%d)\n", instance, callreference);

#if 0 /* Not sure how to handle this yet */
		if (!sub || !sub->owner) {
			c = skinny_new(l, AST_STATE_DOWN, NULL);
		} else {
			c = sub->owner;
		}

		if (!c) {
			ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
		} else {
			sub = c->tech_pvt;
			l->activesub = sub;
			handle_callforward_button(sub, SKINNY_CFWD_NOANSWER);
		}
#endif
		break;
	case SOFTKEY_BKSPC:
		if (skinnydebug)
			ast_verb(1, "Received Softkey Event: Backspace(%d/%d)\n", instance, callreference);
		break;
	case SOFTKEY_ENDCALL:
		if (skinnydebug)
			ast_verb(1, "Received Softkey Event: End Call(%d/%d)\n", instance, callreference);

		if (l->hookstate == SKINNY_ONHOOK) {
			/* Something else already put us back on hook */
			break;
		}
		if (sub) {
			int onlysub = 0;

			if (!AST_LIST_NEXT(sub, list)) {
				onlysub = 1;
			} else {
				AST_LIST_REMOVE(&l->sub, sub, list);
			}

			sub->cxmode = SKINNY_CX_RECVONLY;
			if (onlysub || sub->xferor){    /*Are there other calls to this device */
				l->hookstate = SKINNY_ONHOOK;
				if (skinnydebug)
					ast_debug(1, "Skinny %s@%s-%d went on hook\n", l->name, d->name, callreference);
			}

			if (l->hookstate == SKINNY_ONHOOK) {
				transmit_closereceivechannel(d, sub);
				transmit_stopmediatransmission(d, sub);
				transmit_speaker_mode(d, SKINNY_SPEAKEROFF);
				transmit_clearpromptmessage(d, instance, sub->callid);
				transmit_callstate(d, l->instance, sub->callid, SKINNY_ONHOOK);
				transmit_selectsoftkeys(d, 0, 0, KEYDEF_ONHOOK);
				transmit_activatecallplane(d, l);
			} else if (l->hookstate == SKINNY_OFFHOOK) {
				transmit_callstate(d, l->instance, sub->callid, SKINNY_OFFHOOK);
				transmit_activatecallplane(d, l);
			} else {
				transmit_callstate(d, l->instance, sub->callid, l->hookstate);
			}

			ast_devstate_changed(AST_DEVICE_NOT_INUSE, "Skinny/%s@%s", l->name, d->name);
			if (skinnydebug)
				ast_verb(1, "Skinny %s@%s went on hook\n", l->name, d->name);
			if (l->transfer && sub->xferor && sub->owner->_state >= AST_STATE_RING) {
				/* We're allowed to transfer, we have two active calls and
				   we made at least one of the calls.  Let's try and transfer */
				handle_transfer_button(sub);
			} else {
				/* Hangup the current call */
				/* If there is another active call, skinny_hangup will ring the phone with the other call */
				if (sub->xferor && sub->related){
					sub->related->related = NULL;
					sub->related->blindxfer = 0;
				}

				if (sub->owner) {
					sub->alreadygone = 1;
					ast_queue_hangup(sub->owner);
				} else {
					ast_log(LOG_WARNING, "Skinny(%s@%s-%d) channel already destroyed\n",
						l->name, d->name, sub->callid);
				}
			}
			if ((l->hookstate == SKINNY_ONHOOK) && (AST_LIST_NEXT(sub, list) && !AST_LIST_NEXT(sub, list)->rtp)) {
				ast_devstate_changed(AST_DEVICE_NOT_INUSE, "Skinny/%s@%s", l->name, d->name);
			}
		}
		break;
	case SOFTKEY_RESUME:
		if (skinnydebug)
			ast_verb(1, "Received Softkey Event: Resume(%d/%d)\n", instance, callreference);

		if (sub) {
			if (sub->onhold) {
				skinny_unhold(sub);
				transmit_selectsoftkeys(d, l->instance, sub->callid, KEYDEF_CONNECTED);
			} else {
				skinny_hold(sub);
				transmit_selectsoftkeys(d, l->instance, sub->callid, KEYDEF_ONHOLD);
			}
		}

		break;
	case SOFTKEY_ANSWER:
		if (skinnydebug)
			ast_verb(1, "Received Softkey Event: Answer(%d/%d)\n", instance, callreference);

		transmit_ringer_mode(d, SKINNY_RING_OFF);
		transmit_lamp_indication(d, STIMULUS_LINE, l->instance, SKINNY_LAMP_ON);
		if (l->hookstate == SKINNY_ONHOOK) {
			transmit_speaker_mode(d, SKINNY_SPEAKERON);
			l->hookstate = SKINNY_OFFHOOK;
		}

		if (sub && sub->outgoing) {
			/* We're answering a ringing call */
			ast_queue_control(sub->owner, AST_CONTROL_ANSWER);
			transmit_callstate(d, l->instance, sub->callid, SKINNY_OFFHOOK);
			transmit_activatecallplane(d, l);
			transmit_stop_tone(d, l->instance, sub->callid);
			transmit_callstate(d, sub->parent->instance, sub->callid, SKINNY_CONNECTED);
			transmit_selectsoftkeys(d, l->instance, sub->callid, KEYDEF_CONNECTED);
			start_rtp(sub);
			ast_setstate(sub->owner, AST_STATE_UP);
		}
		break;
	case SOFTKEY_INFO:
		if (skinnydebug)
			ast_verb(1, "Received Softkey Event: Info(%d/%d)\n", instance, callreference);
		break;
	case SOFTKEY_CONFRN:
		if (skinnydebug)
			ast_verb(1, "Received Softkey Event: Conference(%d/%d)\n", instance, callreference);
		/* XXX determine the best way to pull off a conference.  Meetme? */
		break;
	case SOFTKEY_PARK:
		{
		int extout;
		char message[32];

		if (skinnydebug)
			ast_verb(1, "Received Softkey Event: Park Call(%d/%d)\n", instance, callreference);

		if ((sub && sub->owner) && (sub->owner->_state ==  AST_STATE_UP)){
			c = sub->owner;
			if (ast_bridged_channel(c)) {
				if (!ast_masq_park_call(ast_bridged_channel(c), c, 0, &extout)) {
					snprintf(message, sizeof(message), "Call Parked at: %d", extout);
					transmit_displaynotify(d, message, 10);
				} else {
					transmit_displaynotify(d, "Call Park failed", 10);
				}
			} else {
				transmit_displaynotify(d, "Call Park not available", 10);
			}
		} else {
			transmit_displaynotify(d, "Call Park not available", 10);
		}
		break;
		}
	case SOFTKEY_JOIN:
		if (skinnydebug)
			ast_verb(1, "Received Softkey Event: Join(%d/%d)\n", instance, callreference);
		break;
	case SOFTKEY_MEETME:
		/* XXX How is this different from CONFRN? */
		if (skinnydebug)
			ast_verb(1, "Received Softkey Event: Meetme(%d/%d)\n", instance, callreference);
		break;
	case SOFTKEY_PICKUP:
		if (skinnydebug)
			ast_verb(1, "Received Softkey Event: Pickup(%d/%d)\n", instance, callreference);
		break;
	case SOFTKEY_GPICKUP:
		if (skinnydebug)
			ast_verb(1, "Received Softkey Event: Group Pickup(%d/%d)\n", instance, callreference);
		break;
	default:
		if (skinnydebug)
			ast_verb(1, "Received unknown Softkey Event: %d(%d/%d)\n", event, instance, callreference);
		break;
	}

	return 1;
}

static int handle_message(struct skinny_req *req, struct skinnysession *s)
{
	int res = 0;
	struct skinny_speeddial *sd;
	struct skinny_line *l;
	struct skinny_device *d = s->device;
	
	if ((!s->device) && (letohl(req->e) != REGISTER_MESSAGE && letohl(req->e) != ALARM_MESSAGE)) {
		ast_log(LOG_WARNING, "Client sent message #%d without first registering.\n", req->e);
		ast_free(req);
		return 0;
	}

	SKINNY_DEVONLY(if (skinnydebug > 1) {
		ast_verb(4, "Received %s from %s\n", message2str(req->e), s->device->name);
	})

	switch(letohl(req->e)) {
	case KEEP_ALIVE_MESSAGE:
		res = handle_keep_alive_message(req, s);
		break;
	case REGISTER_MESSAGE:
		if (skinnydebug)
			ast_verb(1, "Device %s is attempting to register\n", req->data.reg.name);

		res = handle_register_message(req, s);
		break;
	case IP_PORT_MESSAGE:
		res = handle_ip_port_message(req, s);
		break;
	case KEYPAD_BUTTON_MESSAGE:
	    {
		struct skinny_device *d = s->device;
		struct skinny_subchannel *sub;
		int lineInstance;
		int callReference;

		if (skinnydebug)
			ast_verb(1, "Collected digit: [%d]\n", letohl(req->data.keypad.button));

		lineInstance = letohl(req->data.keypad.lineInstance);
		callReference = letohl(req->data.keypad.callReference);

		if (lineInstance) {
			sub = find_subchannel_by_instance_reference(d, lineInstance, callReference);
		} else {
			sub = d->activeline->activesub;
		}

		if (sub && ((sub->owner && sub->owner->_state <  AST_STATE_UP) || sub->onhold)) {
			char dgt;
			int digit = letohl(req->data.keypad.button);

			if (digit == 14) {
				dgt = '*';
			} else if (digit == 15) {
				dgt = '#';
			} else if (digit >= 0 && digit <= 9) {
				dgt = '0' + digit;
			} else {
				/* digit=10-13 (A,B,C,D ?), or
				* digit is bad value
				*
				* probably should not end up here, but set
				* value for backward compatibility, and log
				* a warning.
				*/
				dgt = '0' + digit;
				ast_log(LOG_WARNING, "Unsupported digit %d\n", digit);
			}

			d->exten[strlen(d->exten)] = dgt;
			d->exten[strlen(d->exten)+1] = '\0';
		} else
			res = handle_keypad_button_message(req, s);
		}
		break;
	case ENBLOC_CALL_MESSAGE:
		res = handle_enbloc_call_message(req, s);
		break;
	case STIMULUS_MESSAGE:
		res = handle_stimulus_message(req, s);
		break;
	case OFFHOOK_MESSAGE:
		res = handle_offhook_message(req, s);
		break;
	case ONHOOK_MESSAGE:
		res = handle_onhook_message(req, s);
		break;
	case CAPABILITIES_RES_MESSAGE:
		if (skinnydebug)
			ast_verb(1, "Received CapabilitiesRes\n");

		res = handle_capabilities_res_message(req, s);
		break;
	case SPEED_DIAL_STAT_REQ_MESSAGE:
		if (skinnydebug)
			ast_verb(1, "Received SpeedDialStatRequest\n");
		if ( (sd = find_speeddial_by_instance(s->device, letohl(req->data.speeddialreq.speedDialNumber), 0)) ) {
			transmit_speeddialstatres(d, sd);
		}
		break;
	case LINE_STATE_REQ_MESSAGE:
		if (skinnydebug)
			ast_verb(1, "Received LineStatRequest\n");
		if ((l = find_line_by_instance(d, letohl(req->data.line.lineNumber)))) {
			transmit_linestatres(d, l);
		}
		break;
	case TIME_DATE_REQ_MESSAGE:
		if (skinnydebug)
			ast_verb(1, "Received Time/Date Request\n");

		transmit_definetimedate(d);
		break;
	case BUTTON_TEMPLATE_REQ_MESSAGE:
		if (skinnydebug)
			ast_verb(1, "Buttontemplate requested\n");

		res = handle_button_template_req_message(req, s);
		break;
	case VERSION_REQ_MESSAGE:
		if (skinnydebug)
			ast_verb(1, "Version Request\n");
		transmit_versionres(d);
		break;
	case SERVER_REQUEST_MESSAGE:
		if (skinnydebug)
			ast_verb(1, "Received Server Request\n");
		transmit_serverres(d);
		break;
	case ALARM_MESSAGE:
		/* no response necessary */
		if (skinnydebug)
			ast_verb(1, "Received Alarm Message: %s\n", req->data.alarm.displayMessage);
		break;
	case OPEN_RECEIVE_CHANNEL_ACK_MESSAGE:
		if (skinnydebug)
			ast_verb(1, "Received Open Receive Channel Ack\n");

		res = handle_open_receive_channel_ack_message(req, s);
		break;
	case SOFT_KEY_SET_REQ_MESSAGE:
		if (skinnydebug)
			ast_verb(1, "Received SoftKeySetReq\n");
		transmit_softkeysetres(d);
		transmit_selectsoftkeys(d, 0, 0, KEYDEF_ONHOOK);
		break;
	case SOFT_KEY_EVENT_MESSAGE:
		res = handle_soft_key_event_message(req, s);
		break;
	case UNREGISTER_MESSAGE:
		if (skinnydebug)
			ast_verb(1, "Received Unregister Request\n");

		res = skinny_unregister(req, s);
		break;
	case SOFT_KEY_TEMPLATE_REQ_MESSAGE:
		if (skinnydebug)
			ast_verb(1, "Received SoftKey Template Request\n");
		transmit_softkeytemplateres(d);
		break;
	case HEADSET_STATUS_MESSAGE:
		/* XXX umm...okay?  Why do I care? */
		break;
	case REGISTER_AVAILABLE_LINES_MESSAGE:
		/* XXX I have no clue what this is for, but my phone was sending it, so... */
		break;
	default:
		if (skinnydebug)
			ast_verb(1, "RECEIVED UNKNOWN MESSAGE TYPE:  %x\n", letohl(req->e));
		break;
	}
	if (res >= 0 && req)
		ast_free(req);
	return res;
}

static void destroy_session(struct skinnysession *s)
{
	struct skinnysession *cur;
	AST_LIST_LOCK(&sessions);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&sessions, cur, list) {
		if (cur == s) {
			AST_LIST_REMOVE_CURRENT(list);
			if (s->fd > -1) 
				close(s->fd);
			
			if (!s->device)
				ast_atomic_fetchadd_int(&unauth_sessions, -1);

			ast_mutex_destroy(&s->lock);
			
			ast_free(s);
		} else {
			ast_log(LOG_WARNING, "Trying to delete nonexistent session %p?\n", s);
		}
	}
	AST_LIST_TRAVERSE_SAFE_END
	AST_LIST_UNLOCK(&sessions);
}

static int get_input(struct skinnysession *s)
{
	int res;
	int dlen = 0;
	int timeout = keep_alive * 1100;
	time_t now;
	int *bufaddr;
	struct pollfd fds[1];

	if (!s->device) {
		if(time(&now) == -1) {
			ast_log(LOG_ERROR, "error executing time(): %s\n", strerror(errno));
			return -1;
		}

		timeout = (auth_timeout - (now - s->start)) * 1000;
		if (timeout < 0) {
			/* we have timed out */
			if (skinnydebug)
				ast_verb(1, "Skinny Client failed to authenticate in %d seconds\n", auth_timeout);
			return -1;
		}
	}

	fds[0].fd = s->fd;
	fds[0].events = POLLIN;
	fds[0].revents = 0;
	res = ast_poll(fds, 1, timeout); /* If nothing has happen, client is dead */
						 /* we add 10% to the keep_alive to deal */
						 /* with network delays, etc */
	if (res < 0) {
		if (errno != EINTR) {
			ast_log(LOG_WARNING, "Select returned error: %s\n", strerror(errno));
			return res;
		}
	} else if (res == 0) {
		if (skinnydebug) {
			if (s->device) {
				ast_verb(1, "Skinny Client was lost, unregistering\n");
			} else {
				ast_verb(1, "Skinny Client failed to authenticate in %d seconds\n", auth_timeout);
			}
		}
		skinny_unregister(NULL, s);
		return -1;
	}
		     
	if (fds[0].revents) {
		ast_mutex_lock(&s->lock);
		memset(s->inbuf, 0, sizeof(s->inbuf));
		res = read(s->fd, s->inbuf, 4);
		if (res < 0) {
			ast_log(LOG_WARNING, "read() returned error: %s\n", strerror(errno));

			if (skinnydebug)
				ast_verb(1, "Skinny Client was lost, unregistering\n");

			skinny_unregister(NULL, s);
			ast_mutex_unlock(&s->lock);
			return res;
		} else if (res != 4) {
			ast_log(LOG_WARNING, "Skinny Client sent less data than expected.  Expected 4 but got %d.\n", res);
			ast_mutex_unlock(&s->lock);
			
			if (res == 0) {
				if (skinnydebug)
					ast_verb(1, "Skinny Client was lost, unregistering\n");
				skinny_unregister(NULL, s);
			}

			return -1;
		}

		bufaddr = (int *)s->inbuf;
		dlen = letohl(*bufaddr);
		if (dlen < 4) {
			ast_debug(1, "Skinny Client sent invalid data.\n");
			ast_mutex_unlock(&s->lock);
			return -1;
		}
		if (dlen+8 > sizeof(s->inbuf)) {
			dlen = sizeof(s->inbuf) - 8;
		}
		*bufaddr = htolel(dlen);

		res = read(s->fd, s->inbuf+4, dlen+4);
		ast_mutex_unlock(&s->lock);
		if (res < 0) {
			ast_log(LOG_WARNING, "read() returned error: %s\n", strerror(errno));
			return res;
		} else if (res != (dlen+4)) {
			ast_log(LOG_WARNING, "Skinny Client sent less data than expected.\n");
			return -1;
		}
		return res;
	}
	return 0;
}

static struct skinny_req *skinny_req_parse(struct skinnysession *s)
{
	struct skinny_req *req;
	int *bufaddr;

	if (!(req = ast_calloc(1, SKINNY_MAX_PACKET)))
		return NULL;

	ast_mutex_lock(&s->lock);
	memcpy(req, s->inbuf, skinny_header_size);
	bufaddr = (int *)(s->inbuf);
	memcpy(&req->data, s->inbuf+skinny_header_size, letohl(*bufaddr)-4);

	ast_mutex_unlock(&s->lock);

	if (letohl(req->e) < 0) {
		ast_log(LOG_ERROR, "Event Message is NULL from socket %d, This is bad\n", s->fd);
		ast_free(req);
		return NULL;
	}

	return req;
}

static void *skinny_session(void *data)
{
	int res;
	struct skinny_req *req;
	struct skinnysession *s = data;

	ast_verb(3, "Starting Skinny session from %s\n", ast_inet_ntoa(s->sin.sin_addr));

	for (;;) {
		res = get_input(s);
		if (res < 0) {
			break;
		}

		if (res > 0)
		{
			if (!(req = skinny_req_parse(s))) {
				destroy_session(s);
				return NULL;
			}

			res = handle_message(req, s);
			if (res < 0) {
				destroy_session(s);
				return NULL;
			}
		}
	}
	ast_debug(3, "Skinny Session returned: %s\n", strerror(errno));

	if (s) 
		destroy_session(s);
	
	return 0;
}

static void *accept_thread(void *ignore)
{
	int as;
	struct sockaddr_in sin;
	socklen_t sinlen;
	struct skinnysession *s;
	struct protoent *p;
	int arg = 1;

	for (;;) {
		sinlen = sizeof(sin);
		as = accept(skinnysock, (struct sockaddr *)&sin, &sinlen);
		if (as < 0) {
			ast_log(LOG_NOTICE, "Accept returned -1: %s\n", strerror(errno));
			continue;
		}

		if (ast_atomic_fetchadd_int(&unauth_sessions, +1) >= auth_limit) {
			close(as);
			ast_atomic_fetchadd_int(&unauth_sessions, -1);
			continue;
		}

		p = getprotobyname("tcp");
		if(p) {
			if( setsockopt(as, p->p_proto, TCP_NODELAY, (char *)&arg, sizeof(arg) ) < 0 ) {
				ast_log(LOG_WARNING, "Failed to set Skinny tcp connection to TCP_NODELAY mode: %s\n", strerror(errno));
			}
		}
		if (!(s = ast_calloc(1, sizeof(struct skinnysession)))) {
			close(as);
			ast_atomic_fetchadd_int(&unauth_sessions, -1);
			continue;
		}

		memcpy(&s->sin, &sin, sizeof(sin));
		ast_mutex_init(&s->lock);
		s->fd = as;

		if(time(&s->start) == -1) {
			ast_log(LOG_ERROR, "error executing time(): %s; disconnecting client\n", strerror(errno));
			destroy_session(s);
			continue;
		}

		AST_LIST_LOCK(&sessions);
		AST_LIST_INSERT_HEAD(&sessions, s, list);
		AST_LIST_UNLOCK(&sessions);

		if (ast_pthread_create(&s->t, NULL, skinny_session, s)) {
			destroy_session(s);
		}
	}
	if (skinnydebug)
		ast_verb(1, "killing accept thread\n");
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

	ast_mutex_lock(&monlock);
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
		if (ast_pthread_create_background(&monitor_thread, NULL, do_monitor, NULL) < 0) {
			ast_mutex_unlock(&monlock);
			ast_log(LOG_ERROR, "Unable to start monitor thread.\n");
			return -1;
		}
	}
	ast_mutex_unlock(&monlock);
	return 0;
}

static int skinny_devicestate(void *data)
{
	struct skinny_line *l;
	char *tmp;

	tmp = ast_strdupa(data);

	l = find_line_by_name(tmp);

	return get_devicestate(l);
}

static struct ast_channel *skinny_request(const char *type, format_t format, const struct ast_channel *requestor, void *data, int *cause)
{
	struct skinny_line *l;
	struct ast_channel *tmpc = NULL;
	char tmp[256];
	char *dest = data;

	if (!(format &= AST_FORMAT_AUDIO_MASK)) {
		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%s'\n", ast_getformatname_multiple(tmp, sizeof(tmp), format));
		return NULL;
	}

	ast_copy_string(tmp, dest, sizeof(tmp));
	if (ast_strlen_zero(tmp)) {
		ast_log(LOG_NOTICE, "Skinny channels require a device\n");
		return NULL;
	}
	l = find_line_by_name(tmp);
	if (!l) {
		ast_log(LOG_NOTICE, "No available lines on: %s\n", dest);
		return NULL;
	}
	ast_verb(3, "skinny_request(%s)\n", tmp);
	tmpc = skinny_new(l, AST_STATE_DOWN, requestor ? requestor->linkedid : NULL);
	if (!tmpc) {
		ast_log(LOG_WARNING, "Unable to make channel for '%s'\n", tmp);
	}
	restart_monitor();
	return tmpc;
}

 #define TYPE_GENERAL 	1
 #define TYPE_DEF_DEVICE 2
 #define TYPE_DEF_LINE 	4
 #define TYPE_DEVICE 	8
 #define TYPE_LINE 	16
 
 #define CLINE_OPTS	((struct skinny_line_options *)item)
 #define CLINE		((struct skinny_line *)item)
 #define CDEV_OPTS	((struct skinny_device_options *)item)
 #define CDEV		((struct skinny_device *)item)
 
 static void config_parse_variables(int type, void *item, struct ast_variable *vptr)
 {
 	struct ast_variable *v;
 	int lineInstance = 1;
 	int speeddialInstance = 1;
 	
 	while(vptr) {
 		v = vptr;
 		vptr = vptr->next;
 
 		if (type & (TYPE_GENERAL)) {
 			char newcontexts[AST_MAX_CONTEXT];
			char oldcontexts[AST_MAX_CONTEXT];
 			char *stringp, *context, *oldregcontext;
 			if (!ast_jb_read_conf(&global_jbconf, v->name, v->value)) {
 				v = v->next;
 				continue;
 			}
 			if (!strcasecmp(v->name, "bindaddr")) {
 				if (!(hp = ast_gethostbyname(v->value, &ahp))) {
 					ast_log(LOG_WARNING, "Invalid address: %s\n", v->value);
 				} else {
 					memcpy(&bindaddr.sin_addr, hp->h_addr, sizeof(bindaddr.sin_addr));
 				}
 				continue;
 			} else if (!strcasecmp(v->name, "keepalive")) {
 				keep_alive = atoi(v->value);
 				continue;
			} else if (!strcasecmp(v->name, "authtimeout")) {
				int timeout = atoi(v->value);

				if (timeout < 1) {
					ast_log(LOG_WARNING, "Invalid authtimeout value '%s', using default value\n", v->value);
					auth_timeout = DEFAULT_AUTH_TIMEOUT;
				} else {
					auth_timeout = timeout;
				}
				continue;
			} else if (!strcasecmp(v->name, "authlimit")) {
				int limit = atoi(v->value);

				if (limit < 1) {
					ast_log(LOG_WARNING, "Invalid authlimit value '%s', using default value\n", v->value);
					auth_limit = DEFAULT_AUTH_LIMIT;
				} else {
					auth_limit = limit;
				}
				continue;
 			} else if (!strcasecmp(v->name, "regcontext")) {
 				ast_copy_string(newcontexts, v->value, sizeof(newcontexts));
 				stringp = newcontexts;
				/* Initialize copy of current global_regcontext for later use in removing stale contexts */
				ast_copy_string(oldcontexts, regcontext, sizeof(oldcontexts));
				oldregcontext = oldcontexts;
 				/* Let's remove any contexts that are no longer defined in regcontext */
 				cleanup_stale_contexts(stringp, oldregcontext);
 				/* Create contexts if they don't exist already */
 				while ((context = strsep(&stringp, "&"))) {
 					ast_copy_string(used_context, context, sizeof(used_context));
 					ast_context_find_or_create(NULL, NULL, context, "Skinny");
 				}
 				ast_copy_string(regcontext, v->value, sizeof(regcontext));
 				continue;
 			} else if (!strcasecmp(v->name, "dateformat")) {
 				memcpy(date_format, v->value, sizeof(date_format));
 				continue;
 			} else if (!strcasecmp(v->name, "tos")) {
 				if (ast_str2tos(v->value, &qos.tos))
 					ast_log(LOG_WARNING, "Invalid tos value at line %d, refer to QoS documentation\n", v->lineno);
 				continue;
 			} else if (!strcasecmp(v->name, "tos_audio")) {
 				if (ast_str2tos(v->value, &qos.tos_audio))
 					ast_log(LOG_WARNING, "Invalid tos_audio value at line %d, refer to QoS documentation\n", v->lineno);
 				continue;
 			} else if (!strcasecmp(v->name, "tos_video")) {
 				if (ast_str2tos(v->value, &qos.tos_video))
 					ast_log(LOG_WARNING, "Invalid tos_video value at line %d, refer to QoS documentation\n", v->lineno);
 				continue;
 			} else if (!strcasecmp(v->name, "cos")) {
 				if (ast_str2cos(v->value, &qos.cos))
 					ast_log(LOG_WARNING, "Invalid cos value at line %d, refer to QoS documentation\n", v->lineno);
 				continue;
 			} else if (!strcasecmp(v->name, "cos_audio")) {
 				if (ast_str2cos(v->value, &qos.cos_audio))
 					ast_log(LOG_WARNING, "Invalid cos_audio value at line %d, refer to QoS documentation\n", v->lineno);
 				continue;
 			} else if (!strcasecmp(v->name, "cos_video")) {
 				if (ast_str2cos(v->value, &qos.cos_video))
 					ast_log(LOG_WARNING, "Invalid cos_video value at line %d, refer to QoS documentation\n", v->lineno);
 				continue;
 			} else if (!strcasecmp(v->name, "bindport")) {
 				if (sscanf(v->value, "%5d", &ourport) == 1) {
 					bindaddr.sin_port = htons(ourport);
 				} else {
 					ast_log(LOG_WARNING, "Invalid bindport '%s' at line %d of %s\n", v->value, v->lineno, config);
 				}
 				continue;
 			} else if (!strcasecmp(v->name, "allow")) {
 				ast_parse_allow_disallow(&default_prefs, &default_capability, v->value, 1);
 				continue;
 			} else if (!strcasecmp(v->name, "disallow")) {
 				ast_parse_allow_disallow(&default_prefs, &default_capability, v->value, 0);
 				continue;
 			} 
 		}
 
 		if (!strcasecmp(v->name, "transfer")) {
 			if (type & (TYPE_DEF_DEVICE | TYPE_DEVICE)) {
 				CDEV_OPTS->transfer = ast_true(v->value);
 				continue;
 			} else if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
 				CLINE_OPTS->transfer = ast_true(v->value);
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "callwaiting")) {
 			if (type & (TYPE_DEF_DEVICE | TYPE_DEVICE)) {
 				CDEV_OPTS->callwaiting = ast_true(v->value);
 				continue;
 			} else if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
 				CLINE_OPTS->callwaiting = ast_true(v->value);
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "directmedia") || !strcasecmp(v->name, "canreinvite")) {
 			if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
 				CLINE_OPTS->directmedia = ast_true(v->value);
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "nat")) {
 			if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
 				CLINE_OPTS->nat = ast_true(v->value);
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "context")) {
 			if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
 				ast_copy_string(CLINE_OPTS->context, v->value, sizeof(CLINE_OPTS->context));
 				continue;
 			}
 		}else if (!strcasecmp(v->name, "vmexten")) {
 			if (type & (TYPE_DEF_DEVICE | TYPE_DEVICE)) {
 				ast_copy_string(CDEV_OPTS->vmexten, v->value, sizeof(CDEV_OPTS->vmexten));
 				continue;
 			} else if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
 				ast_copy_string(CLINE_OPTS->vmexten, v->value, sizeof(CLINE_OPTS->vmexten));
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "mwiblink")) {
 			if (type & (TYPE_DEF_DEVICE | TYPE_DEVICE)) {
 				CDEV_OPTS->mwiblink = ast_true(v->value);
 				continue;
 			} else if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
 				CLINE_OPTS->mwiblink = ast_true(v->value);
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "linelabel")) {
 			if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
 				ast_copy_string(CLINE_OPTS->label, v->value, sizeof(CLINE_OPTS->label));
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "callerid")) {
 			if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
 				if (!strcasecmp(v->value, "asreceived")) {
 					CLINE_OPTS->cid_num[0] = '\0';
 					CLINE_OPTS->cid_name[0] = '\0';
 				} else {
 					ast_callerid_split(v->value, CLINE_OPTS->cid_name, sizeof(CLINE_OPTS->cid_name), CLINE_OPTS->cid_num, sizeof(CLINE_OPTS->cid_num));
 				}
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "amaflags")) {
 			if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
 				int tempamaflags = ast_cdr_amaflags2int(v->value);
 				if (tempamaflags < 0) {
 					ast_log(LOG_WARNING, "Invalid AMA flags: %s at line %d\n", v->value, v->lineno);
 				} else {
 					CLINE_OPTS->amaflags = tempamaflags;
 				}
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "regexten")) {
 			if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
 				ast_copy_string(CLINE_OPTS->regexten, v->value, sizeof(CLINE_OPTS->regexten));
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "language")) {
 			if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
 				ast_copy_string(CLINE_OPTS->language, v->value, sizeof(CLINE_OPTS->language));
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "accountcode")) {
 			if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
 				ast_copy_string(CLINE_OPTS->accountcode, v->value, sizeof(CLINE_OPTS->accountcode));
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "mohinterpret") || !strcasecmp(v->name, "musiconhold")) {
 			if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
 				ast_copy_string(CLINE_OPTS->mohinterpret, v->value, sizeof(CLINE_OPTS->mohinterpret));
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "mohsuggest")) {
 			if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
 				ast_copy_string(CLINE_OPTS->mohsuggest, v->value, sizeof(CLINE_OPTS->mohsuggest));
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "callgroup")) {
 			if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
 				CLINE_OPTS->callgroup = ast_get_group(v->value);
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "pickupgroup")) {
 			if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
 				CLINE_OPTS->pickupgroup = ast_get_group(v->value);
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "immediate")) {
 			if (type & (TYPE_DEF_DEVICE | TYPE_DEVICE | TYPE_DEF_LINE | TYPE_LINE)) {
 				CLINE_OPTS->immediate = ast_true(v->value);
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "cancallforward")) {
 			if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
 				CLINE_OPTS->cancallforward = ast_true(v->value);
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "mailbox")) {
 			if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
 				ast_copy_string(CLINE_OPTS->mailbox, v->value, sizeof(CLINE_OPTS->mailbox));
 				continue;
 			}
 		} else if ( !strcasecmp(v->name, "parkinglot")) {
 			if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
 				ast_copy_string(CLINE_OPTS->parkinglot, v->value, sizeof(CLINE_OPTS->parkinglot));
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "hasvoicemail")) {
 			if (type & (TYPE_LINE)) {
 				if (ast_true(v->value) && ast_strlen_zero(CLINE->mailbox)) {
 					ast_copy_string(CLINE->mailbox, CLINE->name, sizeof(CLINE->mailbox));
 				}
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "callreturn")) {
 			if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
 				CLINE_OPTS->callreturn = ast_true(v->value);
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "threewaycalling")) {
 			if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
 				CLINE_OPTS->threewaycalling = ast_true(v->value);
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "setvar")) {
 			if (type & (TYPE_LINE)) {
 				CLINE->chanvars = add_var(v->value, CLINE->chanvars);
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "earlyrtp")) {
 			if (type & (TYPE_DEF_DEVICE | TYPE_DEVICE)) {
 				CDEV_OPTS->earlyrtp = ast_true(v->value);
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "host")) {
 			if (type & (TYPE_DEVICE)) {
				struct ast_sockaddr CDEV_addr_tmp;

				if (ast_get_ip(&CDEV_addr_tmp, v->value)) {
 					ast_log(LOG_WARNING, "Bad IP '%s' at line %d.\n", v->value, v->lineno);
 				}
				ast_sockaddr_to_sin(&CDEV_addr_tmp,
						    &CDEV->addr);
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "port")) {
 			if (type & (TYPE_DEF_DEVICE)) {
 				CDEV->addr.sin_port = htons(atoi(v->value));
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "device")) {
 			if (type & (TYPE_DEVICE)) {
 				ast_copy_string(CDEV_OPTS->id, v->value, sizeof(CDEV_OPTS->id));
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "permit") || !strcasecmp(v->name, "deny")) {
 			if (type & (TYPE_DEVICE)) {
 				CDEV->ha = ast_append_ha(v->name, v->value, CDEV->ha, NULL);
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "allow")) {
 			if (type & (TYPE_DEF_DEVICE | TYPE_DEVICE)) {
 				ast_parse_allow_disallow(&CDEV_OPTS->confprefs, &CDEV_OPTS->confcapability, v->value, 1);
 				continue;
 			}
 			if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
 				ast_parse_allow_disallow(&CLINE_OPTS->confprefs, &CLINE_OPTS->confcapability, v->value, 1);
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "disallow")) {
 			if (type & (TYPE_DEF_DEVICE | TYPE_DEVICE)) {
 				ast_parse_allow_disallow(&CDEV_OPTS->confprefs, &CDEV_OPTS->confcapability, v->value, 0);
 				continue;
 			}
 			if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
 				ast_parse_allow_disallow(&CLINE_OPTS->confprefs, &CLINE_OPTS->confcapability, v->value, 0);
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "version")) {
 			if (type & (TYPE_DEF_DEVICE | TYPE_DEVICE)) {
 				ast_copy_string(CDEV_OPTS->version_id, v->value, sizeof(CDEV_OPTS->version_id));
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "line")) {
 			if (type & (TYPE_DEVICE)) {
 				struct skinny_line *l;
 				AST_LIST_TRAVERSE(&lines, l, all) {
 					if (!strcasecmp(v->value, l->name) && !l->prune) {

						/* FIXME: temp solution about line conflicts */
						struct skinny_device *d;
						struct skinny_line *l2;
						int lineinuse = 0;
						AST_LIST_TRAVERSE(&devices, d, list) {
							AST_LIST_TRAVERSE(&d->lines, l2, list) {
								if (l2 == l && strcasecmp(d->id, CDEV->id)) {
									ast_log(LOG_WARNING, "Line %s already used by %s. Not connecting to %s.\n", l->name, d->name, CDEV->name);
									lineinuse++;
								}
							}
						}
						if (!lineinuse) {
							if (!AST_LIST_FIRST(&CDEV->lines)) {
								CDEV->activeline = l;
							}
							lineInstance++;
							AST_LIST_INSERT_HEAD(&CDEV->lines, l, list);
						}
 						break;
 					}
 				}
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "speeddial")) {
 			if (type & (TYPE_DEVICE)) {
 				struct skinny_speeddial *sd;
 				if (!(sd = ast_calloc(1, sizeof(*sd)))) {
 					ast_log(LOG_WARNING, "Unable to allocate memory for speeddial %s. Ignoring speeddial.\n", v->name);
 					continue;
 				} else {
 					char buf[256];
 					char *stringp = buf, *exten, *context, *label;
 						ast_copy_string(buf, v->value, sizeof(buf));
 					exten = strsep(&stringp, ",");
 					if ((context = strchr(exten, '@'))) {
 						*context++ = '\0';
 					}
 					label = stringp;
 					ast_mutex_init(&sd->lock);
 					ast_copy_string(sd->exten, exten, sizeof(sd->exten));
 					if (!ast_strlen_zero(context)) {
 						sd->isHint = 1;
 						sd->instance = lineInstance++;
 						ast_copy_string(sd->context, context, sizeof(sd->context));
 					} else {
 						sd->isHint = 0;
 						sd->instance = speeddialInstance++;
 						sd->context[0] = '\0';
 					}
 					ast_copy_string(sd->label, S_OR(label, exten), sizeof(sd->label));
 					sd->parent = CDEV;
 					AST_LIST_INSERT_HEAD(&CDEV->speeddials, sd, list);
 				}
 				continue;
 			}
 		} else if (!strcasecmp(v->name, "addon")) {
 			if (type & (TYPE_DEVICE)) {
 				struct skinny_addon *a;
 				if (!(a = ast_calloc(1, sizeof(*a)))) {
 					ast_log(LOG_WARNING, "Unable to allocate memory for addon %s. Ignoring addon.\n", v->name);
 					continue;
 				} else {
 					ast_mutex_init(&a->lock);
 					ast_copy_string(a->type, v->value, sizeof(a->type));
 					AST_LIST_INSERT_HEAD(&CDEV->addons, a, list);
 				}
 				continue;
 			}

 		} else {
 			ast_log(LOG_WARNING, "Don't know keyword '%s' at line %d\n", v->name, v->lineno);
 			continue;
 		}
 		ast_log(LOG_WARNING, "Invalid category used: %s at line %d\n", v->name, v->lineno);
 	}
 }
 
 static struct skinny_line *config_line(const char *lname, struct ast_variable *v)
 {
 	struct skinny_line *l, *temp;
	int update = 0;
 
 	ast_log(LOG_NOTICE, "Configuring skinny line %s.\n", lname);

	/* We find the old line and remove it just before the new
	   line is created */
 	AST_LIST_LOCK(&lines);
 	AST_LIST_TRAVERSE(&lines, temp, all) {
 		if (!strcasecmp(lname, temp->name) && temp->prune) {
			update = 1;
 			break;
 		}
 	}

 	if (!(l=ast_calloc(1, sizeof(*l)))) {
 		ast_verb(1, "Unable to allocate memory for line %s.\n", lname);
 		AST_LIST_UNLOCK(&lines);
 		return NULL;
 	}

 	memcpy(l, default_line, sizeof(*default_line));
 	ast_mutex_init(&l->lock);
 	ast_copy_string(l->name, lname, sizeof(l->name));
 	AST_LIST_INSERT_TAIL(&lines, l, all);

 	ast_mutex_lock(&l->lock);
 	AST_LIST_UNLOCK(&lines);

 	config_parse_variables(TYPE_LINE, l, v);
 			
 	if (!ast_strlen_zero(l->mailbox)) {
 		char *cfg_mailbox, *cfg_context;
 		cfg_context = cfg_mailbox = ast_strdupa(l->mailbox);
 		ast_verb(3, "Setting mailbox '%s' on line %s\n", cfg_mailbox, l->name);
 		strsep(&cfg_context, "@");
 		if (ast_strlen_zero(cfg_context))
 			 cfg_context = "default";
		l->mwi_event_sub = ast_event_subscribe(AST_EVENT_MWI, mwi_event_cb, "skinny MWI subsciption", l,
 			AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_STR, cfg_mailbox,
 			AST_EVENT_IE_CONTEXT, AST_EVENT_IE_PLTYPE_STR, cfg_context,
 			AST_EVENT_IE_NEWMSGS, AST_EVENT_IE_PLTYPE_EXISTS,
 			AST_EVENT_IE_END);
 	}
 
 	ast_mutex_unlock(&l->lock);
	
	/* We do not want to unlink or free the line yet, it needs
	   to be available to detect a device reconfig when we load the
	   devices.  Old lines will be pruned after the reload completes */

	ast_verb(3, "%s config for line '%s'\n", update ? "Updated" : (skinnyreload ? "Reloaded" : "Created"), l->name);

 	return l;
 }
 
 static struct skinny_device *config_device(const char *dname, struct ast_variable *v)
 {
 	struct skinny_device *d, *temp;
 	struct skinny_line *l, *ltemp;
	struct skinny_subchannel *sub;
	int update = 0;
 
 	ast_log(LOG_NOTICE, "Configuring skinny device %s.\n", dname);

 	AST_LIST_LOCK(&devices);
 	AST_LIST_TRAVERSE(&devices, temp, list) {
 		if (!strcasecmp(dname, temp->name) && temp->prune) {
			update = 1;
 			break;
 		}
 	}

 	if (!(d = ast_calloc(1, sizeof(*d)))) {
 		ast_verb(1, "Unable to allocate memory for device %s.\n", dname);
 		AST_LIST_UNLOCK(&devices);
 		return NULL;
 	}
 	memcpy(d, default_device, sizeof(*default_device));
 	ast_mutex_init(&d->lock);
 	ast_copy_string(d->name, dname, sizeof(d->name));
 	AST_LIST_INSERT_TAIL(&devices, d, list);

 	ast_mutex_lock(&d->lock);
 	AST_LIST_UNLOCK(&devices);
 
 	config_parse_variables(TYPE_DEVICE, d, v);
 
  	if (!AST_LIST_FIRST(&d->lines)) {
 		ast_log(LOG_ERROR, "A Skinny device must have at least one line!\n");
 		ast_mutex_unlock(&d->lock);
 		return NULL;
 	}
 	if (/*d->addr.sin_addr.s_addr && */!ntohs(d->addr.sin_port)) {
 		d->addr.sin_port = htons(DEFAULT_SKINNY_PORT);
 	}
 
	if (skinnyreload){
		AST_LIST_LOCK(&devices);
		AST_LIST_TRAVERSE(&devices, temp, list) {
			if (strcasecmp(d->id, temp->id) || !temp->prune || !temp->session) {
				continue;
			}
			ast_mutex_lock(&d->lock);
			d->session = temp->session;
			d->session->device = d;

			AST_LIST_LOCK(&d->lines);
			AST_LIST_TRAVERSE(&d->lines, l, list){
				l->device = d;	

				AST_LIST_LOCK(&temp->lines);
				AST_LIST_TRAVERSE(&temp->lines, ltemp, list) {
					if (strcasecmp(l->name, ltemp->name)) {
						continue;
					}
					ast_mutex_lock(&ltemp->lock);
					l->instance = ltemp->instance;
					l->hookstate = ltemp->hookstate;
					if (!AST_LIST_EMPTY(&ltemp->sub)) {
						ast_mutex_lock(&l->lock);
						l->sub = ltemp->sub;
						AST_LIST_TRAVERSE(&l->sub, sub, list) {
							sub->parent = l;
						}
						ast_mutex_unlock(&l->lock);
					}
					ast_mutex_unlock(&ltemp->lock);
				}
				AST_LIST_UNLOCK(&temp->lines);
			}
			AST_LIST_UNLOCK(&d->lines);
			ast_mutex_unlock(&d->lock);
		}
		AST_LIST_UNLOCK(&devices);
	}

 	ast_mutex_unlock(&d->lock);

	ast_verb(3, "%s config for device '%s'\n", update ? "Updated" : (skinnyreload ? "Reloaded" : "Created"), d->name);
	
	return d;

 }
 
 static int config_load(void)
 {
  	int on = 1;
  	struct ast_config *cfg;
  	char *cat;
  	int oldport = ntohs(bindaddr.sin_port);
  	struct ast_flags config_flags = { 0 };
 	
 	ast_log(LOG_NOTICE, "Configuring skinny from %s\n", config);
  
  	if (gethostname(ourhost, sizeof(ourhost))) {
 		ast_log(LOG_WARNING, "Unable to get hostname, Skinny disabled.\n");
  		return 0;
  	}
  	cfg = ast_config_load(config, config_flags);
  
  	/* We *must* have a config file otherwise stop immediately */
  	if (!cfg || cfg == CONFIG_STATUS_FILEINVALID) {
 		ast_log(LOG_NOTICE, "Unable to load config %s, Skinny disabled.\n", config);
  		return -1;
  	}
	memset(&bindaddr, 0, sizeof(bindaddr));
	memset(&default_prefs, 0, sizeof(default_prefs));

	/* Copy the default jb config over global_jbconf */
	memcpy(&global_jbconf, &default_jbconf, sizeof(struct ast_jb_conf));

	/* load the general section */
	cat = ast_category_browse(cfg, "general");
	config_parse_variables(TYPE_GENERAL, NULL, ast_variable_browse(cfg, "general"));

	if (ntohl(bindaddr.sin_addr.s_addr)) {
		__ourip = bindaddr.sin_addr;
	} else {
		hp = ast_gethostbyname(ourhost, &ahp);
		if (!hp) {
			ast_log(LOG_WARNING, "Unable to get our IP address, Skinny disabled\n");
			ast_config_destroy(cfg);
			return 0;
		}
		memcpy(&__ourip, hp->h_addr, sizeof(__ourip));
	}
	if (!ntohs(bindaddr.sin_port)) {
		bindaddr.sin_port = htons(DEFAULT_SKINNY_PORT);
	}
	bindaddr.sin_family = AF_INET;

	/* load the lines sections */
	default_line->confcapability = default_capability;
	default_line->confprefs = default_prefs;
	config_parse_variables(TYPE_DEF_LINE, default_line, ast_variable_browse(cfg, "lines"));
	cat = ast_category_browse(cfg, "lines");
	while (cat && strcasecmp(cat, "general") && strcasecmp(cat, "devices")) {
		config_line(cat, ast_variable_browse(cfg, cat));
		cat = ast_category_browse(cfg, cat);
	}
		
	/* load the devices sections */
	default_device->confcapability = default_capability;
	default_device->confprefs = default_prefs;
	config_parse_variables(TYPE_DEF_DEVICE, default_device, ast_variable_browse(cfg, "devices"));
	cat = ast_category_browse(cfg, "devices");
	while (cat && strcasecmp(cat, "general") && strcasecmp(cat, "lines")) {
		config_device(cat, ast_variable_browse(cfg, cat));
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
			ast_log(LOG_ERROR, "Set Socket Options failed: errno %d, %s\n", errno, strerror(errno));
			ast_config_destroy(cfg);
			ast_mutex_unlock(&netlock);
			return 0;
		}
		if (skinnysock < 0) {
			ast_log(LOG_WARNING, "Unable to create Skinny socket: %s\n", strerror(errno));
		} else {
			if (bind(skinnysock, (struct sockaddr *)&bindaddr, sizeof(bindaddr)) < 0) {
				ast_log(LOG_WARNING, "Failed to bind to %s:%d: %s\n",
						ast_inet_ntoa(bindaddr.sin_addr), ntohs(bindaddr.sin_port),
							strerror(errno));
				close(skinnysock);
				skinnysock = -1;
				ast_config_destroy(cfg);
				ast_mutex_unlock(&netlock);
				return 0;
			}
			if (listen(skinnysock, DEFAULT_SKINNY_BACKLOG)) {
					ast_log(LOG_WARNING, "Failed to start listening to %s:%d: %s\n",
						ast_inet_ntoa(bindaddr.sin_addr), ntohs(bindaddr.sin_port),
							strerror(errno));
					close(skinnysock);
					skinnysock = -1;
					ast_config_destroy(cfg);
					ast_mutex_unlock(&netlock);
					return 0;
			}
			ast_verb(2, "Skinny listening on %s:%d\n",
					ast_inet_ntoa(bindaddr.sin_addr), ntohs(bindaddr.sin_port));
			ast_netsock_set_qos(skinnysock, qos.tos, qos.cos, "Skinny");
			ast_pthread_create_background(&accept_t, NULL, accept_thread, NULL);
		}
	}
	ast_mutex_unlock(&netlock);
	ast_config_destroy(cfg);
	return 1;
}

static void delete_devices(void)
{
	struct skinny_device *d;
	struct skinny_line *l;
	struct skinny_speeddial *sd;
	struct skinny_addon *a;

	AST_LIST_LOCK(&devices);
	AST_LIST_LOCK(&lines);

	/* Delete all devices */
	while ((d = AST_LIST_REMOVE_HEAD(&devices, list))) {
		/* Delete all lines for this device */
		while ((l = AST_LIST_REMOVE_HEAD(&d->lines, list))) {
			AST_LIST_REMOVE(&lines, l, all);
			free(l);
		}
		/* Delete all speeddials for this device */
		while ((sd = AST_LIST_REMOVE_HEAD(&d->speeddials, list))) {
			free(sd);
		}
		/* Delete all addons for this device */
		while ((a = AST_LIST_REMOVE_HEAD(&d->addons, list))) {
			free(a);
		} 
		free(d);
	}
	AST_LIST_UNLOCK(&lines);
	AST_LIST_UNLOCK(&devices);
}

int skinny_reload(void)
{
	struct skinny_device *d;
	struct skinny_line *l;
	struct skinny_speeddial *sd;
	struct skinny_addon *a;
	struct skinny_req *req;

	if (skinnyreload) {
		ast_verb(3, "Chan_skinny is already reloading.\n");
		return 0;
	}

	skinnyreload = 1;

	/* Mark all devices and lines as candidates to be pruned */
	AST_LIST_LOCK(&devices);
	AST_LIST_TRAVERSE(&devices, d, list) {
		d->prune = 1;
	}
	AST_LIST_UNLOCK(&devices);

	AST_LIST_LOCK(&lines);
	AST_LIST_TRAVERSE(&lines, l, all) {
		l->prune = 1;
	}
	AST_LIST_UNLOCK(&lines);

        config_load();

	/* Remove any devices that no longer exist in the config */
	AST_LIST_LOCK(&devices);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&devices, d, list) {
		if (!d->prune) {
			continue;
		}
		ast_verb(3, "Removing device '%s'\n", d->name);
		/* Delete all lines for this device. 
		   We do not want to free the line here, that
		   will happen below. */
		while ((l = AST_LIST_REMOVE_HEAD(&d->lines, list))) {
		}
		/* Delete all speeddials for this device */
		while ((sd = AST_LIST_REMOVE_HEAD(&d->speeddials, list))) {
			free(sd);
		}
		/* Delete all addons for this device */
		while ((a = AST_LIST_REMOVE_HEAD(&d->addons, list))) {
			free(a);
		}
		AST_LIST_REMOVE_CURRENT(list);
		free(d);
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&devices);

	AST_LIST_LOCK(&lines);  
	AST_LIST_TRAVERSE_SAFE_BEGIN(&lines, l, all) {
		if (l->prune) {
			AST_LIST_REMOVE_CURRENT(all);
			free(l);
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&lines);  

	AST_LIST_TRAVERSE(&devices, d, list) {
		/* Do a soft reset to re-register the devices after
		   cleaning up the removed devices and lines */
		if (d->session) {
			ast_verb(3, "Restarting device '%s'\n", d->name);
			if ((req = req_alloc(sizeof(struct reset_message), RESET_MESSAGE))) {
				req->data.reset.resetType = 2;
				transmit_response(d, req);
			}
		}
	}
	
	skinnyreload = 0;
        return 0;
}

static int load_module(void)
{
	int res = 0;

	for (; res < ARRAY_LEN(soft_key_template_default); res++) {
		soft_key_template_default[res].softKeyEvent = htolel(soft_key_template_default[res].softKeyEvent);
	}
	/* load and parse config */
	res = config_load();
	if (res == -1) {
		return AST_MODULE_LOAD_DECLINE;
	}

	/* Make sure we can register our skinny channel type */
	if (ast_channel_register(&skinny_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class 'Skinny'\n");
		return -1;
	}

	ast_rtp_glue_register(&skinny_rtp_glue);
	ast_cli_register_multiple(cli_skinny, ARRAY_LEN(cli_skinny));

	ast_manager_register_xml("SKINNYdevices", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, manager_skinny_show_devices);
	ast_manager_register_xml("SKINNYshowdevice", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, manager_skinny_show_device);
	ast_manager_register_xml("SKINNYlines", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, manager_skinny_show_lines);
	ast_manager_register_xml("SKINNYshowline", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, manager_skinny_show_line);

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

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	struct skinnysession *s;
	struct skinny_device *d;
	struct skinny_line *l;
	struct skinny_subchannel *sub;
	struct ast_context *con;

	ast_rtp_glue_unregister(&skinny_rtp_glue);
	ast_channel_unregister(&skinny_tech);
	ast_cli_unregister_multiple(cli_skinny, ARRAY_LEN(cli_skinny));

	ast_manager_unregister("SKINNYdevices");
	ast_manager_unregister("SKINNYshowdevice");
	ast_manager_unregister("SKINNYlines");
	ast_manager_unregister("SKINNYshowline");
	
	AST_LIST_LOCK(&sessions);
	/* Destroy all the interfaces and free their memory */
	while((s = AST_LIST_REMOVE_HEAD(&sessions, list))) {
		d = s->device;
		AST_LIST_TRAVERSE(&d->lines, l, list){
			ast_mutex_lock(&l->lock);
			AST_LIST_TRAVERSE(&l->sub, sub, list) {
				ast_mutex_lock(&sub->lock);
				if (sub->owner) {
					sub->alreadygone = 1;
					ast_softhangup(sub->owner, AST_SOFTHANGUP_APPUNLOAD);
				}
				ast_mutex_unlock(&sub->lock);
			}
			if (l->mwi_event_sub)
				ast_event_unsubscribe(l->mwi_event_sub);
			ast_mutex_unlock(&l->lock);
			manager_event(EVENT_FLAG_SYSTEM, "PeerStatus", "ChannelType: Skinny\r\nPeer: Skinny/%s@%s\r\nPeerStatus: Unregistered\r\n", l->name, d->name);
			unregister_exten(l);
		}
		if (s->fd > -1)
			close(s->fd);
		pthread_cancel(s->t);
		pthread_kill(s->t, SIGURG);
		pthread_join(s->t, NULL);
		free(s);
	}
	AST_LIST_UNLOCK(&sessions);

	delete_devices();

	ast_mutex_lock(&monlock);
	if ((monitor_thread != AST_PTHREADT_NULL) && (monitor_thread != AST_PTHREADT_STOP)) {
		pthread_cancel(monitor_thread);
		pthread_kill(monitor_thread, SIGURG);
		pthread_join(monitor_thread, NULL);
	}
	monitor_thread = AST_PTHREADT_STOP;
	ast_mutex_unlock(&monlock);

	ast_mutex_lock(&netlock);
	if (accept_t && (accept_t != AST_PTHREADT_STOP)) {
		pthread_cancel(accept_t);
		pthread_kill(accept_t, SIGURG);
		pthread_join(accept_t, NULL);
	}
	accept_t = AST_PTHREADT_STOP;
	ast_mutex_unlock(&netlock);

	close(skinnysock);
	if (sched)
		sched_context_destroy(sched);

	con = ast_context_find(used_context);
	if (con)
		ast_context_destroy(con, "Skinny");
	
	return 0;
}

static int reload(void)
{
	skinny_reload();
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Skinny Client Control Protocol (Skinny)",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
		.load_pri = AST_MODPRI_CHANNEL_DRIVER,
);
