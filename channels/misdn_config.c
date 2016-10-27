/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Christian Richter
 *
 * Christian Richter <crich@beronet.com>
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
 *
 */

/*!
 * \file
 *
 * \brief chan_misdn configuration management
 * \author Christian Richter <crich@beronet.com>
 *
 * \ingroup channel_drivers
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include "chan_misdn_config.h"

#include "asterisk/config.h"
#include "asterisk/channel.h"
#include "asterisk/lock.h"
#include "asterisk/pbx.h"
#include "asterisk/strings.h"
#include "asterisk/utils.h"

#define NO_DEFAULT "<>"
#define NONE 0

#define GEN_CFG 1
#define PORT_CFG 2
#define NUM_GEN_ELEMENTS (sizeof(gen_spec) / sizeof(struct misdn_cfg_spec))
#define NUM_PORT_ELEMENTS (sizeof(port_spec) / sizeof(struct misdn_cfg_spec))

/*! Global jitterbuffer configuration - by default, jb is disabled
 *  \note Values shown here match the defaults shown in misdn.conf.sample */
static struct ast_jb_conf default_jbconf =
{
	.flags = 0,
	.max_size = 200,
	.resync_threshold = 1000,
	.impl = "fixed",
	.target_extra = 40,
};

static struct ast_jb_conf global_jbconf;

enum misdn_cfg_type {
	MISDN_CTYPE_STR,
	MISDN_CTYPE_INT,
	MISDN_CTYPE_BOOL,
	MISDN_CTYPE_BOOLINT,
	MISDN_CTYPE_MSNLIST,
	MISDN_CTYPE_ASTGROUP,
	MISDN_CTYPE_ASTNAMEDGROUP
};

struct msn_list {
	char *msn;
	struct msn_list *next;
};

union misdn_cfg_pt {
	char *str;
	int *num;
	struct msn_list *ml;
	ast_group_t *grp;
	struct ast_namedgroups *namgrp;
	void *any;
};

struct misdn_cfg_spec {
	char name[BUFFERSIZE];
	enum misdn_cfg_elements elem;
	enum misdn_cfg_type type;
	char def[BUFFERSIZE];
	int boolint_def;
	char desc[BUFFERSIZE];
};


static const char ports_description[] =
	"Define your ports, e.g. 1,2 (depends on mISDN-driver loading order).";

static const struct misdn_cfg_spec port_spec[] = {
	{ "name", MISDN_CFG_GROUPNAME, MISDN_CTYPE_STR, "default", NONE,
		"Name of the portgroup." },
	{ "allowed_bearers", MISDN_CFG_ALLOWED_BEARERS, MISDN_CTYPE_STR, "all", NONE,
		"Here you can list which bearer capabilities should be allowed:\n"
		"\t  all                  - allow any bearer capability\n"
		"\t  speech               - allow speech\n"
		"\t  3_1khz               - allow 3.1KHz audio\n"
		"\t  digital_unrestricted - allow unrestricted digital\n"
		"\t  digital_restricted   - allow restricted digital\n"
		"\t  video                - allow video" },
	{ "rxgain", MISDN_CFG_RXGAIN, MISDN_CTYPE_INT, "0", NONE,
		"Set this between -8 and 8 to change the RX Gain." },
	{ "txgain", MISDN_CFG_TXGAIN, MISDN_CTYPE_INT, "0", NONE,
		"Set this between -8 and 8 to change the TX Gain." },
	{ "te_choose_channel", MISDN_CFG_TE_CHOOSE_CHANNEL, MISDN_CTYPE_BOOL, "no", NONE,
		"Some telcos especially in NL seem to need this set to yes,\n"
		"\talso in Switzerland this seems to be important." },
	{ "far_alerting", MISDN_CFG_FAR_ALERTING, MISDN_CTYPE_BOOL, "no", NONE,
		"If we should generate ringing for chan_sip and others." },
	{ "pmp_l1_check", MISDN_CFG_PMP_L1_CHECK, MISDN_CTYPE_BOOL, "no", NONE,
		"This option defines, if chan_misdn should check the L1 on a PMP\n"
		"\tbefore making a group call on it. The L1 may go down for PMP Ports\n"
		"\tso we might need this.\n"
		"\tBut be aware! a broken or plugged off cable might be used for a group call\n"
		"\tas well, since chan_misdn has no chance to distinguish if the L1 is down\n"
		"\tbecause of a lost Link or because the Provider shut it down..." },
	{ "block_on_alarm", MISDN_CFG_ALARM_BLOCK, MISDN_CTYPE_BOOL, "no", NONE ,
	  "Block this port if we have an alarm on it." },
	{ "hdlc", MISDN_CFG_HDLC, MISDN_CTYPE_BOOL, "no", NONE,
		"Set this to yes, if you want to bridge a mISDN data channel to\n"
		"\tanother channel type or to an application." },
	{ "context", MISDN_CFG_CONTEXT, MISDN_CTYPE_STR, "default", NONE,
		"Context to use for incoming calls." },
	{ "language", MISDN_CFG_LANGUAGE, MISDN_CTYPE_STR, "en", NONE,
		"Language." },
	{ "musicclass", MISDN_CFG_MUSICCLASS, MISDN_CTYPE_STR, "default", NONE,
		"Sets the musiconhold class." },
	{ "callerid", MISDN_CFG_CALLERID, MISDN_CTYPE_STR, "", NONE,
		"Set the outgoing caller id to the value." },
	{ "incoming_cid_tag", MISDN_CFG_INCOMING_CALLERID_TAG, MISDN_CTYPE_STR, "", NONE,
		"Set the incoming caller id string tag to the value." },
	{ "append_msn_to_cid_tag", MISDN_CFG_APPEND_MSN_TO_CALLERID_TAG, MISDN_CTYPE_BOOL, "no", NONE,
		"Automatically appends incoming or outgoing MSN to the incoming caller\n"
		"\tid string tag. An underscore '_' is used as delimiter. Incoming calls\n"
		"\twill have the dialed number appended, and outgoing calls will have the\n"
		"\tcaller number appended to the tag." },
	{ "method", MISDN_CFG_METHOD, MISDN_CTYPE_STR, "standard", NONE,
		"Set the method to use for channel selection:\n"
		"\t  standard     - Use the first free channel starting from the lowest number.\n"
		"\t  standard_dec - Use the first free channel starting from the highest number.\n"
		"\t  round_robin  - Use the round robin algorithm to select a channel. Use this\n"
		"\t                 if you want to balance your load." },
	{ "dialplan", MISDN_CFG_DIALPLAN, MISDN_CTYPE_INT, "0", NONE,
		"Dialplan means Type Of Number in ISDN Terms\n"
		"\tThere are different types of the dialplan:\n"
		"\n"
		"\tdialplan -> for outgoing call's dialed number\n"
		"\tlocaldialplan -> for outgoing call's callerid\n"
		"\t      (if -1 is set use the value from the asterisk channel)\n"
		"\tcpndialplan -> for incoming call's connected party number sent to caller\n"
		"\t      (if -1 is set use the value from the asterisk channel)\n"
		"\n"
		"\tdialplan options:\n"
		"\n"
		"\t0 - unknown\n"
		"\t1 - International\n"
		"\t2 - National\n"
		"\t4 - Subscriber" },
	{ "localdialplan", MISDN_CFG_LOCALDIALPLAN, MISDN_CTYPE_INT, "0", NONE,
		"Dialplan means Type Of Number in ISDN Terms\n"
		"\tThere are different types of the dialplan:\n"
		"\n"
		"\tdialplan -> for outgoing call's dialed number\n"
		"\tlocaldialplan -> for outgoing call's callerid\n"
		"\t      (if -1 is set use the value from the asterisk channel)\n"
		"\tcpndialplan -> for incoming call's connected party number sent to caller\n"
		"\t      (if -1 is set use the value from the asterisk channel)\n"
		"\n"
		"\tdialplan options:\n"
		"\n"
		"\t0 - unknown\n"
		"\t1 - International\n"
		"\t2 - National\n"
		"\t4 - Subscriber" },
	{ "cpndialplan", MISDN_CFG_CPNDIALPLAN, MISDN_CTYPE_INT, "0", NONE,
		"Dialplan means Type Of Number in ISDN Terms\n"
		"\tThere are different types of the dialplan:\n"
		"\n"
		"\tdialplan -> for outgoing call's dialed number\n"
		"\tlocaldialplan -> for outgoing call's callerid\n"
		"\t      (if -1 is set use the value from the asterisk channel)\n"
		"\tcpndialplan -> for incoming call's connected party number sent to caller\n"
		"\t      (if -1 is set use the value from the asterisk channel)\n"
		"\n"
		"\tdialplan options:\n"
		"\n"
		"\t0 - unknown\n"
		"\t1 - International\n"
		"\t2 - National\n"
		"\t4 - Subscriber" },
	{ "unknownprefix", MISDN_CFG_TON_PREFIX_UNKNOWN, MISDN_CTYPE_STR, "", NONE,
		"Prefix for unknown numbers, this is put before an incoming number\n"
		"\tif its type-of-number is unknown." },
	{ "internationalprefix", MISDN_CFG_TON_PREFIX_INTERNATIONAL, MISDN_CTYPE_STR, "00", NONE,
		"Prefix for international numbers, this is put before an incoming number\n"
		"\tif its type-of-number is international." },
	{ "nationalprefix", MISDN_CFG_TON_PREFIX_NATIONAL, MISDN_CTYPE_STR, "0", NONE,
		"Prefix for national numbers, this is put before an incoming number\n"
		"\tif its type-of-number is national." },
	{ "netspecificprefix", MISDN_CFG_TON_PREFIX_NETWORK_SPECIFIC, MISDN_CTYPE_STR, "", NONE,
		"Prefix for network-specific numbers, this is put before an incoming number\n"
		"\tif its type-of-number is network-specific." },
	{ "subscriberprefix", MISDN_CFG_TON_PREFIX_SUBSCRIBER, MISDN_CTYPE_STR, "", NONE,
		"Prefix for subscriber numbers, this is put before an incoming number\n"
		"\tif its type-of-number is subscriber." },
	{ "abbreviatedprefix", MISDN_CFG_TON_PREFIX_ABBREVIATED, MISDN_CTYPE_STR, "", NONE,
		"Prefix for abbreviated numbers, this is put before an incoming number\n"
		"\tif its type-of-number is abbreviated." },
	{ "presentation", MISDN_CFG_PRES, MISDN_CTYPE_INT, "-1", NONE,
		"These (presentation and screen) are the exact isdn screening and presentation\n"
		"\tindicators.\n"
		"\tIf -1 is given for either value, the presentation indicators are used from\n"
		"\tAsterisk's CALLERPRES function.\n"
		"\n"
		"\tscreen=0, presentation=0 -> callerid presented\n"
		"\tscreen=1, presentation=1 -> callerid restricted (the remote end doesn't see it!)" },
	{ "screen", MISDN_CFG_SCREEN, MISDN_CTYPE_INT, "-1", NONE,
		"These (presentation and screen) are the exact isdn screening and presentation\n"
		"\tindicators.\n"
		"\tIf -1 is given for either value, the presentation indicators are used from\n"
		"\tAsterisk's CALLERPRES function.\n"
		"\n"
		"\tscreen=0, presentation=0 -> callerid presented\n"
		"\tscreen=1, presentation=1 -> callerid restricted (the remote end doesn't see it!)" },
	{ "outgoing_colp", MISDN_CFG_OUTGOING_COLP, MISDN_CTYPE_INT, "0", NONE,
		"Select what to do with outgoing COLP information on this port.\n"
		"\n"
		"\t0 - Send out COLP information unaltered.\n"
		"\t1 - Force COLP to restricted on all outgoing COLP information.\n"
		"\t2 - Do not send COLP information." },
	{ "display_connected", MISDN_CFG_DISPLAY_CONNECTED, MISDN_CTYPE_INT, "0", NONE,
		"Put a display ie in the CONNECT message containing the following\n"
		"\tinformation if it is available (nt port only):\n"
		"\n"
		"\t0 - Do not put the connected line information in the display ie.\n"
		"\t1 - Put the available connected line name in the display ie.\n"
		"\t2 - Put the available connected line number in the display ie.\n"
		"\t3 - Put the available connected line name and number in the display ie." },
	{ "display_setup", MISDN_CFG_DISPLAY_SETUP, MISDN_CTYPE_INT, "0", NONE,
		"Put a display ie in the SETUP message containing the following\n"
		"\tinformation if it is available (nt port only):\n"
		"\n"
		"\t0 - Do not put the caller information in the display ie.\n"
		"\t1 - Put the available caller name in the display ie.\n"
		"\t2 - Put the available caller number in the display ie.\n"
		"\t3 - Put the available caller name and number in the display ie." },
	{ "always_immediate", MISDN_CFG_ALWAYS_IMMEDIATE, MISDN_CTYPE_BOOL, "no", NONE,
		"Enable this to get into the s dialplan-extension.\n"
		"\tThere you can use DigitTimeout if you can't or don't want to use\n"
		"\tisdn overlap dial.\n"
		"\tNOTE: This will jump into the s extension for every exten!" },
	{ "nodialtone", MISDN_CFG_NODIALTONE, MISDN_CTYPE_BOOL, "no", NONE,
		"Enable this to prevent chan_misdn to generate the dialtone\n"
		"\tThis makes only sense together with the always_immediate=yes option\n"
		"\tto generate your own dialtone with Playtones or so." },
	{ "immediate", MISDN_CFG_IMMEDIATE, MISDN_CTYPE_BOOL, "no", NONE,
		"Enable this if you want callers which called exactly the base\n"
		"\tnumber (so no extension is set) to jump into the s extension.\n"
		"\tIf the user dials something more, it jumps to the correct extension\n"
		"\tinstead." },
	{ "senddtmf", MISDN_CFG_SENDDTMF, MISDN_CTYPE_BOOL, "no", NONE,
		"Enable this if we should produce DTMF Tones ourselves." },
	{ "astdtmf", MISDN_CFG_ASTDTMF, MISDN_CTYPE_BOOL, "no", NONE,
		"Enable this if you want to use the Asterisk dtmf detector\n"
		"instead of the mISDN_dsp/hfcmulti one."
		},
	{ "hold_allowed", MISDN_CFG_HOLD_ALLOWED, MISDN_CTYPE_BOOL, "no", NONE,
		"Enable this to have support for hold and retrieve." },
	{ "early_bconnect", MISDN_CFG_EARLY_BCONNECT, MISDN_CTYPE_BOOL, "yes", NONE,
		"Disable this if you don't mind correct handling of Progress Indicators." },
	{ "incoming_early_audio", MISDN_CFG_INCOMING_EARLY_AUDIO, MISDN_CTYPE_BOOL, "no", NONE,
		"Turn this on if you like to send Tone Indications to a Incoming\n"
		"\tisdn channel on a TE Port. Rarely used, only if the Telco allows\n"
		"\tyou to send indications by yourself, normally the Telco sends the\n"
		"\tindications to the remote party." },
	{ "echocancel", MISDN_CFG_ECHOCANCEL, MISDN_CTYPE_BOOLINT, "0", 128,
		"This enables echo cancellation with the given number of taps.\n"
		"\tBe aware: Move this setting only to outgoing portgroups!\n"
		"\tA value of zero turns echo cancellation off.\n"
		"\n"
		"\tPossible values are: 0,32,64,128,256,yes(=128),no(=0)" },
#ifdef MISDN_1_2
	{ "pipeline", MISDN_CFG_PIPELINE, MISDN_CTYPE_STR, NO_DEFAULT, NONE,
		"Set the configuration string for the mISDN dsp pipeline.\n"
		"\n"
		"\tExample for enabling the mg2 echo cancellation module with deftaps\n"
		"\tset to 128:\n"
		"\t\tmg2ec(deftaps=128)" },
#endif
#ifdef WITH_BEROEC
	{ "bnechocancel", MISDN_CFG_BNECHOCANCEL, MISDN_CTYPE_BOOLINT, "yes", 64,
		"echotail in ms (1-200)" },
	{ "bnec_antihowl", MISDN_CFG_BNEC_ANTIHOWL, MISDN_CTYPE_INT, "0", NONE,
		"Use antihowl" },
	{ "bnec_nlp", MISDN_CFG_BNEC_NLP, MISDN_CTYPE_BOOL, "yes", NONE,
		"Nonlinear Processing (much faster adaption)" },
	{ "bnec_zerocoeff", MISDN_CFG_BNEC_ZEROCOEFF, MISDN_CTYPE_BOOL, "no", NONE,
		"ZeroCoeffeciens" },
	{ "bnec_tonedisabler", MISDN_CFG_BNEC_TD, MISDN_CTYPE_BOOL, "no", NONE,
		"Disable Tone" },
	{ "bnec_adaption", MISDN_CFG_BNEC_ADAPT, MISDN_CTYPE_INT, "1", NONE,
		"Adaption mode (0=no,1=full,2=fast)" },
#endif
	{ "need_more_infos", MISDN_CFG_NEED_MORE_INFOS, MISDN_CTYPE_BOOL, "0", NONE,
		"Send Setup_Acknowledge on incoming calls anyway (instead of PROCEEDING),\n"
		"\tthis requests additional Infos, so we can waitfordigits without much\n"
		"\tissues. This works only for PTP Ports" },
	{ "noautorespond_on_setup", MISDN_CFG_NOAUTORESPOND_ON_SETUP, MISDN_CTYPE_BOOL, "0", NONE,
		"Do not send SETUP_ACKNOWLEDGE or PROCEEDING automatically to the calling Party.\n"
		"Instead we directly jump into the dialplan. This might be useful for fast call\n"
		"rejection, or for some broken switches, that need hangup causes like busy in the.\n"
		"RELEASE_COMPLETE Message, instead of the DISCONNECT Message."},
	{ "jitterbuffer", MISDN_CFG_JITTERBUFFER, MISDN_CTYPE_INT, "4000", NONE,
		"The jitterbuffer." },
	{ "jitterbuffer_upper_threshold", MISDN_CFG_JITTERBUFFER_UPPER_THRESHOLD, MISDN_CTYPE_INT, "0", NONE,
		"Change this threshold to enable dejitter functionality." },
	{ "callgroup", MISDN_CFG_CALLGROUP, MISDN_CTYPE_ASTGROUP, NO_DEFAULT, NONE,
		"Callgroup." },
	{ "pickupgroup", MISDN_CFG_PICKUPGROUP, MISDN_CTYPE_ASTGROUP, NO_DEFAULT, NONE,
		"Pickupgroup." },
	{ "namedcallgroup", MISDN_CFG_NAMEDCALLGROUP, MISDN_CTYPE_ASTNAMEDGROUP, NO_DEFAULT, NONE,
		"Named callgroup." },
	{ "namedpickupgroup", MISDN_CFG_NAMEDPICKUPGROUP, MISDN_CTYPE_ASTNAMEDGROUP, NO_DEFAULT, NONE,
		"Named pickupgroup." },
	{ "max_incoming", MISDN_CFG_MAX_IN, MISDN_CTYPE_INT, "-1", NONE,
		"Defines the maximum amount of incoming calls per port for this group.\n"
		"\tCalls which exceed the maximum will be marked with the channel variable\n"
		"\tMAX_OVERFLOW. It will contain the amount of overflowed calls" },
	{ "max_outgoing", MISDN_CFG_MAX_OUT, MISDN_CTYPE_INT, "-1", NONE,
		"Defines the maximum amount of outgoing calls per port for this group\n"
		"\texceeding calls will be rejected" },

	{ "reject_cause", MISDN_CFG_REJECT_CAUSE, MISDN_CTYPE_INT, "21", NONE,
		"Defines the cause with which a 3. call is rejected on PTMP BRI."},
	{ "faxdetect", MISDN_CFG_FAXDETECT, MISDN_CTYPE_STR, "no", NONE,
		"Setup fax detection:\n"
		"\t    no        - no fax detection\n"
		"\t    incoming  - fax detection for incoming calls\n"
		"\t    outgoing  - fax detection for outgoing calls\n"
		"\t    both      - fax detection for incoming and outgoing calls\n"
		"\tAdd +nojump to your value (i.e. faxdetect=both+nojump) if you don't want to jump into the\n"
		"\tfax-extension but still want to detect the fax and prepare the channel for fax transfer." },
	{ "faxdetect_timeout", MISDN_CFG_FAXDETECT_TIMEOUT, MISDN_CTYPE_INT, "5", NONE,
		"Number of seconds the fax detection should do its job. After the given period of time,\n"
		"\twe assume that it's not a fax call and save some CPU time by turning off fax detection.\n"
		"\tSet this to 0 if you don't want a timeout (never stop detecting)." },
	{ "faxdetect_context", MISDN_CFG_FAXDETECT_CONTEXT, MISDN_CTYPE_STR, NO_DEFAULT, NONE,
		"Context to jump into if we detect a fax. Don't set this if you want to stay in the current context." },
	{ "l1watcher_timeout", MISDN_CFG_L1_TIMEOUT, MISDN_CTYPE_BOOLINT, "0", 4,
		"Monitors L1 of the port.  If L1 is down it tries\n"
		"\tto bring it up.  The polling timeout is given in seconds.\n"
		"\tSetting the value to 0 disables monitoring L1 of the port.\n"
		"\n"
		"\tThis option is only read at chan_misdn loading time.\n"
		"\tYou need to unload and load chan_misdn to change the\n"
		"\tvalue.  An asterisk restart will also do the trick." },
	{ "overlapdial", MISDN_CFG_OVERLAP_DIAL, MISDN_CTYPE_BOOLINT, "0", 4,
		"Enables overlap dial for the given amount of seconds.\n"
		"\tPossible values are positive integers or:\n"
		"\t   yes (= 4 seconds)\n"
		"\t   no  (= 0 seconds = disabled)" },
	{ "nttimeout", MISDN_CFG_NTTIMEOUT, MISDN_CTYPE_BOOL, "no", NONE ,
		"Set this to yes if you want calls disconnected in overlap mode\n"
		"\twhen a timeout happens." },
	{ "bridging", MISDN_CFG_BRIDGING, MISDN_CTYPE_BOOL, "yes", NONE,
	 	"Set this to yes/no, default is yes.\n"
		"This can be used to have bridging enabled in general and to\n"
		"disable it for specific ports. It makes sense to disable\n"
		"bridging on NT Port where you plan to use the HOLD/RETRIEVE\n"
		"features with ISDN phones." },
	{ "msns", MISDN_CFG_MSNS, MISDN_CTYPE_MSNLIST, "*", NONE,
		"MSN's for TE ports, listen on those numbers on the above ports, and\n"
		"\tindicate the incoming calls to Asterisk.\n"
		"\tHere you can give a comma separated list, or simply an '*' for any msn." },
	{ "cc_request_retention", MISDN_CFG_CC_REQUEST_RETENTION, MISDN_CTYPE_BOOL, "yes", NONE,
		"Enable/Disable call-completion request retention support (ptp)." },
};

static const struct misdn_cfg_spec gen_spec[] = {
	{ "debug", MISDN_GEN_DEBUG, MISDN_CTYPE_INT, "0", NONE,
		"Sets the debugging flag:\n"
		"\t0 - No Debug\n"
		"\t1 - mISDN Messages and * - Messages, and * - State changes\n"
		"\t2 - Messages + Message specific Informations (e.g. bearer capability)\n"
		"\t3 - very Verbose, the above + lots of Driver specific infos\n"
		"\t4 - even more Verbose than 3" },
#ifndef MISDN_1_2
	{ "misdn_init", MISDN_GEN_MISDN_INIT, MISDN_CTYPE_STR, "/etc/misdn-init.conf", NONE,
		"Set the path to the misdn-init.conf (for nt_ptp mode checking)." },
#endif
	{ "tracefile", MISDN_GEN_TRACEFILE, MISDN_CTYPE_STR, "/var/log/asterisk/misdn.log", NONE,
		"Set the path to the massively growing trace file, if you want that." },
	{ "bridging", MISDN_GEN_BRIDGING, MISDN_CTYPE_BOOL, "yes", NONE,
		"Set this to yes if you want mISDN_dsp to bridge the calls in HW." },
	{ "stop_tone_after_first_digit", MISDN_GEN_STOP_TONE, MISDN_CTYPE_BOOL, "yes", NONE,
		"Stops dialtone after getting first digit on NT Port." },
	{ "append_digits2exten", MISDN_GEN_APPEND_DIGITS2EXTEN, MISDN_CTYPE_BOOL, "yes", NONE,
		"Whether to append overlapdialed Digits to Extension or not." },
	{ "dynamic_crypt", MISDN_GEN_DYNAMIC_CRYPT, MISDN_CTYPE_BOOL, "no", NONE,
		"Whether to look out for dynamic crypting attempts." },
	{ "crypt_prefix", MISDN_GEN_CRYPT_PREFIX, MISDN_CTYPE_STR, NO_DEFAULT, NONE,
		"What is used for crypting Protocol." },
	{ "crypt_keys", MISDN_GEN_CRYPT_KEYS, MISDN_CTYPE_STR, NO_DEFAULT, NONE,
		"Keys for cryption, you reference them in the dialplan\n"
		"\tLater also in dynamic encr." },
 	{ "ntkeepcalls", MISDN_GEN_NTKEEPCALLS, MISDN_CTYPE_BOOL, "no", NONE,
		"avoid dropping calls if the L2 goes down. some Nortel pbx\n"
		"do put down the L2/L1 for some milliseconds even if there\n"
		"are running calls. with this option you can avoid dropping them" },
	{ "ntdebugflags", MISDN_GEN_NTDEBUGFLAGS, MISDN_CTYPE_INT, "0", NONE,
	  	"No description yet."},
	{ "ntdebugfile", MISDN_GEN_NTDEBUGFILE, MISDN_CTYPE_STR, "/var/log/misdn-nt.log", NONE,
	  	"No description yet." }
};


/* array of port configs, default is at position 0. */
static union misdn_cfg_pt **port_cfg;
/* max number of available ports, is set on init */
static int max_ports;
/* general config */
static union misdn_cfg_pt *general_cfg;
/* storing the ptp flag separated to save memory */
static int *ptp;
/* maps enum config elements to array positions */
static int *map;

static ast_mutex_t config_mutex;

#define CLI_ERROR(name, value, section) ({ \
	ast_log(LOG_WARNING, "misdn.conf: \"%s=%s\" (section: %s) invalid or out of range. " \
		"Please edit your misdn.conf and then do a \"misdn reload\".\n", name, value, section); \
})

static int _enum_array_map (void)
{
	int i, j, ok;

	for (i = MISDN_CFG_FIRST + 1; i < MISDN_CFG_LAST; ++i) {
		if (i == MISDN_CFG_PTP)
			continue;
		ok = 0;
		for (j = 0; j < NUM_PORT_ELEMENTS; ++j) {
			if (port_spec[j].elem == i) {
				map[i] = j;
				ok = 1;
				break;
			}
		}
		if (!ok) {
			ast_log(LOG_WARNING, "Enum element %d in misdn_cfg_elements (port section) has no corresponding element in the config struct!\n", i);
			return -1;
		}
	}
	for (i = MISDN_GEN_FIRST + 1; i < MISDN_GEN_LAST; ++i) {
		ok = 0;
		for (j = 0; j < NUM_GEN_ELEMENTS; ++j) {
			if (gen_spec[j].elem == i) {
				map[i] = j;
				ok = 1;
				break;
			}
		}
		if (!ok) {
			ast_log(LOG_WARNING, "Enum element %d in misdn_cfg_elements (general section) has no corresponding element in the config struct!\n", i);
			return -1;
		}
	}
	return 0;
}

static int get_cfg_position (const char *name, int type)
{
	int i;

	switch (type) {
	case PORT_CFG:
		for (i = 0; i < NUM_PORT_ELEMENTS; ++i) {
			if (!strcasecmp(name, port_spec[i].name))
				return i;
		}
		break;
	case GEN_CFG:
		for (i = 0; i < NUM_GEN_ELEMENTS; ++i) {
			if (!strcasecmp(name, gen_spec[i].name))
				return i;
		}
	}

	return -1;
}

static inline void misdn_cfg_lock (void)
{
	ast_mutex_lock(&config_mutex);
}

static inline void misdn_cfg_unlock (void)
{
	ast_mutex_unlock(&config_mutex);
}

static void _free_msn_list (struct msn_list* iter)
{
	if (iter->next)
		_free_msn_list(iter->next);
	if (iter->msn)
		ast_free(iter->msn);
	ast_free(iter);
}

static void _free_port_cfg (void)
{
	int i, j;
	int gn = map[MISDN_CFG_GROUPNAME];
	union misdn_cfg_pt* free_list[max_ports + 2];

	memset(free_list, 0, sizeof(free_list));
	free_list[0] = port_cfg[0];
	for (i = 1; i <= max_ports; ++i) {
		if (port_cfg[i][gn].str) {
			/* we always have a groupname in the non-default case, so this is fine */
			for (j = 1; j <= max_ports; ++j) {
				if (free_list[j] && free_list[j][gn].str == port_cfg[i][gn].str)
					break;
				else if (!free_list[j]) {
					free_list[j] = port_cfg[i];
					break;
				}
			}
		}
	}
	for (j = 0; free_list[j]; ++j) {
		for (i = 0; i < NUM_PORT_ELEMENTS; ++i) {
			if (free_list[j][i].any) {
				if (port_spec[i].type == MISDN_CTYPE_MSNLIST) {
					_free_msn_list(free_list[j][i].ml);
				} else if (port_spec[i].type == MISDN_CTYPE_ASTNAMEDGROUP) {
					ast_unref_namedgroups(free_list[j][i].namgrp);
				} else {
					ast_free(free_list[j][i].any);
				}
			}
		}
	}
}

static void _free_general_cfg (void)
{
	int i;

	for (i = 0; i < NUM_GEN_ELEMENTS; i++)
		if (general_cfg[i].any)
			ast_free(general_cfg[i].any);
}

void misdn_cfg_get(int port, enum misdn_cfg_elements elem, void *buf, int bufsize)
{
	int place;

	if ((elem < MISDN_CFG_LAST) && !misdn_cfg_is_port_valid(port)) {
		memset(buf, 0, bufsize);
		ast_log(LOG_WARNING, "Invalid call to misdn_cfg_get! Port number %d is not valid.\n", port);
		return;
	}

	misdn_cfg_lock();
	if (elem == MISDN_CFG_PTP) {
		if (!memcpy(buf, &ptp[port], (bufsize > ptp[port]) ? sizeof(ptp[port]) : bufsize))
			memset(buf, 0, bufsize);
	} else {
		if ((place = map[elem]) < 0) {
			memset(buf, 0, bufsize);
			ast_log(LOG_WARNING, "Invalid call to misdn_cfg_get! Invalid element (%d) requested.\n", elem);
		} else {
			if (elem < MISDN_CFG_LAST) {
				switch (port_spec[place].type) {
				case MISDN_CTYPE_STR:
					if (port_cfg[port][place].str) {
						ast_copy_string(buf, port_cfg[port][place].str, bufsize);
					} else if (port_cfg[0][place].str) {
						ast_copy_string(buf, port_cfg[0][place].str, bufsize);
					} else
						memset(buf, 0, bufsize);
					break;
				case MISDN_CTYPE_ASTNAMEDGROUP:
					if (bufsize >= sizeof(struct ast_namedgroups *)) {
						if (port_cfg[port][place].namgrp) {
							*(struct ast_namedgroups **)buf = port_cfg[port][place].namgrp;
						} else if (port_cfg[0][place].namgrp) {
							*(struct ast_namedgroups **)buf = port_cfg[0][place].namgrp;
						} else {
							*(struct ast_namedgroups **)buf = NULL;
						}
					}
					break;
				default:
					if (port_cfg[port][place].any)
						memcpy(buf, port_cfg[port][place].any, bufsize);
					else if (port_cfg[0][place].any)
						memcpy(buf, port_cfg[0][place].any, bufsize);
					else
						memset(buf, 0, bufsize);
				}
			} else {
				switch (gen_spec[place].type) {
				case MISDN_CTYPE_STR:
					ast_copy_string(buf, S_OR(general_cfg[place].str, ""), bufsize);
					break;
				default:
					if (general_cfg[place].any)
						memcpy(buf, general_cfg[place].any, bufsize);
					else
						memset(buf, 0, bufsize);
				}
			}
		}
	}
	misdn_cfg_unlock();
}

enum misdn_cfg_elements misdn_cfg_get_elem(const char *name)
{
	int pos;

	/* here comes a hack to replace the (not existing) "name" element with the "ports" element */
	if (!strcmp(name, "ports"))
		return MISDN_CFG_GROUPNAME;
	if (!strcmp(name, "name"))
		return MISDN_CFG_FIRST;

	pos = get_cfg_position(name, PORT_CFG);
	if (pos >= 0)
		return port_spec[pos].elem;

	pos = get_cfg_position(name, GEN_CFG);
	if (pos >= 0)
		return gen_spec[pos].elem;

	return MISDN_CFG_FIRST;
}

void misdn_cfg_get_name(enum misdn_cfg_elements elem, void *buf, int bufsize)
{
	struct misdn_cfg_spec *spec = NULL;
	int place = map[elem];

	/* the ptp hack */
	if (elem == MISDN_CFG_PTP) {
		memset(buf, 0, 1);
		return;
	}

	/* here comes a hack to replace the (not existing) "name" element with the "ports" element */
	if (elem == MISDN_CFG_GROUPNAME) {
		if (!snprintf(buf, bufsize, "ports"))
			memset(buf, 0, 1);
		return;
	}

	if ((elem > MISDN_CFG_FIRST) && (elem < MISDN_CFG_LAST))
		spec = (struct misdn_cfg_spec *)port_spec;
	else if ((elem > MISDN_GEN_FIRST) && (elem < MISDN_GEN_LAST))
		spec = (struct misdn_cfg_spec *)gen_spec;

	ast_copy_string(buf, spec ? spec[place].name : "", bufsize);
}

void misdn_cfg_get_desc (enum misdn_cfg_elements elem, void *buf, int bufsize, void *buf_default, int bufsize_default)
{
	int place = map[elem];
	struct misdn_cfg_spec *spec = NULL;

	/* here comes a hack to replace the (not existing) "name" element with the "ports" element */
	if (elem == MISDN_CFG_GROUPNAME) {
		ast_copy_string(buf, ports_description, bufsize);
		if (buf_default && bufsize_default)
			memset(buf_default, 0, 1);
		return;
	}

	if ((elem > MISDN_CFG_FIRST) && (elem < MISDN_CFG_LAST))
		spec = (struct misdn_cfg_spec *)port_spec;
	else if ((elem > MISDN_GEN_FIRST) && (elem < MISDN_GEN_LAST))
		spec = (struct misdn_cfg_spec *)gen_spec;

	if (!spec)
		memset(buf, 0, 1);
	else {
		ast_copy_string(buf, spec[place].desc, bufsize);
		if (buf_default && bufsize) {
			if (!strcmp(spec[place].def, NO_DEFAULT))
				memset(buf_default, 0, 1);
			else
				ast_copy_string(buf_default, spec[place].def, bufsize_default);
		}
	}
}

int misdn_cfg_is_msn_valid (int port, char* msn)
{
	int re = 0;
	struct msn_list *iter;

	if (!misdn_cfg_is_port_valid(port)) {
		ast_log(LOG_WARNING, "Invalid call to misdn_cfg_is_msn_valid! Port number %d is not valid.\n", port);
		return 0;
	}

	misdn_cfg_lock();
	if (port_cfg[port][map[MISDN_CFG_MSNS]].ml)
		iter = port_cfg[port][map[MISDN_CFG_MSNS]].ml;
	else
		iter = port_cfg[0][map[MISDN_CFG_MSNS]].ml;
	for (; iter; iter = iter->next)
		if (*(iter->msn) == '*' || ast_extension_match(iter->msn, msn)) {
			re = 1;
			break;
		}
	misdn_cfg_unlock();

	return re;
}

int misdn_cfg_is_port_valid (int port)
{
	int gn = map[MISDN_CFG_GROUPNAME];

	return (port >= 1 && port <= max_ports && port_cfg[port][gn].str);
}

int misdn_cfg_is_group_method (char *group, enum misdn_cfg_method meth)
{
	int i, re = 0;
	char *method ;

	misdn_cfg_lock();

	method = port_cfg[0][map[MISDN_CFG_METHOD]].str;

	for (i = 1; i <= max_ports; i++) {
		if (port_cfg[i] && port_cfg[i][map[MISDN_CFG_GROUPNAME]].str) {
			if (!strcasecmp(port_cfg[i][map[MISDN_CFG_GROUPNAME]].str, group))
				method = (port_cfg[i][map[MISDN_CFG_METHOD]].str ?
						  port_cfg[i][map[MISDN_CFG_METHOD]].str : port_cfg[0][map[MISDN_CFG_METHOD]].str);
		}
	}

	if (method) {
		switch (meth) {
		case METHOD_STANDARD:		re = !strcasecmp(method, "standard");
									break;
		case METHOD_ROUND_ROBIN:	re = !strcasecmp(method, "round_robin");
									break;
		case METHOD_STANDARD_DEC:	re = !strcasecmp(method, "standard_dec");
									break;
		}
	}
	misdn_cfg_unlock();

	return re;
}

/*!
 * \brief Generate a comma separated list of all active ports
 */
void misdn_cfg_get_ports_string (char *ports)
{
	char tmp[16];
	int l, i;
	int gn = map[MISDN_CFG_GROUPNAME];

	*ports = 0;

	misdn_cfg_lock();
	for (i = 1; i <= max_ports; i++) {
		if (port_cfg[i][gn].str) {
			if (ptp[i])
				sprintf(tmp, "%dptp,", i);
			else
				sprintf(tmp, "%d,", i);
			strcat(ports, tmp);
		}
	}
	misdn_cfg_unlock();

	if ((l = strlen(ports))) {
		/* Strip trailing ',' */
		ports[l-1] = 0;
	}
}

void misdn_cfg_get_config_string (int port, enum misdn_cfg_elements elem, char* buf, int bufsize)
{
	int place;
	char tempbuf[BUFFERSIZE] = "";
	struct msn_list *iter;

	if ((elem < MISDN_CFG_LAST) && !misdn_cfg_is_port_valid(port)) {
		*buf = 0;
		ast_log(LOG_WARNING, "Invalid call to misdn_cfg_get_config_string! Port number %d is not valid.\n", port);
		return;
	}

	place = map[elem];

	misdn_cfg_lock();
	if (elem == MISDN_CFG_PTP) {
		snprintf(buf, bufsize, " -> ptp: %s", ptp[port] ? "yes" : "no");
	}
	else if (elem > MISDN_CFG_FIRST && elem < MISDN_CFG_LAST) {
		switch (port_spec[place].type) {
		case MISDN_CTYPE_INT:
		case MISDN_CTYPE_BOOLINT:
			if (port_cfg[port][place].num)
				snprintf(buf, bufsize, " -> %s: %d", port_spec[place].name, *port_cfg[port][place].num);
			else if (port_cfg[0][place].num)
				snprintf(buf, bufsize, " -> %s: %d", port_spec[place].name, *port_cfg[0][place].num);
			else
				snprintf(buf, bufsize, " -> %s:", port_spec[place].name);
			break;
		case MISDN_CTYPE_BOOL:
			if (port_cfg[port][place].num)
				snprintf(buf, bufsize, " -> %s: %s", port_spec[place].name, *port_cfg[port][place].num ? "yes" : "no");
			else if (port_cfg[0][place].num)
				snprintf(buf, bufsize, " -> %s: %s", port_spec[place].name, *port_cfg[0][place].num ? "yes" : "no");
			else
				snprintf(buf, bufsize, " -> %s:", port_spec[place].name);
			break;
		case MISDN_CTYPE_ASTGROUP:
			if (port_cfg[port][place].grp)
				snprintf(buf, bufsize, " -> %s: %s", port_spec[place].name,
						 ast_print_group(tempbuf, sizeof(tempbuf), *port_cfg[port][place].grp));
			else if (port_cfg[0][place].grp)
				snprintf(buf, bufsize, " -> %s: %s", port_spec[place].name,
						 ast_print_group(tempbuf, sizeof(tempbuf), *port_cfg[0][place].grp));
			else
				snprintf(buf, bufsize, " -> %s:", port_spec[place].name);
			break;
		case MISDN_CTYPE_ASTNAMEDGROUP:
			if (port_cfg[port][place].namgrp) {
				struct ast_str *tmp_str = ast_str_create(1024);
				if (tmp_str) {
					snprintf(buf, bufsize, " -> %s: %s", port_spec[place].name,
							ast_print_namedgroups(&tmp_str, port_cfg[port][place].namgrp));
					ast_free(tmp_str);
				}
			} else if (port_cfg[0][place].namgrp) {
				struct ast_str *tmp_str = ast_str_create(1024);
				if (tmp_str) {
					snprintf(buf, bufsize, " -> %s: %s", port_spec[place].name,
							ast_print_namedgroups(&tmp_str, port_cfg[0][place].namgrp));
					ast_free(tmp_str);
				}
			} else {
				snprintf(buf, bufsize, " -> %s:", port_spec[place].name);
			}
			break;
		case MISDN_CTYPE_MSNLIST:
			if (port_cfg[port][place].ml)
				iter = port_cfg[port][place].ml;
			else
				iter = port_cfg[0][place].ml;
			if (iter) {
				for (; iter; iter = iter->next) {
					strncat(tempbuf, iter->msn, sizeof(tempbuf) - strlen(tempbuf) - 1);
				}
				if (strlen(tempbuf) > 1) {
					tempbuf[strlen(tempbuf)-2] = 0;
				}
			}
			snprintf(buf, bufsize, " -> msns: %s", *tempbuf ? tempbuf : "none");
			break;
		case MISDN_CTYPE_STR:
			if ( port_cfg[port][place].str) {
				snprintf(buf, bufsize, " -> %s: %s", port_spec[place].name, port_cfg[port][place].str);
			} else if (port_cfg[0][place].str) {
				snprintf(buf, bufsize, " -> %s: %s", port_spec[place].name, port_cfg[0][place].str);
			} else {
				snprintf(buf, bufsize, " -> %s:", port_spec[place].name);
			}
			break;
		}
	} else if (elem > MISDN_GEN_FIRST && elem < MISDN_GEN_LAST) {
		switch (gen_spec[place].type) {
		case MISDN_CTYPE_INT:
		case MISDN_CTYPE_BOOLINT:
			if (general_cfg[place].num)
				snprintf(buf, bufsize, " -> %s: %d", gen_spec[place].name, *general_cfg[place].num);
			else
				snprintf(buf, bufsize, " -> %s:", gen_spec[place].name);
			break;
		case MISDN_CTYPE_BOOL:
			if (general_cfg[place].num)
				snprintf(buf, bufsize, " -> %s: %s", gen_spec[place].name, *general_cfg[place].num ? "yes" : "no");
			else
				snprintf(buf, bufsize, " -> %s:", gen_spec[place].name);
			break;
		case MISDN_CTYPE_STR:
			if ( general_cfg[place].str) {
				snprintf(buf, bufsize, " -> %s: %s", gen_spec[place].name, general_cfg[place].str);
			} else {
				snprintf(buf, bufsize, " -> %s:", gen_spec[place].name);
			}
			break;
		default:
			snprintf(buf, bufsize, " -> type of %s not handled yet", gen_spec[place].name);
			break;
		}
	} else {
		*buf = 0;
		ast_log(LOG_WARNING, "Invalid call to misdn_cfg_get_config_string! Invalid config element (%d) requested.\n", elem);
	}
	misdn_cfg_unlock();
}

int misdn_cfg_get_next_port (int port)
{
	int p = -1;
	int gn = map[MISDN_CFG_GROUPNAME];

	misdn_cfg_lock();
	for (port++; port <= max_ports; port++) {
		if (port_cfg[port][gn].str) {
			p = port;
			break;
		}
	}
	misdn_cfg_unlock();

	return p;
}

int misdn_cfg_get_next_port_spin (int port)
{
	int p = misdn_cfg_get_next_port(port);
	return (p > 0) ? p : misdn_cfg_get_next_port(0);
}

static int _parse (union misdn_cfg_pt *dest, const char *value, enum misdn_cfg_type type, int boolint_def)
{
	int re = 0;
	int len, tmp;
	char *valtmp;
	char *tmp2 = ast_strdupa(value);

	switch (type) {
	case MISDN_CTYPE_STR:
		if (dest->str) {
			ast_free(dest->str);
		}
		if ((len = strlen(value))) {
			dest->str = ast_malloc((len + 1) * sizeof(char));
			strncpy(dest->str, value, len);
			dest->str[len] = 0;
		} else {
			dest->str = ast_malloc(sizeof(char));
			dest->str[0] = 0;
		}
		break;
	case MISDN_CTYPE_INT:
	{
		int res;

		if (strchr(value,'x')) {
			res = sscanf(value, "%30x", &tmp);
		} else {
			res = sscanf(value, "%30d", &tmp);
		}
		if (res) {
			if (!dest->num) {
				dest->num = ast_malloc(sizeof(int));
			}
			memcpy(dest->num, &tmp, sizeof(int));
		} else
			re = -1;
	}
		break;
	case MISDN_CTYPE_BOOL:
		if (!dest->num) {
			dest->num = ast_malloc(sizeof(int));
		}
		*(dest->num) = (ast_true(value) ? 1 : 0);
		break;
	case MISDN_CTYPE_BOOLINT:
		if (!dest->num) {
			dest->num = ast_malloc(sizeof(int));
		}
		if (sscanf(value, "%30d", &tmp)) {
			memcpy(dest->num, &tmp, sizeof(int));
		} else {
			*(dest->num) = (ast_true(value) ? boolint_def : 0);
		}
		break;
	case MISDN_CTYPE_MSNLIST:
		for (valtmp = strsep(&tmp2, ","); valtmp; valtmp = strsep(&tmp2, ",")) {
			if ((len = strlen(valtmp))) {
				struct msn_list *ml = ast_malloc(sizeof(*ml));
				ml->msn = ast_calloc(len+1, sizeof(char));
				strncpy(ml->msn, valtmp, len);
				ml->next = dest->ml;
				dest->ml = ml;
			}
		}
		break;
	case MISDN_CTYPE_ASTGROUP:
		if (!dest->grp) {
			dest->grp = ast_malloc(sizeof(ast_group_t));
		}
		*(dest->grp) = ast_get_group(value);
		break;
	case MISDN_CTYPE_ASTNAMEDGROUP:
		dest->namgrp = ast_get_namedgroups(value);
		break;
	}

	return re;
}

static void _build_general_config (struct ast_variable *v)
{
	int pos;

	for (; v; v = v->next) {
		if (!ast_jb_read_conf(&global_jbconf, v->name, v->value))
			continue;
		if (((pos = get_cfg_position(v->name, GEN_CFG)) < 0) ||
			(_parse(&general_cfg[pos], v->value, gen_spec[pos].type, gen_spec[pos].boolint_def) < 0))
			CLI_ERROR(v->name, v->value, "general");
	}
}

static void _build_port_config (struct ast_variable *v, char *cat)
{
	int pos, i;
	union misdn_cfg_pt cfg_tmp[NUM_PORT_ELEMENTS];
	int cfg_for_ports[max_ports + 1];

	if (!v || !cat)
		return;

	memset(cfg_tmp, 0, sizeof(cfg_tmp));
	memset(cfg_for_ports, 0, sizeof(cfg_for_ports));

	if (!strcasecmp(cat, "default")) {
		cfg_for_ports[0] = 1;
	}

	if (((pos = get_cfg_position("name", PORT_CFG)) < 0) ||
		(_parse(&cfg_tmp[pos], cat, port_spec[pos].type, port_spec[pos].boolint_def) < 0)) {
		CLI_ERROR(v->name, v->value, cat);
		return;
	}

	for (; v; v = v->next) {
		if (!strcasecmp(v->name, "ports")) {
			char *token, *tmp = ast_strdupa(v->value);
			char ptpbuf[BUFFERSIZE] = "";
			int start, end;
			for (token = strsep(&tmp, ","); token; token = strsep(&tmp, ","), *ptpbuf = 0) {
				if (!*token)
					continue;
				if (sscanf(token, "%30d-%30d%511s", &start, &end, ptpbuf) >= 2) {
					for (; start <= end; start++) {
						if (start <= max_ports && start > 0) {
							cfg_for_ports[start] = 1;
							ptp[start] = (strstr(ptpbuf, "ptp")) ? 1 : 0;
						} else
							CLI_ERROR(v->name, v->value, cat);
					}
				} else {
					if (sscanf(token, "%30d%511s", &start, ptpbuf)) {
						if (start <= max_ports && start > 0) {
							cfg_for_ports[start] = 1;
							ptp[start] = (strstr(ptpbuf, "ptp")) ? 1 : 0;
						} else
							CLI_ERROR(v->name, v->value, cat);
					} else
						CLI_ERROR(v->name, v->value, cat);
				}
			}
		} else {
			if (((pos = get_cfg_position(v->name, PORT_CFG)) < 0) ||
				(_parse(&cfg_tmp[pos], v->value, port_spec[pos].type, port_spec[pos].boolint_def) < 0))
				CLI_ERROR(v->name, v->value, cat);
		}
	}

	for (i = 0; i < (max_ports + 1); ++i) {
		if (i > 0 && cfg_for_ports[0]) {
			/* default category, will populate the port_cfg with additional port
			categories in subsequent calls to this function */
			memset(cfg_tmp, 0, sizeof(cfg_tmp));
		}
		if (cfg_for_ports[i]) {
			memcpy(port_cfg[i], cfg_tmp, sizeof(cfg_tmp));
		}
	}
}

void misdn_cfg_update_ptp (void)
{
#ifndef MISDN_1_2
	char misdn_init[BUFFERSIZE];
	char line[BUFFERSIZE];
	FILE *fp;
	char *tok, *p, *end;
	int port;

	misdn_cfg_get(0, MISDN_GEN_MISDN_INIT, &misdn_init, sizeof(misdn_init));

	if (!ast_strlen_zero(misdn_init)) {
		fp = fopen(misdn_init, "r");
		if (fp) {
			while(fgets(line, sizeof(line), fp)) {
				if (!strncmp(line, "nt_ptp", 6)) {
					for (tok = strtok_r(line,",=", &p);
						 tok;
						 tok = strtok_r(NULL,",=", &p)) {
						port = strtol(tok, &end, 10);
						if (end != tok && misdn_cfg_is_port_valid(port)) {
							misdn_cfg_lock();
							ptp[port] = 1;
							misdn_cfg_unlock();
						}
					}
				}
			}
			fclose(fp);
		} else {
			ast_log(LOG_WARNING,"Couldn't open %s: %s\n", misdn_init, strerror(errno));
		}
	}
#else
	int i;
	int proto;
	char filename[128];
	FILE *fp;

	for (i = 1; i <= max_ports; ++i) {
		snprintf(filename, sizeof(filename), "/sys/class/mISDN-stacks/st-%08x/protocol", i << 8);
		fp = fopen(filename, "r");
		if (!fp) {
			ast_log(LOG_WARNING, "Could not open %s: %s\n", filename, strerror(errno));
			continue;
		}
		if (fscanf(fp, "0x%08x", &proto) != 1)
			ast_log(LOG_WARNING, "Could not parse contents of %s!\n", filename);
		else
			ptp[i] = proto & 1<<5 ? 1 : 0;
		fclose(fp);
	}
#endif
}

static void _fill_defaults (void)
{
	int i;

	for (i = 0; i < NUM_PORT_ELEMENTS; ++i) {
		if (!port_cfg[0][i].any && strcasecmp(port_spec[i].def, NO_DEFAULT))
			_parse(&(port_cfg[0][i]), (char *)port_spec[i].def, port_spec[i].type, port_spec[i].boolint_def);
	}
	for (i = 0; i < NUM_GEN_ELEMENTS; ++i) {
		if (!general_cfg[i].any && strcasecmp(gen_spec[i].def, NO_DEFAULT))
			_parse(&(general_cfg[i]), (char *)gen_spec[i].def, gen_spec[i].type, gen_spec[i].boolint_def);
	}
}

void misdn_cfg_reload (void)
{
	misdn_cfg_init(0, 1);
}

void misdn_cfg_destroy (void)
{
	misdn_cfg_lock();

	_free_port_cfg();
	_free_general_cfg();

	ast_free(port_cfg);
	ast_free(general_cfg);
	ast_free(ptp);
	ast_free(map);

	misdn_cfg_unlock();
	ast_mutex_destroy(&config_mutex);
}

int misdn_cfg_init(int this_max_ports, int reload)
{
	char config[] = "misdn.conf";
	char *cat, *p;
	int i;
	struct ast_config *cfg;
	struct ast_variable *v;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };

	if (!(cfg = ast_config_load2(config, "chan_misdn", config_flags)) || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "missing or invalid file: misdn.conf\n");
		return -1;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED)
		return 0;

	ast_mutex_init(&config_mutex);

	/* Copy the default jb config over global_jbconf */
	memcpy(&global_jbconf, &default_jbconf, sizeof(struct ast_jb_conf));

	misdn_cfg_lock();

	if (this_max_ports) {
		/* this is the first run */
		max_ports = this_max_ports;
		map = ast_calloc(MISDN_GEN_LAST + 1, sizeof(int));
		if (_enum_array_map())
			return -1;
		p = ast_calloc(1, (max_ports + 1) * sizeof(union misdn_cfg_pt *)
						   + (max_ports + 1) * NUM_PORT_ELEMENTS * sizeof(union misdn_cfg_pt));
		port_cfg = (union misdn_cfg_pt **)p;
		p += (max_ports + 1) * sizeof(union misdn_cfg_pt *);
		for (i = 0; i <= max_ports; ++i) {
			port_cfg[i] = (union misdn_cfg_pt *)p;
			p += NUM_PORT_ELEMENTS * sizeof(union misdn_cfg_pt);
		}
		general_cfg = ast_calloc(1, sizeof(union misdn_cfg_pt *) * NUM_GEN_ELEMENTS);
		ptp = ast_calloc(max_ports + 1, sizeof(int));
	}
	else {
		/* misdn reload */
		_free_port_cfg();
		_free_general_cfg();
		memset(port_cfg[0], 0, NUM_PORT_ELEMENTS * sizeof(union misdn_cfg_pt) * (max_ports + 1));
		memset(general_cfg, 0, sizeof(union misdn_cfg_pt *) * NUM_GEN_ELEMENTS);
		memset(ptp, 0, sizeof(int) * (max_ports + 1));
	}

	cat = ast_category_browse(cfg, NULL);

	while(cat) {
		v = ast_variable_browse(cfg, cat);
		if (!strcasecmp(cat, "general")) {
			_build_general_config(v);
		} else {
			_build_port_config(v, cat);
		}
		cat = ast_category_browse(cfg, cat);
	}

	_fill_defaults();

	misdn_cfg_unlock();
	ast_config_destroy(cfg);

	return 0;
}

struct ast_jb_conf *misdn_get_global_jbconf() {
	return &global_jbconf;
}
