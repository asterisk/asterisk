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

/*! \li \ref chan_skinny.c uses the configuration file \ref skinny.conf
 * \addtogroup configuration_file
 */

/*! \page skinny.conf skinny.conf
 * \verbinclude skinny.conf.sample
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>
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
#include "asterisk/netsock2.h"
#include "asterisk/acl.h"
#include "asterisk/callerid.h"
#include "asterisk/cli.h"
#include "asterisk/manager.h"
#include "asterisk/say.h"
#include "asterisk/astdb.h"
#include "asterisk/causes.h"
#include "asterisk/pickup.h"
#include "asterisk/app.h"
#include "asterisk/musiconhold.h"
#include "asterisk/utils.h"
#include "asterisk/dsp.h"
#include "asterisk/stringfields.h"
#include "asterisk/abstract_jb.h"
#include "asterisk/threadstorage.h"
#include "asterisk/devicestate.h"
#include "asterisk/indications.h"
#include "asterisk/linkedlists.h"
#include "asterisk/stasis_endpoints.h"
#include "asterisk/bridge.h"
#include "asterisk/parking.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/format_cache.h"

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

/* Skinny debugging only available if asterisk configured with --enable-dev-mode */
#ifdef AST_DEVMODE
static int skinnydebug = 0;
char dbgcli_buf[256];
#define DEBUG_GENERAL	(1 << 1)
#define DEBUG_SUB	(1 << 2)
#define DEBUG_PACKET	(1 << 3)
#define DEBUG_AUDIO	(1 << 4)
#define DEBUG_LOCK	(1 << 5)
#define DEBUG_TEMPLATE	(1 << 6)
#define DEBUG_THREAD	(1 << 7)
#define DEBUG_HINT	(1 << 8)
#define DEBUG_KEEPALIVE (1 << 9)
#define SKINNY_DEBUG(type, verb_level, text, ...)						\
	do{											\
		if (skinnydebug & (type)) {							\
			ast_verb(verb_level, "[%d] " text, ast_get_tid(), ##__VA_ARGS__);	\
		}										\
	}while(0)
#else
#define SKINNY_DEBUG(type, verb_level, text, ...)
#endif

/*************************************
 * Skinny/Asterisk Protocol Settings *
 *************************************/
static const char tdesc[] = "Skinny Client Control Protocol (Skinny)";
static const char config[] = "skinny.conf";

static struct ast_format_cap *default_cap;

enum skinny_codecs {
	SKINNY_CODEC_ALAW = 2,
	SKINNY_CODEC_ULAW = 4,
	SKINNY_CODEC_G722 = 6,
	SKINNY_CODEC_G723_1 = 9,
	SKINNY_CODEC_G729A = 12,
	SKINNY_CODEC_G726_32 = 82, /* XXX Which packing order does this translate to? */
	SKINNY_CODEC_H261 = 100,
	SKINNY_CODEC_H263 = 101
};

#define DEFAULT_SKINNY_PORT 2000
#define DEFAULT_SKINNY_BACKLOG 2
#define SKINNY_MAX_PACKET 2000
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
static char immed_dialchar;
static char vmexten[AST_MAX_EXTENSION];      /* Voicemail pilot number */
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

#ifdef AST_DEVMODE
AST_THREADSTORAGE(message2str_threadbuf);
#define MESSAGE2STR_BUFSIZE   35
#endif

AST_THREADSTORAGE(device2str_threadbuf);
#define DEVICE2STR_BUFSIZE   15

AST_THREADSTORAGE(control2str_threadbuf);
#define CONTROL2STR_BUFSIZE   100

AST_THREADSTORAGE(substate2str_threadbuf);
#define SUBSTATE2STR_BUFSIZE   15

AST_THREADSTORAGE(callstate2str_threadbuf);
#define CALLSTATE2STR_BUFSIZE   15

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
	uint32_t space;
	uint8_t protocolVersion;
	/*! \brief space2 is used for newer version of skinny */
	char space2[3];
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
	uint32_t codec; /* skinny codec, not ast codec */
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
struct open_receive_channel_ack_message_ip4 {
	uint32_t status;
	uint32_t ipAddr;
	uint32_t port;
	uint32_t callReference;
};
struct open_receive_channel_ack_message_ip6 {
	uint32_t status;
	uint32_t space;
	char ipAddr[16];
	uint32_t port;
	uint32_t callReference;
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

#define SERVICEURL_STATREQ_MESSAGE 0x0033
struct serviceurl_statreq_message {
	uint32_t instance;
};

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
	uint32_t space;
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
	uint32_t packets;
	uint32_t bitRate;
};

struct start_media_transmission_message_ip4 {
	uint32_t conferenceId;
	uint32_t passThruPartyId;
	uint32_t remoteIp;
	uint32_t remotePort;
	uint32_t packetSize;
	uint32_t payloadType;
	struct media_qualifier qualifier;
	uint32_t space[19];
};

struct start_media_transmission_message_ip6 {
	uint32_t conferenceId;
	uint32_t passThruPartyId;
	uint32_t space;
	char remoteIp[16];
	uint32_t remotePort;
	uint32_t packetSize;
	uint32_t payloadType;
	struct media_qualifier qualifier;
	/*! \brief space2 is used for newer version of skinny */
	uint32_t space2[19];
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
#define STIMULUS_SERVICEURL 0x14
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
#define BT_SERVICEURL STIMULUS_SERVICEURL
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
struct clear_display_message {
	uint32_t space;
};

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
	uint32_t space[36];
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

#define BKSP_REQ_MESSAGE 0x0119
struct bksp_req_message {
	uint32_t instance;
	uint32_t callreference;
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
#define KEYDEF_SLAHOLD 11
#define KEYDEF_SLACONNECTEDNOTACTIVE 12
#define KEYDEF_RINGOUTWITHTRANS 13

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
#define SOFTKEY_FORCEDIAL 0x15

#define KEYMASK_ALL            0xFFFFFFFF
#define KEYMASK_NONE           (1 << 0)
#define KEYMASK_REDIAL         (1 << 1)
#define KEYMASK_NEWCALL        (1 << 2)
#define KEYMASK_HOLD           (1 << 3)
#define KEYMASK_TRNSFER        (1 << 4)
#define KEYMASK_CFWDALL        (1 << 5)
#define KEYMASK_CFWDBUSY       (1 << 6)
#define KEYMASK_CFWDNOANSWER   (1 << 7)
#define KEYMASK_BKSPC          (1 << 8)
#define KEYMASK_ENDCALL        (1 << 9)
#define KEYMASK_RESUME         (1 << 10)
#define KEYMASK_ANSWER         (1 << 11)
#define KEYMASK_INFO           (1 << 12)
#define KEYMASK_CONFRN         (1 << 13)
#define KEYMASK_PARK           (1 << 14)
#define KEYMASK_JOIN           (1 << 15)
#define KEYMASK_MEETME         (1 << 16)
#define KEYMASK_PICKUP         (1 << 17)
#define KEYMASK_GPICKUP        (1 << 18)
#define KEYMASK_DND            (1 << 29)
#define KEYMASK_IDIVERT        (1 << 20)
#define KEYMASK_FORCEDIAL      (1 << 21)

/* Localized message "codes" (in octal)
   Below is en_US (taken from a 7970) */

/*                            "\200\000"        ??? */
#define OCTAL_REDIAL          "\200\001"     /* Redial */
#define OCTAL_NEWCALL         "\200\002"     /* New Call */
#define OCTAL_HOLD            "\200\003"     /* Hold */
#define OCTAL_TRANSFER        "\200\004"     /* Transfer */
#define OCTAL_CFWDALL         "\200\005"     /* CFwdALL */
#define OCTAL_CFWDBUSY        "\200\006"     /* CFwdBusy */
#define OCTAL_CFWDNOAN        "\200\007"     /* CFwdNoAnswer */
#define OCTAL_BKSPC           "\200\010"     /* << */
#define OCTAL_ENDCALL         "\200\011"     /* EndCall */
#define OCTAL_RESUME          "\200\012"     /* Resume */
#define OCTAL_ANSWER          "\200\013"     /* Answer */
#define OCTAL_INFO            "\200\014"     /* Info */
#define OCTAL_CONFRN          "\200\015"     /* Confrn */
#define OCTAL_PARK            "\200\016"     /* Park */
#define OCTAL_JOIN            "\200\017"     /* Join */
#define OCTAL_MEETME          "\200\020"     /* MeetMe */
#define OCTAL_PICKUP          "\200\021"     /* PickUp */
#define OCTAL_GPICKUP         "\200\022"     /* GPickUp */
#define OCTAL_CUROPTS         "\200\023"     /* Your current options */
#define OCTAL_OFFHOOK         "\200\024"     /* Off Hook */
#define OCTAL_ONHOOK          "\200\025"     /* On Hook */
#define OCTAL_RINGOUT         "\200\026"     /* Ring out */
#define OCTAL_FROM            "\200\027"     /* From */
#define OCTAL_CONNECTED       "\200\030"     /* Connected */
#define OCTAL_BUSY            "\200\031"     /* Busy */
#define OCTAL_LINEINUSE       "\200\032"     /* Line In Use */
#define OCTAL_CALLWAITING     "\200\033"     /* Call Waiting */
#define OCTAL_CALLXFER        "\200\034"     /* Call Transfer */
#define OCTAL_CALLPARK        "\200\035"     /* Call Park */
#define OCTAL_CALLPROCEED     "\200\036"     /* Call Proceed */
#define OCTAL_INUSEREMOTE     "\200\037"     /* In Use Remote */
#define OCTAL_ENTRNUM         "\200\040"     /* Enter number */
#define OCTAL_PARKAT          "\200\041"     /* Call park At */
#define OCTAL_PRIMONLY        "\200\042"     /* Primary Only */
#define OCTAL_TMPFAIL         "\200\043"     /* Temp Fail */
#define OCTAL_HAVEVMAIL       "\200\044"     /* You Have VoiceMail */
#define OCTAL_FWDEDTO         "\200\045"     /* Forwarded to */
#define OCTAL_CANTCOMPCNF     "\200\046"     /* Can Not Complete Conference */
#define OCTAL_NOCONFBRDG      "\200\047"     /* No Conference Bridge */
#define OCTAL_NOPRIMARYCTL    "\200\050"     /* Can Not Hold Primary Control */
#define OCTAL_INVALCONFPART   "\200\051"     /* Invalid Conference Participant */
#define OCTAL_INCONFALREADY   "\200\052"     /* In Conference Already */
#define OCTAL_NOPARTINFO      "\200\053"     /* No Participant Info */
#define OCTAL_MAXPARTEXCEED   "\200\054"     /* Exceed Maximum Parties */
#define OCTAL_KEYNOTACTIVE    "\200\055"     /* Key Is Not Active */
#define OCTAL_ERRNOLIC        "\200\056"     /* Error No License */
#define OCTAL_ERRDBCFG        "\200\057"     /* Error DBConfig */
#define OCTAL_ERRDB           "\200\060"     /* Error Database */
#define OCTAL_ERRPASSLMT      "\200\061"     /* Error Pass Limit */
#define OCTAL_ERRUNK          "\200\062"     /* Error Unknown */
#define OCTAL_ERRMISMATCH     "\200\063"     /* Error Mismatch */
#define OCTAL_CONFERENCE      "\200\064"     /* Conference */
#define OCTAL_PARKNO          "\200\065"     /* Park Number */
#define OCTAL_PRIVATE         "\200\066"     /* Private */
#define OCTAL_INSUFBANDW      "\200\067"     /* Not Enough Bandwidth */
#define OCTAL_UNKNUM          "\200\070"     /* Unknown Number */
#define OCTAL_RMLSTC          "\200\071"     /* RmLstC */
#define OCTAL_VOICEMAIL       "\200\072"     /* Voicemail */
#define OCTAL_IMMDIV          "\200\073"     /* ImmDiv */
#define OCTAL_INTRCPT         "\200\074"     /* Intrcpt */
#define OCTAL_SETWTCH         "\200\075"     /* SetWtch */
#define OCTAL_TRNSFVM         "\200\076"     /* TrnsfVM */
#define OCTAL_DND             "\200\077"     /* DND */
#define OCTAL_DIVALL          "\200\100"     /* DivAll */
#define OCTAL_CALLBACK        "\200\101"     /* CallBack */
#define OCTAL_NETCNGREROUT    "\200\102"     /* Network congestion,rerouting */
#define OCTAL_BARGE           "\200\103"     /* Barge */
#define OCTAL_BARGEFAIL       "\200\104"     /* Failed to setup Barge */
#define OCTAL_BARGEEXIST      "\200\105"     /* Another Barge exists */
#define OCTAL_INCOMPATDEV     "\200\106"     /* Incompatible device type */
#define OCTAL_PARKNONUM       "\200\107"     /* No Park Number Available */
#define OCTAL_PARKREVERSION   "\200\110"     /* CallPark Reversion */
#define OCTAL_SRVNOTACTIVE    "\200\111"     /* Service is not Active */
#define OCTAL_HITRAFFIC       "\200\112"     /* High Traffic Try Again Later */
#define OCTAL_QRT             "\200\113"     /* QRT */
#define OCTAL_MCID            "\200\114"     /* MCID */
#define OCTAL_DIRTRFR         "\200\115"     /* DirTrfr */
#define OCTAL_SELECT          "\200\116"     /* Select */
#define OCTAL_CONFLIST        "\200\117"     /* ConfList */
#define OCTAL_IDIVERT         "\200\120"     /* iDivert */
#define OCTAL_CBARGE          "\200\121"     /* cBarge */
#define OCTAL_CANTCOMPLXFER   "\200\122"     /* Can Not Complete Transfer */
#define OCTAL_CANTJOINCALLS   "\200\123"     /* Can Not Join Calls */
#define OCTAL_MCIDSUCCESS     "\200\124"     /* Mcid Successful */
#define OCTAL_NUMNOTCFG       "\200\125"     /* Number Not Configured */
#define OCTAL_SECERROR        "\200\126"     /* Security Error */
#define OCTAL_VIDBANDWNA      "\200\127"     /* Video Bandwidth Unavailable */
#define OCTAL_VIDMODE         "\200\130"     /* VidMode */
#define OCTAL_CALLDURTIMEOUT  "\200\131"     /* Max Call Duration Timeout */
#define OCTAL_HOLDDURTIMEOUT  "\200\132"     /* Max Hold Duration Timeout */
#define OCTAL_OPICKUP         "\200\133"     /* OPickUp */
/*                            "\200\134"        ??? */
/*                            "\200\135"        ??? */
/*                            "\200\136"        ??? */
/*                            "\200\137"        ??? */
/*                            "\200\140"        ??? */
#define OCTAL_EXTXFERRESTRICT "\200\141"     /* External Transfer Restricted */
/*                            "\200\142"        ??? */
/*                            "\200\143"        ??? */
/*                            "\200\144"        ??? */
#define OCTAL_MACADD          "\200\145"     /* Mac Address */
#define OCTAL_HOST            "\200\146"     /* Host Name */
#define OCTAL_DOMAIN          "\200\147"     /* Domain Name */
#define OCTAL_IPADD           "\200\150"     /* IP Address */
#define OCTAL_SUBMASK         "\200\151"     /* Subnet Mask */
#define OCTAL_TFTP1           "\200\152"     /* TFTP Server 1 */
#define OCTAL_ROUTER1         "\200\153"     /* Default Router 1 */
#define OCTAL_ROUTER2         "\200\154"     /* Default Router 2 */
#define OCTAL_ROUTER3         "\200\155"     /* Default Router 3 */
#define OCTAL_ROUTER4         "\200\156"     /* Default Router 4 */
#define OCTAL_ROUTER5         "\200\157"     /* Default Router 5 */
#define OCTAL_DNS1            "\200\160"     /* DNS Server 1 */
#define OCTAL_DNS2            "\200\161"     /* DNS Server 2 */
#define OCTAL_DNS3            "\200\162"     /* DNS Server 3 */
#define OCTAL_DNS4            "\200\163"     /* DNS Server 4 */
#define OCTAL_DNS5            "\200\164"     /* DNS Server 5 */
#define OCTAL_VLANOPID        "\200\165"     /* Operational VLAN Id */
#define OCTAL_VLANADID        "\200\166"     /* Admin. VLAN Id */
#define OCTAL_CM1             "\200\167"     /* CallManager 1 */
#define OCTAL_CM2             "\200\170"     /* CallManager 2 */
#define OCTAL_CM3             "\200\171"     /* CallManager 3 */
#define OCTAL_CM4             "\200\172"     /* CallManager 4 */
#define OCTAL_CM5             "\200\173"     /* CallManager 5 */
#define OCTAL_URLINFO         "\200\174"     /* Information URL */
#define OCTAL_URLDIRS         "\200\175"     /* Directories URL */
#define OCTAL_URLMSGS         "\200\176"     /* Messages URL */
#define OCTAL_URLSRVS         "\200\177"     /* Services URL */

static struct soft_key_template_definition soft_key_template_default[] = {
	{ OCTAL_REDIAL, SOFTKEY_REDIAL },
	{ OCTAL_NEWCALL, SOFTKEY_NEWCALL },
	{ OCTAL_HOLD, SOFTKEY_HOLD },
	{ OCTAL_TRANSFER, SOFTKEY_TRNSFER },
	{ OCTAL_CFWDALL, SOFTKEY_CFWDALL },
	{ OCTAL_CFWDBUSY, SOFTKEY_CFWDBUSY },
	{ OCTAL_CFWDNOAN, SOFTKEY_CFWDNOANSWER },
	{ OCTAL_BKSPC, SOFTKEY_BKSPC },
	{ OCTAL_ENDCALL, SOFTKEY_ENDCALL },
	{ OCTAL_RESUME, SOFTKEY_RESUME },
	{ OCTAL_ANSWER, SOFTKEY_ANSWER },
	{ OCTAL_INFO, SOFTKEY_INFO },
	{ OCTAL_CONFRN, SOFTKEY_CONFRN },
	{ OCTAL_PARK, SOFTKEY_PARK },
	{ OCTAL_JOIN, SOFTKEY_JOIN },
	{ OCTAL_MEETME, SOFTKEY_MEETME },
	{ OCTAL_PICKUP, SOFTKEY_PICKUP },
	{ OCTAL_GPICKUP, SOFTKEY_GPICKUP },
	{ OCTAL_DND, SOFTKEY_DND },
	{ OCTAL_IDIVERT, SOFTKEY_IDIVERT },
	{ "Dial", SOFTKEY_FORCEDIAL},
};

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
	SOFTKEY_CFWDNOANSWER,
	SOFTKEY_DND,
	SOFTKEY_GPICKUP,
	/*SOFTKEY_CONFRN,*/
};

static const uint8_t soft_key_default_connected[] = {
	SOFTKEY_HOLD,
	SOFTKEY_ENDCALL,
	SOFTKEY_TRNSFER,
	SOFTKEY_PARK,
	SOFTKEY_CFWDALL,
	SOFTKEY_CFWDBUSY,
	SOFTKEY_CFWDNOANSWER,
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
	SOFTKEY_TRNSFER,
	SOFTKEY_CFWDALL,
	SOFTKEY_CFWDBUSY,
	SOFTKEY_CFWDNOANSWER,
	SOFTKEY_GPICKUP,
};

static const uint8_t soft_key_default_connwithtrans[] = {
	SOFTKEY_HOLD,
	SOFTKEY_ENDCALL,
	SOFTKEY_TRNSFER,
	SOFTKEY_PARK,
	SOFTKEY_CFWDALL,
	SOFTKEY_CFWDBUSY,
	SOFTKEY_CFWDNOANSWER,
};

static const uint8_t soft_key_default_dadfd[] = {
	SOFTKEY_BKSPC,
	SOFTKEY_ENDCALL,
	SOFTKEY_TRNSFER,
	SOFTKEY_FORCEDIAL,
};

static const uint8_t soft_key_default_connwithconf[] = {
	SOFTKEY_NONE,
};

static const uint8_t soft_key_default_ringout[] = {
	SOFTKEY_NONE,
	SOFTKEY_ENDCALL,
};

static const uint8_t soft_key_default_ringoutwithtransfer[] = {
	SOFTKEY_NONE,
	SOFTKEY_ENDCALL,
	SOFTKEY_TRNSFER,
};

static const uint8_t soft_key_default_offhookwithfeat[] = {
	SOFTKEY_REDIAL,
	SOFTKEY_ENDCALL,
	SOFTKEY_TRNSFER,
};

static const uint8_t soft_key_default_unknown[] = {
	SOFTKEY_NONE,
};

static const uint8_t soft_key_default_SLAhold[] = {
	SOFTKEY_REDIAL,
	SOFTKEY_NEWCALL,
	SOFTKEY_RESUME,
};

static const uint8_t soft_key_default_SLAconnectednotactive[] = {
	SOFTKEY_REDIAL,
	SOFTKEY_NEWCALL,
	SOFTKEY_JOIN,
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
	{KEYDEF_RINGOUTWITHTRANS, soft_key_default_ringoutwithtransfer, sizeof(soft_key_default_ringoutwithtransfer) / sizeof(uint8_t)},
	{KEYDEF_OFFHOOKWITHFEAT, soft_key_default_offhookwithfeat, sizeof(soft_key_default_offhookwithfeat) / sizeof(uint8_t)},
	{KEYDEF_UNKNOWN, soft_key_default_unknown, sizeof(soft_key_default_unknown) / sizeof(uint8_t)},
	{KEYDEF_SLAHOLD, soft_key_default_SLAhold, sizeof(soft_key_default_SLAhold) / sizeof(uint8_t)},
	{KEYDEF_SLACONNECTEDNOTACTIVE, soft_key_default_SLAconnectednotactive, sizeof(soft_key_default_SLAconnectednotactive) / sizeof(uint8_t)}
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

#define MAX_SERVICEURL 256
#define SERVICEURL_STAT_MESSAGE 0x012F
struct serviceurl_stat_message {
	uint32_t instance;
	char url[MAX_SERVICEURL];
	char displayName[40];
};

#define MAXCALLINFOSTR 256
#define MAXDISPLAYNOTIFYSTR 32

#define DISPLAY_PRINOTIFY_MESSAGE 0x0120
struct display_prinotify_message {
	uint32_t timeout;
	uint32_t priority;
	char text[MAXDISPLAYNOTIFYSTR];
};

#define CLEAR_PRINOTIFY_MESSAGE 0x0121
struct clear_prinotify_message {
	uint32_t priority;
};

#define CALL_INFO_MESSAGE_VARIABLE 0x014A
struct call_info_message_variable {
	uint32_t instance;
	uint32_t callreference;
	uint32_t calldirection;
	uint32_t unknown1;
	uint32_t unknown2;
	uint32_t unknown3;
	uint32_t unknown4;
	uint32_t unknown5;
	char calldetails[MAXCALLINFOSTR];
};

#define DISPLAY_PRINOTIFY_MESSAGE_VARIABLE 0x0144
struct display_prinotify_message_variable {
	uint32_t timeout;
	uint32_t priority;
	char text[MAXDISPLAYNOTIFYSTR];
};

#define DISPLAY_PROMPT_STATUS_MESSAGE_VARIABLE 0x0145
struct display_prompt_status_message_variable {
	uint32_t unknown;
	uint32_t lineInstance;
	uint32_t callReference;
	char promptMessage[MAXCALLINFOSTR];
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
	struct clear_display_message cleardisplay;
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
	struct start_media_transmission_message_ip4 startmedia_ip4;
	struct start_media_transmission_message_ip6 startmedia_ip6;
	struct stop_media_transmission_message stopmedia;
	struct open_receive_channel_message openreceivechannel;
	struct open_receive_channel_ack_message_ip4 openreceivechannelack_ip4;
	struct open_receive_channel_ack_message_ip6 openreceivechannelack_ip6;
	struct close_receive_channel_message closereceivechannel;
	struct display_notify_message displaynotify;
	struct dialed_number_message dialednumber;
	struct soft_key_event_message softkeyeventmessage;
	struct enbloc_call_message enbloccallmessage;
	struct forward_stat_message forwardstat;
	struct bksp_req_message bkspmessage;
	struct call_info_message_variable callinfomessagevariable;
	struct display_prompt_status_message_variable displaypromptstatusvar;
	struct serviceurl_stat_message serviceurlmessage;
	struct clear_prinotify_message clearprinotify;
	struct display_prinotify_message displayprinotify;
	struct display_prinotify_message_variable displayprinotifyvar;
};

/* packet composition */
struct skinny_req {
	uint32_t len;
	uint32_t res;
	uint32_t e;
	union skinny_data data;
};

/* XXX This is the combined size of the variables above.  (len, res, e)
   If more are added, this MUST change.
   (sizeof(skinny_req) - sizeof(skinny_data)) DOES NOT WORK on all systems (amd64?). */
static int skinny_header_size = 12;

/*****************************
 * Asterisk specific globals *
 *****************************/

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

#define SKINNY_INCOMING 1
#define SKINNY_OUTGOING 2

#define SKINNY_SILENCE 0x00		/* Note sure this is part of the protocol, remove? */
#define SKINNY_DIALTONE 0x21
#define SKINNY_BUSYTONE 0x23
#define SKINNY_ALERT 0x24
#define SKINNY_REORDER 0x25
#define SKINNY_CALLWAITTONE 0x2D
#define SKINNY_ZIPZIP 0x31
#define SKINNY_ZIP 0x32
#define SKINNY_BEEPBONK 0x33
#define SKINNY_BARGIN 0x43
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
static struct ast_sched_context *sched = NULL;

/* Protect the network socket */
AST_MUTEX_DEFINE_STATIC(netlock);

/* Wait up to 16 seconds for first digit */
static int firstdigittimeout = 16000;

/* How long to wait for following digits */
static int gendigittimeout = 8000;

/* How long to wait for an extra digit, if there is an ambiguous match */
static int matchdigittimeout = 3000;

/*!
 * To apease the stupid compiler option on ast_sched_del()
 * since we don't care about the return value.
 */
static int not_used;

#define SUBSTATE_UNSET 0
#define SUBSTATE_OFFHOOK 1
#define SUBSTATE_ONHOOK 2
#define SUBSTATE_RINGOUT 3
#define SUBSTATE_RINGIN 4
#define SUBSTATE_CONNECTED 5
#define SUBSTATE_BUSY 6
#define SUBSTATE_CONGESTION 7
#define SUBSTATE_HOLD 8
#define SUBSTATE_CALLWAIT 9
#define SUBSTATE_PROGRESS 12
#define SUBSTATE_DIALING 101

#define DIALTYPE_NORMAL      1<<0
#define DIALTYPE_CFWD        1<<1
#define DIALTYPE_XFER        1<<2

struct skinny_subchannel {
	struct ast_channel *owner;
	struct ast_rtp_instance *rtp;
	struct ast_rtp_instance *vrtp;
	unsigned int callid;
	char exten[AST_MAX_EXTENSION];
	/* time_t lastouttime; */ /* Unused */
	int progress;
	int ringing;
	/* int lastout; */ /* Unused */
	int cxmode;
	int nat;
	int calldirection;
	int blindxfer;
	int xferor;
	int substate;
	int aa_sched;
	int aa_beep;
	int aa_mute;
	int dialer_sched;
	int cfwd_sched;
	int dialType;
	int getforward;
	char *origtonum;
	char *origtoname;

	AST_LIST_ENTRY(skinny_subchannel) list;
	struct skinny_subchannel *related;
	struct skinny_line *line;
	struct skinny_subline *subline;
};

#define SKINNY_LINE_OPTIONS				\
	char name[80];					\
	char label[24];					\
	char accountcode[AST_MAX_ACCOUNT_CODE];		\
	char exten[AST_MAX_EXTENSION];			\
	char context[AST_MAX_CONTEXT];			\
	char language[MAX_LANGUAGE];			\
	char cid_num[AST_MAX_EXTENSION];		\
	char cid_name[AST_MAX_EXTENSION];		\
	char lastcallerid[AST_MAX_EXTENSION];		\
	int cfwdtype;					\
	char call_forward_all[AST_MAX_EXTENSION];	\
	char call_forward_busy[AST_MAX_EXTENSION];	\
	char call_forward_noanswer[AST_MAX_EXTENSION];	\
	char mailbox[AST_MAX_MAILBOX_UNIQUEID];		\
	char vmexten[AST_MAX_EXTENSION];		\
	char regexten[AST_MAX_EXTENSION];		\
	char regcontext[AST_MAX_CONTEXT];		\
	char parkinglot[AST_MAX_CONTEXT];		\
	char mohinterpret[MAX_MUSICCLASS];		\
	char mohsuggest[MAX_MUSICCLASS];		\
	char lastnumberdialed[AST_MAX_EXTENSION];	\
	char dialoutexten[AST_MAX_EXTENSION];		\
	char dialoutcontext[AST_MAX_CONTEXT];		\
	ast_group_t callgroup;				\
	ast_group_t pickupgroup;			\
	struct ast_namedgroups *named_callgroups;	\
	struct ast_namedgroups *named_pickupgroups;	\
	int callwaiting;				\
	int transfer;					\
	int threewaycalling;				\
	int mwiblink;					\
	int cancallforward;				\
	int callfwdtimeout;				\
	int dnd;					\
	int hidecallerid;				\
	int amaflags;					\
	int instance;					\
	int group;					\
	int nonCodecCapability;				\
	int immediate;					\
	int nat;					\
	int directmedia;				\
	int prune;

struct skinny_line {
	SKINNY_LINE_OPTIONS
	ast_mutex_t lock;
	struct skinny_container *container;
	struct stasis_subscription *mwi_event_sub; /* Event based MWI */
	struct skinny_subchannel *activesub;
	AST_LIST_HEAD(, skinny_subchannel) sub;
	AST_LIST_HEAD(, skinny_subline) sublines;
	AST_LIST_ENTRY(skinny_line) list;
	AST_LIST_ENTRY(skinny_line) all;
	struct skinny_device *device;
	struct ast_format_cap *cap;
	struct ast_format_cap *confcap;
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
	.callfwdtimeout = 20000,
	.prune = 0,
};
static struct skinny_line_options *default_line = &default_line_struct;

static AST_LIST_HEAD_STATIC(lines, skinny_line);

struct skinny_subline {
	struct skinny_container *container;
	struct skinny_line *line;
	struct skinny_subchannel *sub;
	AST_LIST_ENTRY(skinny_subline) list;
	char name[80];
	char context[AST_MAX_CONTEXT];
	char exten[AST_MAX_EXTENSION];
	char stname[AST_MAX_EXTENSION];
	char lnname[AST_MAX_EXTENSION];
	char ourName[40];
	char ourNum[24];
	char theirName[40];
	char theirNum[24];
	int calldirection;
	int substate;
	int extenstate;
	unsigned int callid;
};

struct skinny_speeddial {
	ast_mutex_t lock;
	struct skinny_container *container;
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

struct skinny_serviceurl {
	int instance;
	char url[MAX_SERVICEURL];
	char displayName[40];
	AST_LIST_ENTRY(skinny_serviceurl) list;
	struct skinny_device *device;
};

#define SKINNY_DEVICECONTAINER 1
#define SKINNY_LINECONTAINER 2
#define SKINNY_SUBLINECONTAINER 3
#define SKINNY_SDCONTAINER 4

struct skinny_container {
	int type;
	void *data;
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
	char vmexten[AST_MAX_EXTENSION];			\
	int type;						\
	int protocolversion;				\
	int hookstate;					\
	int lastlineinstance;					\
	int lastcallreference;					\
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
	struct ast_format_cap *cap;
	struct ast_format_cap *confcap;
	struct ast_endpoint *endpoint;
	AST_LIST_HEAD(, skinny_line) lines;
	AST_LIST_HEAD(, skinny_speeddial) speeddials;
	AST_LIST_HEAD(, skinny_serviceurl) serviceurls;
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
	.prune = 0,
	.hookstate = SKINNY_ONHOOK,
};
static struct skinny_device_options *default_device = &default_device_struct;

static AST_LIST_HEAD_STATIC(devices, skinny_device);

struct skinnysession {
	pthread_t t;
	ast_mutex_t lock;
	struct timeval start;
	struct sockaddr_in sin;
	int fd;
	char outbuf[SKINNY_MAX_PACKET];
	struct skinny_device *device;
	AST_LIST_ENTRY(skinnysession) list;
	int lockstate; /* Only for use in the skinny_session thread */
	int auth_timeout_sched;
	int keepalive_timeout_sched;
	struct timeval last_keepalive;
	int keepalive_count;
};

static struct ast_channel *skinny_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *dest, int *cause);
static AST_LIST_HEAD_STATIC(sessions, skinnysession);

static int skinny_devicestate(const char *data);
static int skinny_call(struct ast_channel *ast, const char *dest, int timeout);
static int skinny_hangup(struct ast_channel *ast);
static int skinny_answer(struct ast_channel *ast);
static struct ast_frame *skinny_read(struct ast_channel *ast);
static int skinny_write(struct ast_channel *ast, struct ast_frame *frame);
static int skinny_indicate(struct ast_channel *ast, int ind, const void *data, size_t datalen);
static int skinny_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static int skinny_senddigit_begin(struct ast_channel *ast, char digit);
static int skinny_senddigit_end(struct ast_channel *ast, char digit, unsigned int duration);
static void mwi_event_cb(void *userdata, struct stasis_subscription *sub, struct stasis_message *msg);
static int skinny_dialer_cb(const void *data);
static int skinny_reload(void);

static void skinny_set_owner(struct skinny_subchannel* sub, struct ast_channel* chan);
static void setsubstate(struct skinny_subchannel *sub, int state);
static void dumpsub(struct skinny_subchannel *sub, int forcehangup);
static void activatesub(struct skinny_subchannel *sub, int state);
static void dialandactivatesub(struct skinny_subchannel *sub, char exten[AST_MAX_EXTENSION]);
static int skinny_nokeepalive_cb(const void *data);

static void transmit_definetimedate(struct skinny_device *d);

static struct ast_channel_tech skinny_tech = {
	.type = "Skinny",
	.description = tdesc,
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
};

static int skinny_extensionstate_cb(const char *context, const char *exten, struct ast_state_cb_info *info, void *data);

static struct skinny_line *skinny_line_alloc(void)
{
	struct skinny_line *l;
	if (!(l = ast_calloc(1, sizeof(*l)))) {
		return NULL;
	}

	l->cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	l->confcap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!l->cap || !l->confcap) {
		ao2_cleanup(l->cap);
		ao2_cleanup(l->confcap);
		ast_free(l);
		return NULL;
	}
	return l;
}
static struct skinny_line *skinny_line_destroy(struct skinny_line *l)
{
	ao2_ref(l->cap, -1);
	ao2_ref(l->confcap, -1);
	l->named_callgroups = ast_unref_namedgroups(l->named_callgroups);
	l->named_pickupgroups = ast_unref_namedgroups(l->named_pickupgroups);
	ast_free(l->container);
	ast_free(l);
	return NULL;
}
static struct skinny_device *skinny_device_alloc(const char *dname)
{
	struct skinny_device *d;
	if (!(d = ast_calloc(1, sizeof(*d)))) {
		return NULL;
	}

	d->cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	d->confcap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	d->endpoint = ast_endpoint_create("Skinny", dname);
	if (!d->cap || !d->confcap || !d->endpoint) {
		ao2_cleanup(d->cap);
		ao2_cleanup(d->confcap);
		ast_free(d);
		return NULL;
	}

	ast_copy_string(d->name, dname, sizeof(d->name));

	return d;
}
static struct skinny_device *skinny_device_destroy(struct skinny_device *d)
{
	ao2_ref(d->cap, -1);
	ao2_ref(d->confcap, -1);
	ast_endpoint_shutdown(d->endpoint);
	ast_free(d);
	return NULL;
}

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
		} else
			continue;

		/* Found the device (or we don't care which device) */
		AST_LIST_TRAVERSE(&d->lines, l, list){
			/* Search for the right line */
			if (!strcasecmp(l->name, line)) {
				if (tmpl) {
					ast_log(LOG_WARNING, "Ambiguous line name: %s\n", line);
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

static struct skinny_subline *find_subline_by_name(const char *dest)
{
	struct skinny_line *l;
	struct skinny_subline *subline;
	struct skinny_subline *tmpsubline = NULL;
	struct skinny_device *d;

	AST_LIST_LOCK(&devices);
	AST_LIST_TRAVERSE(&devices, d, list){
		AST_LIST_TRAVERSE(&d->lines, l, list){
			AST_LIST_TRAVERSE(&l->sublines, subline, list){
				if (!strcasecmp(subline->name, dest)) {
					if (tmpsubline) {
						ast_verb(2, "Ambiguous subline name: %s\n", dest);
						AST_LIST_UNLOCK(&devices);
						return NULL;
					} else
						tmpsubline = subline;
				}
			}
		}
	}
	AST_LIST_UNLOCK(&devices);
	return tmpsubline;
}

static struct skinny_subline *find_subline_by_callid(struct skinny_device *d, int callid)
{
	struct skinny_subline *subline;
	struct skinny_line *l;

	AST_LIST_TRAVERSE(&d->lines, l, list){
		AST_LIST_TRAVERSE(&l->sublines, subline, list){
			if (subline->callid == callid) {
				return subline;
			}
		}
	}
	return NULL;
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

static void skinny_locksub(struct skinny_subchannel *sub)
{
	if (sub && sub->owner) {
		ast_channel_lock(sub->owner);
	}
}

static void skinny_unlocksub(struct skinny_subchannel *sub)
{
	if (sub && sub->owner) {
		ast_channel_unlock(sub->owner);
	}
}

static int skinny_sched_del(int sched_id, struct skinny_subchannel *sub)
{
	SKINNY_DEBUG(DEBUG_SUB, 3, "Sub %u - Deleting SCHED %d\n",
		sub->callid, sched_id);
	return ast_sched_del(sched, sched_id);
}

static int skinny_sched_add(int when, ast_sched_cb callback, struct skinny_subchannel *sub)
{
	int ret;
	ret = ast_sched_add(sched, when, callback, sub);
	SKINNY_DEBUG(DEBUG_SUB, 3, "Sub %u - Added SCHED %d\n",
		sub->callid, ret);
	return ret;
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

static struct ast_format *codec_skinny2ast(enum skinny_codecs skinnycodec)
{
	switch (skinnycodec) {
	case SKINNY_CODEC_ALAW:
		return ast_format_alaw;
	case SKINNY_CODEC_ULAW:
		return ast_format_ulaw;
	case SKINNY_CODEC_G722:
		return ast_format_g722;
	case SKINNY_CODEC_G723_1:
		return ast_format_g723;
	case SKINNY_CODEC_G729A:
		return ast_format_g729;
	case SKINNY_CODEC_G726_32:
		return ast_format_g726; /* XXX Is this right? */
	case SKINNY_CODEC_H261:
		return ast_format_h261;
	case SKINNY_CODEC_H263:
		return ast_format_h263;
	default:
		return ast_format_none;
	}
}

static int codec_ast2skinny(const struct ast_format *astcodec)
{
	if (ast_format_cmp(astcodec, ast_format_alaw) == AST_FORMAT_CMP_EQUAL) {
		return SKINNY_CODEC_ALAW;
	} else if (ast_format_cmp(astcodec, ast_format_ulaw) == AST_FORMAT_CMP_EQUAL) {
		return SKINNY_CODEC_ULAW;
	} else if (ast_format_cmp(astcodec, ast_format_g722) == AST_FORMAT_CMP_EQUAL) {
		return SKINNY_CODEC_G722;
	} else if (ast_format_cmp(astcodec, ast_format_g723) == AST_FORMAT_CMP_EQUAL) {
		return SKINNY_CODEC_G723_1;
	} else if (ast_format_cmp(astcodec, ast_format_g729) == AST_FORMAT_CMP_EQUAL) {
		return SKINNY_CODEC_G729A;
	} else if (ast_format_cmp(astcodec, ast_format_g726) == AST_FORMAT_CMP_EQUAL) {
		return SKINNY_CODEC_G726_32;
	} else if (ast_format_cmp(astcodec, ast_format_h261) == AST_FORMAT_CMP_EQUAL) {
		return SKINNY_CODEC_H261;
	} else if (ast_format_cmp(astcodec, ast_format_h263) == AST_FORMAT_CMP_EQUAL) {
		return SKINNY_CODEC_H263;
	} else {
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
		stalecontext = NULL;
		ast_copy_string(newlist, new, sizeof(newlist));
		stringp = newlist;
		while ((newcontext = strsep(&stringp, "&"))) {
			if (strcmp(newcontext, oldcontext) == 0) {
				/* This is not the context you're looking for */
				stalecontext = NULL;
				break;
			} else if (strcmp(newcontext, oldcontext)) {
				stalecontext = oldcontext;
			}

		}
		ast_context_destroy_by_name(stalecontext, "Skinny");
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
	struct skinny_subline *subline;
	struct skinny_speeddial *sd;
	struct sockaddr_in sin;
	socklen_t slen;
	int instance;
	int res = -1;

	if (-1 < s->auth_timeout_sched) {
		not_used = ast_sched_del(sched, s->auth_timeout_sched);
		s->auth_timeout_sched = -1;
	}

	AST_LIST_LOCK(&devices);
	AST_LIST_TRAVERSE(&devices, d, list){
		struct ast_sockaddr addr;
		ast_sockaddr_from_sin(&addr, &s->sin);
		if (!strcasecmp(req->data.reg.name, d->id)
				&& ast_apply_ha(d->ha, &addr)) {
			RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);

			if (d->session) {
				ast_log(LOG_WARNING, "Device already registered.\n");
				transmit_definetimedate(d);
				res = 0;
				break;
			}
			s->device = d;
			d->type = letohl(req->data.reg.type);
			d->protocolversion = letohl(req->data.reg.protocolVersion);
			if (ast_strlen_zero(d->version_id)) {
				ast_copy_string(d->version_id, version_id, sizeof(d->version_id));
			}
			d->session = s;

			slen = sizeof(sin);
			if (getsockname(s->fd, (struct sockaddr *)&sin, &slen)) {
				ast_log(LOG_WARNING, "Cannot get socket name\n");
				sin.sin_addr = __ourip;
			}
			d->ourip = sin.sin_addr;

			AST_LIST_TRAVERSE(&d->speeddials, sd, list) {
				sd->stateid = ast_extension_state_add(sd->context, sd->exten, skinny_extensionstate_cb, sd->container);
			}
			instance = 0;
			AST_LIST_TRAVERSE(&d->lines, l, list) {
				instance++;
			}
			AST_LIST_TRAVERSE(&d->lines, l, list) {
				ast_format_cap_get_compatible(l->confcap, d->cap, l->cap);
				/* l->capability = d->capability; */
				l->instance = instance;
				l->newmsgs = ast_app_has_voicemail(l->mailbox, NULL);
				set_callforwards(l, NULL, SKINNY_CFWD_ALL|SKINNY_CFWD_BUSY|SKINNY_CFWD_NOANSWER);
				register_exten(l);
				/* initialize MWI on line and device */
				mwi_event_cb(l, NULL, NULL);
				AST_LIST_TRAVERSE(&l->sublines, subline, list) {
					ast_extension_state_add(subline->context, subline->exten, skinny_extensionstate_cb, subline->container);
				}
				ast_devstate_changed(AST_DEVICE_NOT_INUSE, AST_DEVSTATE_CACHABLE, "Skinny/%s", l->name);
				--instance;
			}
			ast_endpoint_set_state(d->endpoint, AST_ENDPOINT_ONLINE);
			blob = ast_json_pack("{s: s}", "peer_status", "Registered");
			ast_endpoint_blob_publish(d->endpoint, ast_endpoint_state_type(), blob);
			res = 1;
			break;
		}
	}
	AST_LIST_UNLOCK(&devices);
	return res;
}

static void end_session(struct skinnysession *s)
{
	pthread_cancel(s->t);
}

#ifdef AST_DEVMODE
static char *callstate2str(int ind)
{
	char *tmp;

	switch (ind) {
	case SKINNY_OFFHOOK:
		return "SKINNY_OFFHOOK";
	case SKINNY_ONHOOK:
		return "SKINNY_ONHOOK";
	case SKINNY_RINGOUT:
		return "SKINNY_RINGOUT";
	case SKINNY_RINGIN:
		return "SKINNY_RINGIN";
	case SKINNY_CONNECTED:
		return "SKINNY_CONNECTED";
	case SKINNY_BUSY:
		return "SKINNY_BUSY";
	case SKINNY_CONGESTION:
		return "SKINNY_CONGESTION";
	case SKINNY_PROGRESS:
		return "SKINNY_PROGRESS";
	case SKINNY_HOLD:
		return "SKINNY_HOLD";
	case SKINNY_CALLWAIT:
		return "SKINNY_CALLWAIT";
	default:
		if (!(tmp = ast_threadstorage_get(&callstate2str_threadbuf, CALLSTATE2STR_BUFSIZE)))
                        return "Unknown";
		snprintf(tmp, CALLSTATE2STR_BUFSIZE, "UNKNOWN-%d", ind);
		return tmp;
	}
}

#endif

static int transmit_response_bysession(struct skinnysession *s, struct skinny_req *req)
{
	int res = 0;
	unsigned long len;

	if (!s) {
		ast_log(LOG_WARNING, "Asked to transmit to a non-existent session!\n");
		return -1;
	}

	ast_mutex_lock(&s->lock);

	/* Don't optimize out assigning letohl() to len. It is necessary to guarantee that the comparison will always catch invalid values.
	 * letohl() may or may not return a signed value depending upon which definition is used. */
	len = letohl(req->len);
	if (SKINNY_MAX_PACKET < len) {
		ast_log(LOG_WARNING, "transmit_response: the length of the request (%u) is out of bounds (%d)\n", letohl(req->len), SKINNY_MAX_PACKET);
		ast_mutex_unlock(&s->lock);
		return -1;
	}

	memset(s->outbuf, 0, sizeof(s->outbuf));
	memcpy(s->outbuf, req, skinny_header_size);
	memcpy(s->outbuf+skinny_header_size, &req->data, letohl(req->len));

	res = write(s->fd, s->outbuf, letohl(req->len)+8);

	if (res != letohl(req->len)+8) {
		ast_log(LOG_WARNING, "Transmit: write only sent %d out of %u bytes: %s\n", res, letohl(req->len)+8, strerror(errno));
		if (res == -1) {
			ast_log(LOG_WARNING, "Transmit: Skinny Client was lost, unregistering\n");
			end_session(s);
		}

	}

	ast_free(req);
	ast_mutex_unlock(&s->lock);
	return 1;
}

static void transmit_response(struct skinny_device *d, struct skinny_req *req)
{
	transmit_response_bysession(d->session, req);
}

static void transmit_registerrej(struct skinnysession *s)
{
	struct skinny_req *req;
	char name[16];

	if (!(req = req_alloc(sizeof(struct register_rej_message), REGISTER_REJ_MESSAGE)))
		return;

	memcpy(&name, req->data.reg.name, sizeof(name));
	snprintf(req->data.regrej.errMsg, sizeof(req->data.regrej.errMsg), "No Authority: %s", name);

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting REGISTER_REJ_MESSAGE to UNKNOWN_DEVICE\n");
	transmit_response_bysession(s, req);
}

static void transmit_speaker_mode(struct skinny_device *d, int mode)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct set_speaker_message), SET_SPEAKER_MESSAGE)))
		return;

	req->data.setspeaker.mode = htolel(mode);

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting SET_SPEAKER_MESSAGE to %s, mode %d\n", d->name, mode);
	transmit_response(d, req);
}

static void transmit_microphone_mode(struct skinny_device *d, int mode)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct set_microphone_message), SET_MICROPHONE_MESSAGE)))
		return;

	req->data.setmicrophone.mode = htolel(mode);

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting SET_MICROPHONE_MESSAGE to %s, mode %d\n", d->name, mode);
	transmit_response(d, req);
}

//static void transmit_callinfo(struct skinny_subchannel *sub)
static void transmit_callinfo(struct skinny_device *d, int instance, int callid,
	char *fromname, char *fromnum, char *toname, char *tonum, int calldirection, char *origtonum, char *origtoname)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct call_info_message), CALL_INFO_MESSAGE)))
		return;

	ast_copy_string(req->data.callinfo.callingPartyName, fromname, sizeof(req->data.callinfo.callingPartyName));
	ast_copy_string(req->data.callinfo.callingParty, fromnum, sizeof(req->data.callinfo.callingParty));
	ast_copy_string(req->data.callinfo.calledPartyName, toname, sizeof(req->data.callinfo.calledPartyName));
	ast_copy_string(req->data.callinfo.calledParty, tonum, sizeof(req->data.callinfo.calledParty));
	if (origtoname) {
		ast_copy_string(req->data.callinfo.originalCalledPartyName, origtoname, sizeof(req->data.callinfo.originalCalledPartyName));
	}
	if (origtonum) {
		ast_copy_string(req->data.callinfo.originalCalledParty, origtonum, sizeof(req->data.callinfo.originalCalledParty));
	}

	req->data.callinfo.instance = htolel(instance);
	req->data.callinfo.reference = htolel(callid);
	req->data.callinfo.type = htolel(calldirection);

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting CALL_INFO_MESSAGE to %s, to %s(%s) from %s(%s), origto %s(%s) (dir=%d) on %s(%d)\n",
		d->name, toname, tonum, fromname, fromnum, origtoname, origtonum, calldirection, d->name, instance);
	transmit_response(d, req);
}

static void transmit_callinfo_variable(struct skinny_device *d, int instance, int callreference,
	char *fromname, char *fromnum, char *toname, char *tonum, int calldirection, char *origtonum, char *origtoname)
{
	struct skinny_req *req;
	char *strptr;
	char *thestrings[13];
	int i;
	int callinfostrleft = MAXCALLINFOSTR;

	if (!(req = req_alloc(sizeof(struct call_info_message_variable), CALL_INFO_MESSAGE_VARIABLE)))
		return;

	req->data.callinfomessagevariable.instance = htolel(instance);
	req->data.callinfomessagevariable.callreference = htolel(callreference);
	req->data.callinfomessagevariable.calldirection = htolel(calldirection);

	req->data.callinfomessagevariable.unknown1 = htolel(0x00);
	req->data.callinfomessagevariable.unknown2 = htolel(0x00);
	req->data.callinfomessagevariable.unknown3 = htolel(0x00);
	req->data.callinfomessagevariable.unknown4 = htolel(0x00);
	req->data.callinfomessagevariable.unknown5 = htolel(0x00);

	thestrings[0] = fromnum;
	thestrings[1] = "";                     /* Appears to be origfrom */
	if (calldirection == SKINNY_OUTGOING) {
		thestrings[2] = tonum;
		thestrings[3] = origtonum;
	} else {
		thestrings[2] = "";
		thestrings[3] = "";
	}
	thestrings[4] = "";
	thestrings[5] = "";
	thestrings[6] = "";
	thestrings[7] = "";

	thestrings[8] = "";
	thestrings[9] = fromname;
	thestrings[10] = toname;
	thestrings[11] = origtoname;
	thestrings[12] = "";

	strptr = req->data.callinfomessagevariable.calldetails;

	for(i = 0; i < 13; i++) {
		if (thestrings[i]) {
			ast_copy_string(strptr, thestrings[i], callinfostrleft);
			strptr += strlen(thestrings[i]) + 1;
			callinfostrleft -= strlen(thestrings[i]) + 1;
		} else {
			ast_copy_string(strptr, "", callinfostrleft);
			strptr++;
			callinfostrleft--;
		}
	}

	req->len = req->len - (callinfostrleft & ~0x3);

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting CALL_INFO_MESSAGE_VARIABLE to %s, to %s(%s) from %s(%s), origto %s(%s) (dir=%d) on %s(%d)\n",
		d->name, toname, tonum, fromname, fromnum, origtoname, origtonum, calldirection, d->name, instance);
	transmit_response(d, req);
}

static void send_callinfo(struct skinny_subchannel *sub)
{
	struct ast_channel *ast;
	struct skinny_device *d;
	struct skinny_line *l;
	struct ast_party_id connected_id;
	char *fromname;
	char *fromnum;
	char *toname;
	char *tonum;

	if (!sub || !sub->owner || !sub->line || !sub->line->device) {
		return;
	}

	ast = sub->owner;
	l = sub->line;
	d = l->device;
	connected_id = ast_channel_connected_effective_id(ast);

	if (sub->calldirection == SKINNY_INCOMING) {
		if ((ast_party_id_presentation(&connected_id) & AST_PRES_RESTRICTION) == AST_PRES_ALLOWED) {
			fromname = S_COR(connected_id.name.valid, connected_id.name.str, "");
			fromnum = S_COR(connected_id.number.valid, connected_id.number.str, "");
		} else {
			fromname = "";
			fromnum = "";
		}
		toname = S_COR(ast_channel_caller(ast)->id.name.valid, ast_channel_caller(ast)->id.name.str, "");
		tonum = S_COR(ast_channel_caller(ast)->id.number.valid, ast_channel_caller(ast)->id.number.str, "");
	} else if (sub->calldirection == SKINNY_OUTGOING) {
		fromname = S_COR(ast_channel_caller(ast)->id.name.valid, ast_channel_caller(ast)->id.name.str, "");
		fromnum = S_COR(ast_channel_caller(ast)->id.number.valid, ast_channel_caller(ast)->id.number.str, "");
		toname = S_COR(ast_channel_connected(ast)->id.name.valid, ast_channel_connected(ast)->id.name.str, "");
		tonum = S_COR(ast_channel_connected(ast)->id.number.valid, ast_channel_connected(ast)->id.number.str, l->lastnumberdialed);
	} else {
		ast_verb(1, "Error sending Callinfo to %s(%d) - No call direction in sub\n", d->name, l->instance);
		return;
	}

	if (d->protocolversion < 17) {
		transmit_callinfo(d, l->instance, sub->callid, fromname, fromnum, toname, tonum, sub->calldirection, sub->origtonum, sub->origtoname);
	} else {
		transmit_callinfo_variable(d, l->instance, sub->callid, fromname, fromnum, toname, tonum, sub->calldirection, sub->origtonum, sub->origtoname);
	}
}

static void push_callinfo(struct skinny_subline *subline, struct skinny_subchannel *sub)
{
	struct ast_channel *ast;
	struct skinny_device *d;
	struct skinny_line *l;
	struct ast_party_id connected_id;
	char *fromname;
	char *fromnum;
	char *toname;
	char *tonum;

	if (!sub || !sub->owner || !sub->line || !sub->line->device) {
		return;
	}

	ast = sub->owner;
	l = sub->line;
	d = l->device;
	connected_id = ast_channel_connected_effective_id(ast);

	if (sub->calldirection == SKINNY_INCOMING) {
		if((ast_party_id_presentation(&connected_id) & AST_PRES_RESTRICTION) == AST_PRES_ALLOWED) {
			fromname = S_COR(connected_id.name.valid, connected_id.name.str, "");
			fromnum = S_COR(connected_id.number.valid, connected_id.number.str, "");
		} else {
			fromname = "";
			fromnum = "";
		}
		toname = S_COR(ast_channel_caller(ast)->id.name.valid, ast_channel_caller(ast)->id.name.str, "");
		tonum = S_COR(ast_channel_caller(ast)->id.number.valid, ast_channel_caller(ast)->id.number.str, "");
	} else if (sub->calldirection == SKINNY_OUTGOING) {
		fromname = S_COR(ast_channel_caller(ast)->id.name.valid, ast_channel_caller(ast)->id.name.str, "");
		fromnum = S_COR(ast_channel_caller(ast)->id.number.valid, ast_channel_caller(ast)->id.number.str, "");
		toname = S_COR(ast_channel_connected(ast)->id.name.valid, ast_channel_connected(ast)->id.name.str, "");
		tonum = S_COR(ast_channel_connected(ast)->id.number.valid, ast_channel_connected(ast)->id.number.str, l->lastnumberdialed);
	} else {
		ast_verb(1, "Error sending Callinfo to %s(%d) - No call direction in sub\n", d->name, l->instance);
		return;
	}

	if (d->protocolversion < 17) {
		transmit_callinfo(subline->line->device, subline->line->instance, subline->callid, fromname, fromnum, toname, tonum, sub->calldirection, sub->origtonum, sub->origtoname);
	} else {
		transmit_callinfo_variable(subline->line->device, subline->line->instance, subline->callid, fromname, fromnum, toname, tonum, sub->calldirection, sub->origtonum, sub->origtoname);
	}
}

static void transmit_connect(struct skinny_device *d, struct skinny_subchannel *sub)
{
	struct skinny_req *req;
	struct skinny_line *l = sub->line;
	struct ast_format *tmpfmt;
	unsigned int framing;

	if (!(req = req_alloc(sizeof(struct open_receive_channel_message), OPEN_RECEIVE_CHANNEL_MESSAGE)))
		return;

	tmpfmt = ast_format_cap_get_format(l->cap, 0);
	framing = ast_format_cap_get_format_framing(l->cap, tmpfmt);

	req->data.openreceivechannel.conferenceId = htolel(sub->callid);
	req->data.openreceivechannel.partyId = htolel(sub->callid);
	req->data.openreceivechannel.packets = htolel(framing);
	req->data.openreceivechannel.capability = htolel(codec_ast2skinny(tmpfmt));
	req->data.openreceivechannel.echo = htolel(0);
	req->data.openreceivechannel.bitrate = htolel(0);

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting OPEN_RECEIVE_CHANNEL_MESSAGE to %s, confid %u, partyid %u, ms %u, fmt %d, echo %d, brate %d\n",
		d->name, sub->callid, sub->callid, framing, codec_ast2skinny(tmpfmt), 0, 0);

	ao2_ref(tmpfmt, -1);

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

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting START_TONE_MESSAGE to %s, tone %d, inst %d, ref %d\n",
		d->name, tone, instance, reference);
	transmit_response(d, req);
}

static void transmit_stop_tone(struct skinny_device *d, int instance, int reference)
{
	struct skinny_req *req;
	if (!(req = req_alloc(sizeof(struct stop_tone_message), STOP_TONE_MESSAGE)))
		return;
	req->data.stoptone.instance = htolel(instance);
	req->data.stoptone.reference = htolel(reference);

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting STOP_TONE_MESSAGE to %s, inst %d, ref %d\n",
		d->name, instance, reference);
	transmit_response(d, req);
}

static int keyset_translatebitmask(int keyset, int intmask)
{
	int extmask = 0;
	int x, y;
	const struct soft_key_definitions *softkeymode = soft_key_default_definitions;

	for(x = 0; x < ARRAY_LEN(soft_key_default_definitions); x++) {
		if (softkeymode[x].mode == keyset) {
			const uint8_t *defaults = softkeymode[x].defaults;

			for (y = 0; y < softkeymode[x].count; y++) {
				if (intmask & (1 << (defaults[y]))) {
					extmask |= (1 << ((y)));
				}
			}
			break;
		}
	}

	return extmask;
}

static void transmit_selectsoftkeys(struct skinny_device *d, int instance, int callid, int softkey, int mask)
{
	struct skinny_req *req;
	int newmask;

	if (!(req = req_alloc(sizeof(struct select_soft_keys_message), SELECT_SOFT_KEYS_MESSAGE)))
		return;

	newmask = keyset_translatebitmask(softkey, mask);
	req->data.selectsoftkey.instance = htolel(instance);
	req->data.selectsoftkey.reference = htolel(callid);
	req->data.selectsoftkey.softKeySetIndex = htolel(softkey);
	req->data.selectsoftkey.validKeyMask = htolel(newmask);

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting SELECT_SOFT_KEYS_MESSAGE to %s, inst %d, callid %d, softkey %d, mask 0x%08x\n",
		d->name, instance, callid, softkey, (unsigned)newmask);
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

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting SET_LAMP_MESSAGE to %s, stim %d, inst %d, ind %d\n",
		d->name, stimulus, instance, indication);
	transmit_response(d, req);
}

static void transmit_ringer_mode(struct skinny_device *d, int mode)
{
	struct skinny_req *req;

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

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting SET_RINGER_MESSAGE to %s, mode %d, unk1 1, unk2 1\n",
		d->name, mode);
	transmit_response(d, req);
}

static void transmit_clear_display_message(struct skinny_device *d, int instance, int reference)
{
	struct skinny_req *req;
	if (!(req = req_alloc(sizeof(struct clear_display_message), CLEAR_DISPLAY_MESSAGE)))
		return;

	//what do we want hear CLEAR_DISPLAY_MESSAGE or CLEAR_PROMPT_STATUS???
	//if we are clearing the display, it appears there is no instance and refernece info (size 0)
	//req->data.clearpromptstatus.lineInstance = instance;
	//req->data.clearpromptstatus.callReference = reference;

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting CLEAR_DISPLAY_MESSAGE to %s\n", d->name);
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
	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting DISPLAYTEXT_MESSAGE to %s, text %s\n", d->name, text);
	transmit_response(d, req);
} */

static void transmit_displaynotify(struct skinny_device *d, const char *text, int t)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct display_notify_message), DISPLAY_NOTIFY_MESSAGE)))
		return;

	ast_copy_string(req->data.displaynotify.displayMessage, text, sizeof(req->data.displaynotify.displayMessage));
	req->data.displaynotify.displayTimeout = htolel(t);

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting DISPLAY_NOTIFY_MESSAGE to %s, text %s\n", d->name, text);
	transmit_response(d, req);
}

static void transmit_clearprinotify(struct skinny_device *d, int priority)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct clear_prinotify_message), CLEAR_PRINOTIFY_MESSAGE)))
		return;

	req->data.clearprinotify.priority = htolel(priority);

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting CLEAR_PRINOTIFY_MESSAGE to %s, priority %d\n", d->name, priority);
	transmit_response(d, req);
}

static void _transmit_displayprinotify(struct skinny_device *d, const char *text, const char *extratext, int timeout, int priority)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct display_prinotify_message), DISPLAY_PRINOTIFY_MESSAGE)))
		return;

	req->data.displayprinotify.timeout = htolel(timeout);
	req->data.displayprinotify.priority = htolel(priority);

	if ((char)*text == '\200') {
		int octalstrlen = strlen(text);
		ast_copy_string(req->data.displayprinotify.text, text, sizeof(req->data.displayprinotify.text));
		ast_copy_string(req->data.displayprinotify.text+octalstrlen, extratext, sizeof(req->data.displayprinotify.text)-octalstrlen);
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting DISPLAY_PRINOTIFY_MESSAGE to %s, '\\%03o\\%03o', '%s', timeout=%d, priority=%d\n",
			d->name, (unsigned)*text, (unsigned)*(text+1), extratext, timeout, priority);
	} else {
		ast_copy_string(req->data.displayprinotify.text, text, sizeof(req->data.displayprinotify.text));
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting DISPLAY_PRINOTIFY_MESSAGE to %s, '%s', timeout=%d, priority=%d\n",
			d->name, text, timeout, priority);
	}

	transmit_response(d, req);
}

static void _transmit_displayprinotifyvar(struct skinny_device *d, const char *text, const char *extratext, int timeout, int priority)
{
	struct skinny_req *req;
	int packetlen;

	if (!(req = req_alloc(sizeof(struct display_prinotify_message_variable), DISPLAY_PRINOTIFY_MESSAGE_VARIABLE)))
		return;

	req->data.displayprinotifyvar.timeout = htolel(timeout);
	req->data.displayprinotifyvar.priority = htolel(priority);

	if ((char)*text == '\200') {
		int octalstrlen = strlen(text);
		ast_copy_string(req->data.displayprinotifyvar.text, text, sizeof(req->data.displayprinotifyvar.text));
		ast_copy_string(req->data.displayprinotifyvar.text+octalstrlen, extratext, sizeof(req->data.displayprinotifyvar.text)-octalstrlen);
		packetlen = req->len - MAXDISPLAYNOTIFYSTR + strlen(text) + strlen(extratext);
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting DISPLAY_PRINOTIFY_MESSAGE_VARIABLE to %s, '\\%03o\\%03o', '%s', timeout=%d, priority=%d\n",
			d->name, (unsigned)*text, (unsigned)*(text+1), extratext, timeout, priority);
	} else {
		ast_copy_string(req->data.displayprinotifyvar.text, text, sizeof(req->data.displayprinotifyvar.text));
		packetlen = req->len - MAXDISPLAYNOTIFYSTR + strlen(text);
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting DISPLAY_PRINOTIFY_MESSAGE_VARIABLE to %s, '%s', timeout=%d, priority=%d\n",
			d->name, text, timeout, priority);
	}

	req->len = (packetlen & ~0x3) + 4;

	transmit_response(d, req);
}

static void send_displayprinotify(struct skinny_device *d, const char *text, const char *extratext, int timeout, int priority)
{
	if (d->protocolversion < 17) {
		_transmit_displayprinotify(d, text, extratext, timeout, priority);
	} else {
		_transmit_displayprinotifyvar(d, text, extratext, timeout, priority);
	}
}

static void transmit_displaypromptstatus(struct skinny_device *d, const char *text, const char *extratext, int t, int instance, int callid)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct display_prompt_status_message), DISPLAY_PROMPT_STATUS_MESSAGE)))
		return;

	req->data.displaypromptstatus.messageTimeout = htolel(t);
	req->data.displaypromptstatus.lineInstance = htolel(instance);
	req->data.displaypromptstatus.callReference = htolel(callid);

	if ((char)*text == '\200') {
		int octalstrlen = strlen(text);
		ast_copy_string(req->data.displaypromptstatus.promptMessage, text, sizeof(req->data.displaypromptstatusvar.promptMessage));
		ast_copy_string(req->data.displaypromptstatus.promptMessage+octalstrlen, extratext, sizeof(req->data.displaypromptstatus.promptMessage)-octalstrlen);
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting DISPLAY_PROMPT_STATUS_MESSAGE to %s, '\\%03o\\%03o', '%s'\n",
			d->name, (unsigned)*text, (unsigned)*(text+1), extratext);
	} else {
		ast_copy_string(req->data.displaypromptstatus.promptMessage, text, sizeof(req->data.displaypromptstatus.promptMessage));
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting DISPLAY_PROMPT_STATUS_MESSAGE to %s, '%s'\n",
			d->name, text);
	}
	transmit_response(d, req);
}

static void transmit_displaypromptstatusvar(struct skinny_device *d, const char *text, const char *extratext, int t, int instance, int callid)
{
	struct skinny_req *req;
	int packetlen;

	if (!(req = req_alloc(sizeof(struct display_prompt_status_message_variable), DISPLAY_PROMPT_STATUS_MESSAGE_VARIABLE)))
		return;

	req->data.displaypromptstatusvar.lineInstance = htolel(instance);
	req->data.displaypromptstatusvar.callReference = htolel(callid);

	if ((char)*text == '\200') {
		int octalstrlen = strlen(text);
		ast_copy_string(req->data.displaypromptstatusvar.promptMessage, text, sizeof(req->data.displaypromptstatusvar.promptMessage));
		ast_copy_string(req->data.displaypromptstatusvar.promptMessage+octalstrlen, extratext, sizeof(req->data.displaypromptstatusvar.promptMessage)-octalstrlen);
		packetlen = req->len - MAXCALLINFOSTR + strlen(text) + strlen(extratext);
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting DISPLAY_PROMPT_STATUS_MESSAGE_VARIABLE to %s, '\\%03o\\%03o', '%s'\n",
			d->name, (unsigned)*text, (unsigned)*(text+1), extratext);
	} else {
		ast_copy_string(req->data.displaypromptstatusvar.promptMessage, text, sizeof(req->data.displaypromptstatus.promptMessage));
		packetlen = req->len - MAXCALLINFOSTR + strlen(text);
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting DISPLAY_PROMPT_STATUS_MESSAGE_VARIABLE to %s, '%s'\n",
			d->name, text);
	}
	req->len = (packetlen & ~0x3) + 4;

	transmit_response(d, req);
}

static void send_displaypromptstatus(struct skinny_device *d, const char *text, const char *extratext, int t, int instance, int callid)
{
	if (d->protocolversion < 17) {
		transmit_displaypromptstatus(d, text, extratext, t, instance, callid);
	} else {
		transmit_displaypromptstatusvar(d, text, extratext, t, instance, callid);
	}
}

static void transmit_clearpromptmessage(struct skinny_device *d, int instance, int callid)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct clear_prompt_message), CLEAR_PROMPT_MESSAGE)))
		return;

	req->data.clearpromptstatus.lineInstance = htolel(instance);
	req->data.clearpromptstatus.callReference = htolel(callid);

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting CLEAR_PROMPT_MESSAGE to %s, inst %d, callid %d\n",
		d->name, instance, callid);
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

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting DIALED_NUMBER_MESSAGE to %s, num %s, inst %d, callid %d\n",
		d->name, text, instance, callid);
	transmit_response(d, req);
}

static void transmit_closereceivechannel(struct skinny_device *d, struct skinny_subchannel *sub)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct close_receive_channel_message), CLOSE_RECEIVE_CHANNEL_MESSAGE)))
		return;

	req->data.closereceivechannel.conferenceId = htolel(0);
	req->data.closereceivechannel.partyId = htolel(sub->callid);

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting CLOSE_RECEIVE_CHANNEL_MESSAGE to %s, confid %d, callid %u\n",
		d->name, 0, sub->callid);
	transmit_response(d, req);
}

static void transmit_stopmediatransmission(struct skinny_device *d, struct skinny_subchannel *sub)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct stop_media_transmission_message), STOP_MEDIA_TRANSMISSION_MESSAGE)))
		return;

	req->data.stopmedia.conferenceId = htolel(0);
	req->data.stopmedia.passThruPartyId = htolel(sub->callid);

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting STOP_MEDIA_TRANSMISSION_MESSAGE to %s, confid %d, passthrupartyid %u\n",
		d->name, 0, sub->callid);
	transmit_response(d, req);
}

static void transmit_startmediatransmission(struct skinny_device *d, struct skinny_subchannel *sub, struct sockaddr_in dest,
	struct ast_format *format, unsigned int framing)
{
	struct skinny_req *req;

	if (d->protocolversion < 17) {
		if (!(req = req_alloc(sizeof(struct start_media_transmission_message_ip4), START_MEDIA_TRANSMISSION_MESSAGE)))
			return;
		req->data.startmedia_ip4.conferenceId = htolel(sub->callid);
		req->data.startmedia_ip4.passThruPartyId = htolel(sub->callid);
		req->data.startmedia_ip4.remoteIp = dest.sin_addr.s_addr;
		req->data.startmedia_ip4.remotePort = htolel(ntohs(dest.sin_port));
		req->data.startmedia_ip4.packetSize = htolel(framing);
		req->data.startmedia_ip4.payloadType = htolel(codec_ast2skinny(format));
		req->data.startmedia_ip4.qualifier.precedence = htolel(127);
		req->data.startmedia_ip4.qualifier.vad = htolel(0);
		req->data.startmedia_ip4.qualifier.packets = htolel(0);
		req->data.startmedia_ip4.qualifier.bitRate = htolel(0);
	} else {
		if (!(req = req_alloc(sizeof(struct start_media_transmission_message_ip6), START_MEDIA_TRANSMISSION_MESSAGE)))
			return;
		req->data.startmedia_ip6.conferenceId = htolel(sub->callid);
		req->data.startmedia_ip6.passThruPartyId = htolel(sub->callid);
		memcpy(req->data.startmedia_ip6.remoteIp, &dest.sin_addr.s_addr, sizeof(dest.sin_addr.s_addr));
		req->data.startmedia_ip6.remotePort = htolel(ntohs(dest.sin_port));
		req->data.startmedia_ip6.packetSize = htolel(framing);
		req->data.startmedia_ip6.payloadType = htolel(codec_ast2skinny(format));
		req->data.startmedia_ip6.qualifier.precedence = htolel(127);
		req->data.startmedia_ip6.qualifier.vad = htolel(0);
		req->data.startmedia_ip6.qualifier.packets = htolel(0);
		req->data.startmedia_ip6.qualifier.bitRate = htolel(0);
	}

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting START_MEDIA_TRANSMISSION_MESSAGE to %s, callid %u, passthrupartyid %u, ip %s:%d, ms %u, fmt %d, prec 127\n",
		d->name, sub->callid, sub->callid, ast_inet_ntoa(dest.sin_addr), dest.sin_port, framing, codec_ast2skinny(format));
	transmit_response(d, req);
}

static void transmit_activatecallplane(struct skinny_device *d, struct skinny_line *l)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct activate_call_plane_message), ACTIVATE_CALL_PLANE_MESSAGE)))
		return;

	req->data.activatecallplane.lineInstance = htolel(l->instance);

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting ACTIVATE_CALL_PLANE_MESSAGE to %s, inst %d\n",
		d->name, l->instance);
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

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting CALL_STATE_MESSAGE to %s, state %s, inst %d, callid %u\n",
		d->name, callstate2str(state), buttonInstance, callid);
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

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting FORWARD_STAT_MESSAGE to %s, inst %d, all %s, busy %s, noans %s, acitve %d\n",
		d->name, l->instance, l->call_forward_all, l->call_forward_busy, l->call_forward_noanswer, anyon ? 7 : 0);
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

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting SPEED_DIAL_STAT_RES_MESSAGE to %s, inst %d, dir %s, display %s\n",
		d->name, sd->instance, sd->exten, sd->label);
	transmit_response(d, req);
}

//static void transmit_linestatres(struct skinny_device *d, struct skinny_line *l)
static void transmit_linestatres(struct skinny_device *d, int instance)
{
	struct skinny_req *req;
	struct skinny_line *l;
	struct skinny_speeddial *sd;

	if (!(req = req_alloc(sizeof(struct line_stat_res_message), LINE_STAT_RES_MESSAGE)))
		return;

	if ((l = find_line_by_instance(d, instance))) {
		req->data.linestat.lineNumber = letohl(l->instance);
		memcpy(req->data.linestat.lineDirNumber, l->name, sizeof(req->data.linestat.lineDirNumber));
		memcpy(req->data.linestat.lineDisplayName, l->label, sizeof(req->data.linestat.lineDisplayName));
	} else if ((sd = find_speeddial_by_instance(d, instance, 1))) {
		req->data.linestat.lineNumber = letohl(sd->instance);
		memcpy(req->data.linestat.lineDirNumber, sd->label, sizeof(req->data.linestat.lineDirNumber));
		memcpy(req->data.linestat.lineDisplayName, sd->label, sizeof(req->data.linestat.lineDisplayName));
	}

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting LINE_STAT_RES_MESSAGE to %s, inst %d, num %s, label %s\n",
		d->name, l->instance, req->data.linestat.lineDirNumber, req->data.linestat.lineDisplayName);
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

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting DEFINETIMEDATE_MESSAGE to %s, date %u %u %u dow %u time %u:%u:%u.%u\n",
		d->name, req->data.definetimedate.year, req->data.definetimedate.month, req->data.definetimedate.day, req->data.definetimedate.dayofweek,
		req->data.definetimedate.hour, req->data.definetimedate.minute, req->data.definetimedate.seconds, req->data.definetimedate.milliseconds);
	transmit_response(d, req);
}

static void transmit_versionres(struct skinny_device *d)
{
	struct skinny_req *req;
	if (!(req = req_alloc(sizeof(struct version_res_message), VERSION_RES_MESSAGE)))
		return;

	ast_copy_string(req->data.version.version, d->version_id, sizeof(req->data.version.version));

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting VERSION_RES_MESSAGE to %s, version %s\n", d->name, d->version_id);
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

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting SERVER_RES_MESSAGE to %s, srvname %s %s:%d\n",
		d->name, ourhost, ast_inet_ntoa(d->ourip), ourport);
	transmit_response(d, req);
}

static void transmit_softkeysetres(struct skinny_device *d)
{
	struct skinny_req *req;
	int i;
	int x;
	int y;
	int keydefcount;
	const struct soft_key_definitions *softkeymode = soft_key_default_definitions;

	if (!(req = req_alloc(sizeof(struct soft_key_set_res_message), SOFT_KEY_SET_RES_MESSAGE)))
		return;

	SKINNY_DEBUG(DEBUG_TEMPLATE, 3, "Creating Softkey Template\n");

	keydefcount = ARRAY_LEN(soft_key_default_definitions);
	req->data.softkeysets.softKeySetOffset = htolel(0);
	req->data.softkeysets.softKeySetCount = htolel(keydefcount);
	req->data.softkeysets.totalSoftKeySetCount = htolel(keydefcount);
	for (x = 0; x < keydefcount; x++) {
		const uint8_t *defaults = softkeymode->defaults;
		/* XXX I wanted to get the size of the array dynamically, but that wasn't wanting to work.
		   This will have to do for now. */
		for (y = 0; y < softkeymode->count; y++) {
			for (i = 0; i < (sizeof(soft_key_template_default) / sizeof(struct soft_key_template_definition)); i++) {
				if (defaults[y] == i+1) {
					req->data.softkeysets.softKeySetDefinition[softkeymode->mode].softKeyTemplateIndex[y] = (i+1);
					req->data.softkeysets.softKeySetDefinition[softkeymode->mode].softKeyInfoIndex[y] = htoles(i+301);
					SKINNY_DEBUG(DEBUG_TEMPLATE, 4, "softKeySetDefinition : softKeyTemplateIndex: %d softKeyInfoIndex: %d\n",
						i+1, i+301);
				}
			}
		}
		softkeymode++;
	}

	SKINNY_DEBUG(DEBUG_PACKET | DEBUG_TEMPLATE, 3, "Transmitting SOFT_KEY_SET_RES_MESSAGE to %s, template data\n",
		d->name);
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

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting SOFT_KEY_TEMPLATE_RES_MESSAGE to %s, offset 0, keycnt %u, totalkeycnt %u, template data\n",
		d->name, req->data.softkeytemplate.softKeyCount, req->data.softkeytemplate.totalSoftKeyCount);
	transmit_response(d, req);
}

static void transmit_reset(struct skinny_device *d, int fullrestart)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct reset_message), RESET_MESSAGE)))
		return;

	if (fullrestart)
		req->data.reset.resetType = 2;
	else
		req->data.reset.resetType = 1;

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting RESET_MESSAGE to %s, type %s\n",
		d->name, (fullrestart) ? "Restarting" : "Resetting");
	transmit_response(d, req);
}

static void transmit_keepaliveack(struct skinnysession *s)
{
	struct skinny_req *req;

	if (!(req = req_alloc(0, KEEP_ALIVE_ACK_MESSAGE)))
		return;

#ifdef AST_DEVMODE
	{
	struct skinny_device *d = s->device;
	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting KEEP_ALIVE_ACK_MESSAGE to %s\n", (d ? d->name : "unregistered"));
	}
#endif
	transmit_response_bysession(s, req);
}

static void transmit_registerack(struct skinny_device *d)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct register_ack_message), REGISTER_ACK_MESSAGE)))
		return;

	req->data.regack.res[0] = '0';
	req->data.regack.res[1] = '\0';
	req->data.regack.keepAlive = htolel(keep_alive);
	memcpy(req->data.regack.dateTemplate, date_format, sizeof(req->data.regack.dateTemplate));
	req->data.regack.res2[0] = '0';
	req->data.regack.res2[1] = '\0';
	req->data.regack.secondaryKeepAlive = htolel(keep_alive);

#ifdef AST_DEVMODE
	{
	short res = req->data.regack.res[0] << 8 | req->data.regack.res[1];
	unsigned int res2 = req->data.regack.res2[0] << 24 | req->data.regack.res2[1] << 16 | req->data.regack.res2[2] << 8 | req->data.regack.res2[3];
	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting REGISTER_ACK_MESSAGE to %s, keepalive %d, datetemplate %s, seckeepalive %d, res 0x%04x, res2 0x%08x\n",
		d->name, keep_alive, date_format, keep_alive, (unsigned)res, res2);
	}
#endif

	transmit_response(d, req);
}

static void transmit_capabilitiesreq(struct skinny_device *d)
{
	struct skinny_req *req;

	if (!(req = req_alloc(0, CAPABILITIES_REQ_MESSAGE)))
		return;

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting CAPABILITIES_REQ_MESSAGE to %s\n", d->name);
	transmit_response(d, req);
}

static void transmit_backspace(struct skinny_device *d, int instance, unsigned callid)
{
	struct skinny_req *req;

	if (!(req = req_alloc(sizeof(struct bksp_req_message), BKSP_REQ_MESSAGE)))
		return;

	req->data.bkspmessage.instance = htolel(instance);
	req->data.bkspmessage.callreference = htolel(callid);

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting BKSP_REQ_MESSAGE to %s, inst %d, callid %u \n",
		d->name, instance, callid);
	transmit_response(d, req);
}

static void transmit_serviceurlstat(struct skinny_device *d, int instance)
{
	struct skinny_req *req;
	struct skinny_serviceurl *surl;

	if (!(req = req_alloc(sizeof(struct serviceurl_stat_message), SERVICEURL_STAT_MESSAGE)))
		return;

	AST_LIST_TRAVERSE(&d->serviceurls, surl, list) {
		if (surl->instance == instance) {
			break;
		}
	}

	if (surl) {
		memcpy(req->data.serviceurlmessage.displayName, surl->displayName, sizeof(req->data.serviceurlmessage.displayName));
		memcpy(req->data.serviceurlmessage.url, surl->url, sizeof(req->data.serviceurlmessage.url));
	}
	req->data.serviceurlmessage.instance = htolel(instance);

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Transmitting SERVICEURL_STAT_MESSAGE to %s, inst %d\n",
		d->name, instance);
	transmit_response(d, req);
}

static int skinny_extensionstate_cb(const char *context, const char *exten, struct ast_state_cb_info *info, void *data)
{
	struct skinny_container *container = data;
	struct skinny_device *d = NULL;
	char hint[AST_MAX_EXTENSION];
	int state = info->exten_state;

	/* only interested in device state here */
	if (info->reason != AST_HINT_UPDATE_DEVICE) {
		return 0;
	}

	if (container->type == SKINNY_SDCONTAINER) {
		struct skinny_speeddial *sd = container->data;
		d = sd->parent;

		SKINNY_DEBUG(DEBUG_HINT, 3, "Got hint %s on speeddial %s\n", ast_extension_state2str(state), sd->label);

		if (ast_get_hint(hint, sizeof(hint), NULL, 0, NULL, sd->context, sd->exten)) {
			/* If they are not registered, we will override notification and show no availability */
			if (ast_device_state(hint) == AST_DEVICE_UNAVAILABLE) {
				transmit_lamp_indication(d, STIMULUS_LINE, sd->instance, SKINNY_LAMP_FLASH);
				transmit_callstate(d, sd->instance, 0, SKINNY_ONHOOK);
				return 0;
			}
			switch (state) {
			case AST_EXTENSION_DEACTIVATED: /* Retry after a while */
			case AST_EXTENSION_REMOVED:     /* Extension is gone */
				SKINNY_DEBUG(DEBUG_HINT, 3, "Extension state: Watcher for hint %s %s. Notify Device %s\n",
					exten, state == AST_EXTENSION_DEACTIVATED ? "deactivated" : "removed", d->name);
				sd->stateid = -1;
				transmit_lamp_indication(d, STIMULUS_LINE, sd->instance, SKINNY_LAMP_OFF);
				transmit_callstate(d, sd->instance, 0, SKINNY_ONHOOK);
				break;
			case AST_EXTENSION_RINGING:
			case AST_EXTENSION_UNAVAILABLE:
				transmit_lamp_indication(d, STIMULUS_LINE, sd->instance, SKINNY_LAMP_BLINK);
				transmit_callstate(d, sd->instance, 0, SKINNY_RINGIN);
				break;
			case AST_EXTENSION_BUSY: /* callstate = SKINNY_BUSY wasn't wanting to work - I'll settle for this */
			case AST_EXTENSION_INUSE:
				transmit_lamp_indication(d, STIMULUS_LINE, sd->instance, SKINNY_LAMP_ON);
				transmit_callstate(d, sd->instance, 0, SKINNY_CALLREMOTEMULTILINE);
				break;
			case AST_EXTENSION_ONHOLD:
				transmit_lamp_indication(d, STIMULUS_LINE, sd->instance, SKINNY_LAMP_WINK);
				transmit_callstate(d, sd->instance, 0, SKINNY_HOLD);
				break;
			case AST_EXTENSION_NOT_INUSE:
			default:
				transmit_lamp_indication(d, STIMULUS_LINE, sd->instance, SKINNY_LAMP_OFF);
				transmit_callstate(d, sd->instance, 0, SKINNY_ONHOOK);
				break;
			}
		}
		sd->laststate = state;
	} else if (container->type == SKINNY_SUBLINECONTAINER) {
		struct skinny_subline *subline = container->data;
		struct skinny_line *l = subline->line;
		d = l->device;

		SKINNY_DEBUG(DEBUG_HINT, 3, "Got hint %s on subline %s (%s@%s)\n", ast_extension_state2str(state), subline->name, exten, context);

		subline->extenstate = state;

		if (subline->callid == 0) {
			return 0;
		}

		switch (state) {
		case AST_EXTENSION_RINGING: /* Handled by normal ringin */
			break;
		case AST_EXTENSION_INUSE:
			if (subline->sub && (subline->sub->substate == SKINNY_CONNECTED)) { /* Device has a real call */
				transmit_callstate(d, l->instance, subline->callid, SKINNY_CONNECTED);
				transmit_selectsoftkeys(d, l->instance, subline->callid, KEYDEF_CONNECTED, KEYMASK_ALL);
				send_displaypromptstatus(d, OCTAL_CONNECTED, "", 0, l->instance, subline->callid);
			} else { /* Some other device has active call */
				transmit_callstate(d, l->instance, subline->callid, SKINNY_CALLREMOTEMULTILINE);
				transmit_selectsoftkeys(d, l->instance, subline->callid, KEYDEF_SLACONNECTEDNOTACTIVE, KEYMASK_ALL);
				send_displaypromptstatus(d, "In Use", "", 0, l->instance, subline->callid);
			}
			transmit_lamp_indication(d, STIMULUS_LINE, l->instance, SKINNY_LAMP_ON);
			transmit_ringer_mode(d, SKINNY_RING_OFF);
			transmit_activatecallplane(d, l);
			break;
		case AST_EXTENSION_ONHOLD:
			transmit_callstate(d, l->instance, subline->callid, SKINNY_HOLD);
			transmit_selectsoftkeys(d, l->instance, subline->callid, KEYDEF_SLAHOLD, KEYMASK_ALL);
			send_displaypromptstatus(d, "Hold", "", 0, l->instance, subline->callid);
			transmit_lamp_indication(d, STIMULUS_LINE, l->instance, SKINNY_LAMP_BLINK);
			transmit_activatecallplane(d, l);
			break;
		case AST_EXTENSION_NOT_INUSE:
			transmit_callstate(d, l->instance, subline->callid, SKINNY_ONHOOK);
			transmit_selectsoftkeys(d, l->instance, subline->callid, KEYDEF_ONHOOK, KEYMASK_ALL);
			transmit_clearpromptmessage(d, l->instance, subline->callid);
			transmit_lamp_indication(d, STIMULUS_LINE, l->instance, SKINNY_LAMP_OFF);
			transmit_activatecallplane(d, l);
			subline->callid = 0;
			break;
		default:
			ast_log(LOG_WARNING, "AST_EXTENSION_STATE %s not configured\n", ast_extension_state2str(state));
		}
	} else {
		ast_log(LOG_WARNING, "Invalid data supplied to skinny_extensionstate_cb\n");
	}

	return 0;
}

static void update_connectedline(struct skinny_subchannel *sub, const void *data, size_t datalen)
{
	struct ast_channel *c = sub->owner;
	struct skinny_line *l = sub->line;
	struct skinny_device *d = l->device;

	if (!d->session) {
		return;
	}

	if (sub->calldirection == SKINNY_OUTGOING && !sub->origtonum) {
		/* Do not set origtonum before here or origtoname won't be set */
		sub->origtonum = ast_strdup(sub->exten);
		if (ast_channel_connected(c)->id.name.valid) {
			sub->origtoname = ast_strdup(ast_channel_connected(c)->id.name.str);
		}
	}

	if (!ast_channel_caller(c)->id.number.valid
		|| ast_strlen_zero(ast_channel_caller(c)->id.number.str)
		|| !ast_channel_connected(c)->id.number.valid
		|| ast_strlen_zero(ast_channel_connected(c)->id.number.str))
		return;

	SKINNY_DEBUG(DEBUG_SUB, 3, "Sub %u - Updating\n", sub->callid);

	send_callinfo(sub);
}

static void mwi_event_cb(void *userdata, struct stasis_subscription *sub, struct stasis_message *msg)
{
	struct skinny_line *l = userdata;
	struct skinny_device *d = l->device;
	struct skinny_line *l2;
	int dev_msgs = 0;

	if (!d || !d->session) {
		return;
	}

	if (msg && ast_mwi_state_type() == stasis_message_type(msg)) {
		struct ast_mwi_state *mwi_state = stasis_message_data(msg);
		l->newmsgs = mwi_state->new_msgs;
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
	ast_verb(3, "Skinny mwi_event_cb found %d new messages\n", l->newmsgs);
}

/* I do not believe skinny can deal with video.
   Anyone know differently? */
/* Yes, it can.  Currently 7985 and Cisco VT Advantage do video. */
static enum ast_rtp_glue_result skinny_get_vrtp_peer(struct ast_channel *c, struct ast_rtp_instance **instance)
{
	struct skinny_subchannel *sub = NULL;

	if (!(sub = ast_channel_tech_pvt(c)) || !(sub->vrtp))
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

	SKINNY_DEBUG(DEBUG_AUDIO, 4, "skinny_get_rtp_peer() Channel = %s\n", ast_channel_name(c));

	if (!(sub = ast_channel_tech_pvt(c)))
		return AST_RTP_GLUE_RESULT_FORBID;

	skinny_locksub(sub);

	if (!(sub->rtp)){
		skinny_unlocksub(sub);
		return AST_RTP_GLUE_RESULT_FORBID;
	}

	ao2_ref(sub->rtp, +1);
	*instance = sub->rtp;

	l = sub->line;

	if (!l->directmedia || l->nat){
		res = AST_RTP_GLUE_RESULT_LOCAL;
		SKINNY_DEBUG(DEBUG_AUDIO, 4, "skinny_get_rtp_peer() Using AST_RTP_GLUE_RESULT_LOCAL \n");
	}

	skinny_unlocksub(sub);

	return res;

}

static int skinny_set_rtp_peer(struct ast_channel *c, struct ast_rtp_instance *rtp, struct ast_rtp_instance *vrtp, struct ast_rtp_instance *trtp, const struct ast_format_cap *codecs, int nat_active)
{
	struct skinny_subchannel *sub;
	struct skinny_line *l;
	struct skinny_device *d;
	struct sockaddr_in us = { 0, };
	struct sockaddr_in them = { 0, };
	struct ast_sockaddr them_tmp;
	struct ast_sockaddr us_tmp;

	sub = ast_channel_tech_pvt(c);

	if (ast_channel_state(c) != AST_STATE_UP)
		return 0;

	if (!sub) {
		return -1;
	}

	l = sub->line;
	d = l->device;

	if (rtp){
		struct ast_format *tmpfmt;
		unsigned int framing;
		ast_rtp_instance_get_remote_address(rtp, &them_tmp);
		ast_sockaddr_to_sin(&them_tmp, &them);

		/* Shutdown any early-media or previous media on re-invite */
		transmit_stopmediatransmission(d, sub);

		SKINNY_DEBUG(DEBUG_AUDIO, 4, "Peerip = %s:%d\n", ast_inet_ntoa(them.sin_addr), ntohs(them.sin_port));

		tmpfmt = ast_format_cap_get_format(l->cap, 0);
		framing = ast_format_cap_get_format_framing(l->cap, tmpfmt);

		SKINNY_DEBUG(DEBUG_AUDIO, 4, "Setting payloadType to '%s' (%u ms)\n", ast_format_get_name(tmpfmt), framing);

		if (!(l->directmedia) || (l->nat)){
			ast_rtp_instance_get_local_address(rtp, &us_tmp);
			ast_sockaddr_to_sin(&us_tmp, &us);
			us.sin_addr.s_addr = us.sin_addr.s_addr ? us.sin_addr.s_addr : d->ourip.s_addr;
			transmit_startmediatransmission(d, sub, us, tmpfmt, framing);
		} else {
			transmit_startmediatransmission(d, sub, them, tmpfmt, framing);
		}

		ao2_ref(tmpfmt, -1);

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

#ifdef AST_DEVMODE
static char *skinny_debugs(void)
{
	char *ptr;
	int posn = 0;

	ptr = dbgcli_buf;
	ptr[0] = '\0';
	if (skinnydebug & DEBUG_GENERAL) {
		strcpy(ptr, "general "); /* SAFE */
		posn += 8;
		ptr += 8;
	}
	if (skinnydebug & DEBUG_SUB) {
		strcpy(ptr, "sub "); /* SAFE */
		posn += 4;
		ptr += 4;
	}
	if (skinnydebug & DEBUG_AUDIO) {
		strcpy(ptr, "audio "); /* SAFE */
		posn += 6;
		ptr += 6;
	}
	if (skinnydebug & DEBUG_PACKET) {
		strcpy(ptr, "packet "); /* SAFE */
		posn += 7;
		ptr += 7;
	}
	if (skinnydebug & DEBUG_LOCK) {
		strcpy(ptr, "lock "); /* SAFE */
		posn += 5;
		ptr += 5;
	}
	if (skinnydebug & DEBUG_TEMPLATE) {
		strcpy(ptr, "template "); /* SAFE */
		posn += 9;
		ptr += 9;
	}
	if (skinnydebug & DEBUG_THREAD) {
		strcpy(ptr, "thread "); /* SAFE */
		posn += 7;
		ptr += 7;
	}
	if (skinnydebug & DEBUG_HINT) {
		strcpy(ptr, "hint "); /* SAFE */
		posn += 5;
		ptr += 5;
	}
	if (skinnydebug & DEBUG_KEEPALIVE) {
		strcpy(ptr, "keepalive "); /* SAFE */
		posn += 10;
		ptr += 10;
	}
	if (posn > 0) {
		strncpy(--ptr, "\0", 1);
	}
	return dbgcli_buf;
}

static char *complete_skinny_debug(const char *line, const char *word, int pos, int state)
{
	const char *debugOpts[]={ "all","audio","hint","keepalive","lock","off","packet","show","sub","template","thread",NULL };
	char *wordptr = (char *)word;
	char buf[32];
	char *bufptr = buf;
	int buflen = sizeof(buf);
	int wordlen;
	int which = 0;
	int i = 0;

	if (*word == '+' || *word == '-' || *word == '!') {
		*bufptr = *word;
		wordptr++;
		bufptr++;
		buflen--;
	}
	wordlen = strlen(wordptr);

	while (debugOpts[i]) {
		if (!strncasecmp(wordptr, debugOpts[i], wordlen) && ++which > state) {
			ast_copy_string(bufptr, debugOpts[i], buflen);
			return ast_strdup(buf);
		}
		i++;
	}

	return NULL;
}

static char *handle_skinny_set_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int i;
	int result = 0;
	const char *arg;
	int bitmask;
	int negate;

	switch (cmd) {
	case CLI_INIT:
		e->command = "skinny debug";
		e->usage =
			"Usage: skinny debug {audio|hint|keepalive|lock|off|packet|show|sub|template|thread}\n"
			"       Enables/Disables various Skinny debugging messages\n";
		return NULL;
	case CLI_GENERATE:
		return complete_skinny_debug(a->line, a->word, a->pos, a->n);

	}

	if (a->argc < 3)
		return CLI_SHOWUSAGE;

	if (a->argc == 3 && !strncasecmp(a->argv[e->args-1], "show", 4)) {
		ast_cli(a->fd, "Skinny Debugging - %s\n", skinny_debugs());
		return CLI_SUCCESS;
	}

	for(i = e->args; i < a->argc; i++) {
		result++;
		arg = a->argv[i];

		if (!strncasecmp(arg, "off", 3)) {
			skinnydebug = 0;
			continue;
		}

		if (!strncasecmp(arg, "all", 3)) {
			skinnydebug = DEBUG_GENERAL|DEBUG_SUB|DEBUG_PACKET|DEBUG_AUDIO|DEBUG_LOCK|DEBUG_TEMPLATE|DEBUG_THREAD|DEBUG_HINT|DEBUG_KEEPALIVE;
			continue;
		}

		if (!strncasecmp(arg, "-", 1) || !strncasecmp(arg, "!", 1)) {
			negate = 1;
			arg++;
		} else if (!strncasecmp(arg, "+", 1)) {
			negate = 0;
			arg++;
		} else {
			negate = 0;
		}

		if (!strncasecmp(arg, "general", 7)) {
			bitmask = DEBUG_GENERAL;
		} else if (!strncasecmp(arg, "sub", 3)) {
			bitmask = DEBUG_SUB;
		} else if (!strncasecmp(arg, "packet", 6)) {
			bitmask = DEBUG_PACKET;
		} else if (!strncasecmp(arg, "audio", 5)) {
			bitmask = DEBUG_AUDIO;
		} else if (!strncasecmp(arg, "lock", 4)) {
			bitmask = DEBUG_LOCK;
		} else if (!strncasecmp(arg, "template", 8)) {
			bitmask = DEBUG_TEMPLATE;
		} else if (!strncasecmp(arg, "thread", 6)) {
			bitmask = DEBUG_THREAD;
		} else if (!strncasecmp(arg, "hint", 4)) {
			bitmask = DEBUG_HINT;
		} else if (!strncasecmp(arg, "keepalive", 9)) {
			bitmask = DEBUG_KEEPALIVE;
		} else {
			ast_cli(a->fd, "Skinny Debugging - option '%s' unknown\n", a->argv[i]);
			result--;
			continue;
		}

		if (negate) {
			skinnydebug &= ~bitmask;
		} else {
			skinnydebug |= bitmask;
		}
	}
	if (result) {
		ast_cli(a->fd, "Skinny Debugging - %s\n", skinnydebug ? skinny_debugs() : "off");
		return CLI_SUCCESS;
	} else {
		return CLI_SHOWUSAGE;
	}
}
#endif

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
	int wordlen = strlen(word), which = 0;

	AST_LIST_TRAVERSE(&devices, d, list) {
		if (!strncasecmp(word, d->name, wordlen) && ++which > state) {
                       return ast_strdup(d->name);
               }
	}

	return NULL;
}

static char *complete_skinny_show_device(const char *line, const char *word, int pos, int state)
{
	return (pos == 3 ? complete_skinny_devices(word, state) : NULL);
}

static char *complete_skinny_reset(const char *line, const char *word, int pos, int state)
{
	if (pos == 2) {
		static const char * const completions[] = { "all", NULL };
		char *ret = ast_cli_complete(word, completions, state);
		if (!ret) {
			ret = complete_skinny_devices(word, state - 1);
		}
		return ret;
	} else if (pos == 3) {
		static const char * const completions[] = { "restart", NULL };
		return ast_cli_complete(word, completions, state);
	}

	return NULL;
}

static char *complete_skinny_show_line(const char *line, const char *word, int pos, int state)
{
	if (pos == 3) {
		struct skinny_device *d;
		struct skinny_line *l;
		int wordlen = strlen(word), which = 0;

		AST_LIST_TRAVERSE(&devices, d, list) {
			AST_LIST_TRAVERSE(&d->lines, l, list) {
				if (!strncasecmp(word, l->name, wordlen) && ++which > state) {
					return ast_strdup(l->name);
				}
			}
		}
	} else if (pos == 4) {
		static const char * const completions[] = { "on", NULL };
		return ast_cli_complete(word, completions, state);
	} else if (pos == 5) {
		return complete_skinny_devices(word, state);
	}

	return NULL;
}

static char *handle_skinny_reset(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct skinny_device *d;

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
		int resetonly = 1;
		if (!strcasecmp(a->argv[2], d->id) || !strcasecmp(a->argv[2], d->name) || !strcasecmp(a->argv[2], "all")) {
			if (!(d->session))
				continue;

			if (a->argc == 4 && !strcasecmp(a->argv[3], "restart"))
				resetonly = 0;

			transmit_reset(d, resetonly);
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

static char *_skinny_show_devices(int fd, int *total, struct mansession *s, const struct message *m, int argc, const char * const *argv)
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
				d->session?'Y':'N',
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
				d->session?"registered":"unregistered",
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
	const char *a[] = {"skinny", "show", "devices"};
	int total = 0;

	astman_send_listack(s, m, "Device status list will follow", "start");

	/* List the devices in separate manager events */
	_skinny_show_devices(-1, &total, s, m, 3, a);

	/* Send final confirmation */
	astman_send_list_complete_start(s, m, "DevicelistComplete", total);
	astman_send_list_complete_end(s);
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

	return _skinny_show_devices(a->fd, NULL, NULL, NULL, a->argc, a->argv);
}

static char *_skinny_show_device(int type, int fd, struct mansession *s, const struct message *m, int argc, const char * const *argv)
{
	struct skinny_device *d;
	struct skinny_line *l;
	struct skinny_speeddial *sd;
	struct skinny_addon *sa;
	struct skinny_serviceurl *surl;
	struct ast_str *codec_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);

	if (argc < 4) {
		return CLI_SHOWUSAGE;
	}

	AST_LIST_LOCK(&devices);
	AST_LIST_TRAVERSE(&devices, d, list) {
		if (!strcasecmp(argv[3], d->id) || !strcasecmp(argv[3], d->name)) {
			int numlines = 0, numaddons = 0, numspeeddials = 0, numserviceurls = 0;

			AST_LIST_TRAVERSE(&d->lines, l, list){
				numlines++;
			}

			AST_LIST_TRAVERSE(&d->addons, sa, list) {
				numaddons++;
			}

			AST_LIST_TRAVERSE(&d->speeddials, sd, list) {
				numspeeddials++;
			}

			AST_LIST_TRAVERSE(&d->serviceurls, surl, list) {
				numserviceurls++;
			}

			if (type == 0) { /* CLI */
				ast_cli(fd, "Name:        %s\n", d->name);
				ast_cli(fd, "Id:          %s\n", d->id);
				ast_cli(fd, "version:     %s\n", S_OR(d->version_id, "Unknown"));
				ast_cli(fd, "Ip address:  %s\n", (d->session ? ast_inet_ntoa(d->session->sin.sin_addr) : "Unknown"));
				ast_cli(fd, "Port:        %d\n", (d->session ? ntohs(d->session->sin.sin_port) : 0));
				ast_cli(fd, "Device Type: %s\n", device2str(d->type));
				ast_cli(fd, "Conf Codecs: %s\n", ast_format_cap_get_names(d->confcap, &codec_buf));
				ast_cli(fd, "Neg Codecs: %s\n", ast_format_cap_get_names(d->cap, &codec_buf));
				ast_cli(fd, "Registered:  %s\n", (d->session ? "Yes" : "No"));
				ast_cli(fd, "Lines:       %d\n", numlines);
				AST_LIST_TRAVERSE(&d->lines, l, list) {
					ast_cli(fd, "  %s (%s)\n", l->name, l->label);
				}
				ast_cli(fd, "Addons:      %d\n", numaddons);
				AST_LIST_TRAVERSE(&d->addons, sa, list) {
					ast_cli(fd, "  %s\n", sa->type);
				}
				ast_cli(fd, "Speeddials:  %d\n", numspeeddials);
				AST_LIST_TRAVERSE(&d->speeddials, sd, list) {
					ast_cli(fd, "  %s (%s) ishint: %d\n", sd->exten, sd->label, sd->isHint);
				}
				ast_cli(fd, "ServiceURLs:  %d\n", numserviceurls);
				AST_LIST_TRAVERSE(&d->serviceurls, surl, list) {
					ast_cli(fd, "  %s (%s)\n", surl->displayName, surl->url);
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
				astman_append(s, "Codecs: %s\r\n", ast_format_cap_get_names(d->confcap, &codec_buf));
				astman_append(s, "CodecOrder: %s\r\n", ast_format_cap_get_names(d->cap, &codec_buf));
				astman_append(s, "Devicestatus: %s\r\n", (d->session?"registered":"unregistered"));
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
				astman_append(s, "ServiceURLs:  %d\r\n", numserviceurls);
				AST_LIST_TRAVERSE(&d->serviceurls, surl, list) {
					astman_append(s, "  %s (%s)\r\n", surl->displayName, surl->url);
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

	return _skinny_show_device(0, a->fd, NULL, NULL, a->argc, a->argv);
}

static char *_skinny_show_lines(int fd, int *total, struct mansession *s, const struct message *m, int argc, const char * const *argv)
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
					RAII_VAR(struct ast_channel *, bridged, ast_channel_bridge_peer(sub->owner), ao2_cleanup);

					ast_cli(fd, "  %s> %s to %s\n",
						(sub == l->activesub?"Active  ":"Inactive"),
						ast_channel_name(sub->owner),
						bridged ? ast_channel_name(bridged) : ""
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
				(l->device ? l->device->name : "None"),
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
	const char *a[] = {"skinny", "show", "lines"};
	int total = 0;

	astman_send_listack(s, m, "Line status list will follow", "start");

	/* List the lines in separate manager events */
	_skinny_show_lines(-1, &total, s, m, 3, a);

	/* Send final confirmation */
	astman_send_list_complete_start(s, m, "LinelistComplete", total);
	astman_send_list_complete_end(s);
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

	return _skinny_show_lines(a->fd, NULL, NULL, NULL, a->argc, a->argv);
}

static char *_skinny_show_line(int type, int fd, struct mansession *s, const struct message *m, int argc, const char * const *argv)
{
	struct skinny_device *d;
	struct skinny_line *l;
	struct skinny_subline *subline;
	struct ast_str *codec_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);
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
			struct ast_str *tmp_str = ast_str_alloca(512);

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
				ast_cli(fd, "NamedCallGroup:   %s\n", ast_print_namedgroups(&tmp_str, l->named_callgroups));
				ast_str_reset(tmp_str);
				ast_cli(fd, "NamedPickupGroup: %s\n", ast_print_namedgroups(&tmp_str, l->named_pickupgroups));
				ast_str_reset(tmp_str);
				ast_cli(fd, "Language:         %s\n", S_OR(l->language, "<not set>"));
				ast_cli(fd, "Accountcode:      %s\n", S_OR(l->accountcode, "<not set>"));
				ast_cli(fd, "AmaFlag:          %s\n", ast_channel_amaflags2string(l->amaflags));
				ast_cli(fd, "CallerId Number:  %s\n", S_OR(l->cid_num, "<not set>"));
				ast_cli(fd, "CallerId Name:    %s\n", S_OR(l->cid_name, "<not set>"));
				ast_cli(fd, "Hide CallerId:    %s\n", (l->hidecallerid ? "Yes" : "No"));
				ast_cli(fd, "CFwdAll:          %s\n", S_COR((l->cfwdtype & SKINNY_CFWD_ALL), l->call_forward_all, "<not set>"));
				ast_cli(fd, "CFwdBusy:         %s\n", S_COR((l->cfwdtype & SKINNY_CFWD_BUSY), l->call_forward_busy, "<not set>"));
				ast_cli(fd, "CFwdNoAnswer:     %s\n", S_COR((l->cfwdtype & SKINNY_CFWD_NOANSWER), l->call_forward_noanswer, "<not set>"));
				ast_cli(fd, "CFwdTimeout:      %dms\n", l->callfwdtimeout);
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
				ast_cli(fd, "Conf Codecs:      %s\n", ast_format_cap_get_names(l->confcap, &codec_buf));
				ast_cli(fd, "Neg Codecs:       %s\n", ast_format_cap_get_names(l->cap, &codec_buf));
				if  (AST_LIST_FIRST(&l->sublines)) {
					ast_cli(fd, "Sublines:\n");
					AST_LIST_TRAVERSE(&l->sublines, subline, list) {
						ast_cli(fd, "     %s, %s@%s\n", subline->name, subline->exten, subline->context);
					}
				}
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
				astman_append(s, "NamedCallGroup: %s\r\n", ast_print_namedgroups(&tmp_str, l->named_callgroups));
				ast_str_reset(tmp_str);
				astman_append(s, "NamedPickupGroup: %s\r\n", ast_print_namedgroups(&tmp_str, l->named_pickupgroups));
				ast_str_reset(tmp_str);
				astman_append(s, "Language: %s\r\n", S_OR(l->language, "<not set>"));
				astman_append(s, "Accountcode: %s\r\n", S_OR(l->accountcode, "<not set>"));
				astman_append(s, "AMAflags: %s\r\n", ast_channel_amaflags2string(l->amaflags));
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
				astman_append(s, "Codecs: %s\r\n", ast_format_cap_get_names(l->confcap, &codec_buf));
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
			"Usage: skinny show line <Line> [on <DeviceID|DeviceName>]\n"
			"       List all lineinformation of a specific line known to the Skinny subsystem.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_skinny_show_line(a->line, a->word, a->pos, a->n);
	}

	return _skinny_show_line(0, a->fd, NULL, NULL, a->argc, a->argv);
}

/*! \brief List global settings for the Skinny subsystem. */
static char *handle_skinny_show_settings(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char immed_str[2] = {immed_dialchar, '\0'};

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
	ast_cli(a->fd, "  Voice Mail Extension:   %s\n", S_OR(vmexten, "(not set)"));
	ast_cli(a->fd, "  Reg. context:           %s\n", S_OR(regcontext, "(not set)"));
	ast_cli(a->fd, "  Immed. Dial Key:        %s\n", S_OR(immed_str, "(not set)"));
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

static char *_skinny_message_set(int type, int fd, struct mansession *s, const struct message *m, int argc, const char * const *argv)
{
	struct skinny_device *d;
	char text_buf[32];

	if (argc < 7) {
		return CLI_SHOWUSAGE;
	}

	AST_LIST_LOCK(&devices);
	AST_LIST_TRAVERSE(&devices, d, list) {
		if (!strcasecmp(argv[3], d->name)) {
			int i;
			char *strp = text_buf;
			int charleft = sizeof(text_buf);
			int priority = atoi(argv[4]);
			int timeout = atoi(argv[5]);
			ast_copy_string(strp, argv[6], charleft);
			charleft -= strlen(strp);
			strp += strlen(strp);
			for(i=7; i<argc; i++) {
				ast_copy_string(strp++, " ", charleft--);
				ast_copy_string(strp, argv[i], charleft);
				charleft -= strlen(strp);
				strp += strlen(strp);
			}
			send_displayprinotify(d, text_buf, "", timeout, priority);
		}
	}
	AST_LIST_UNLOCK(&devices);
	return CLI_SUCCESS;
}

/*! \brief Handle sending messages to devices. */
static char *handle_skinny_message_set(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "skinny message set";
		e->usage =
			"Usage: skinny message set <device> <priority> <timeout> <message>\n"
			"       Set the current priority level message on a device.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_skinny_show_device(a->line, a->word, a->pos, a->n);
	}

	return _skinny_message_set(0, a->fd, NULL, NULL, a->argc, a->argv);
}

static char *_skinny_message_clear(int type, int fd, struct mansession *s, const struct message *m, int argc, const char * const *argv)
{
	struct skinny_device *d;

	if (argc != 5) {
		return CLI_SHOWUSAGE;
	}

	AST_LIST_LOCK(&devices);
	AST_LIST_TRAVERSE(&devices, d, list) {
		if (!strcasecmp(argv[3], d->name)) {
			int priority = atoi(argv[4]);
			transmit_clearprinotify(d, priority);
		}
	}
	AST_LIST_UNLOCK(&devices);
	return CLI_SUCCESS;
}

/*! \brief Handle clearing messages to devices. */
static char *handle_skinny_message_clear(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "skinny message clear";
		e->usage =
			"Usage: skinny message clear <device> <priority>\n"
			"       Clear the current priority level message on device.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_skinny_show_device(a->line, a->word, a->pos, a->n);
	}

	return _skinny_message_clear(0, a->fd, NULL, NULL, a->argc, a->argv);
}

static struct ast_cli_entry cli_skinny[] = {
	AST_CLI_DEFINE(handle_skinny_show_devices, "List defined Skinny devices"),
	AST_CLI_DEFINE(handle_skinny_show_device, "List Skinny device information"),
	AST_CLI_DEFINE(handle_skinny_show_lines, "List defined Skinny lines per device"),
	AST_CLI_DEFINE(handle_skinny_show_line, "List Skinny line information"),
	AST_CLI_DEFINE(handle_skinny_show_settings, "List global Skinny settings"),
#ifdef AST_DEVMODE
	AST_CLI_DEFINE(handle_skinny_set_debug, "Enable/Disable Skinny debugging"),
#endif
	AST_CLI_DEFINE(handle_skinny_reset, "Reset Skinny device(s)"),
	AST_CLI_DEFINE(handle_skinny_reload, "Reload Skinny config"),
	AST_CLI_DEFINE(handle_skinny_message_set, "Send message to devices"),
	AST_CLI_DEFINE(handle_skinny_message_clear, "Clear message to devices"),
};

static void start_rtp(struct skinny_subchannel *sub)
{
	struct skinny_line *l = sub->line;
	struct skinny_device *d = l->device;
#if 0
	int hasvideo = 0;
#endif
	struct ast_sockaddr bindaddr_tmp;

	skinny_locksub(sub);
	SKINNY_DEBUG(DEBUG_AUDIO, 3, "Sub %u - Starting RTP\n", sub->callid);
	ast_sockaddr_from_sin(&bindaddr_tmp, &bindaddr);
	sub->rtp = ast_rtp_instance_new("asterisk", sched, &bindaddr_tmp, NULL);
#if 0
	if (hasvideo)
		sub->vrtp = ast_rtp_instance_new("asterisk", sched, &bindaddr_tmp, NULL);
#endif

	if (sub->rtp) {
		ast_rtp_instance_set_prop(sub->rtp, AST_RTP_PROPERTY_RTCP, 1);
	}
	if (sub->vrtp) {
		ast_rtp_instance_set_prop(sub->vrtp, AST_RTP_PROPERTY_RTCP, 1);
	}

	if (sub->rtp && sub->owner) {
		ast_rtp_instance_set_channel_id(sub->rtp, ast_channel_uniqueid(sub->owner));
		ast_channel_set_fd(sub->owner, 0, ast_rtp_instance_fd(sub->rtp, 0));
		ast_channel_set_fd(sub->owner, 1, ast_rtp_instance_fd(sub->rtp, 1));
	}
#if 0
	if (hasvideo && sub->vrtp && sub->owner) {
		ast_rtp_instance_set_channel_id(sub->vrtp, ast_channel_uniqueid(sub->owner));
		ast_channel_set_fd(sub->owner, 2, ast_rtp_instance_fd(sub->vrtp, 0));
		ast_channel_set_fd(sub->owner, 3, ast_rtp_instance_fd(sub->vrtp, 1));
	}
#endif
	if (sub->rtp) {
		ast_rtp_instance_set_qos(sub->rtp, qos.tos_audio, qos.cos_audio, "Skinny RTP");
		ast_rtp_instance_set_prop(sub->rtp, AST_RTP_PROPERTY_NAT, l->nat);
		/* Set frame packetization */
		ast_rtp_codecs_set_framing(ast_rtp_instance_get_codecs(sub->rtp),
			ast_format_cap_get_framing(l->cap));
	}
	if (sub->vrtp) {
		ast_rtp_instance_set_qos(sub->vrtp, qos.tos_video, qos.cos_video, "Skinny VRTP");
		ast_rtp_instance_set_prop(sub->vrtp, AST_RTP_PROPERTY_NAT, l->nat);
	}

	/* Create the RTP connection */
	transmit_connect(d, sub);
	skinny_unlocksub(sub);
}

static void destroy_rtp(struct skinny_subchannel *sub)
{
	if (sub->rtp) {
		SKINNY_DEBUG(DEBUG_AUDIO, 3, "Sub %u - Destroying RTP\n", sub->callid);
		ast_rtp_instance_stop(sub->rtp);
		ast_rtp_instance_destroy(sub->rtp);
		sub->rtp = NULL;
	}
	if (sub->vrtp) {
		SKINNY_DEBUG(DEBUG_AUDIO, 3, "Sub %u - Destroying VRTP\n", sub->callid);
		ast_rtp_instance_stop(sub->vrtp);
		ast_rtp_instance_destroy(sub->vrtp);
		sub->vrtp = NULL;
	}
}

static void *skinny_newcall(void *data)
{
	struct ast_channel *c = data;
	struct skinny_subchannel *sub = ast_channel_tech_pvt(c);
	struct skinny_line *l = sub->line;
	struct skinny_device *d = l->device;
	int res = 0;

	ast_channel_lock(c);
	ast_set_callerid(c,
		l->hidecallerid ? "" : l->cid_num,
		l->hidecallerid ? "" : l->cid_name,
		ast_channel_caller(c)->ani.number.valid ? NULL : l->cid_num);
#if 1	/* XXX This code is probably not necessary */
	ast_party_number_free(&ast_channel_connected(c)->id.number);
	ast_party_number_init(&ast_channel_connected(c)->id.number);
	ast_channel_connected(c)->id.number.valid = 1;
	ast_channel_connected(c)->id.number.str = ast_strdup(ast_channel_exten(c));
	ast_party_name_free(&ast_channel_connected(c)->id.name);
	ast_party_name_init(&ast_channel_connected(c)->id.name);
#endif
	ast_setstate(c, AST_STATE_RING);
	ast_channel_unlock(c);
	if (!sub->rtp) {
		start_rtp(sub);
	}
	ast_verb(3, "Sub %u - Calling %s@%s\n", sub->callid, ast_channel_exten(c), ast_channel_context(c));
	res = ast_pbx_run(c);
	if (res) {
		ast_log(LOG_WARNING, "PBX exited non-zero\n");
		transmit_start_tone(d, SKINNY_REORDER, l->instance, sub->callid);
	}
	return NULL;
}

static void skinny_dialer(struct skinny_subchannel *sub, int timedout)
{
	struct ast_channel *c = sub->owner;
	struct skinny_line *l = sub->line;
	struct skinny_device *d = l->device;

	if (timedout || !ast_matchmore_extension(c, ast_channel_context(c), sub->exten, 1, l->cid_num)) {
		SKINNY_DEBUG(DEBUG_SUB, 3, "Sub %u - Force dialing '%s' because of %s\n", sub->callid, sub->exten, (timedout ? "timeout" : "exactmatch"));
		if (ast_exists_extension(c, ast_channel_context(c), sub->exten, 1, l->cid_num)) {
			if (sub->substate == SUBSTATE_OFFHOOK) {
				dialandactivatesub(sub, sub->exten);
			}
		} else {
			if (d->hookstate == SKINNY_OFFHOOK) {
				// FIXME: redundant because below will onhook before the sound plays, but it correct to send it.
				transmit_start_tone(d, SKINNY_REORDER, l->instance, sub->callid);
			}
			dumpsub(sub, 0);
		}
	} else {
		SKINNY_DEBUG(DEBUG_SUB, 3, "Sub %u - Wait for more digits\n", sub->callid);
		if (ast_exists_extension(c, ast_channel_context(c), sub->exten, 1, l->cid_num)) {
			transmit_selectsoftkeys(d, l->instance, sub->callid, KEYDEF_DADFD, KEYMASK_ALL);
			sub->dialer_sched = skinny_sched_add(matchdigittimeout, skinny_dialer_cb, sub);
		} else {
			sub->dialer_sched = skinny_sched_add(gendigittimeout, skinny_dialer_cb, sub);
		}
	}
}

static int skinny_dialer_cb(const void *data)
{
	struct skinny_subchannel *sub = (struct skinny_subchannel *)data;
	SKINNY_DEBUG(DEBUG_SUB, 3, "Sub %u - Dialer called from SCHED %d\n", sub->callid, sub->dialer_sched);
	sub->dialer_sched = -1;
	skinny_dialer(sub, 1);
	return 0;
}

static int skinny_autoanswer_cb(const void *data)
{
	struct skinny_subchannel *sub = (struct skinny_subchannel *)data;
	skinny_locksub(sub);
	sub->aa_sched = -1;
	setsubstate(sub, SKINNY_CONNECTED);
	skinny_unlocksub(sub);
	return 0;
}

static int skinny_cfwd_cb(const void *data)
{
	struct skinny_subchannel *sub = (struct skinny_subchannel *)data;
	struct skinny_line *l = sub->line;
	sub->cfwd_sched = -1;
	SKINNY_DEBUG(DEBUG_SUB, 3, "Sub %u - CFWDNOANS to %s.\n", sub->callid, l->call_forward_noanswer);
	ast_channel_call_forward_set(sub->owner, l->call_forward_noanswer);
	ast_queue_control(sub->owner, AST_CONTROL_REDIRECTING);
	return 0;
}

static int skinny_call(struct ast_channel *ast, const char *dest, int timeout)
{
	int res = 0;
	struct skinny_subchannel *sub = ast_channel_tech_pvt(ast);
	struct skinny_line *l = sub->line;
	struct skinny_device *d = l->device;
	struct ast_var_t *current;
	int doautoanswer = 0;

	if (!d || !d->session) {
		ast_log(LOG_WARNING, "Device not registered, cannot call %s\n", dest);
		return -1;
	}

	if ((ast_channel_state(ast) != AST_STATE_DOWN) && (ast_channel_state(ast) != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "skinny_call called on %s, neither down nor reserved\n", ast_channel_name(ast));
		return -1;
	}

	SKINNY_DEBUG(DEBUG_SUB, 3, "Skinny Call (%s) - Sub %u\n",
		ast_channel_name(ast), sub->callid);

	if (l->dnd) {
		ast_queue_control(ast, AST_CONTROL_BUSY);
		return -1;
	}

	if (AST_LIST_NEXT(sub,list) && !l->callwaiting) {
		ast_queue_control(ast, AST_CONTROL_BUSY);
		return -1;
	}

	skinny_locksub(sub);
	AST_LIST_TRAVERSE(ast_channel_varshead(ast), current, entries) {
		if (!(strcmp(ast_var_name(current), "SKINNY_AUTOANSWER"))) {
			if (d->hookstate == SKINNY_ONHOOK && sub->aa_sched < 0) {
				char buf[24];
				int aatime;
				char *stringp = buf, *curstr;
				ast_copy_string(buf, ast_var_value(current), sizeof(buf));
				curstr = strsep(&stringp, ":");
				aatime = atoi(curstr);
				while ((curstr = strsep(&stringp, ":"))) {
					if (!(strcasecmp(curstr,"BEEP"))) {
						sub->aa_beep = 1;
					} else if (!(strcasecmp(curstr,"MUTE"))) {
						sub->aa_mute = 1;
					}
				}
				SKINNY_DEBUG(DEBUG_SUB, 3, "Sub %u - setting autoanswer time=%dms %s%s\n",
					sub->callid, aatime, sub->aa_beep ? "BEEP " : "", sub->aa_mute ? "MUTE" : "");
				if (aatime) {
					//sub->aa_sched = ast_sched_add(sched, aatime, skinny_autoanswer_cb, sub);
					sub->aa_sched = skinny_sched_add(aatime, skinny_autoanswer_cb, sub);
				} else {
					doautoanswer = 1;
				}
			}
		}
	}

	setsubstate(sub, SUBSTATE_RINGIN);
	if (doautoanswer) {
		setsubstate(sub, SUBSTATE_CONNECTED);
	}
	skinny_unlocksub(sub);
	return res;
}

static int skinny_hangup(struct ast_channel *ast)
{
	struct skinny_subchannel *sub = ast_channel_tech_pvt(ast);

	if (!sub) {
		ast_debug(1, "Asked to hangup channel not connected\n");
		return 0;
	}

	dumpsub(sub, 1);

	SKINNY_DEBUG(DEBUG_SUB, 3, "Sub %u - Destroying\n", sub->callid);

	skinny_set_owner(sub, NULL);
	ast_channel_tech_pvt_set(ast, NULL);
	destroy_rtp(sub);
	ast_free(sub->origtonum);
	ast_free(sub->origtoname);
	ast_free(sub);
	ast_module_unref(ast_module_info->self);
	return 0;
}

static int skinny_answer(struct ast_channel *ast)
{
	int res = 0;
	struct skinny_subchannel *sub = ast_channel_tech_pvt(ast);

	sub->cxmode = SKINNY_CX_SENDRECV;

	SKINNY_DEBUG(DEBUG_SUB, 3, "skinny_answer(%s) on %s@%s-%u\n",
		ast_channel_name(ast), sub->line->name, sub->line->device->name, sub->callid);

	setsubstate(sub, SUBSTATE_CONNECTED);

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

	switch(ast_channel_fdno(ast)) {
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
			if (ast_format_cap_iscompatible_format(ast_channel_nativeformats(ast), f->subclass.format) == AST_FORMAT_CMP_NOT_EQUAL) {
				struct ast_format_cap *caps;

				ast_debug(1, "Oooh, format changed to %s\n", ast_format_get_name(f->subclass.format));

				caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
				if (caps) {
					ast_format_cap_append(caps, f->subclass.format, 0);
					ast_channel_nativeformats_set(ast, caps);
					ao2_ref(caps, -1);
				}
				ast_set_read_format(ast, ast_channel_readformat(ast));
				ast_set_write_format(ast, ast_channel_writeformat(ast));
			}
		}
	}
	return f;
}

static struct ast_frame *skinny_read(struct ast_channel *ast)
{
	struct ast_frame *fr;
	struct skinny_subchannel *sub = ast_channel_tech_pvt(ast);
	skinny_locksub(sub);
	fr = skinny_rtp_read(sub);
	skinny_unlocksub(sub);
	return fr;
}

static int skinny_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct skinny_subchannel *sub = ast_channel_tech_pvt(ast);
	int res = 0;
	if (frame->frametype != AST_FRAME_VOICE) {
		if (frame->frametype == AST_FRAME_IMAGE) {
			return 0;
		} else {
			ast_log(LOG_WARNING, "Can't send %u type frames with skinny_write\n", frame->frametype);
			return 0;
		}
	} else {
		if (ast_format_cap_iscompatible_format(ast_channel_nativeformats(ast), frame->subclass.format) == AST_FORMAT_CMP_NOT_EQUAL) {
			struct ast_str *codec_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);
			ast_log(LOG_WARNING, "Asked to transmit frame type %s, while native formats is %s (read/write = %s/%s)\n",
				ast_format_get_name(frame->subclass.format),
				ast_format_cap_get_names(ast_channel_nativeformats(ast), &codec_buf),
				ast_format_get_name(ast_channel_readformat(ast)),
				ast_format_get_name(ast_channel_writeformat(ast)));
			return -1;
		}
	}
	if (sub) {
		skinny_locksub(sub);
		if (sub->rtp) {
			res = ast_rtp_instance_write(sub->rtp, frame);
		}
		skinny_unlocksub(sub);
	}
	return res;
}

static int skinny_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct skinny_subchannel *sub = ast_channel_tech_pvt(newchan);
	ast_log(LOG_NOTICE, "skinny_fixup(%s, %s)\n", ast_channel_name(oldchan), ast_channel_name(newchan));
	if (sub->owner != oldchan) {
		ast_log(LOG_WARNING, "old channel wasn't %p but was %p\n", oldchan, sub->owner);
		return -1;
	}
	skinny_set_owner(sub, newchan);
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
	struct skinny_line *l = sub->line;
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
	else if (!l->device || !l->device->session)
		res = AST_DEVICE_UNAVAILABLE;
	else if (l->dnd)
		res = AST_DEVICE_BUSY;
	else {
		if (l->device->hookstate == SKINNY_ONHOOK) {
			res = AST_DEVICE_NOT_INUSE;
		} else {
			res = AST_DEVICE_INUSE;
		}

		AST_LIST_TRAVERSE(&l->sub, sub, list) {
			if (sub->substate == SUBSTATE_HOLD) {
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
	case AST_CONTROL_VIDUPDATE:
		return "VidUpdate";
	case AST_CONTROL_SRCUPDATE:
		return "Media Source Update";
	case AST_CONTROL_TRANSFER:
		return "Transfer";
	case AST_CONTROL_CONNECTED_LINE:
		return "Connected Line";
	case AST_CONTROL_REDIRECTING:
		return "Redirecting";
	case AST_CONTROL_T38_PARAMETERS:
		return "T38_Parameters";
	case AST_CONTROL_CC:
		return "CC Not Possible";
	case AST_CONTROL_SRCCHANGE:
		return "Media Source Change";
	case AST_CONTROL_INCOMPLETE:
		return "Incomplete";
	case -1:
		return "Stop tone";
	default:
		if (!(tmp = ast_threadstorage_get(&control2str_threadbuf, CONTROL2STR_BUFSIZE)))
                        return "Unknown";
		snprintf(tmp, CONTROL2STR_BUFSIZE, "UNKNOWN-%d", ind);
		return tmp;
	}
}

static void skinny_transfer_attended(struct skinny_subchannel *sub)
{
	struct skinny_subchannel *xferee;
	struct skinny_subchannel *xferor;
	enum ast_transfer_result res;

	if (sub->xferor) {
		xferor = sub;
		xferee = sub->related;
	} else {
		xferor = sub;
		xferee = sub->related;
	}

	ast_queue_control(xferee->owner, AST_CONTROL_UNHOLD);
	if (ast_channel_state(xferor->owner) == AST_STATE_RINGING) {
		ast_queue_control(xferee->owner, AST_CONTROL_RINGING);
	}
	res = ast_bridge_transfer_attended(xferee->owner, xferor->owner);

	if (res != AST_BRIDGE_TRANSFER_SUCCESS) {
		SKINNY_DEBUG(DEBUG_SUB, 3, "Sub %u failed to transfer %u to '%s'@'%s' - %u\n",
			xferor->callid, xferee->callid, xferor->exten, xferor->line->context, res);
		send_displayprinotify(xferor->line->device, "Transfer failed", NULL, 10, 5);
		ast_queue_control(xferee->owner, AST_CONTROL_HOLD);
	}
}

static void skinny_transfer_blind(struct skinny_subchannel *sub)
{
	struct skinny_subchannel *xferee = sub->related;
	enum ast_transfer_result res;

	sub->related = NULL;
	xferee->related = NULL;

	ast_queue_control(xferee->owner, AST_CONTROL_UNHOLD);
	res = ast_bridge_transfer_blind(1, xferee->owner, sub->exten, sub->line->context, NULL, NULL);

	if (res != AST_BRIDGE_TRANSFER_SUCCESS) {
		SKINNY_DEBUG(DEBUG_SUB, 3, "Sub %u failed to blind transfer %u to '%s'@'%s' - %u\n",
			sub->callid, xferee->callid, sub->exten, sub->line->context, res);
		send_displayprinotify(sub->line->device, "Transfer failed", NULL, 10, 5);
		ast_queue_control(xferee->owner, AST_CONTROL_HOLD);
	}
	dumpsub(sub, 1);
}

static int skinny_indicate(struct ast_channel *ast, int ind, const void *data, size_t datalen)
{
	struct skinny_subchannel *sub = ast_channel_tech_pvt(ast);
	struct skinny_line *l = sub->line;
	struct skinny_device *d = l->device;
	struct skinnysession *s = d->session;

	if (!s) {
		ast_log(LOG_NOTICE, "Asked to indicate '%s' condition on channel %s, but session does not exist.\n", control2str(ind), ast_channel_name(ast));
		return -1;
	}

	SKINNY_DEBUG(DEBUG_SUB, 3, "Asked to indicate '%s' condition on channel %s (Sub %u)\n",
		control2str(ind), ast_channel_name(ast), sub->callid);
	switch(ind) {
	case AST_CONTROL_RINGING:
		setsubstate(sub, SUBSTATE_RINGOUT);
		return (d->earlyrtp ? -1 : 0); /* Tell asterisk to provide inband signalling if rtp started */
	case AST_CONTROL_BUSY:
		setsubstate(sub, SUBSTATE_BUSY);
		return (d->earlyrtp ? -1 : 0); /* Tell asterisk to provide inband signalling if rtp started */
	case AST_CONTROL_INCOMPLETE:
		/* Support for incomplete not supported for chan_skinny; treat as congestion */
	case AST_CONTROL_CONGESTION:
		setsubstate(sub, SUBSTATE_CONGESTION);
		return (d->earlyrtp ? -1 : 0); /* Tell asterisk to provide inband signalling if rtp started */
	case AST_CONTROL_PROGRESS:
		setsubstate(sub, SUBSTATE_PROGRESS);
		return (d->earlyrtp ? -1 : 0); /* Tell asterisk to provide inband signalling if rtp started */
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
		if (sub->rtp) {
			ast_rtp_instance_update_source(sub->rtp);
		}
		break;
	case AST_CONTROL_SRCCHANGE:
		if (sub->rtp) {
			ast_rtp_instance_change_source(sub->rtp);
		}
		break;
	case AST_CONTROL_CONNECTED_LINE:
		update_connectedline(sub, data, datalen);
		break;
	default:
		ast_log(LOG_WARNING, "Don't know how to indicate condition %d\n", ind);
		/* fallthrough */
	case AST_CONTROL_PVT_CAUSE_CODE:
	case AST_CONTROL_MASQUERADE_NOTIFY:
		return -1; /* Tell asterisk to provide inband signalling */
	}
	return 0;
}

static void skinny_set_owner(struct skinny_subchannel* sub, struct ast_channel* chan)
{
	sub->owner = chan;
	if (sub->rtp) {
		ast_rtp_instance_set_channel_id(sub->rtp, sub->owner ? ast_channel_uniqueid(sub->owner) : "");
	}
	if (sub->vrtp) {
		ast_rtp_instance_set_channel_id(sub->vrtp, sub->owner ? ast_channel_uniqueid(sub->owner) : "");
	}
}

static struct ast_channel *skinny_new(struct skinny_line *l, struct skinny_subline *subline, int state, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, int direction)
{
	struct ast_channel *tmp;
	struct skinny_subchannel *sub;
	struct skinny_device *d = l->device;
	struct ast_variable *v = NULL;
	struct ast_format *tmpfmt;
	struct ast_format_cap *caps;
#ifdef AST_DEVMODE
	struct ast_str *codec_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);
#endif

	if (!l->device || !l->device->session) {
		ast_log(LOG_WARNING, "Device for line %s is not registered.\n", l->name);
		return NULL;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		return NULL;
	}

	tmp = ast_channel_alloc(1, state, l->cid_num, l->cid_name, l->accountcode, l->exten, l->context, assignedids, requestor, l->amaflags, "Skinny/%s@%s-%d", l->name, d->name, callnums);
	if (!tmp) {
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
		ao2_ref(caps, -1);
		return NULL;
	} else {
		sub = ast_calloc(1, sizeof(*sub));
		if (!sub) {
			ast_log(LOG_WARNING, "Unable to allocate Skinny subchannel\n");
			ast_channel_unlock(tmp);
			ast_channel_unref(tmp);
			ao2_ref(caps, -1);
			return NULL;
		} else {
			skinny_set_owner(sub, tmp);
			sub->callid = callnums++;
			d->lastlineinstance = l->instance;
			d->lastcallreference = sub->callid;
			sub->cxmode = SKINNY_CX_INACTIVE;
			sub->nat = l->nat;
			sub->line = l;
			sub->blindxfer = 0;
			sub->xferor = 0;
			sub->related = NULL;
			sub->calldirection = direction;
			sub->aa_sched = -1;
			sub->dialer_sched = -1;
			sub->cfwd_sched = -1;
			sub->dialType = DIALTYPE_NORMAL;
			sub->getforward = 0;

			if (subline) {
				sub->subline = subline;
				subline->sub = sub;
			} else {
				sub->subline = NULL;
			}

			AST_LIST_INSERT_HEAD(&l->sub, sub, list);
			//l->activesub = sub;
		}
		ast_channel_stage_snapshot(tmp);
		ast_channel_tech_set(tmp, &skinny_tech);
		ast_channel_tech_pvt_set(tmp, sub);
		if (!ast_format_cap_count(l->cap)) {
			ast_format_cap_append_from_cap(caps, l->cap, AST_MEDIA_TYPE_UNKNOWN);
		} else {
			ast_format_cap_append_from_cap(caps, default_cap, AST_MEDIA_TYPE_UNKNOWN);
		}
		ast_channel_nativeformats_set(tmp, caps);
		ao2_ref(caps, -1);
		tmpfmt = ast_format_cap_get_format(ast_channel_nativeformats(tmp), 0);
		SKINNY_DEBUG(DEBUG_SUB, 3, "skinny_new: tmp->nativeformats=%s fmt=%s\n",
			ast_format_cap_get_names(ast_channel_nativeformats(tmp), &codec_buf),
			ast_format_get_name(tmpfmt));
		if (sub->rtp) {
			ast_channel_set_fd(tmp, 0, ast_rtp_instance_fd(sub->rtp, 0));
		}
		if (state == AST_STATE_RING) {
			ast_channel_rings_set(tmp, 1);
		}
		ast_channel_set_writeformat(tmp, tmpfmt);
		ast_channel_set_rawwriteformat(tmp, tmpfmt);
		ast_channel_set_readformat(tmp, tmpfmt);
		ast_channel_set_rawreadformat(tmp, tmpfmt);
		ao2_ref(tmpfmt, -1);

		if (!ast_strlen_zero(l->language))
			ast_channel_language_set(tmp, l->language);
		if (!ast_strlen_zero(l->accountcode))
			ast_channel_accountcode_set(tmp, l->accountcode);
		if (!ast_strlen_zero(l->parkinglot))
			ast_channel_parkinglot_set(tmp, l->parkinglot);
		if (l->amaflags)
			ast_channel_amaflags_set(tmp, l->amaflags);

		ast_module_ref(ast_module_info->self);
		ast_channel_callgroup_set(tmp, l->callgroup);
		ast_channel_pickupgroup_set(tmp, l->pickupgroup);

		ast_channel_named_callgroups_set(tmp, l->named_callgroups);
		ast_channel_named_pickupgroups_set(tmp, l->named_pickupgroups);

		if (l->cfwdtype & SKINNY_CFWD_ALL) {
			SKINNY_DEBUG(DEBUG_SUB, 3, "Sub %u - CFWDALL to %s.\n", sub->callid, l->call_forward_all);
			ast_channel_call_forward_set(tmp, l->call_forward_all);
		} else if ((l->cfwdtype & SKINNY_CFWD_BUSY) && (get_devicestate(l) != AST_DEVICE_NOT_INUSE)) {
			SKINNY_DEBUG(DEBUG_SUB, 3, "Sub %u - CFWDBUSY to %s.\n", sub->callid, l->call_forward_busy);
			ast_channel_call_forward_set(tmp, l->call_forward_busy);
		} else if (l->cfwdtype & SKINNY_CFWD_NOANSWER) {
			SKINNY_DEBUG(DEBUG_SUB, 3, "Sub %u - CFWDNOANS Scheduling for %d seconds.\n", sub->callid, l->callfwdtimeout/1000);
			sub->cfwd_sched = skinny_sched_add(l->callfwdtimeout, skinny_cfwd_cb, sub);
		}

		if (subline) {
			ast_channel_context_set(tmp, subline->context);
		} else {
			ast_channel_context_set(tmp, l->context);
		}
		ast_channel_exten_set(tmp, l->exten);

		/* Don't use ast_set_callerid() here because it will
		 * generate a needless NewCallerID event */
		if (!ast_strlen_zero(l->cid_num)) {
			ast_channel_caller(tmp)->ani.number.valid = 1;
			ast_channel_caller(tmp)->ani.number.str = ast_strdup(l->cid_num);
		}

		ast_channel_priority_set(tmp, 1);
		ast_channel_adsicpe_set(tmp, AST_ADSI_UNAVAILABLE);

		if (sub->rtp)
			ast_jb_configure(tmp, &global_jbconf);

		/* Set channel variables for this call from configuration */
		for (v = l->chanvars ; v ; v = v->next)
			pbx_builtin_setvar_helper(tmp, v->name, v->value);

		ast_channel_stage_snapshot_done(tmp);
		ast_channel_unlock(tmp);

		if (state != AST_STATE_DOWN) {
			if (ast_pbx_start(tmp)) {
				ast_log(LOG_WARNING, "Unable to start PBX on %s\n", ast_channel_name(tmp));
				ast_hangup(tmp);
				tmp = NULL;
			}
		}
	}
	return tmp;
}
static char *substate2str(int ind) {
	char *tmp;

	switch (ind) {
	case SUBSTATE_UNSET:
		return "SUBSTATE_UNSET";
	case SUBSTATE_OFFHOOK:
		return "SUBSTATE_OFFHOOK";
	case SUBSTATE_ONHOOK:
		return "SUBSTATE_ONHOOK";
	case SUBSTATE_RINGOUT:
		return "SUBSTATE_RINGOUT";
	case SUBSTATE_RINGIN:
		return "SUBSTATE_RINGIN";
	case SUBSTATE_CONNECTED:
		return "SUBSTATE_CONNECTED";
	case SUBSTATE_BUSY:
		return "SUBSTATE_BUSY";
	case SUBSTATE_CONGESTION:
		return "SUBSTATE_CONGESTION";
	case SUBSTATE_PROGRESS:
		return "SUBSTATE_PROGRESS";
	case SUBSTATE_HOLD:
		return "SUBSTATE_HOLD";
	case SUBSTATE_CALLWAIT:
		return "SUBSTATE_CALLWAIT";
	case SUBSTATE_DIALING:
		return "SUBSTATE_DIALING";
	default:
		if (!(tmp = ast_threadstorage_get(&substate2str_threadbuf, SUBSTATE2STR_BUFSIZE)))
                        return "Unknown";
		snprintf(tmp, SUBSTATE2STR_BUFSIZE, "UNKNOWN-%d", ind);
		return tmp;
	}
}

static void setsubstate(struct skinny_subchannel *sub, int state)
{
	struct skinny_line *l = sub->line;
	struct skinny_subline *subline = sub->subline;
	struct skinny_device *d = l->device;
	struct ast_channel *c = sub->owner;
	struct ast_party_id connected_id;
	pthread_t t;
	int actualstate = state;
	char *fromnum;

	if (sub->substate == SUBSTATE_ONHOOK) {
		return;
	}

	skinny_locksub(sub);

	if (-1 < sub->dialer_sched) {
		skinny_sched_del(sub->dialer_sched, sub);
		sub->dialer_sched = -1;
	}

	if (state != SUBSTATE_RINGIN && -1 < sub->aa_sched) {
		skinny_sched_del(sub->aa_sched, sub);
		sub->aa_sched = -1;
		sub->aa_beep = 0;
		sub->aa_mute = 0;
	}

	if (sub->cfwd_sched > -1) {
		if (state == SUBSTATE_CONNECTED) {
			if (skinny_sched_del(sub->cfwd_sched, sub)) {
				SKINNY_DEBUG(DEBUG_SUB, 3, "Sub %u - trying to change state from %s to %s, but already forwarded because no answer.\n",
					sub->callid, substate2str(sub->substate), substate2str(actualstate));
				skinny_unlocksub(sub);
				return;
			}
			sub->cfwd_sched = -1;
		} else if (state == SUBSTATE_ONHOOK) {
			skinny_sched_del(sub->cfwd_sched, sub);
			sub->cfwd_sched = -1;
		}
	}

	if ((state == SUBSTATE_RINGIN) && ((d->hookstate == SKINNY_OFFHOOK) || (AST_LIST_NEXT(AST_LIST_FIRST(&l->sub), list)))) {
		actualstate = SUBSTATE_CALLWAIT;
	}

	if (sub->substate == SUBSTATE_RINGIN && state != SUBSTATE_RINGIN) {
		transmit_clearprinotify(d, 5);
	}

	if ((state == SUBSTATE_CONNECTED) && (!subline) && (AST_LIST_FIRST(&l->sublines))) {
		const char *slastation;
		struct skinny_subline *tmpsubline;
		slastation = pbx_builtin_getvar_helper(c, "SLASTATION");
		ast_verb(3, "Connecting %s to subline\n", slastation);
		if (slastation) {
			AST_LIST_TRAVERSE(&l->sublines, tmpsubline, list) {
				if (!strcasecmp(tmpsubline->stname, slastation)) {
					subline = tmpsubline;
					break;
				}
			}
			if (subline) {
				struct skinny_line *tmpline;
				subline->sub = sub;
				sub->subline = subline;
				subline->callid = sub->callid;
				send_callinfo(sub);
				AST_LIST_TRAVERSE(&lines, tmpline, all) {
					AST_LIST_TRAVERSE(&tmpline->sublines, tmpsubline, list) {
						if (!(subline == tmpsubline)) {
							if (!strcasecmp(subline->lnname, tmpsubline->lnname)) {
								struct ast_state_cb_info info = {
									.exten_state = tmpsubline->extenstate,
								};
								tmpsubline->callid = callnums++;
								transmit_callstate(tmpsubline->line->device, tmpsubline->line->instance, tmpsubline->callid, SKINNY_OFFHOOK);
								push_callinfo(tmpsubline, sub);
								skinny_extensionstate_cb(NULL, NULL, &info, tmpsubline->container);
							}
						}
					}
				}
			}
		}
	}

	if (subline) { /* Different handling for subs under a subline, indications come through hints */
		switch (actualstate) {
		case SUBSTATE_ONHOOK:
			AST_LIST_REMOVE(&l->sub, sub, list);
			if (sub->related) {
				sub->related->related = NULL;
			}

			if (sub == l->activesub) {
				l->activesub = NULL;
				transmit_closereceivechannel(d, sub);
				transmit_stopmediatransmission(d, sub);
			}

			if (subline->callid) {
				transmit_stop_tone(d, l->instance, sub->callid);
				transmit_callstate(d, l->instance, subline->callid, SKINNY_CALLREMOTEMULTILINE);
				transmit_selectsoftkeys(d, l->instance, subline->callid, KEYDEF_SLACONNECTEDNOTACTIVE, KEYMASK_ALL);
				send_displaypromptstatus(d, "In Use", "", 0, l->instance, subline->callid);
			}

			sub->cxmode = SKINNY_CX_RECVONLY;
			sub->substate = SUBSTATE_ONHOOK;
			sub->substate = SUBSTATE_ONHOOK;
			if (sub->owner) {
				ast_queue_hangup(sub->owner);
			}
			break;
		case SUBSTATE_CONNECTED:
			transmit_activatecallplane(d, l);
			transmit_stop_tone(d, l->instance, sub->callid);
			transmit_selectsoftkeys(d, l->instance, subline->callid, KEYDEF_CONNECTED, KEYMASK_ALL);
			transmit_callstate(d, l->instance, subline->callid, SKINNY_CONNECTED);
			if (!sub->rtp) {
				start_rtp(sub);
			}
			if (sub->substate == SUBSTATE_RINGIN || sub->substate == SUBSTATE_CALLWAIT) {
				ast_queue_control(sub->owner, AST_CONTROL_ANSWER);
			}
			if (sub->substate == SUBSTATE_DIALING || sub->substate == SUBSTATE_RINGOUT) {
				transmit_dialednumber(d, sub->exten, l->instance, sub->callid);
			}
			if (ast_channel_state(sub->owner) != AST_STATE_UP) {
				ast_setstate(sub->owner, AST_STATE_UP);
			}
			sub->substate = SUBSTATE_CONNECTED;
			l->activesub = sub;
			break;
		case SUBSTATE_HOLD:
			if (sub->substate != SUBSTATE_CONNECTED) {
				ast_log(LOG_WARNING, "Cannot set substate to SUBSTATE_HOLD from %s (on call-%u)\n", substate2str(sub->substate), sub->callid);
				return;
			}
			transmit_activatecallplane(d, l);
			transmit_closereceivechannel(d, sub);
			transmit_stopmediatransmission(d, sub);

			transmit_callstate(d, l->instance, subline->callid, SKINNY_CALLREMOTEMULTILINE);
			transmit_selectsoftkeys(d, l->instance, subline->callid, KEYDEF_SLACONNECTEDNOTACTIVE, KEYMASK_ALL);
			send_displaypromptstatus(d, "In Use", "", 0, l->instance, subline->callid);

			sub->substate = SUBSTATE_HOLD;

			ast_queue_hold(sub->owner, l->mohsuggest);

			break;
		default:
			ast_log(LOG_WARNING, "Substate handling under subline for state %d not implemented on Sub-%u\n", state, sub->callid);
		}
		skinny_unlocksub(sub);
		return;
	}

	if ((d->hookstate == SKINNY_ONHOOK) && ((actualstate == SUBSTATE_OFFHOOK) || (actualstate == SUBSTATE_DIALING)
		|| (actualstate == SUBSTATE_RINGOUT) || (actualstate == SUBSTATE_CONNECTED) || (actualstate == SUBSTATE_BUSY)
		|| (actualstate == SUBSTATE_CONGESTION) || (actualstate == SUBSTATE_PROGRESS))) {
			d->hookstate = SKINNY_OFFHOOK;
			transmit_speaker_mode(d, SKINNY_SPEAKERON);
	}

	SKINNY_DEBUG(DEBUG_SUB, 3, "Sub %u - change state from %s to %s\n",
		sub->callid, substate2str(sub->substate), substate2str(actualstate));

	if (actualstate == sub->substate) {
		skinny_unlocksub(sub);
		return;
	}

	switch (actualstate) {
	case SUBSTATE_OFFHOOK:
		ast_verb(1, "Call-id: %u\n", sub->callid);
		l->activesub = sub;
		transmit_callstate(d, l->instance, sub->callid, SKINNY_OFFHOOK);
		transmit_activatecallplane(d, l);
		transmit_clear_display_message(d, l->instance, sub->callid);
		transmit_start_tone(d, SKINNY_DIALTONE, l->instance, sub->callid);
		transmit_selectsoftkeys(d, l->instance, sub->callid, KEYDEF_OFFHOOK, KEYMASK_ALL);
		send_displaypromptstatus(d, OCTAL_ENTRNUM, "", 0, l->instance, sub->callid);

		sub->substate = SUBSTATE_OFFHOOK;
		sub->dialer_sched = skinny_sched_add(firstdigittimeout, skinny_dialer_cb, sub);
		break;
	case SUBSTATE_ONHOOK:
		AST_LIST_REMOVE(&l->sub, sub, list);
		if (sub->related) {
			sub->related->related = NULL;
		}

		if ((sub->substate == SUBSTATE_RINGIN || sub->substate == SUBSTATE_CALLWAIT) && ast_channel_hangupcause(sub->owner) == AST_CAUSE_ANSWERED_ELSEWHERE) {
			transmit_callstate(d, l->instance, sub->callid, SKINNY_CONNECTED);
		}

		if (sub == l->activesub) {
			l->activesub = NULL;
			transmit_closereceivechannel(d, sub);
			transmit_stopmediatransmission(d, sub);
			transmit_stop_tone(d, l->instance, sub->callid);
			transmit_callstate(d, l->instance, sub->callid, SKINNY_ONHOOK);
			transmit_clearpromptmessage(d, l->instance, sub->callid);
			transmit_ringer_mode(d, SKINNY_RING_OFF);
			transmit_definetimedate(d);
			transmit_lamp_indication(d, STIMULUS_LINE, l->instance, SKINNY_LAMP_OFF);
		} else {
			transmit_stop_tone(d, l->instance, sub->callid);
			transmit_callstate(d, l->instance, sub->callid, SKINNY_ONHOOK);
			transmit_clearpromptmessage(d, l->instance, sub->callid);
		}

		sub->cxmode = SKINNY_CX_RECVONLY;
		if (sub->owner) {
			if (sub->substate == SUBSTATE_OFFHOOK) {
				sub->substate = SUBSTATE_ONHOOK;
				skinny_unlocksub(sub);
				ast_hangup(sub->owner);
				return;
			} else {
				sub->substate = SUBSTATE_ONHOOK;
				ast_queue_hangup(sub->owner);
			}
		} else {
			sub->substate = SUBSTATE_ONHOOK;
		}
		break;
	case SUBSTATE_DIALING:
		if (ast_strlen_zero(sub->exten) || !ast_exists_extension(c, ast_channel_context(c), sub->exten, 1, l->cid_num)) {
			ast_log(LOG_WARNING, "Exten (%s)@(%s) does not exist, unable to set substate DIALING on sub %u\n", sub->exten, ast_channel_context(c), sub->callid);
			break;
		}

		if (d->hookstate == SKINNY_ONHOOK) {
			d->hookstate = SKINNY_OFFHOOK;
			transmit_speaker_mode(d, SKINNY_SPEAKERON);
			transmit_activatecallplane(d, l);
		}

		if (!sub->subline) {
			transmit_callstate(d, l->instance, sub->callid, SKINNY_OFFHOOK);
			transmit_stop_tone(d, l->instance, sub->callid);
			transmit_clear_display_message(d, l->instance, sub->callid);
			transmit_selectsoftkeys(d, l->instance, sub->callid, KEYDEF_RINGOUT, KEYMASK_ALL);
			send_displaypromptstatus(d, "Dialing", "", 0, l->instance, sub->callid);
		}

		if  (AST_LIST_FIRST(&l->sublines)) {
			if (subline) {
				ast_channel_exten_set(c, subline->exten);
				ast_channel_context_set(c, "sla_stations");
			} else {
				pbx_builtin_setvar_helper(c, "_DESTEXTEN", sub->exten);
				pbx_builtin_setvar_helper(c, "_DESTCONTEXT", ast_channel_context(c));
				ast_channel_exten_set(c, l->dialoutexten);
				ast_channel_context_set(c, l->dialoutcontext);
				ast_copy_string(l->lastnumberdialed, sub->exten, sizeof(l->lastnumberdialed));
			}
		} else {
			ast_channel_exten_set(c, sub->exten);
			ast_copy_string(l->lastnumberdialed, sub->exten, sizeof(l->lastnumberdialed));
		}

		sub->substate = SUBSTATE_DIALING;

		if (ast_pthread_create(&t, NULL, skinny_newcall, c)) {
			ast_log(LOG_WARNING, "Unable to create new call thread: %s\n", strerror(errno));
			ast_hangup(c);
		}
		break;
	case SUBSTATE_RINGOUT:
		if (!(sub->substate == SUBSTATE_DIALING || sub->substate == SUBSTATE_PROGRESS)) {
			ast_log(LOG_WARNING, "Cannot set substate to SUBSTATE_RINGOUT from %s (on call-%u)\n", substate2str(sub->substate), sub->callid);
			break;
		}
		if (sub->substate != SUBSTATE_PROGRESS) {
			transmit_callstate(d, l->instance, sub->callid, SKINNY_PROGRESS);
		}

		if (!d->earlyrtp) {
			transmit_start_tone(d, SKINNY_ALERT, l->instance, sub->callid);
		}
		transmit_callstate(d, l->instance, sub->callid, SKINNY_RINGOUT);
		if (sub->related) {
			transmit_selectsoftkeys(d, l->instance, sub->callid, KEYDEF_RINGOUTWITHTRANS, KEYMASK_ALL);
		} else {
			transmit_selectsoftkeys(d, l->instance, sub->callid, KEYDEF_RINGOUT, KEYMASK_ALL);
		}
		transmit_dialednumber(d, sub->exten, l->instance, sub->callid);
		send_displaypromptstatus(d, OCTAL_RINGOUT, "", 0, l->instance, sub->callid);
		send_callinfo(sub);
		sub->substate = SUBSTATE_RINGOUT;
		break;
	case SUBSTATE_RINGIN:
		connected_id = ast_channel_connected_effective_id(c);
		if ((ast_party_id_presentation(&connected_id) & AST_PRES_RESTRICTION) == AST_PRES_ALLOWED) {
			fromnum = S_COR(connected_id.number.valid, connected_id.number.str, "Unknown");
		} else {
			fromnum = "Unknown";
		}
		transmit_callstate(d, l->instance, sub->callid, SKINNY_RINGIN);
		transmit_selectsoftkeys(d, l->instance, sub->callid, KEYDEF_RINGIN, KEYMASK_ALL);
		send_displaypromptstatus(d, OCTAL_FROM, fromnum, 0, l->instance, sub->callid);
		send_displayprinotify(d, OCTAL_FROM, fromnum, 10, 5);
		send_callinfo(sub);
		transmit_lamp_indication(d, STIMULUS_LINE, l->instance, SKINNY_LAMP_BLINK);
		transmit_ringer_mode(d, SKINNY_RING_INSIDE);
		transmit_activatecallplane(d, l);

		if (d->hookstate == SKINNY_ONHOOK) {
			l->activesub = sub;
		}

		if (sub->substate != SUBSTATE_RINGIN || sub->substate != SUBSTATE_CALLWAIT) {
			ast_setstate(c, AST_STATE_RINGING);
			ast_queue_control(c, AST_CONTROL_RINGING);
		}
		sub->substate = SUBSTATE_RINGIN;
		break;
	case SUBSTATE_CALLWAIT:
		transmit_callstate(d, l->instance, sub->callid, SKINNY_RINGIN);
		transmit_callstate(d, l->instance, sub->callid, SKINNY_CALLWAIT);
		transmit_selectsoftkeys(d, l->instance, sub->callid, KEYDEF_RINGIN, KEYMASK_ALL);
		send_displaypromptstatus(d, OCTAL_CALLWAITING, "", 0, l->instance, sub->callid);
		send_callinfo(sub);
		transmit_lamp_indication(d, STIMULUS_LINE, l->instance, SKINNY_LAMP_BLINK);
		transmit_start_tone(d, SKINNY_CALLWAITTONE, l->instance, sub->callid);

		ast_setstate(c, AST_STATE_RINGING);
		ast_queue_control(c, AST_CONTROL_RINGING);
		sub->substate = SUBSTATE_CALLWAIT;
		break;
	case SUBSTATE_CONNECTED:
		if (sub->substate == SUBSTATE_RINGIN) {
			transmit_callstate(d, l->instance, sub->callid, SKINNY_OFFHOOK);
		}
		if (sub->substate == SUBSTATE_HOLD) {
			ast_queue_unhold(sub->owner);
			transmit_connect(d, sub);
		}
		transmit_ringer_mode(d, SKINNY_RING_OFF);
		transmit_activatecallplane(d, l);
		transmit_stop_tone(d, l->instance, sub->callid);
		send_callinfo(sub);
		transmit_callstate(d, l->instance, sub->callid, SKINNY_CONNECTED);
		send_displaypromptstatus(d, OCTAL_CONNECTED, "", 0, l->instance, sub->callid);
		transmit_selectsoftkeys(d, l->instance, sub->callid, KEYDEF_CONNECTED, KEYMASK_ALL);
		if (!sub->rtp) {
			start_rtp(sub);
		}
		if (sub->aa_beep) {
			transmit_start_tone(d, SKINNY_ZIP, l->instance, sub->callid);
		}
		if (sub->aa_mute) {
			transmit_microphone_mode(d, SKINNY_MICOFF);
		}
		if (sub->substate == SUBSTATE_RINGIN || sub->substate == SUBSTATE_CALLWAIT) {
			ast_queue_control(sub->owner, AST_CONTROL_ANSWER);
		}
		if (sub->substate == SUBSTATE_DIALING || sub->substate == SUBSTATE_RINGOUT) {
			transmit_dialednumber(d, sub->exten, l->instance, sub->callid);
		}
		if (ast_channel_state(sub->owner) != AST_STATE_UP) {
			ast_setstate(sub->owner, AST_STATE_UP);
		}
		sub->substate = SUBSTATE_CONNECTED;
		l->activesub = sub;
		break;
	case SUBSTATE_BUSY:
		if (!(sub->substate == SUBSTATE_DIALING || sub->substate == SUBSTATE_PROGRESS || sub->substate == SUBSTATE_RINGOUT)) {
			ast_log(LOG_WARNING, "Cannot set substate to SUBSTATE_BUSY from %s (on call-%u)\n", substate2str(sub->substate), sub->callid);
			break;
		}

		if (!d->earlyrtp) {
			transmit_start_tone(d, SKINNY_BUSYTONE, l->instance, sub->callid);
		}
		send_callinfo(sub);
		transmit_callstate(d, l->instance, sub->callid, SKINNY_BUSY);
		send_displaypromptstatus(d, OCTAL_BUSY, "", 0, l->instance, sub->callid);
		sub->substate = SUBSTATE_BUSY;
		break;
	case SUBSTATE_CONGESTION:
		if (!(sub->substate == SUBSTATE_DIALING || sub->substate == SUBSTATE_PROGRESS || sub->substate == SUBSTATE_RINGOUT)) {
			ast_log(LOG_WARNING, "Cannot set substate to SUBSTATE_CONGESTION from %s (on call-%u)\n", substate2str(sub->substate), sub->callid);
			break;
		}

		if (!d->earlyrtp) {
			transmit_start_tone(d, SKINNY_REORDER, l->instance, sub->callid);
		}
		send_callinfo(sub);
		transmit_callstate(d, l->instance, sub->callid, SKINNY_CONGESTION);
		send_displaypromptstatus(d, "Congestion", "", 0, l->instance, sub->callid);
		sub->substate = SUBSTATE_CONGESTION;
		break;
	case SUBSTATE_PROGRESS:
		if (sub->substate != SUBSTATE_DIALING) {
			ast_log(LOG_WARNING, "Cannot set substate to SUBSTATE_PROGRESS from %s (on call-%u)\n", substate2str(sub->substate), sub->callid);
			break;
		}

		if (!d->earlyrtp) {
			transmit_start_tone(d, SKINNY_ALERT, l->instance, sub->callid);
		}
		send_callinfo(sub);
		transmit_callstate(d, l->instance, sub->callid, SKINNY_PROGRESS);
		send_displaypromptstatus(d, "Call Progress", "", 0, l->instance, sub->callid);
		sub->substate = SUBSTATE_PROGRESS;
		break;
	case SUBSTATE_HOLD:
		if (sub->substate != SUBSTATE_CONNECTED) {
			ast_log(LOG_WARNING, "Cannot set substate to SUBSTATE_HOLD from %s (on call-%u)\n", substate2str(sub->substate), sub->callid);
			break;
		}
		ast_queue_hold(sub->owner, l->mohsuggest);

		transmit_activatecallplane(d, l);
		transmit_closereceivechannel(d, sub);
		transmit_stopmediatransmission(d, sub);

		transmit_callstate(d, l->instance, sub->callid, SKINNY_HOLD);
		transmit_lamp_indication(d, STIMULUS_LINE, l->instance, SKINNY_LAMP_WINK);
		transmit_selectsoftkeys(d, l->instance, sub->callid, KEYDEF_ONHOLD, KEYMASK_ALL);
		sub->substate = SUBSTATE_HOLD;
		break;
	default:
		ast_log(LOG_WARNING, "Was asked to change to nonexistant substate %d on Sub-%u\n", state, sub->callid);
	}
	skinny_unlocksub(sub);
}

static void dumpsub(struct skinny_subchannel *sub, int forcehangup)
{
	struct skinny_line *l = sub->line;
	struct skinny_device *d = l->device;
	struct skinny_subchannel *activate_sub = NULL;
	struct skinny_subchannel *tsub;

	SKINNY_DEBUG(DEBUG_SUB, 3, "Sub %u - Dumping\n", sub->callid);

	if (!forcehangup && sub->substate == SUBSTATE_HOLD) {
		l->activesub = NULL;
		return;
	}

	if (sub == l->activesub) {
		d->hookstate = SKINNY_ONHOOK;
		transmit_speaker_mode(d, SKINNY_SPEAKEROFF);
		if (sub->related) {
			activate_sub = sub->related;
			setsubstate(sub, SUBSTATE_ONHOOK);
			l->activesub = activate_sub;
			if (l->activesub->substate != SUBSTATE_HOLD) {
				ast_log(LOG_WARNING, "Sub-%u was related but not at SUBSTATE_HOLD\n", sub->callid);
				return;
			}
			setsubstate(l->activesub, SUBSTATE_HOLD);
		} else {
			setsubstate(sub, SUBSTATE_ONHOOK);
			AST_LIST_TRAVERSE(&l->sub, tsub, list) {
				if (tsub->substate == SUBSTATE_CALLWAIT) {
					activate_sub = tsub;
				}
			}
			if (activate_sub) {
				setsubstate(activate_sub, SUBSTATE_RINGIN);
				return;
			}
			AST_LIST_TRAVERSE(&l->sub, tsub, list) {
				if (tsub->substate == SUBSTATE_HOLD) {
					activate_sub = tsub;
				}
			}
			if (activate_sub) {
				setsubstate(activate_sub, SUBSTATE_HOLD);
				return;
			}
		}
	} else {
		setsubstate(sub, SUBSTATE_ONHOOK);
	}
}

static void activatesub(struct skinny_subchannel *sub, int state)
{
	struct skinny_line *l = sub->line;

	SKINNY_DEBUG(DEBUG_SUB, 3, "Sub %u - Activating, and deactivating sub %u\n",
		sub->callid, l->activesub ? l->activesub->callid : 0);

	if (sub == l->activesub) {
		setsubstate(sub, state);
	} else {
		if (l->activesub) {
			if (l->activesub->substate == SUBSTATE_RINGIN) {
				setsubstate(l->activesub, SUBSTATE_CALLWAIT);
			} else if (l->activesub->substate != SUBSTATE_HOLD) {
				setsubstate(l->activesub, SUBSTATE_ONHOOK);
			}
		}
		l->activesub = sub;
		setsubstate(sub, state);
	}
}

static void dialandactivatesub(struct skinny_subchannel *sub, char exten[AST_MAX_EXTENSION])
{
	struct skinny_line *l = sub->line;
	struct skinny_device *d = l->device;

	if (sub->dialType == DIALTYPE_NORMAL) {
		SKINNY_DEBUG(DEBUG_SUB, 3, "Sub %u - Dial %s and Activate\n", sub->callid, exten);
		ast_copy_string(sub->exten, exten, sizeof(sub->exten));
		activatesub(sub, SUBSTATE_DIALING);
	} else if (sub->dialType == DIALTYPE_CFWD) {
		SKINNY_DEBUG(DEBUG_SUB, 3, "Sub %u - Set callforward(%d) to %s\n", sub->callid, sub->getforward, exten);
		set_callforwards(l, sub->exten, sub->getforward);
		dumpsub(sub, 1);
		transmit_cfwdstate(d, l);
		transmit_displaynotify(d, "CFwd enabled", 10);
	} else if (sub->dialType == DIALTYPE_XFER) {
		ast_copy_string(sub->exten, exten, sizeof(sub->exten));
		skinny_transfer_blind(sub);
	}
}

static int handle_hold_button(struct skinny_subchannel *sub)
{
	if (!sub)
		return -1;
	if (sub->related) {
		setsubstate(sub, SUBSTATE_HOLD);
		activatesub(sub->related, SUBSTATE_CONNECTED);
	} else {
		if (sub->substate == SUBSTATE_HOLD) {
			activatesub(sub, SUBSTATE_CONNECTED);
		} else {
			setsubstate(sub, SUBSTATE_HOLD);
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

	if (!sub) {
		ast_verbose("Transfer: No subchannel to transfer\n");
		return -1;
	}

	l = sub->line;
	d = l->device;

	if (!d->session) {
		ast_log(LOG_WARNING, "Device for line %s is not registered.\n", l->name);
		return -1;
	}

	if (!sub->related) {
		/* Another sub has not been created so this must be first XFER press */
		if (!(sub->substate == SUBSTATE_HOLD)) {
			setsubstate(sub, SUBSTATE_HOLD);
		}
		c = skinny_new(l, NULL, AST_STATE_DOWN, NULL, NULL, SKINNY_OUTGOING);
		if (c) {
			newsub = ast_channel_tech_pvt(c);
			/* point the sub and newsub at each other so we know they are related */
			newsub->related = sub;
			sub->related = newsub;
			newsub->xferor = 1;
			setsubstate(newsub, SUBSTATE_OFFHOOK);
		} else {
			ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
		}
	} else {
		/* We already have a related sub so we can either complete XFER or go into BLINDXFER (or cancel BLINDXFER */
		if (sub->substate == SUBSTATE_OFFHOOK) {
			if (sub->dialType == DIALTYPE_XFER) {
				sub->dialType = DIALTYPE_NORMAL;
			} else {
				sub->dialType = DIALTYPE_XFER;
			}
		} else {
			skinny_transfer_attended(sub);
		}
	}
	return 0;
}

static void handle_callforward_button(struct skinny_line *l, struct skinny_subchannel *sub, int cfwdtype)
{
	struct skinny_device *d = l->device;
	struct ast_channel *c;

	if (!d->session) {
		ast_log(LOG_WARNING, "Device for line %s is not registered.\n", l->name);
		return;
	}

	if (!sub && (l->cfwdtype & cfwdtype)) {
		set_callforwards(l, NULL, cfwdtype);
		if (sub) {
			dumpsub(sub, 1);
		}
		transmit_cfwdstate(d, l);
		transmit_displaynotify(d, "CFwd disabled", 10);
	} else {
		if (!sub || !sub->owner) {
			if (!(c = skinny_new(l, NULL, AST_STATE_DOWN, NULL, NULL, SKINNY_OUTGOING))) {
				ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
				return;
			}
			sub = ast_channel_tech_pvt(c);
			l->activesub = sub;
			setsubstate(sub, SUBSTATE_OFFHOOK);
		}
		sub->getforward |= cfwdtype;
		sub->dialType = DIALTYPE_CFWD;
	}
}

static int handle_ip_port_message(struct skinny_req *req, struct skinnysession *s)
{
	/* no response necessary */
	return 1;
}

static void handle_keepalive_message(struct skinny_req *req, struct skinnysession *s)
{
	not_used = ast_sched_del(sched, s->keepalive_timeout_sched);

#ifdef AST_DEVMODE
	{
		long keepalive_diff;
		keepalive_diff = (long) ast_tvdiff_ms(ast_tvnow(), ast_tvadd(s->last_keepalive, ast_tv(keep_alive, 0)));
		SKINNY_DEBUG(DEBUG_PACKET|DEBUG_KEEPALIVE, 3,
			"Keep_alive %d on %s, %.3fs %s\n",
				++s->keepalive_count,
				(s->device ? s->device->name : "unregistered"),
				(float) labs(keepalive_diff) / 1000,
				(keepalive_diff > 0 ? "late" : "early"));
	}
#endif

	s->keepalive_timeout_sched = ast_sched_add(sched, keep_alive*3000, skinny_nokeepalive_cb, s);
	s->last_keepalive = ast_tvnow();
	transmit_keepaliveack(s);
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
	size_t len;

	digit = letohl(req->data.keypad.button);
	lineInstance = letohl(req->data.keypad.lineInstance);
	callReference = letohl(req->data.keypad.callReference);

	if (lineInstance && callReference) {
		sub = find_subchannel_by_instance_reference(d, lineInstance, callReference);
	} else {
		sub = d->activeline->activesub;
	}

	if (!sub)
		return 0;

	l = sub->line;

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

	if ((sub->owner && ast_channel_state(sub->owner) <  AST_STATE_UP)) {
		if (-1 < sub->dialer_sched && !skinny_sched_del(sub->dialer_sched, sub)) {
			SKINNY_DEBUG(DEBUG_SUB, 3, "Sub %u - Got a digit and not timed out, so try dialing\n", sub->callid);
			sub->dialer_sched = -1;
			len = strlen(sub->exten);
			if (len == 0) {
				transmit_stop_tone(d, l->instance, sub->callid);
				transmit_selectsoftkeys(d, l->instance, sub->callid, KEYDEF_DADFD, KEYMASK_ALL&~KEYMASK_FORCEDIAL);
			}
			if (len < sizeof(sub->exten) - 1 && dgt != immed_dialchar) {
				sub->exten[len] = dgt;
				sub->exten[len + 1] = '\0';
			}
			if (len == sizeof(sub->exten) - 1 || dgt == immed_dialchar) {
				skinny_dialer(sub, 1);
			} else {
				skinny_dialer(sub, 0);
			}
		} else {
			SKINNY_DEBUG(DEBUG_SUB, 3, "Sub %u  Got a digit already timedout, ignore\n", sub->callid);
			/* Timed out so the call is being progressed elsewhere, to late for digits */
			return 0;
		}
	} else {
		SKINNY_DEBUG(DEBUG_SUB, 3, "Sub %u - Got a digit and sending as DTMF\n", sub->callid);
		f.subclass.integer = dgt;
		f.src = "skinny";
		if (sub->owner) {
			if (ast_channel_state(sub->owner) == 0) {
				f.frametype = AST_FRAME_DTMF_BEGIN;
				ast_queue_frame(sub->owner, &f);
			}
			/* XXX MUST queue this frame to all lines in threeway call if threeway call is active */
			f.frametype = AST_FRAME_DTMF_END;
			ast_queue_frame(sub->owner, &f);
			/* XXX This seriously needs to be fixed */
			if (AST_LIST_NEXT(sub, list) && AST_LIST_NEXT(sub, list)->owner) {
				if (ast_channel_state(sub->owner) == 0) {
					f.frametype = AST_FRAME_DTMF_BEGIN;
					ast_queue_frame(AST_LIST_NEXT(sub, list)->owner, &f);
				}
				f.frametype = AST_FRAME_DTMF_END;
				ast_queue_frame(AST_LIST_NEXT(sub, list)->owner, &f);
			}
		} else {
			ast_log(LOG_WARNING, "Got digit on %s, but not associated with channel\n", l->name);
		}
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
	int event;
	int instance;
#ifdef AST_DEVMODE
	int callreference;
	/* This is only used in AST_DEVMODE, as an argument to SKINNY_DEBUG */
	callreference = letohl(req->data.stimulus.callreference);
#endif

	event = letohl(req->data.stimulus.stimulus);
	instance = letohl(req->data.stimulus.stimulusInstance);

	/*  Note that this call should be using the passed in instance and callreference */
	sub = find_subchannel_by_instance_reference(d, d->lastlineinstance, d->lastcallreference);

	if (!sub) {
		l = find_line_by_instance(d, d->lastlineinstance);
		if (!l) {
			return 0;
		}
		sub = l->activesub;
	} else {
		l = sub->line;
	}

	switch(event) {
	case STIMULUS_REDIAL:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received STIMULUS_REDIAL from %s, inst %d, callref %d\n",
			d->name, instance, callreference);

		if (ast_strlen_zero(l->lastnumberdialed)) {
			ast_log(LOG_WARNING, "Attempted redial, but no previously dialed number found. Ignoring button.\n");
			break;
		}

		c = skinny_new(l, NULL, AST_STATE_DOWN, NULL, NULL, SKINNY_OUTGOING);
		if (!c) {
			ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
		} else {
			sub = ast_channel_tech_pvt(c);
			l = sub->line;
			dialandactivatesub(sub, l->lastnumberdialed);
		}
		break;
	case STIMULUS_SPEEDDIAL:
	    {
		struct skinny_speeddial *sd;

		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received STIMULUS_SPEEDDIAL from %s, inst %d, callref %d\n",
			d->name, instance, callreference);

		if (!(sd = find_speeddial_by_instance(d, instance, 0))) {
			return 0;
		}

		if (!sub || !sub->owner)
			c = skinny_new(l, NULL, AST_STATE_DOWN, NULL, NULL, SKINNY_OUTGOING);
		else
			c = sub->owner;

		if (!c) {
			ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
		} else {
			sub = ast_channel_tech_pvt(c);
			dialandactivatesub(sub, sd->exten);
		}
	    }
		break;
	case STIMULUS_HOLD:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received STIMULUS_HOLD from %s, inst %d, callref %d\n",
			d->name, instance, callreference);
		handle_hold_button(sub);
		break;
	case STIMULUS_TRANSFER:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received STIMULUS_TRANSFER from %s, inst %d, callref %d\n",
			d->name, instance, callreference);
		if (l->transfer)
			handle_transfer_button(sub);
		else
			transmit_displaynotify(d, "Transfer disabled", 10);
		break;
	case STIMULUS_CONFERENCE:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received STIMULUS_CONFERENCE from %s, inst %d, callref %d\n",
			d->name, instance, callreference);
		/* XXX determine the best way to pull off a conference.  Meetme? */
		break;
	case STIMULUS_VOICEMAIL:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received STIMULUS_VOICEMAIL from %s, inst %d, callref %d\n",
			d->name, instance, callreference);

		if (!sub || !sub->owner) {
			c = skinny_new(l, NULL, AST_STATE_DOWN, NULL, NULL, SKINNY_OUTGOING);
		} else {
			c = sub->owner;
		}

		if (!c) {
			ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
			break;
		}

		sub = ast_channel_tech_pvt(c);
		if (sub->substate == SUBSTATE_UNSET || sub->substate == SUBSTATE_OFFHOOK){
			dialandactivatesub(sub, l->vmexten);
		}
		break;
	case STIMULUS_CALLPARK:
		{
		char extout[AST_MAX_EXTENSION];
		RAII_VAR(struct ast_bridge_channel *, bridge_channel, NULL, ao2_cleanup);
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received STIMULUS_CALLPARK from %s, inst %d, callref %d\n",
			d->name, instance, callreference);

		if (!ast_parking_provider_registered()) {
			transmit_displaynotify(d, "Call Park not available", 10);
			break;
		}

		if ((sub && sub->owner) && (ast_channel_state(sub->owner) ==  AST_STATE_UP)) {
			c = sub->owner;
			ast_channel_lock(c);
			bridge_channel = ast_channel_get_bridge_channel(c);
			ast_channel_unlock(c);

			if (!bridge_channel) {
				transmit_displaynotify(d, "Call Park failed", 10);
				break;
			}

			if (!ast_parking_park_call(bridge_channel, extout, sizeof(extout))) {
				static const char msg_prefix[] = "Call Parked at: ";
				char message[sizeof(msg_prefix) + sizeof(extout)];

				snprintf(message, sizeof(message), "%s%s", msg_prefix, extout);
				transmit_displaynotify(d, message, 10);
				break;
			}
			transmit_displaynotify(d, "Call Park failed", 10);
		} else {
			transmit_displaynotify(d, "Call Park not available", 10);
		}
		break;
		}
	case STIMULUS_DND:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received STIMULUS_DND from %s, inst %d, callref %d\n",
			d->name, instance, callreference);

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
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received STIMULUS_FORWARDALL from %s, inst %d, callref %d\n",
			d->name, instance, callreference);
		handle_callforward_button(l, sub, SKINNY_CFWD_ALL);
		break;
	case STIMULUS_FORWARDBUSY:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received STIMULUS_FORWARDBUSY from %s, inst %d, callref %d\n",
			d->name, instance, callreference);
		handle_callforward_button(l, sub, SKINNY_CFWD_BUSY);
		break;
	case STIMULUS_FORWARDNOANSWER:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received STIMULUS_FORWARDNOANSWER from %s, inst %d, callref %d\n",
			d->name, instance, callreference);
		handle_callforward_button(l, sub, SKINNY_CFWD_NOANSWER);
		break;
	case STIMULUS_DISPLAY:
		/* Not sure what this is */
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received STIMULUS_DISPLAY from %s, inst %d, callref %d\n",
			d->name, instance, callreference);
		break;
	case STIMULUS_LINE:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received STIMULUS_LINE from %s, inst %d, callref %d\n",
			d->name, instance, callreference);

		l = find_line_by_instance(d, instance);

		if (!l) {
			return 0;
		}

		d->activeline = l;

		/* turn the speaker on */
		transmit_speaker_mode(d, SKINNY_SPEAKERON);
		transmit_ringer_mode(d, SKINNY_RING_OFF);
		transmit_lamp_indication(d, STIMULUS_LINE, l->instance, SKINNY_LAMP_ON);

		d->hookstate = SKINNY_OFFHOOK;

		if (sub && sub->calldirection == SKINNY_INCOMING) {
			setsubstate(sub, SUBSTATE_CONNECTED);
		} else {
			if (sub && sub->owner) {
				ast_debug(1, "Current subchannel [%s] already has owner\n", ast_channel_name(sub->owner));
			} else {
				c = skinny_new(l, NULL, AST_STATE_DOWN, NULL, NULL, SKINNY_OUTGOING);
				if (c) {
					setsubstate(ast_channel_tech_pvt(c), SUBSTATE_OFFHOOK);
				} else {
					ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
				}
			}
		}
		break;
	default:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received UNKNOWN_STIMULUS(%d) from %s, inst %d, callref %d\n",
			event, d->name, instance, callreference);
		break;
	}
	ast_devstate_changed(AST_DEVICE_UNKNOWN, AST_DEVSTATE_CACHABLE, "Skinny/%s", l->name);

	return 1;
}

static int handle_offhook_message(struct skinny_req *req, struct skinnysession *s)
{
	struct skinny_device *d = s->device;
	struct skinny_line *l = NULL;
	struct skinny_subchannel *sub = NULL;
	struct ast_channel *c;
	int instance;
	int reference;

	instance = letohl(req->data.offhook.instance);
	reference = letohl(req->data.offhook.reference);

	if (d->hookstate == SKINNY_OFFHOOK) {
		ast_verb(3, "Got offhook message when device (%s) already offhook\n", d->name);
		return 0;
	}

	if (reference) {
		sub = find_subchannel_by_instance_reference(d, instance, reference);
		if (sub) {
			l = sub->line;
		}
	}
	if (!sub) {
		if (instance) {
			l = find_line_by_instance(d, instance);
		} else {
			l = d->activeline;
		}
		sub = l->activesub;
	}

	transmit_ringer_mode(d, SKINNY_RING_OFF);
	d->hookstate = SKINNY_OFFHOOK;

	ast_devstate_changed(AST_DEVICE_INUSE, AST_DEVSTATE_CACHABLE, "Skinny/%s", l->name);

	if (sub && sub->substate == SUBSTATE_HOLD) {
		return 1;
	}

	transmit_lamp_indication(d, STIMULUS_LINE, l->instance, SKINNY_LAMP_ON);

	if (sub && sub->calldirection == SKINNY_INCOMING) {
		setsubstate(sub, SUBSTATE_CONNECTED);
	} else {
		/* Not ideal, but let's send updated time at onhook and offhook, as it clears the display */
		transmit_definetimedate(d);

		if (sub && sub->owner) {
			ast_debug(1, "Current sub [%s] already has owner\n", ast_channel_name(sub->owner));
		} else {
			c = skinny_new(l, NULL, AST_STATE_DOWN, NULL, NULL, SKINNY_OUTGOING);
			if (c) {
				setsubstate(ast_channel_tech_pvt(c), SUBSTATE_OFFHOOK);
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

	instance = letohl(req->data.onhook.instance);
	reference = letohl(req->data.onhook.reference);

	if (instance && reference) {
		sub = find_subchannel_by_instance_reference(d, instance, reference);
		if (!sub) {
			return 0;
		}
		l = sub->line;
	} else {
		l = d->activeline;
		sub = l->activesub;
		if (!sub) {
			return 0;
		}
	}

	if (d->hookstate == SKINNY_ONHOOK) {
		/* Something else already put us back on hook */
		/* Not ideal, but let's send updated time anyway, as it clears the display */
		transmit_definetimedate(d);
		return 0;
	}

	if (l->transfer && sub->xferor && ast_channel_state(sub->owner) >= AST_STATE_RING) {
		/* We're allowed to transfer, we have two active calls and
		   we made at least one of the calls.  Let's try and transfer */
		handle_transfer_button(sub);
		return 0;
	}

	ast_devstate_changed(AST_DEVICE_NOT_INUSE, AST_DEVSTATE_CACHABLE, "Skinny/%s", l->name);

	dumpsub(sub, 0);

	d->hookstate = SKINNY_ONHOOK;

	/* Not ideal, but let's send updated time at onhook and offhook, as it clears the display */
	transmit_definetimedate(d);

	return 1;
}

static int handle_capabilities_res_message(struct skinny_req *req, struct skinnysession *s)
{
	struct skinny_device *d = s->device;
	struct skinny_line *l;
	uint32_t count = 0;
	struct ast_format_cap *codecs = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	int i;
#ifdef AST_DEVMODE
	struct ast_str *codec_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);
#endif


	if (!codecs) {
		return 0;
	}

	count = letohl(req->data.caps.count);
	if (count > SKINNY_MAX_CAPABILITIES) {
		count = SKINNY_MAX_CAPABILITIES;
		ast_log(LOG_WARNING, "Received more capabilities than we can handle (%d).  Ignoring the rest.\n", SKINNY_MAX_CAPABILITIES);
	}

	for (i = 0; i < count; i++) {
		struct ast_format *acodec;
		int scodec = 0;
		scodec = letohl(req->data.caps.caps[i].codec);
		acodec = codec_skinny2ast(scodec);
		SKINNY_DEBUG(DEBUG_AUDIO, 4, "Adding codec capability %s (%d)\n", ast_format_get_name(acodec), scodec);
		ast_format_cap_append(codecs, acodec, 0);
	}

	ast_format_cap_get_compatible(d->confcap, codecs, d->cap);
	SKINNY_DEBUG(DEBUG_AUDIO, 4, "Device capability set to '%s'\n", ast_format_cap_get_names(d->cap, &codec_buf));
	AST_LIST_TRAVERSE(&d->lines, l, list) {
		ast_mutex_lock(&l->lock);
		ast_format_cap_get_compatible(l->confcap, d->cap, l->cap);
		ast_mutex_unlock(&l->lock);
	}

	ao2_ref(codecs, -1);
	return 1;
}

static int handle_button_template_req_message(struct skinny_req *req, struct skinnysession *s)
{
	struct skinny_device *d = s->device;
	struct skinny_line *l;
	int i;
	struct skinny_speeddial *sd;
	struct skinny_serviceurl *surl;
	struct button_definition_template btn[42];
	int lineInstance = 1;
	int speeddialInstance = 1;
	int serviceurlInstance = 1;
	int buttonCount = 0;

	if (!(req = req_alloc(sizeof(struct button_template_res_message), BUTTON_TEMPLATE_RES_MESSAGE)))
		return -1;

	SKINNY_DEBUG(DEBUG_TEMPLATE, 3, "Creating Button Template\n");

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
						SKINNY_DEBUG(DEBUG_TEMPLATE, 4, "Adding button: %d, %d\n", BT_LINE, lineInstance);
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
							SKINNY_DEBUG(DEBUG_TEMPLATE, 4, "Adding button: %d, %d\n", BT_LINE, lineInstance);
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
						SKINNY_DEBUG(DEBUG_TEMPLATE, 4, "Adding button: %d, %d\n", BT_LINE, lineInstance);
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
							SKINNY_DEBUG(DEBUG_TEMPLATE, 4, "Adding button: %d, %d\n", BT_LINE, lineInstance);
							req->data.buttontemplate.definition[i].buttonDefinition = BT_LINE;
							req->data.buttontemplate.definition[i].instanceNumber = lineInstance;
							lineInstance++;
							buttonCount++;
							btnSet = 1;
							break;
						} else if (!sd->isHint && sd->instance == speeddialInstance) {
							SKINNY_DEBUG(DEBUG_TEMPLATE, 4, "Adding button: %d, %d\n", BT_SPEEDDIAL, speeddialInstance);
							req->data.buttontemplate.definition[i].buttonDefinition = BT_SPEEDDIAL;
							req->data.buttontemplate.definition[i].instanceNumber = speeddialInstance;
							speeddialInstance++;
							buttonCount++;
							btnSet = 1;
							break;
						}
					}
				}

				if (!btnSet) {
					AST_LIST_TRAVERSE(&d->serviceurls, surl, list) {
						if (surl->instance == serviceurlInstance) {
							SKINNY_DEBUG(DEBUG_TEMPLATE, 4, "Adding button: %d, %d\n", BT_SERVICEURL, serviceurlInstance);
							req->data.buttontemplate.definition[i].buttonDefinition = BT_SERVICEURL;
							req->data.buttontemplate.definition[i].instanceNumber = serviceurlInstance;
							serviceurlInstance++;
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
						SKINNY_DEBUG(DEBUG_TEMPLATE, 4, "Adding button: %d, %d\n", BT_LINE, lineInstance);
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
						SKINNY_DEBUG(DEBUG_TEMPLATE, 4, "Adding button: %d, %d\n", BT_SPEEDDIAL, speeddialInstance);
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
				SKINNY_DEBUG(DEBUG_TEMPLATE, 4, "Adding button: %d, %d\n", btn[i].buttonDefinition, 0);
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

	SKINNY_DEBUG(DEBUG_PACKET | DEBUG_TEMPLATE, 3, "Transmitting BUTTON_TEMPLATE_RES_MESSAGE to %s, type %d\n",
		d->name, d->type);
	transmit_response(d, req);
	return 1;
}

static int handle_open_receive_channel_ack_message(struct skinny_req *req, struct skinnysession *s)
{
	struct skinny_device *d = s->device;
	struct skinny_line *l;
	struct skinny_subchannel *sub;
	struct sockaddr_in sin = { 0, };
	struct sockaddr_in us = { 0, };
	struct ast_sockaddr sin_tmp;
	struct ast_sockaddr us_tmp;
	struct ast_format *tmpfmt;
	uint32_t addr;
	int port;
	int status;
	int callid;
	unsigned int framing;

	status = (d->protocolversion<17) ? letohl(req->data.openreceivechannelack_ip4.status) : letohl(req->data.openreceivechannelack_ip6.status);

	if (status) {
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received OPEN_RECEIVE_CHANNEL_ACK_MESSAGE from %s, status %d\n",
			d->name, status);
		ast_log(LOG_ERROR, "Open Receive Channel Failure\n");
		return 0;
	}

	if (d->protocolversion<17) {
		addr = req->data.openreceivechannelack_ip4.ipAddr;
		port = letohl(req->data.openreceivechannelack_ip4.port);
		callid = letohl(req->data.openreceivechannelack_ip4.callReference);
	} else {
		memcpy(&addr, &req->data.openreceivechannelack_ip6.ipAddr, sizeof(addr));
		port = letohl(req->data.openreceivechannelack_ip6.port);
		callid = letohl(req->data.openreceivechannelack_ip6.callReference);
	}

	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = addr;
	sin.sin_port = htons(port);

	SKINNY_DEBUG(DEBUG_PACKET, 3, "Received OPEN_RECEIVE_CHANNEL_ACK_MESSAGE from %s, status %d, callid %d, ip %s:%d\n",
		d->name, status, callid, ast_inet_ntoa(sin.sin_addr), port);

	sub = find_subchannel_by_reference(d, callid);

	if (!sub) {
		ast_log(LOG_ERROR, "Open Receive Channel Failure - can't find sub for %d\n", callid);
		return 0;
	}

	l = sub->line;

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

	SKINNY_DEBUG(DEBUG_PACKET, 4, "device ipaddr = %s:%d\n", ast_inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
	SKINNY_DEBUG(DEBUG_PACKET, 4, "asterisk ipaddr = %s:%d\n", ast_inet_ntoa(us.sin_addr), ntohs(us.sin_port));

	tmpfmt = ast_format_cap_get_format(l->cap, 0);
	framing = ast_format_cap_get_format_framing(l->cap, tmpfmt);

	SKINNY_DEBUG(DEBUG_PACKET, 4, "Setting payloadType to '%s' (%u ms)\n", ast_format_get_name(tmpfmt), framing);

	transmit_startmediatransmission(d, sub, us, tmpfmt, framing);

	ao2_ref(tmpfmt, -1);

	return 1;
}

static int handle_enbloc_call_message(struct skinny_req *req, struct skinnysession *s)
{
	struct skinny_device *d = s->device;
	struct skinny_line *l;
	struct skinny_subchannel *sub = NULL;
	struct ast_channel *c;

	sub = find_subchannel_by_instance_reference(d, d->lastlineinstance, d->lastcallreference);

	if (!sub) {
		l = find_line_by_instance(d, d->lastlineinstance);
		if (!l) {
			return 0;
		}
	} else {
		l = sub->line;
	}

	c = skinny_new(l, NULL, AST_STATE_DOWN, NULL, NULL, SKINNY_OUTGOING);

	if(!c) {
		ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
	} else {
		d->hookstate = SKINNY_OFFHOOK;

		sub = ast_channel_tech_pvt(c);
		dialandactivatesub(sub, req->data.enbloccallmessage.calledParty);
	}

	return 1;
}

static int handle_soft_key_event_message(struct skinny_req *req, struct skinnysession *s)
{
	struct skinny_device *d = s->device;
	struct skinny_line *l;
	struct skinny_subchannel *sub = NULL;
	struct ast_channel *c;
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
		ast_log(LOG_WARNING,  "Received Softkey Event: %d(%d/%d) but can't find line\n", event, instance, callreference);
		return 0;
	}

	ast_devstate_changed(AST_DEVICE_INUSE, AST_DEVSTATE_CACHABLE, "Skinny/%s", l->name);

	switch(event) {
	case SOFTKEY_NONE:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received SOFTKEY_NONE from %s, inst %d, callref %d\n",
			d->name, instance, callreference);
		break;
	case SOFTKEY_REDIAL:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received SOFTKEY_REDIAL from %s, inst %d, callref %d\n",
			d->name, instance, callreference);

		if (ast_strlen_zero(l->lastnumberdialed)) {
			ast_log(LOG_WARNING, "Attempted redial, but no previously dialed number found. Ignoring button.\n");
			break;
		}

		if (!sub || !sub->owner) {
			c = skinny_new(l, NULL, AST_STATE_DOWN, NULL, NULL, SKINNY_OUTGOING);
		} else {
			c = sub->owner;
		}

		if (!c) {
			ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
		} else {
			sub = ast_channel_tech_pvt(c);
			dialandactivatesub(sub, l->lastnumberdialed);
		}
		break;
	case SOFTKEY_NEWCALL:  /* Actually the DIAL softkey */
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received SOFTKEY_NEWCALL from %s, inst %d, callref %d\n",
			d->name, instance, callreference);

		/* New Call ALWAYS gets a new sub-channel */
		c = skinny_new(l, NULL, AST_STATE_DOWN, NULL, NULL, SKINNY_OUTGOING);
		sub = ast_channel_tech_pvt(c);

		if (!c) {
			ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
		} else {
			activatesub(sub, SUBSTATE_OFFHOOK);
		}
		break;
	case SOFTKEY_HOLD:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received SOFTKEY_HOLD from %s, inst %d, callref %d\n",
			d->name, instance, callreference);

		if (sub) {
			setsubstate(sub, SUBSTATE_HOLD);
		} else { /* No sub, maybe an SLA call */
			struct skinny_subline *subline;
			if ((subline = find_subline_by_callid(d, callreference))) {
				setsubstate(subline->sub, SUBSTATE_HOLD);
			}
		}

		break;
	case SOFTKEY_TRNSFER:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received SOFTKEY_TRNSFER from %s, inst %d, callref %d\n",
			d->name, instance, callreference);
		if (l->transfer)
			handle_transfer_button(sub);
		else
			transmit_displaynotify(d, "Transfer disabled", 10);

		break;
	case SOFTKEY_DND:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received SOFTKEY_DND from %s, inst %d, callref %d\n",
			d->name, instance, callreference);

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
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received SOFTKEY_CFWDALL from %s, inst %d, callref %d\n",
			d->name, instance, callreference);
		handle_callforward_button(l, sub, SKINNY_CFWD_ALL);
		break;
	case SOFTKEY_CFWDBUSY:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received SOFTKEY_CFWDBUSY from %s, inst %d, callref %d\n",
			d->name, instance, callreference);
		handle_callforward_button(l, sub, SKINNY_CFWD_BUSY);
		break;
	case SOFTKEY_CFWDNOANSWER:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received SOFTKEY_CFWDNOANSWER from %s, inst %d, callref %d\n",
			d->name, instance, callreference);
		handle_callforward_button(l, sub, SKINNY_CFWD_NOANSWER);
		break;
	case SOFTKEY_BKSPC:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received SOFTKEY_BKSPC from %s, inst %d, callref %d\n",
			d->name, instance, callreference);
		if (-1 < sub->dialer_sched && !skinny_sched_del(sub->dialer_sched, sub)) {
			size_t len;
			sub->dialer_sched = -1;
			len = strlen(sub->exten);
			if (len > 0) {
				sub->exten[len-1] = '\0';
				if (len == 1) {
					transmit_start_tone(d, SKINNY_DIALTONE, l->instance, sub->callid);
					transmit_selectsoftkeys(d, l->instance, sub->callid, KEYDEF_OFFHOOK, KEYMASK_ALL);
				}
				transmit_backspace(d, l->instance, sub->callid);
			}
			sub->dialer_sched = skinny_sched_add(gendigittimeout, skinny_dialer_cb, sub);
		}
		break;
	case SOFTKEY_ENDCALL:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received SOFTKEY_ENDCALL from %s, inst %d, callref %d\n",
			d->name, instance, callreference);

		ast_devstate_changed(AST_DEVICE_NOT_INUSE, AST_DEVSTATE_CACHABLE, "Skinny/%s", l->name);

		if (sub) {
			dumpsub(sub, 1);
		} else { /* No sub, maybe an SLA call */
			struct skinny_subline *subline;
			if ((subline = find_subline_by_callid(d, callreference))) {
				dumpsub(subline->sub, 1);
			}
		}

		d->hookstate = SKINNY_ONHOOK;

		/* Not ideal, but let's send updated time at onhook and offhook, as it clears the display */
		transmit_definetimedate(d);

		break;
	case SOFTKEY_RESUME:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received SOFTKEY_RESUME from %s, inst %d, callref %d\n",
			d->name, instance, callreference);

		if (sub) {
			activatesub(sub, SUBSTATE_CONNECTED);
		} else { /* No sub, maybe an inactive SLA call */
			struct skinny_subline *subline;
			subline = find_subline_by_callid(d, callreference);
			c = skinny_new(l, subline, AST_STATE_DOWN, NULL, NULL, SKINNY_OUTGOING);
			if (!c) {
				ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
			} else {
				sub = ast_channel_tech_pvt(c);
				dialandactivatesub(sub, subline->exten);
			}
		}
		break;
	case SOFTKEY_ANSWER:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received SOFTKEY_ANSWER from %s, inst %d, callref %d\n",
			d->name, instance, callreference);

		transmit_ringer_mode(d, SKINNY_RING_OFF);
		transmit_lamp_indication(d, STIMULUS_LINE, l->instance, SKINNY_LAMP_ON);
		if (d->hookstate == SKINNY_ONHOOK) {
			transmit_speaker_mode(d, SKINNY_SPEAKERON);
			d->hookstate = SKINNY_OFFHOOK;
		}

		if (sub && sub->calldirection == SKINNY_INCOMING) {
			activatesub(sub, SUBSTATE_CONNECTED);
		}
		break;
	case SOFTKEY_INFO:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received SOFTKEY_INFO from %s, inst %d, callref %d\n",
			d->name, instance, callreference);
		break;
	case SOFTKEY_CONFRN:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received SOFTKEY_CONFRN from %s, inst %d, callref %d\n",
			d->name, instance, callreference);
		/* XXX determine the best way to pull off a conference.  Meetme? */
		break;
	case SOFTKEY_PARK:
		{
		char extout[AST_MAX_EXTENSION];
		RAII_VAR(struct ast_bridge_channel *, bridge_channel, NULL, ao2_cleanup);
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received SOFTKEY_PARK from %s, inst %d, callref %d\n",
			d->name, instance, callreference);

		if (!ast_parking_provider_registered()) {
			transmit_displaynotify(d, "Call Park not available", 10);
			break;
		}

		if ((sub && sub->owner) && (ast_channel_state(sub->owner) ==  AST_STATE_UP)) {
			c = sub->owner;
			ast_channel_lock(c);
			bridge_channel = ast_channel_get_bridge_channel(c);
			ast_channel_unlock(c);

			if (!bridge_channel) {
				transmit_displaynotify(d, "Call Park failed", 10);
				break;
			}

			if (!ast_parking_park_call(bridge_channel, extout, sizeof(extout))) {
				static const char msg_prefix[] = "Call Parked at: ";
				char message[sizeof(msg_prefix) + sizeof(extout)];

				snprintf(message, sizeof(message), "%s%s", msg_prefix, extout);
				transmit_displaynotify(d, message, 10);
				break;
			}
			transmit_displaynotify(d, "Call Park failed", 10);
		} else {
			transmit_displaynotify(d, "Call Park not available", 10);
		}
		break;
		}
	case SOFTKEY_JOIN:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received SOFTKEY_JOIN from %s, inst %d, callref %d\n",
			d->name, instance, callreference);
		/* this is SLA territory, should not get here unless there is a meetme at subline */
		{
			struct skinny_subline *subline;
			subline = find_subline_by_callid(d, callreference);
			c = skinny_new(l, subline, AST_STATE_DOWN, NULL, NULL, SKINNY_OUTGOING);
			if (!c) {
				ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
			} else {
				sub = ast_channel_tech_pvt(c);
				dialandactivatesub(sub, subline->exten);
			}
		}
		break;
	case SOFTKEY_MEETME:
		/* XXX How is this different from CONFRN? */
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received SOFTKEY_MEETME from %s, inst %d, callref %d\n",
			d->name, instance, callreference);
		break;
	case SOFTKEY_PICKUP:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received SOFTKEY_PICKUP from %s, inst %d, callref %d\n",
			d->name, instance, callreference);
		break;
	case SOFTKEY_GPICKUP:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received SOFTKEY_GPICKUP from %s, inst %d, callref %d\n",
			d->name, instance, callreference);
		if (!sub || !sub->owner) {
			c = skinny_new(l, NULL, AST_STATE_DOWN, NULL, NULL, SKINNY_INCOMING);
		} else {
			c = sub->owner;
		}

		if (!c) {
			ast_log(LOG_WARNING, "Unable to create channel for %s@%s\n", l->name, d->name);
		} else {
			ast_channel_ref(c);
			sub = ast_channel_tech_pvt(c);
			ast_pickup_call(c);
			if (sub->owner == c) {
				ast_channel_unref(c);
				dumpsub(sub, 1);
			} else {
				ast_hangup(c);
				setsubstate(sub, SUBSTATE_CONNECTED);
				ast_channel_unref(c);
			}
		}
		break;
	case SOFTKEY_FORCEDIAL:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received SOFTKEY_FORCEDIAL from %s, inst %d, callref %d\n",
			d->name, instance, callreference);
		skinny_dialer(sub, 1);

		break;
	default:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received SOFTKEY_UNKNOWN(%d) from %s, inst %d, callref %d\n",
			event, d->name, instance, callreference);
		break;
	}

	return 1;
}

static int handle_message(struct skinny_req *req, struct skinnysession *s)
{
	int res = 0;
	struct skinny_speeddial *sd;
	struct skinny_device *d = s->device;

	if (!d && !(letohl(req->e) == REGISTER_MESSAGE || letohl(req->e) == ALARM_MESSAGE || letohl(req->e) == KEEP_ALIVE_MESSAGE)) {
		ast_log(LOG_WARNING, "Client sent message #%u without first registering.\n", req->e);
		return 0;
	}

	switch(letohl(req->e)) {
	case KEEP_ALIVE_MESSAGE:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received KEEP_ALIVE_MESSAGE from %s\n", (d ? d->name : "unregistered"));
		handle_keepalive_message(req, s);
		break;
	case REGISTER_MESSAGE:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received REGISTER_MESSAGE from %s, name %s, type %u, protovers %d\n",
			d->name, req->data.reg.name, letohl(req->data.reg.type), letohl(req->data.reg.protocolVersion));
		res = skinny_register(req, s);
		if (!res) {
			sleep(2);
			res = skinny_register(req, s);
		}
		if (res != 1) {
			transmit_registerrej(s);
			return -1;
		}
		ast_atomic_fetchadd_int(&unauth_sessions, -1);
		ast_verb(3, "Device '%s' successfully registered (protoVers %d)\n", s->device->name, s->device->protocolversion);
		transmit_registerack(s->device);
		transmit_capabilitiesreq(s->device);
		res = 0;
		break;
	case IP_PORT_MESSAGE:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received IP_PORT_MESSAGE from %s\n", d->name);
		res = handle_ip_port_message(req, s);
		break;
	case KEYPAD_BUTTON_MESSAGE:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received KEYPAD_BUTTON_MESSAGE from %s, digit %u, inst %u, callref %u\n",
			d->name, letohl(req->data.keypad.button), letohl(req->data.keypad.lineInstance), letohl(req->data.keypad.callReference));
		res = handle_keypad_button_message(req, s);
		break;
	case ENBLOC_CALL_MESSAGE:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received ENBLOC_CALL_MESSAGE from %s, calledParty %s\n",
			d->name, req->data.enbloccallmessage.calledParty);
		res = handle_enbloc_call_message(req, s);
		break;
	case STIMULUS_MESSAGE:
		/* SKINNY_PACKETDEBUG handled in handle_stimulus_message */
		res = handle_stimulus_message(req, s);
		break;
	case OFFHOOK_MESSAGE:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received OFFHOOK_MESSAGE from %s, inst %u, ref %u\n",
			d->name, letohl(req->data.offhook.instance), letohl(req->data.offhook.reference));
		res = handle_offhook_message(req, s);
		break;
	case ONHOOK_MESSAGE:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received ONHOOK_MESSAGE from %s, inst %u, ref %u\n",
			d->name, letohl(req->data.offhook.instance), letohl(req->data.offhook.reference));
		res = handle_onhook_message(req, s);
		break;
	case CAPABILITIES_RES_MESSAGE:
		SKINNY_DEBUG(DEBUG_PACKET | DEBUG_AUDIO, 3, "Received CAPABILITIES_RES_MESSAGE from %s, count %u, codec data\n",
			d->name, letohl(req->data.caps.count));
		res = handle_capabilities_res_message(req, s);
		break;
	case SPEED_DIAL_STAT_REQ_MESSAGE:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received SPEED_DIAL_STAT_REQ_MESSAGE from %s, sdNum %u\n",
			d->name, letohl(req->data.speeddialreq.speedDialNumber));
		if ( (sd = find_speeddial_by_instance(s->device, letohl(req->data.speeddialreq.speedDialNumber), 0)) ) {
			transmit_speeddialstatres(d, sd);
		}
		break;
	case LINE_STATE_REQ_MESSAGE:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received LINE_STATE_REQ_MESSAGE from %s, lineNum %u\n",
			d->name, letohl(req->data.line.lineNumber));
		transmit_linestatres(d, letohl(req->data.line.lineNumber));
		break;
	case TIME_DATE_REQ_MESSAGE:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received TIME_DATE_REQ_MESSAGE from %s\n", d->name);
		transmit_definetimedate(d);
		break;
	case BUTTON_TEMPLATE_REQ_MESSAGE:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received BUTTON_TEMPLATE_REQ_MESSAGE from %s\n", d->name);
		res = handle_button_template_req_message(req, s);
		break;
	case VERSION_REQ_MESSAGE:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received VERSION_REQ_MESSAGE from %s\n", d->name);
		transmit_versionres(d);
		break;
	case SERVER_REQUEST_MESSAGE:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received SERVER_REQUEST_MESSAGE from %s\n", d->name);
		transmit_serverres(d);
		break;
	case ALARM_MESSAGE:
		/* no response necessary */
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received ALARM_MESSAGE from %s, alarm %s\n",
			d->name, req->data.alarm.displayMessage);
		break;
	case OPEN_RECEIVE_CHANNEL_ACK_MESSAGE:
		/* SKINNY_PACKETDEBUG handled in handle_open_receive_channel_ack_message */
		res = handle_open_receive_channel_ack_message(req, s);
		break;
	case SOFT_KEY_SET_REQ_MESSAGE:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received SOFT_KEY_SET_REQ_MESSAGE from %s\n", d->name);
		transmit_softkeysetres(d);
		transmit_selectsoftkeys(d, 0, 0, KEYDEF_ONHOOK, KEYMASK_ALL);
		break;
	case SOFT_KEY_EVENT_MESSAGE:
		/* SKINNY_PACKETDEBUG handled in handle_soft_key_event_message */
		res = handle_soft_key_event_message(req, s);
		break;
	case UNREGISTER_MESSAGE:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received UNREGISTER_MESSAGE from %s\n", d->name);
		ast_log(LOG_NOTICE, "Received UNREGISTER_MESSAGE from %s\n", d->name);
		end_session(s);
		break;
	case SOFT_KEY_TEMPLATE_REQ_MESSAGE:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received SOFT_KEY_TEMPLATE_REQ_MESSAGE from %s\n", d->name);
		transmit_softkeytemplateres(d);
		break;
	case HEADSET_STATUS_MESSAGE:
		/* XXX umm...okay?  Why do I care? */
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received HEADSET_STATUS_MESSAGE from %s\n", d->name);
		break;
	case REGISTER_AVAILABLE_LINES_MESSAGE:
		/* XXX I have no clue what this is for, but my phone was sending it, so... */
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received REGISTER_AVAILABLE_LINES_MESSAGE from %s\n", d->name);
		break;
	case SERVICEURL_STATREQ_MESSAGE:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "SERVICEURL_STATREQ_MESSAGE from %s\n", d->name);
		transmit_serviceurlstat(d, letohl(req->data.serviceurlmessage.instance));
		break;
	default:
		SKINNY_DEBUG(DEBUG_PACKET, 3, "Received UNKNOWN_MESSAGE(%x) from %s\n", (unsigned)letohl(req->e), d->name);
		break;
	}
	return res;
}

static void destroy_session(struct skinnysession *s)
{
	ast_mutex_lock(&s->lock);
	if (s->fd > -1) {
		close(s->fd);
	}

	if (s->device) {
		s->device->session = NULL;
	} else {
		ast_atomic_fetchadd_int(&unauth_sessions, -1);
	}
	ast_mutex_unlock(&s->lock);
	ast_mutex_destroy(&s->lock);

	if (s->t != AST_PTHREADT_NULL) {
		pthread_detach(s->t);
	}

	ast_free(s);
}

static int skinny_noauth_cb(const void *data)
{
	struct skinnysession *s = (struct skinnysession *)data;
	ast_log(LOG_WARNING, "Skinny Client failed to authenticate in %d seconds (SCHED %d)\n", auth_timeout, s->auth_timeout_sched);
	s->auth_timeout_sched = -1;
	end_session(s);
	return 0;
}

static int skinny_nokeepalive_cb(const void *data)
{
	struct skinnysession *s = (struct skinnysession *)data;
	ast_log(LOG_WARNING, "Skinny Client failed to send keepalive in last %d seconds (SCHED %d)\n", keep_alive*3, s->keepalive_timeout_sched);
	s->keepalive_timeout_sched = -1;
	end_session(s);
	return 0;
}

static void skinny_session_cleanup(void *data)
{
	struct skinnysession *s = (struct skinnysession *)data;
	struct skinny_device *d = s->device;
	struct skinny_line *l;
	struct skinny_speeddial *sd;

	ast_log(LOG_NOTICE, "Ending Skinny session from %s at %s\n", d ? d->name : "unknown", ast_inet_ntoa(s->sin.sin_addr));

	if (s->lockstate) {
		ast_mutex_unlock(&s->lock);
	}

	if (-1 < s->auth_timeout_sched) {
		not_used = ast_sched_del(sched, s->auth_timeout_sched);
		s->auth_timeout_sched = -1;
	}
	if (-1 < s->keepalive_timeout_sched) {
		not_used = ast_sched_del(sched, s->keepalive_timeout_sched);
		s->keepalive_timeout_sched = -1;
	}

	if (d) {
		RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);
		d->session = NULL;

		AST_LIST_TRAVERSE(&d->speeddials, sd, list) {
			if (sd->stateid > -1)
				ast_extension_state_del(sd->stateid, NULL);
		}
		AST_LIST_TRAVERSE(&d->lines, l, list) {
			if (l->device != d) {
				continue;
			}
			ast_format_cap_remove_by_type(l->cap, AST_MEDIA_TYPE_UNKNOWN);
			ast_format_cap_update_by_allow_disallow(l->cap, "all", 0);
			l->instance = 0;
			unregister_exten(l);
			ast_devstate_changed(AST_DEVICE_UNAVAILABLE, AST_DEVSTATE_CACHABLE, "Skinny/%s", l->name);
		}
		ast_endpoint_set_state(d->endpoint, AST_ENDPOINT_OFFLINE);
		blob = ast_json_pack("{s: s}", "peer_status", "Unregistered");
		ast_endpoint_blob_publish(d->endpoint, ast_endpoint_state_type(), blob);
	}

	AST_LIST_LOCK(&sessions);
	AST_LIST_REMOVE(&sessions, s, list);
	AST_LIST_UNLOCK(&sessions);

	destroy_session(s);
}

#define PACKET_TIMEOUT 10000

static void *skinny_session(void *data)
{
	int res;
	int bytesread;
	struct skinny_req *req = NULL;
	struct skinnysession *s = data;

	int dlen = 0;
	int eventmessage = 0;
	struct pollfd fds[1];

	ast_log(LOG_NOTICE, "Starting Skinny session from %s\n", ast_inet_ntoa(s->sin.sin_addr));

	pthread_cleanup_push(skinny_session_cleanup, s);

	s->start = ast_tvnow();
	s->last_keepalive = ast_tvnow();
	s->keepalive_count = 0;
	s->lockstate = 0;

	AST_LIST_LOCK(&sessions);
	AST_LIST_INSERT_HEAD(&sessions, s, list);
	AST_LIST_UNLOCK(&sessions);

	s->auth_timeout_sched = ast_sched_add(sched, auth_timeout*1000, skinny_noauth_cb, s);
	s->keepalive_timeout_sched = ast_sched_add(sched, keep_alive*3000, skinny_nokeepalive_cb, s);

	for (;;) {

		fds[0].fd = s->fd;
		fds[0].events = POLLIN;
		fds[0].revents = 0;
		res = ast_poll(fds, 1, -1); /* block */
		if (res < 0) {
			if (errno != EINTR) {
				ast_log(LOG_WARNING, "Select returned error: %s\n", strerror(errno));
				ast_verb(3, "Ending Skinny session from %s (bad input)\n", ast_inet_ntoa(s->sin.sin_addr));
				break;
			}
		}

		if (!fds[0].revents) {
			continue;
		}
		ast_debug(1, "Reading header\n");

		if (!(req = ast_calloc(1, SKINNY_MAX_PACKET))) {
			ast_log(LOG_WARNING, "Unable to allocated memorey for skinny_req.\n");
			break;
		}

		ast_mutex_lock(&s->lock);
		s->lockstate = 1;

		if ((res = read(s->fd, req, skinny_header_size)) != skinny_header_size) {
			if (res < 0) {
				ast_log(LOG_WARNING, "Header read() returned error: %s\n", strerror(errno));
			} else {
				ast_log(LOG_WARNING, "Unable to read header. Only found %d bytes.\n", res);
			}
			break;
		}

		eventmessage = letohl(req->e);
		if (eventmessage < 0) {
			ast_log(LOG_ERROR, "Event Message is NULL from socket %d, This is bad\n", s->fd);
			break;
		}

		dlen = letohl(req->len) - 4;
		if (dlen < 0) {
			ast_log(LOG_WARNING, "Skinny Client sent invalid data.\n");
			break;
		}
		if (dlen > (SKINNY_MAX_PACKET - skinny_header_size)) {
			ast_log(LOG_WARNING, "Skinny packet too large (%d bytes), max length(%d bytes)\n", dlen+8, SKINNY_MAX_PACKET);
			break;
		}

		ast_debug(1, "Read header: Message ID: 0x%04x,  %d bytes in packet\n", eventmessage, dlen);

		bytesread = 0;
		while (bytesread < dlen) {
			ast_debug(1, "Waiting %dms for %d bytes of %d\n", PACKET_TIMEOUT, dlen - bytesread, dlen);
			fds[0].revents = 0;
			res = ast_poll(fds, 1, PACKET_TIMEOUT);
			if (res <= 0) {
				if (res == 0) {
					ast_debug(1, "Poll timed out waiting for %d bytes\n", dlen - bytesread);
				} else {
					ast_log(LOG_WARNING, "Poll failed waiting for %d bytes: %s\n",
						dlen - bytesread, strerror(errno));
				}
				ast_verb(3, "Ending Skinny session from %s (bad input)\n", ast_inet_ntoa(s->sin.sin_addr));
				res = -1;

				break;
			}
			if (!fds[0].revents) {
				continue;
			}

			res = read(s->fd, ((char*)&req->data)+bytesread, dlen-bytesread);
			if (res < 0) {
				ast_log(LOG_WARNING, "Data read() returned error: %s\n", strerror(errno));
				break;
			}
			bytesread += res;
			ast_debug(1, "Read %d bytes.  %d of %d now read\n", res, bytesread, dlen);
		}

		s->lockstate = 0;
		ast_mutex_unlock(&s->lock);
		if (res < 0) {
			break;
		}

		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
		res = handle_message(req, s);
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

		if (req) {
			ast_free(req);
			req = NULL;
		}
	}

	ast_log(LOG_NOTICE, "Skinny Session returned: %s\n", strerror(errno));
	if (req) {
		ast_free(req);
	}

	pthread_cleanup_pop(1);

	return 0;
}

static void *accept_thread(void *ignore)
{
	int as;
	struct sockaddr_in sin;
	socklen_t sinlen;
	struct skinnysession *s;
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

		if (setsockopt(as, IPPROTO_TCP, TCP_NODELAY, (char *) &arg, sizeof(arg)) < 0) {
			ast_log(LOG_WARNING, "Failed to set TCP_NODELAY on Skinny TCP connection: %s\n", strerror(errno));
		}

		if (!(s = ast_calloc(1, sizeof(struct skinnysession)))) {
			close(as);
			ast_atomic_fetchadd_int(&unauth_sessions, -1);
			continue;
		}

		ast_mutex_init(&s->lock);
		memcpy(&s->sin, &sin, sizeof(sin));
		s->fd = as;
		s->auth_timeout_sched = -1;
		s->keepalive_timeout_sched = -1;

		if (ast_pthread_create(&s->t, NULL, skinny_session, s)) {
			s->t = AST_PTHREADT_NULL;
			destroy_session(s);
		}
	}
	SKINNY_DEBUG(DEBUG_THREAD, 3, "Killing accept thread\n");
	close(as);
	return 0;
}

static int skinny_devicestate(const char *data)
{
	struct skinny_line *l;
	char *tmp;

	tmp = ast_strdupa(data);

	l = find_line_by_name(tmp);

	return get_devicestate(l);
}

static struct ast_channel *skinny_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *dest, int *cause)
{
	struct skinny_line *l;
	struct skinny_subline *subline = NULL;
	struct ast_channel *tmpc = NULL;
	char tmp[256];

	if (!(ast_format_cap_has_type(cap, AST_MEDIA_TYPE_AUDIO))) {
		struct ast_str *codec_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);
		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%s'\n", ast_format_cap_get_names(cap, &codec_buf));
		return NULL;
	}

	ast_copy_string(tmp, dest, sizeof(tmp));
	if (ast_strlen_zero(tmp)) {
		ast_log(LOG_NOTICE, "Skinny channels require a device\n");
		return NULL;
	}
	l = find_line_by_name(tmp);
	if (!l) {
		subline = find_subline_by_name(tmp);
		if (!subline) {
			ast_log(LOG_NOTICE, "No available lines on: %s\n", dest);
			return NULL;
		}
		l = subline->line;
	}
	ast_verb(3, "skinny_request(%s)\n", tmp);
	tmpc = skinny_new(l, subline, AST_STATE_DOWN, assignedids, requestor, SKINNY_INCOMING);
	if (!tmpc) {
		ast_log(LOG_WARNING, "Unable to make channel for '%s'\n", tmp);
	} else if (subline) {
		struct skinny_subchannel *sub = ast_channel_tech_pvt(tmpc);
		subline->sub = sub;
		subline->calldirection = SKINNY_INCOMING;
		subline->substate = SUBSTATE_UNSET;
		subline->callid = sub->callid;
		sub->subline = subline;
	}
	return tmpc;
}

#define TYPE_GENERAL	1
#define TYPE_DEF_DEVICE 2
#define TYPE_DEF_LINE	4
#define TYPE_DEVICE	8
#define TYPE_LINE	16

#define CLINE_OPTS	((struct skinny_line_options *)item)
#define CLINE		((struct skinny_line *)item)
#define CDEV_OPTS	((struct skinny_device_options *)item)
#define CDEV		((struct skinny_device *)item)

static void config_parse_variables(int type, void *item, struct ast_variable *vptr)
{
	struct ast_variable *v;
	int lineInstance = 1;
	int speeddialInstance = 1;
	int serviceUrlInstance = 1;

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
			} else if (!strcasecmp(v->name, "vmexten")) {
				ast_copy_string(vmexten, v->value, sizeof(vmexten));
				continue;
			} else if (!strcasecmp(v->name, "immeddialkey")) {
				if (!strcmp(v->value,"#")) {
					immed_dialchar = '#';
				} else if (!strcmp(v->value,"*")) {
					immed_dialchar = '*';
				} else {
					ast_log(LOG_WARNING, "Invalid immeddialkey '%s' at line %d, only # or * accepted. Immeddial key disabled\n", v->value, v->lineno);
				}

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
				ast_format_cap_update_by_allow_disallow(default_cap, v->value, 1);
				continue;
			} else if (!strcasecmp(v->name, "disallow")) {
				ast_format_cap_update_by_allow_disallow(default_cap, v->value, 0);
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
				int tempamaflags = ast_channel_string2amaflag(v->value);
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
		} else if (!strcasecmp(v->name, "namedcallgroup")) {
			if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
				CLINE_OPTS->named_callgroups = ast_get_namedgroups(v->value);
				continue;
			}
		} else if (!strcasecmp(v->name, "namedpickupgroup")) {
			if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
				CLINE_OPTS->named_pickupgroups = ast_get_namedgroups(v->value);
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
		} else if (!strcasecmp(v->name, "callfwdtimeout")) {
			if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
				CLINE_OPTS->callfwdtimeout = atoi(v->value);
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
					/*
					 * hasvoicemail is a users.conf legacy voicemail enable method.
					 * hasvoicemail is only going to work for app_voicemail mailboxes.
					 */
					if (strchr(CLINE->name, '@')) {
						ast_copy_string(CLINE->mailbox, CLINE->name, sizeof(CLINE->mailbox));
					} else {
						snprintf(CLINE->mailbox, sizeof(CLINE->mailbox), "%s@default",
							CLINE->name);
					}
				}
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

				CDEV_addr_tmp.ss.ss_family = AF_INET;
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
				int acl_error = 0;

				CDEV->ha = ast_append_ha(v->name, v->value, CDEV->ha, &acl_error);
				if (acl_error) {
					ast_log(LOG_ERROR, "Invalid ACL '%s' on line '%d'. Deleting device\n",
							v->value, v->lineno);
					CDEV->prune = 1;
				}
				continue;
			}
		} else if (!strcasecmp(v->name, "allow")) {
			if (type & (TYPE_DEF_DEVICE | TYPE_DEVICE)) {
				ast_format_cap_update_by_allow_disallow(CDEV->confcap, v->value, 1);
				continue;
			}
			if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
				ast_format_cap_update_by_allow_disallow(CLINE->confcap, v->value, 1);
				continue;
			}
		} else if (!strcasecmp(v->name, "disallow")) {
			if (type & (TYPE_DEF_DEVICE | TYPE_DEVICE)) {
				ast_format_cap_update_by_allow_disallow(CDEV->confcap, v->value, 0);
				continue;
			}
			if (type & (TYPE_DEF_LINE | TYPE_LINE)) {
				ast_format_cap_update_by_allow_disallow(CLINE->confcap, v->value, 0);
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
							l->device = CDEV;
						}
						break;
					}
				}
				continue;
			}
		} else if (!strcasecmp(v->name, "subline")) {
			if (type & (TYPE_LINE)) {
				struct skinny_subline *subline;
				struct skinny_container *container;
				char buf[256];
				char *stringp = buf, *exten, *stname, *context;

				if (!(subline = ast_calloc(1, sizeof(*subline)))) {
					ast_log(LOG_WARNING, "Unable to allocate memory for subline %s. Ignoring subline.\n", v->value);
					continue;
				}
				if (!(container = ast_calloc(1, sizeof(*container)))) {
					ast_log(LOG_WARNING, "Unable to allocate memory for subline %s container. Ignoring subline.\n", v->value);
					ast_free(subline);
					continue;
				}

				ast_copy_string(buf, v->value, sizeof(buf));
				exten = strsep(&stringp, "@");
				ast_copy_string(subline->exten, ast_strip(exten), sizeof(subline->exten));
				stname = strsep(&exten, "_");
				ast_copy_string(subline->stname, ast_strip(stname), sizeof(subline->stname));
				ast_copy_string(subline->lnname, ast_strip(exten), sizeof(subline->lnname));
				context = strsep(&stringp, ",");
				ast_copy_string(subline->name, ast_strip(stringp), sizeof(subline->name));
				ast_copy_string(subline->context, ast_strip(context), sizeof(subline->context));

				subline->line = CLINE;
				subline->sub = NULL;

				container->type = SKINNY_SUBLINECONTAINER;
				container->data = subline;
				subline->container = container;
				AST_LIST_INSERT_HEAD(&CLINE->sublines, subline, list);
				continue;
			}
		} else if (!strcasecmp(v->name, "dialoutcontext")) {
			if (type & (TYPE_LINE)) {
				ast_copy_string(CLINE_OPTS->dialoutcontext, v->value, sizeof(CLINE_OPTS->dialoutcontext));
				continue;
			}
		} else if (!strcasecmp(v->name, "dialoutexten")) {
			if (type & (TYPE_LINE)) {
				ast_copy_string(CLINE_OPTS->dialoutexten, v->value, sizeof(CLINE_OPTS->dialoutexten));
				continue;
			}
		} else if (!strcasecmp(v->name, "speeddial")) {
			if (type & (TYPE_DEVICE)) {
				struct skinny_speeddial *sd;
				struct skinny_container *container;
				char buf[256];
				char *stringp = buf, *exten, *context, *label;

				if (!(sd = ast_calloc(1, sizeof(*sd)))) {
					ast_log(LOG_WARNING, "Unable to allocate memory for speeddial %s. Ignoring speeddial.\n", v->name);
					continue;
				}
				if (!(container = ast_calloc(1, sizeof(*container)))) {
					ast_log(LOG_WARNING, "Unable to allocate memory for speeddial %s container. Ignoring speeddial.\n", v->name);
					ast_free(sd);
					continue;
				}

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
				container->type = SKINNY_SDCONTAINER;
				container->data = sd;
				sd->container = container;
				AST_LIST_INSERT_HEAD(&CDEV->speeddials, sd, list);
				continue;
			}
		} else if (!strcasecmp(v->name, "serviceurl")) {
			if (type & (TYPE_DEVICE)) {
				struct skinny_serviceurl *surl;
				char buf[256];
				char *stringp = buf, *serviceUrl, *displayName;
				if (!(surl = ast_calloc(1, sizeof(*surl)))) {
					ast_log(LOG_WARNING, "Unable to allocate memory for serviceurl %s. Ignoring service URL.\n", v->name);
					continue;
				}
				ast_copy_string(buf, v->value, sizeof(buf));
				displayName = strsep(&stringp, ",");
				if (stringp) {
					serviceUrl = stringp;
					ast_copy_string(surl->url, ast_strip(serviceUrl), sizeof(surl->url));
					ast_copy_string(surl->displayName, displayName, sizeof(surl->displayName));
					surl->instance = serviceUrlInstance++;
					surl->device = CDEV;
					AST_LIST_INSERT_HEAD(&CDEV->serviceurls, surl, list);
				} else {
					ast_free(surl);
					ast_log(LOG_WARNING, "Badly formed option for service URL in %s. Ignoring service URL.\n", v->name);
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
	struct skinny_container *container;

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

	if (!(l = skinny_line_alloc())) {
		ast_verb(1, "Unable to allocate memory for line %s.\n", lname);
		AST_LIST_UNLOCK(&lines);
		return NULL;
	}
	if (!(container = ast_calloc(1, sizeof(*container)))) {
		ast_log(LOG_WARNING, "Unable to allocate memory for line %s container.\n", lname);
		skinny_line_destroy(l);
		AST_LIST_UNLOCK(&lines);
		return NULL;
	}

	container->type = SKINNY_LINECONTAINER;
	container->data = l;
	l->container = container;

	memcpy(l, default_line, sizeof(*default_line));
	ast_mutex_init(&l->lock);
	ast_copy_string(l->name, lname, sizeof(l->name));
	ast_format_cap_append_from_cap(l->confcap, default_cap, AST_MEDIA_TYPE_UNKNOWN);
	AST_LIST_INSERT_TAIL(&lines, l, all);

	ast_mutex_lock(&l->lock);
	AST_LIST_UNLOCK(&lines);

	config_parse_variables(TYPE_LINE, l, v);

	if (!ast_strlen_zero(l->mailbox)) {
		struct stasis_topic *mailbox_specific_topic;

		ast_verb(3, "Setting mailbox '%s' on line %s\n", l->mailbox, l->name);

		mailbox_specific_topic = ast_mwi_topic(l->mailbox);
		if (mailbox_specific_topic) {
			l->mwi_event_sub = stasis_subscribe_pool(mailbox_specific_topic, mwi_event_cb, l);
			stasis_subscription_accept_message_type(l->mwi_event_sub, ast_mwi_state_type());
			stasis_subscription_set_filter(l->mwi_event_sub, STASIS_SUBSCRIPTION_FILTER_SELECTIVE);
		}
	}

	if (!ast_strlen_zero(vmexten) && ast_strlen_zero(l->vmexten)) {
		ast_copy_string(l->vmexten, vmexten, sizeof(l->vmexten));
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

	if (!(d = skinny_device_alloc(dname))) {
		ast_verb(1, "Unable to allocate memory for device %s.\n", dname);
		AST_LIST_UNLOCK(&devices);
		return NULL;
	}
	memcpy(d, default_device, sizeof(*default_device));
	ast_mutex_init(&d->lock);
	ast_copy_string(d->name, dname, sizeof(d->name));
	ast_format_cap_append_from_cap(d->confcap, default_cap, AST_MEDIA_TYPE_UNKNOWN);
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
			d->hookstate = temp->hookstate;

			AST_LIST_LOCK(&d->lines);
			AST_LIST_TRAVERSE(&d->lines, l, list){

				AST_LIST_LOCK(&temp->lines);
				AST_LIST_TRAVERSE(&temp->lines, ltemp, list) {
					if (strcasecmp(l->name, ltemp->name)) {
						continue;
					}
					ast_mutex_lock(&ltemp->lock);
					l->instance = ltemp->instance;
					if (l == temp->activeline) {
						d->activeline = l;
					}
					if (!AST_LIST_EMPTY(&ltemp->sub)) {
						ast_mutex_lock(&l->lock);
						l->sub = ltemp->sub;
						l->activesub = ltemp->activesub;
						AST_LIST_TRAVERSE(&l->sub, sub, list) {
							sub->line = l;
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
	immed_dialchar = '\0';
	memset(&vmexten, '\0', sizeof(vmexten));


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
	config_parse_variables(TYPE_DEF_LINE, default_line, ast_variable_browse(cfg, "lines"));
	cat = ast_category_browse(cfg, "lines");
	while (cat && strcasecmp(cat, "general") && strcasecmp(cat, "devices")) {
		config_line(cat, ast_variable_browse(cfg, cat));
		cat = ast_category_browse(cfg, cat);
	}

	/* load the devices sections */
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
			ast_set_qos(skinnysock, qos.tos, qos.cos, "Skinny");
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
	struct skinny_serviceurl *surl;

	AST_LIST_LOCK(&devices);
	AST_LIST_LOCK(&lines);

	/* Delete all devices */
	while ((d = AST_LIST_REMOVE_HEAD(&devices, list))) {
		/* Delete all lines for this device */
		while ((l = AST_LIST_REMOVE_HEAD(&d->lines, list))) {
			AST_LIST_REMOVE(&lines, l, all);
			AST_LIST_REMOVE(&d->lines, l, list);
			l = skinny_line_destroy(l);
		}
		/* Delete all speeddials for this device */
		while ((sd = AST_LIST_REMOVE_HEAD(&d->speeddials, list))) {
			ast_free(sd->container);
			ast_free(sd);
		}
		/* Delete all serviceurls for this device */
		while ((surl = AST_LIST_REMOVE_HEAD(&d->serviceurls, list))) {
			ast_free(surl);
		}
		/* Delete all addons for this device */
		while ((a = AST_LIST_REMOVE_HEAD(&d->addons, list))) {
			ast_free(a);
		}
		d = skinny_device_destroy(d);
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
			if (l->mwi_event_sub) {
				l->mwi_event_sub = stasis_unsubscribe(l->mwi_event_sub);
			}
		}
		/* Delete all speeddials for this device */
		while ((sd = AST_LIST_REMOVE_HEAD(&d->speeddials, list))) {
			ast_free(sd);
		}
		/* Delete all addons for this device */
		while ((a = AST_LIST_REMOVE_HEAD(&d->addons, list))) {
			ast_free(a);
		}
		AST_LIST_REMOVE_CURRENT(list);
		d = skinny_device_destroy(d);
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&devices);

	AST_LIST_LOCK(&lines);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&lines, l, all) {
		if (l->prune) {
			AST_LIST_REMOVE_CURRENT(all);
			l = skinny_line_destroy(l);
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&lines);

	AST_LIST_TRAVERSE(&devices, d, list) {
		/* Do a soft reset to re-register the devices after
		   cleaning up the removed devices and lines */
		if (d->session) {
			ast_verb(3, "Restarting device '%s'\n", d->name);
			transmit_reset(d, 1);
		}
	}

	skinnyreload = 0;
        return 0;
}

/*!
 * \brief Load the module
 *
 * Module loading including tests for configuration or dependencies.
 * This function can return AST_MODULE_LOAD_FAILURE, AST_MODULE_LOAD_DECLINE,
 * or AST_MODULE_LOAD_SUCCESS. If a dependency or environment variable fails
 * tests return AST_MODULE_LOAD_FAILURE. If the module can not load the
 * configuration file or other non-critical problem return
 * AST_MODULE_LOAD_DECLINE. On success return AST_MODULE_LOAD_SUCCESS.
 */
static int load_module(void)
{
	int res = 0;

	if (!(default_cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		return AST_MODULE_LOAD_DECLINE;
	}
	if (!(skinny_tech.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		ao2_ref(default_cap, -1);
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_format_cap_append_by_type(skinny_tech.capabilities, AST_MEDIA_TYPE_AUDIO);
	ast_format_cap_append(default_cap, ast_format_ulaw, 0);
	ast_format_cap_append(default_cap, ast_format_alaw, 0);

	for (; res < ARRAY_LEN(soft_key_template_default); res++) {
		soft_key_template_default[res].softKeyEvent = htolel(soft_key_template_default[res].softKeyEvent);
	}
	/* load and parse config */
	res = config_load();
	if (res == -1) {
		ao2_ref(skinny_tech.capabilities, -1);
		ao2_ref(default_cap, -1);
		return AST_MODULE_LOAD_DECLINE;
	}

	sched = ast_sched_context_create();
	if (!sched) {
		ao2_ref(skinny_tech.capabilities, -1);
		ao2_ref(default_cap, -1);
		ast_log(LOG_WARNING, "Unable to create schedule context\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	/* Make sure we can register our skinny channel type */
	if (ast_channel_register(&skinny_tech)) {
		ao2_ref(default_cap, -1);
		ao2_ref(skinny_tech.capabilities, -1);
		ast_log(LOG_ERROR, "Unable to register channel class 'Skinny'\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_rtp_glue_register(&skinny_rtp_glue);
	ast_cli_register_multiple(cli_skinny, ARRAY_LEN(cli_skinny));

	ast_manager_register_xml("SKINNYdevices", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, manager_skinny_show_devices);
	ast_manager_register_xml("SKINNYshowdevice", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, manager_skinny_show_device);
	ast_manager_register_xml("SKINNYlines", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, manager_skinny_show_lines);
	ast_manager_register_xml("SKINNYshowline", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, manager_skinny_show_line);

	if (ast_sched_start_thread(sched)) {
		ast_sched_context_destroy(sched);
		sched = NULL;
		ast_channel_unregister(&skinny_tech);
		ao2_ref(default_cap, -1);
		ao2_ref(skinny_tech.capabilities, -1);
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	struct skinnysession *s;
	struct skinny_device *d;
	struct skinny_line *l;
	struct skinny_subchannel *sub;
	pthread_t tempthread;

	ast_rtp_glue_unregister(&skinny_rtp_glue);
	ast_channel_unregister(&skinny_tech);
	ao2_cleanup(skinny_tech.capabilities);
	ast_cli_unregister_multiple(cli_skinny, ARRAY_LEN(cli_skinny));

	ast_manager_unregister("SKINNYdevices");
	ast_manager_unregister("SKINNYshowdevice");
	ast_manager_unregister("SKINNYlines");
	ast_manager_unregister("SKINNYshowline");

	ast_mutex_lock(&netlock);
	if (accept_t && (accept_t != AST_PTHREADT_STOP)) {
		pthread_cancel(accept_t);
		pthread_kill(accept_t, SIGURG);
		pthread_join(accept_t, NULL);
	}
	accept_t = AST_PTHREADT_STOP;
	ast_mutex_unlock(&netlock);

	AST_LIST_LOCK(&sessions);
	/* Destroy all the interfaces and free their memory */
	while((s = AST_LIST_REMOVE_HEAD(&sessions, list))) {
		RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);

		AST_LIST_UNLOCK(&sessions);
		d = s->device;
		AST_LIST_TRAVERSE(&d->lines, l, list){
			ast_mutex_lock(&l->lock);
			AST_LIST_TRAVERSE(&l->sub, sub, list) {
				skinny_locksub(sub);
				if (sub->owner) {
					ast_softhangup(sub->owner, AST_SOFTHANGUP_APPUNLOAD);
				}
				skinny_unlocksub(sub);
			}
			if (l->mwi_event_sub) {
				l->mwi_event_sub = stasis_unsubscribe_and_join(l->mwi_event_sub);
			}
			ast_mutex_unlock(&l->lock);
			unregister_exten(l);
		}
		ast_endpoint_set_state(d->endpoint, AST_ENDPOINT_OFFLINE);
		blob = ast_json_pack("{s: s}", "peer_status", "Unregistered");
		ast_endpoint_blob_publish(d->endpoint, ast_endpoint_state_type(), blob);
		tempthread = s->t;
		pthread_cancel(tempthread);
		pthread_join(tempthread, NULL);
		AST_LIST_LOCK(&sessions);
	}
	AST_LIST_UNLOCK(&sessions);

	delete_devices();

	close(skinnysock);
	if (sched) {
		ast_sched_context_destroy(sched);
	}

	ast_context_destroy_by_name(used_context, "Skinny");

	ao2_ref(default_cap, -1);
	return 0;
}

static int reload(void)
{
	skinny_reload();
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Skinny Client Control Protocol (Skinny)",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CHANNEL_DRIVER,
);
