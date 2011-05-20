/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2007, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * SLA Implementation by:
 * Russell Bryant <russell@digium.com>
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
 * \brief Meet me conference bridge and Shared Line Appearances
 *
 * \author Mark Spencer <markster@digium.com>
 * \author (SLA) Russell Bryant <russell@digium.com>
 * 
 * \ingroup applications
 */

/*** MODULEINFO
	<depend>dahdi</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/app.h"
#include "asterisk/dsp.h"
#include "asterisk/musiconhold.h"
#include "asterisk/manager.h"
#include "asterisk/options.h"
#include "asterisk/cli.h"
#include "asterisk/say.h"
#include "asterisk/utils.h"
#include "asterisk/translate.h"
#include "asterisk/ulaw.h"
#include "asterisk/astobj.h"
#include "asterisk/astobj2.h"
#include "asterisk/devicestate.h"
#include "asterisk/dial.h"
#include "asterisk/causes.h"

#include "asterisk/dahdi_compat.h"

#include "enter.h"
#include "leave.h"

#define CONFIG_FILE_NAME "meetme.conf"
#define SLA_CONFIG_FILE  "sla.conf"

/*! each buffer is 20ms, so this is 640ms total */
#define DEFAULT_AUDIO_BUFFERS  32

enum {
	ADMINFLAG_MUTED =     (1 << 1), /*!< User is muted */
	ADMINFLAG_SELFMUTED = (1 << 2), /*!< User muted self */
	ADMINFLAG_KICKME =    (1 << 3)  /*!< User has been kicked */
};

#define MEETME_DELAYDETECTTALK     300
#define MEETME_DELAYDETECTENDTALK  1000

#define AST_FRAME_BITS  32

enum volume_action {
	VOL_UP,
	VOL_DOWN
};

enum entrance_sound {
	ENTER,
	LEAVE
};

enum recording_state {
	MEETME_RECORD_OFF,
	MEETME_RECORD_STARTED,
	MEETME_RECORD_ACTIVE,
	MEETME_RECORD_TERMINATE
};

#define CONF_SIZE  320

enum {
	/*! user has admin access on the conference */
	CONFFLAG_ADMIN = (1 << 0),
	/*! If set the user can only receive audio from the conference */
	CONFFLAG_MONITOR = (1 << 1),
	/*! If set asterisk will exit conference when '#' is pressed */
	CONFFLAG_POUNDEXIT = (1 << 2),
	/*! If set asterisk will provide a menu to the user when '*' is pressed */
	CONFFLAG_STARMENU = (1 << 3),
	/*! If set the use can only send audio to the conference */
	CONFFLAG_TALKER = (1 << 4),
	/*! If set there will be no enter or leave sounds */
	CONFFLAG_QUIET = (1 << 5),
	/*! If set, when user joins the conference, they will be told the number 
	 *  of users that are already in */
	CONFFLAG_ANNOUNCEUSERCOUNT = (1 << 6),
	/*! Set to run AGI Script in Background */
	CONFFLAG_AGI = (1 << 7),
	/*! Set to have music on hold when user is alone in conference */
	CONFFLAG_MOH = (1 << 8),
	/*! If set the MeetMe will return if all marked with this flag left */
	CONFFLAG_MARKEDEXIT = (1 << 9),
	/*! If set, the MeetMe will wait until a marked user enters */
	CONFFLAG_WAITMARKED = (1 << 10),
	/*! If set, the MeetMe will exit to the specified context */
	CONFFLAG_EXIT_CONTEXT = (1 << 11),
	/*! If set, the user will be marked */
	CONFFLAG_MARKEDUSER = (1 << 12),
	/*! If set, user will be ask record name on entry of conference */
	CONFFLAG_INTROUSER = (1 << 13),
	/*! If set, the MeetMe will be recorded */
	CONFFLAG_RECORDCONF = (1<< 14),
	/*! If set, the user will be monitored if the user is talking or not */
	CONFFLAG_MONITORTALKER = (1 << 15),
	CONFFLAG_DYNAMIC = (1 << 16),
	CONFFLAG_DYNAMICPIN = (1 << 17),
	CONFFLAG_EMPTY = (1 << 18),
	CONFFLAG_EMPTYNOPIN = (1 << 19),
	CONFFLAG_ALWAYSPROMPT = (1 << 20),
	/*! If set, treats talking users as muted users */
	CONFFLAG_OPTIMIZETALKER = (1 << 21),
	/*! If set, won't speak the extra prompt when the first person 
	 *  enters the conference */
	CONFFLAG_NOONLYPERSON = (1 << 22),
	/*! If set, user will be asked to record name on entry of conference 
	 *  without review */
	CONFFLAG_INTROUSERNOREVIEW = (1 << 23),
	/*! If set, the user will be initially self-muted */
	CONFFLAG_STARTMUTED = (1 << 24),
	/*! Pass DTMF through the conference */
	CONFFLAG_PASS_DTMF = (1 << 25),
	/*! This is a SLA station. (Only for use by the SLA applications.) */
	CONFFLAG_SLA_STATION = (1 << 26),
	/*! This is a SLA trunk. (Only for use by the SLA applications.) */
	CONFFLAG_SLA_TRUNK = (1 << 27),
	/*! Do not write any audio to this channel until the state is up. */
	CONFFLAG_NO_AUDIO_UNTIL_UP = (1 << 28),
};

enum {
	OPT_ARG_WAITMARKED = 0,
	OPT_ARG_ARRAY_SIZE = 1,
};

AST_APP_OPTIONS(meetme_opts, BEGIN_OPTIONS
	AST_APP_OPTION('A', CONFFLAG_MARKEDUSER ),
	AST_APP_OPTION('a', CONFFLAG_ADMIN ),
	AST_APP_OPTION('b', CONFFLAG_AGI ),
	AST_APP_OPTION('c', CONFFLAG_ANNOUNCEUSERCOUNT ),
	AST_APP_OPTION('D', CONFFLAG_DYNAMICPIN ),
	AST_APP_OPTION('d', CONFFLAG_DYNAMIC ),
	AST_APP_OPTION('E', CONFFLAG_EMPTYNOPIN ),
	AST_APP_OPTION('e', CONFFLAG_EMPTY ),
	AST_APP_OPTION('F', CONFFLAG_PASS_DTMF ),
	AST_APP_OPTION('i', CONFFLAG_INTROUSER ),
	AST_APP_OPTION('I', CONFFLAG_INTROUSERNOREVIEW ),
	AST_APP_OPTION('M', CONFFLAG_MOH ),
	AST_APP_OPTION('m', CONFFLAG_STARTMUTED ),
	AST_APP_OPTION('o', CONFFLAG_OPTIMIZETALKER ),
	AST_APP_OPTION('P', CONFFLAG_ALWAYSPROMPT ),
	AST_APP_OPTION('p', CONFFLAG_POUNDEXIT ),
	AST_APP_OPTION('q', CONFFLAG_QUIET ),
	AST_APP_OPTION('r', CONFFLAG_RECORDCONF ),
	AST_APP_OPTION('s', CONFFLAG_STARMENU ),
	AST_APP_OPTION('T', CONFFLAG_MONITORTALKER ),
	AST_APP_OPTION('l', CONFFLAG_MONITOR ),
	AST_APP_OPTION('t', CONFFLAG_TALKER ),
	AST_APP_OPTION_ARG('w', CONFFLAG_WAITMARKED, OPT_ARG_WAITMARKED ),
	AST_APP_OPTION('X', CONFFLAG_EXIT_CONTEXT ),
	AST_APP_OPTION('x', CONFFLAG_MARKEDEXIT ),
	AST_APP_OPTION('1', CONFFLAG_NOONLYPERSON ),
END_OPTIONS );

static const char *app = "MeetMe";
static const char *app2 = "MeetMeCount";
static const char *app3 = "MeetMeAdmin";
static const char *slastation_app = "SLAStation";
static const char *slatrunk_app = "SLATrunk";

static const char *synopsis = "MeetMe conference bridge";
static const char *synopsis2 = "MeetMe participant count";
static const char *synopsis3 = "MeetMe conference Administration";
static const char *slastation_synopsis = "Shared Line Appearance Station";
static const char *slatrunk_synopsis = "Shared Line Appearance Trunk";

static const char *descrip =
"  MeetMe([confno][,[options][,pin]]): Enters the user into a specified MeetMe\n"
"conference.  If the conference number is omitted, the user will be prompted\n"
"to enter one.  User can exit the conference by hangup, or if the 'p' option\n"
"is specified, by pressing '#'.\n"
"Please note: The DAHDI kernel modules and at least one hardware driver (or dahdi_dummy)\n"
"             must be present for conferencing to operate properly. In addition, the chan_dahdi\n"
"             channel driver must be loaded for the 'i' and 'r' options to operate at all.\n\n"
"The option string may contain zero or more of the following characters:\n"
"      'a' -- set admin mode\n"
"      'A' -- set marked mode\n"
"      'b' -- run AGI script specified in ${MEETME_AGI_BACKGROUND}\n"
"             Default: conf-background.agi  (Note: This does not work with\n"
"             non-DAHDI channels in the same conference)\n"
"      'c' -- announce user(s) count on joining a conference\n"
"      'd' -- dynamically add conference\n"
"      'D' -- dynamically add conference, prompting for a PIN\n"
"      'e' -- select an empty conference\n"
"      'E' -- select an empty pinless conference\n"
"      'F' -- Pass DTMF through the conference.\n"
"      'i' -- announce user join/leave with review\n"
"      'I' -- announce user join/leave without review\n"
"      'l' -- set listen only mode (Listen only, no talking)\n"
"      'm' -- set initially muted\n"
"      'M' -- enable music on hold when the conference has a single caller\n"
"      'o' -- set talker optimization - treats talkers who aren't speaking as\n"
"             being muted, meaning (a) No encode is done on transmission and\n"
"             (b) Received audio that is not registered as talking is omitted\n"
"             causing no buildup in background noise.  Note that this option\n"
"             will be removed in 1.6 and enabled by default.\n"
"      'p' -- allow user to exit the conference by pressing '#'\n"
"      'P' -- always prompt for the pin even if it is specified\n"
"      'q' -- quiet mode (don't play enter/leave sounds)\n"
"      'r' -- Record conference (records as ${MEETME_RECORDINGFILE}\n"
"             using format ${MEETME_RECORDINGFORMAT}). Default filename is\n"
"             meetme-conf-rec-${CONFNO}-${UNIQUEID} and the default format is\n"
"             wav.\n"
"      's' -- Present menu (user or admin) when '*' is received ('send' to menu)\n"
"      't' -- set talk only mode. (Talk only, no listening)\n"
"      'T' -- set talker detection (sent to manager interface and meetme list)\n"
"      'w[(<secs>)]'\n"
"          -- wait until the marked user enters the conference\n"
"      'x' -- close the conference when last marked user exits\n"
"      'X' -- allow user to exit the conference by entering a valid single\n"
"             digit extension ${MEETME_EXIT_CONTEXT} or the current context\n"
"             if that variable is not defined.\n"
"      '1' -- do not play message when first person enters\n";

static const char *descrip2 =
"  MeetMeCount(confno[|var]): Plays back the number of users in the specified\n"
"MeetMe conference. If var is specified, playback will be skipped and the value\n"
"will be returned in the variable. Upon app completion, MeetMeCount will hangup\n"
"the channel, unless priority n+1 exists, in which case priority progress will\n"
"continue.\n";

static const char *descrip3 = 
"  MeetMeAdmin(confno,command[,user]): Run admin command for conference\n"
"      'e' -- Eject last user that joined\n"
"      'k' -- Kick one user out of conference\n"
"      'K' -- Kick all users out of conference\n"
"      'l' -- Unlock conference\n"
"      'L' -- Lock conference\n"
"      'm' -- Unmute one user\n"
"      'M' -- Mute one user\n"
"      'n' -- Unmute all users in the conference\n"
"      'N' -- Mute all non-admin users in the conference\n"
"      'r' -- Reset one user's volume settings\n"
"      'R' -- Reset all users volume settings\n"
"      's' -- Lower entire conference speaking volume\n"
"      'S' -- Raise entire conference speaking volume\n"
"      't' -- Lower one user's talk volume\n"
"      'T' -- Raise one user's talk volume\n"
"      'u' -- Lower one user's listen volume\n"
"      'U' -- Raise one user's listen volume\n"
"      'v' -- Lower entire conference listening volume\n"
"      'V' -- Raise entire conference listening volume\n"
"";

static const char *slastation_desc =
"  SLAStation(station):\n"
"This application should be executed by an SLA station.  The argument depends\n"
"on how the call was initiated.  If the phone was just taken off hook, then\n"
"the argument \"station\" should be just the station name.  If the call was\n"
"initiated by pressing a line key, then the station name should be preceded\n"
"by an underscore and the trunk name associated with that line button.\n"
"For example: \"station1_line1\"."
"  On exit, this application will set the variable SLASTATION_STATUS to\n"
"one of the following values:\n"
"    FAILURE | CONGESTION | SUCCESS\n"
"";

static const char *slatrunk_desc =
"  SLATrunk(trunk):\n"
"This application should be executed by an SLA trunk on an inbound call.\n"
"The channel calling this application should correspond to the SLA trunk\n"
"with the name \"trunk\" that is being passed as an argument.\n"
"  On exit, this application will set the variable SLATRUNK_STATUS to\n"
"one of the following values:\n"
"   FAILURE | SUCCESS | UNANSWERED | RINGTIMEOUT\n" 
"";

#define MAX_CONFNUM 80
#define MAX_PIN     80

/* Enough space for "<conference #>,<pin>,<admin pin>" followed by a 0 byte. */
#define MAX_SETTINGS (MAX_CONFNUM + MAX_PIN + MAX_PIN + 3)

enum announcetypes {
	CONF_HASJOIN,
	CONF_HASLEFT
};

struct announce_listitem {
	AST_LIST_ENTRY(announce_listitem) entry;
	char namerecloc[PATH_MAX];				/*!< Name Recorded file Location */
	char language[MAX_LANGUAGE];
	struct ast_channel *confchan;
	int confusers;
	enum announcetypes announcetype;
};

/*! \brief The MeetMe Conference object */
struct ast_conference {
	ast_mutex_t playlock;                   /*!< Conference specific lock (players) */
	ast_mutex_t listenlock;                 /*!< Conference specific lock (listeners) */
	char confno[MAX_CONFNUM];               /*!< Conference */
	struct ast_channel *chan;               /*!< Announcements channel */
	struct ast_channel *lchan;              /*!< Listen/Record channel */
	int fd;                                 /*!< Announcements fd */
	int zapconf;                            /*!< Zaptel Conf # */
	int users;                              /*!< Number of active users */
	int markedusers;                        /*!< Number of marked users */
	time_t start;                           /*!< Start time (s) */
	int refcount;                           /*!< reference count of usage */
	enum recording_state recording:2;       /*!< recording status */
	unsigned int isdynamic:1;               /*!< Created on the fly? */
	unsigned int locked:1;                  /*!< Is the conference locked? */
	pthread_t recordthread;                 /*!< thread for recording */
	ast_mutex_t recordthreadlock;           /*!< control threads trying to start recordthread */
	pthread_attr_t attr;                    /*!< thread attribute */
	const char *recordingfilename;          /*!< Filename to record the Conference into */
	const char *recordingformat;            /*!< Format to record the Conference in */
	char pin[MAX_PIN];                      /*!< If protected by a PIN */
	char pinadmin[MAX_PIN];                 /*!< If protected by a admin PIN */
	struct ast_frame *transframe[32];
	struct ast_frame *origframe;
	struct ast_trans_pvt *transpath[32];
	struct ao2_container *usercontainer;
	AST_LIST_ENTRY(ast_conference) list;
	/* announce_thread related data */
	pthread_t announcethread;
	ast_mutex_t announcethreadlock;
	unsigned int announcethread_stop:1;
	ast_cond_t announcelist_addition;
	AST_LIST_HEAD_NOLOCK(, announce_listitem) announcelist;
	ast_mutex_t announcelistlock;
};

static AST_LIST_HEAD_STATIC(confs, ast_conference);

static unsigned int conf_map[1024] = {0, };

struct volume {
	int desired;                            /*!< Desired volume adjustment */
	int actual;                             /*!< Actual volume adjustment (for channels that can't adjust) */
};

struct ast_conf_user {
	int user_no;                            /*!< User Number */
	int userflags;                          /*!< Flags as set in the conference */
	int adminflags;                         /*!< Flags set by the Admin */
	struct ast_channel *chan;               /*!< Connected channel */
	int talking;                            /*!< Is user talking */
	int zapchannel;                         /*!< Is a Zaptel channel */
	char usrvalue[50];                      /*!< Custom User Value */
	char namerecloc[PATH_MAX];				/*!< Name Recorded file Location */
	time_t jointime;                        /*!< Time the user joined the conference */
	struct volume talk;
	struct volume listen;
	AST_LIST_ENTRY(ast_conf_user) list;
};

enum sla_which_trunk_refs {
	ALL_TRUNK_REFS,
	INACTIVE_TRUNK_REFS,
};

enum sla_trunk_state {
	SLA_TRUNK_STATE_IDLE,
	SLA_TRUNK_STATE_RINGING,
	SLA_TRUNK_STATE_UP,
	SLA_TRUNK_STATE_ONHOLD,
	SLA_TRUNK_STATE_ONHOLD_BYME,
};

enum sla_hold_access {
	/*! This means that any station can put it on hold, and any station
	 * can retrieve the call from hold. */
	SLA_HOLD_OPEN,
	/*! This means that only the station that put the call on hold may
	 * retrieve it from hold. */
	SLA_HOLD_PRIVATE,
};

struct sla_trunk_ref;

struct sla_station {
	AST_RWLIST_ENTRY(sla_station) entry;
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);	
		AST_STRING_FIELD(device);	
		AST_STRING_FIELD(autocontext);	
	);
	AST_LIST_HEAD_NOLOCK(, sla_trunk_ref) trunks;
	struct ast_dial *dial;
	/*! Ring timeout for this station, for any trunk.  If a ring timeout
	 *  is set for a specific trunk on this station, that will take
	 *  priority over this value. */
	unsigned int ring_timeout;
	/*! Ring delay for this station, for any trunk.  If a ring delay
	 *  is set for a specific trunk on this station, that will take
	 *  priority over this value. */
	unsigned int ring_delay;
	/*! This option uses the values in the sla_hold_access enum and sets the
	 * access control type for hold on this station. */
	unsigned int hold_access:1;
};

struct sla_station_ref {
	AST_LIST_ENTRY(sla_station_ref) entry;
	struct sla_station *station;
};

struct sla_trunk {
	AST_RWLIST_ENTRY(sla_trunk) entry;
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);
		AST_STRING_FIELD(device);
		AST_STRING_FIELD(autocontext);	
	);
	AST_LIST_HEAD_NOLOCK(, sla_station_ref) stations;
	/*! Number of stations that use this trunk */
	unsigned int num_stations;
	/*! Number of stations currently on a call with this trunk */
	unsigned int active_stations;
	/*! Number of stations that have this trunk on hold. */
	unsigned int hold_stations;
	struct ast_channel *chan;
	unsigned int ring_timeout;
	/*! If set to 1, no station will be able to join an active call with
	 *  this trunk. */
	unsigned int barge_disabled:1;
	/*! This option uses the values in the sla_hold_access enum and sets the
	 * access control type for hold on this trunk. */
	unsigned int hold_access:1;
	/*! Whether this trunk is currently on hold, meaning that once a station
	 *  connects to it, the trunk channel needs to have UNHOLD indicated to it. */
	unsigned int on_hold:1;
};

struct sla_trunk_ref {
	AST_LIST_ENTRY(sla_trunk_ref) entry;
	struct sla_trunk *trunk;
	enum sla_trunk_state state;
	struct ast_channel *chan;
	/*! Ring timeout to use when this trunk is ringing on this specific
	 *  station.  This takes higher priority than a ring timeout set at
	 *  the station level. */
	unsigned int ring_timeout;
	/*! Ring delay to use when this trunk is ringing on this specific
	 *  station.  This takes higher priority than a ring delay set at
	 *  the station level. */
	unsigned int ring_delay;
};

static AST_RWLIST_HEAD_STATIC(sla_stations, sla_station);
static AST_RWLIST_HEAD_STATIC(sla_trunks, sla_trunk);

static const char sla_registrar[] = "SLA";

/*! \brief Event types that can be queued up for the SLA thread */
enum sla_event_type {
	/*! A station has put the call on hold */
	SLA_EVENT_HOLD,
	/*! The state of a dial has changed */
	SLA_EVENT_DIAL_STATE,
	/*! The state of a ringing trunk has changed */
	SLA_EVENT_RINGING_TRUNK,
};

struct sla_event {
	enum sla_event_type type;
	struct sla_station *station;
	struct sla_trunk_ref *trunk_ref;
	AST_LIST_ENTRY(sla_event) entry;
};

/*! \brief A station that failed to be dialed 
 * \note Only used by the SLA thread. */
struct sla_failed_station {
	struct sla_station *station;
	struct timeval last_try;
	AST_LIST_ENTRY(sla_failed_station) entry;
};

/*! \brief A trunk that is ringing */
struct sla_ringing_trunk {
	struct sla_trunk *trunk;
	/*! The time that this trunk started ringing */
	struct timeval ring_begin;
	AST_LIST_HEAD_NOLOCK(, sla_station_ref) timed_out_stations;
	AST_LIST_ENTRY(sla_ringing_trunk) entry;
};

enum sla_station_hangup {
	SLA_STATION_HANGUP_NORMAL,
	SLA_STATION_HANGUP_TIMEOUT,
};

/*! \brief A station that is ringing */
struct sla_ringing_station {
	struct sla_station *station;
	/*! The time that this station started ringing */
	struct timeval ring_begin;
	AST_LIST_ENTRY(sla_ringing_station) entry;
};

/*!
 * \brief A structure for data used by the sla thread
 */
static struct {
	/*! The SLA thread ID */
	pthread_t thread;
	ast_cond_t cond;
	ast_mutex_t lock;
	AST_LIST_HEAD_NOLOCK(, sla_ringing_trunk) ringing_trunks;
	AST_LIST_HEAD_NOLOCK(, sla_ringing_station) ringing_stations;
	AST_LIST_HEAD_NOLOCK(, sla_failed_station) failed_stations;
	AST_LIST_HEAD_NOLOCK(, sla_event) event_q;
	unsigned int stop:1;
	/*! Attempt to handle CallerID, even though it is known not to work
	 *  properly in some situations. */
	unsigned int attempt_callerid:1;
} sla = {
	.thread = AST_PTHREADT_NULL,
};

/*! The number of audio buffers to be allocated on pseudo channels
 *  when in a conference */
static int audio_buffers;

/*! Map 'volume' levels from -5 through +5 into
 *  decibel (dB) settings for channel drivers
 *  Note: these are not a straight linear-to-dB
 *  conversion... the numbers have been modified
 *  to give the user a better level of adjustability
 */
static char const gain_map[] = {
	-15,
	-13,
	-10,
	-6,
	0,
	0,
	0,
	6,
	10,
	13,
	15,
};


static int admin_exec(struct ast_channel *chan, void *data);
static void *recordthread(void *args);

static char *istalking(int x)
{
	if (x > 0)
		return "(talking)";
	else if (x < 0)
		return "(unmonitored)";
	else 
		return "(not talking)";
}

static int careful_write(int fd, unsigned char *data, int len, int block)
{
	int res;
	int x;

	while (len) {
		if (block) {
			x = DAHDI_IOMUX_WRITE | DAHDI_IOMUX_SIGEVENT;
			res = ioctl(fd, DAHDI_IOMUX, &x);
		} else
			res = 0;
		if (res >= 0)
			res = write(fd, data, len);
		if (res < 1) {
			if (errno != EAGAIN) {
				ast_log(LOG_WARNING, "Failed to write audio data to conference: %s\n", strerror(errno));
				return -1;
			} else
				return 0;
		}
		len -= res;
		data += res;
	}

	return 0;
}

static int set_talk_volume(struct ast_conf_user *user, int volume)
{
	char gain_adjust;

	/* attempt to make the adjustment in the channel driver;
	   if successful, don't adjust in the frame reading routine
	*/
	gain_adjust = gain_map[volume + 5];

	return ast_channel_setoption(user->chan, AST_OPTION_RXGAIN, &gain_adjust, sizeof(gain_adjust), 0);
}

static int set_listen_volume(struct ast_conf_user *user, int volume)
{
	char gain_adjust;

	/* attempt to make the adjustment in the channel driver;
	   if successful, don't adjust in the frame reading routine
	*/
	gain_adjust = gain_map[volume + 5];

	return ast_channel_setoption(user->chan, AST_OPTION_TXGAIN, &gain_adjust, sizeof(gain_adjust), 0);
}

static void tweak_volume(struct volume *vol, enum volume_action action)
{
	switch (action) {
	case VOL_UP:
		switch (vol->desired) { 
		case 5:
			break;
		case 0:
			vol->desired = 2;
			break;
		case -2:
			vol->desired = 0;
			break;
		default:
			vol->desired++;
			break;
		}
		break;
	case VOL_DOWN:
		switch (vol->desired) {
		case -5:
			break;
		case 2:
			vol->desired = 0;
			break;
		case 0:
			vol->desired = -2;
			break;
		default:
			vol->desired--;
			break;
		}
	}
}

static void tweak_talk_volume(struct ast_conf_user *user, enum volume_action action)
{
	tweak_volume(&user->talk, action);
	/* attempt to make the adjustment in the channel driver;
	   if successful, don't adjust in the frame reading routine
	*/
	if (!set_talk_volume(user, user->talk.desired))
		user->talk.actual = 0;
	else
		user->talk.actual = user->talk.desired;
}

static void tweak_listen_volume(struct ast_conf_user *user, enum volume_action action)
{
	tweak_volume(&user->listen, action);
	/* attempt to make the adjustment in the channel driver;
	   if successful, don't adjust in the frame reading routine
	*/
	if (!set_listen_volume(user, user->listen.desired))
		user->listen.actual = 0;
	else
		user->listen.actual = user->listen.desired;
}

static void reset_volumes(struct ast_conf_user *user)
{
	signed char zero_volume = 0;

	ast_channel_setoption(user->chan, AST_OPTION_TXGAIN, &zero_volume, sizeof(zero_volume), 0);
	ast_channel_setoption(user->chan, AST_OPTION_RXGAIN, &zero_volume, sizeof(zero_volume), 0);
}

static void conf_play(struct ast_channel *chan, struct ast_conference *conf, enum entrance_sound sound)
{
	unsigned char *data;
	int len;
	int res = -1;

	if (!chan->_softhangup)
		res = ast_autoservice_start(chan);

	AST_LIST_LOCK(&confs);

	switch(sound) {
	case ENTER:
		data = enter;
		len = sizeof(enter);
		break;
	case LEAVE:
		data = leave;
		len = sizeof(leave);
		break;
	default:
		data = NULL;
		len = 0;
	}
	if (data) {
		careful_write(conf->fd, data, len, 1);
	}

	AST_LIST_UNLOCK(&confs);

	if (!res) 
		ast_autoservice_stop(chan);
}

static int user_no_cmp(void *obj, void *arg, int flags)
{
	struct ast_conf_user *user = obj;
	int *user_no = arg;

	if (user->user_no == *user_no) {
		return (CMP_MATCH | CMP_STOP);
	}

	return 0;
}

static int user_max_cmp(void *obj, void *arg, int flags)
{
	struct ast_conf_user *user = obj;
	int *max_no = arg;

	if (user->user_no > *max_no) {
		*max_no = user->user_no;
	}

	return 0;
}

/*!
 * \brief Find or create a conference
 *
 * \param confno The conference name/number
 * \param pin The regular user pin
 * \param pinadmin The admin pin
 * \param make Make the conf if it doesn't exist
 * \param dynamic Mark the newly created conference as dynamic
 * \param refcount How many references to mark on the conference
 *
 * \return A pointer to the conference struct, or NULL if it wasn't found and
 *         make or dynamic were not set.
 */
static struct ast_conference *build_conf(char *confno, char *pin, char *pinadmin, int make, int dynamic, int refcount)
{
	struct ast_conference *cnf;
	struct dahdi_confinfo ztc = { 0, };
	int confno_int = 0;

	AST_LIST_LOCK(&confs);

	AST_LIST_TRAVERSE(&confs, cnf, list) {
		if (!strcmp(confno, cnf->confno)) 
			break;
	}

	if (cnf || (!make && !dynamic))
		goto cnfout;

	/* Make a new one */
	if (!(cnf = ast_calloc(1, sizeof(*cnf))) ||
		!(cnf->usercontainer = ao2_container_alloc(1, NULL, user_no_cmp))) {
		goto cnfout;
	}

	ast_mutex_init(&cnf->playlock);
	ast_mutex_init(&cnf->listenlock);
	cnf->recordthread = AST_PTHREADT_NULL;
	ast_mutex_init(&cnf->recordthreadlock);
	cnf->announcethread = AST_PTHREADT_NULL;
	ast_mutex_init(&cnf->announcethreadlock);
	ast_copy_string(cnf->confno, confno, sizeof(cnf->confno));
	ast_copy_string(cnf->pin, pin, sizeof(cnf->pin));
	ast_copy_string(cnf->pinadmin, pinadmin, sizeof(cnf->pinadmin));

	/* Setup a new zap conference */
	ztc.confno = -1;
	ztc.confmode = DAHDI_CONF_CONFANN | DAHDI_CONF_CONFANNMON;
	cnf->fd = open(DAHDI_FILE_PSEUDO, O_RDWR);
	if (cnf->fd < 0 || ioctl(cnf->fd, DAHDI_SETCONF, &ztc)) {
		ast_log(LOG_WARNING, "Unable to open DAHDI pseudo device\n");
		if (cnf->fd >= 0)
			close(cnf->fd);
		ao2_ref(cnf->usercontainer, -1);
		ast_mutex_destroy(&cnf->playlock);
		ast_mutex_destroy(&cnf->listenlock);
		ast_mutex_destroy(&cnf->recordthreadlock);
		ast_mutex_destroy(&cnf->announcethreadlock);
		free(cnf);
		cnf = NULL;
		goto cnfout;
	}

	cnf->zapconf = ztc.confno;

	/* Setup a new channel for playback of audio files */
	cnf->chan = ast_request(dahdi_chan_name, AST_FORMAT_SLINEAR, "pseudo", NULL);
	if (cnf->chan) {
		ast_set_read_format(cnf->chan, AST_FORMAT_SLINEAR);
		ast_set_write_format(cnf->chan, AST_FORMAT_SLINEAR);
		ztc.chan = 0;
		ztc.confno = cnf->zapconf;
		ztc.confmode = DAHDI_CONF_CONFANN | DAHDI_CONF_CONFANNMON;
		if (ioctl(cnf->chan->fds[0], DAHDI_SETCONF, &ztc)) {
			ast_log(LOG_WARNING, "Error setting conference\n");
			if (cnf->chan)
				ast_hangup(cnf->chan);
			else
				close(cnf->fd);
			ao2_ref(cnf->usercontainer, -1);
			ast_mutex_destroy(&cnf->playlock);
			ast_mutex_destroy(&cnf->listenlock);
			ast_mutex_destroy(&cnf->recordthreadlock);
			ast_mutex_destroy(&cnf->announcethreadlock);
			free(cnf);
			cnf = NULL;
			goto cnfout;
		}
	}

	/* Fill the conference struct */
	cnf->start = time(NULL);
	cnf->isdynamic = dynamic ? 1 : 0;
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Created MeetMe conference %d for conference '%s'\n", cnf->zapconf, cnf->confno);
	AST_LIST_INSERT_HEAD(&confs, cnf, list);

	/* Reserve conference number in map */
	if ((sscanf(cnf->confno, "%30d", &confno_int) == 1) && (confno_int >= 0 && confno_int < 1024))
		conf_map[confno_int] = 1;
	
cnfout:
	if (cnf)
		ast_atomic_fetchadd_int(&cnf->refcount, refcount);

	AST_LIST_UNLOCK(&confs);

	return cnf;
}

static int meetme_cmd(int fd, int argc, char **argv) 
{
	/* Process the command */
	struct ast_conference *cnf;
	struct ast_conf_user *user;
	int hr, min, sec;
	int i = 0, total = 0;
	time_t now;
	char *header_format = "%-14s %-14s %-10s %-8s  %-8s\n";
	char *data_format = "%-12.12s   %4.4d	      %4.4s       %02d:%02d:%02d  %-8s\n";
	char cmdline[1024] = "";

	if (argc > 8)
		ast_cli(fd, "Invalid Arguments.\n");
	/* Check for length so no buffer will overflow... */
	for (i = 0; i < argc; i++) {
		if (strlen(argv[i]) > 100)
			ast_cli(fd, "Invalid Arguments.\n");
	}
	if (argc == 1) {
		/* 'MeetMe': List all the conferences */	
		now = time(NULL);
		AST_LIST_LOCK(&confs);
		if (AST_LIST_EMPTY(&confs)) {
			ast_cli(fd, "No active MeetMe conferences.\n");
			AST_LIST_UNLOCK(&confs);
			return RESULT_SUCCESS;
		}
		ast_cli(fd, header_format, "Conf Num", "Parties", "Marked", "Activity", "Creation");
		AST_LIST_TRAVERSE(&confs, cnf, list) {
			if (cnf->markedusers == 0)
				strcpy(cmdline, "N/A ");
			else 
				snprintf(cmdline, sizeof(cmdline), "%4.4d", cnf->markedusers);
			hr = (now - cnf->start) / 3600;
			min = ((now - cnf->start) % 3600) / 60;
			sec = (now - cnf->start) % 60;

			ast_cli(fd, data_format, cnf->confno, cnf->users, cmdline, hr, min, sec, cnf->isdynamic ? "Dynamic" : "Static");

			total += cnf->users; 	
		}
		AST_LIST_UNLOCK(&confs);
		ast_cli(fd, "* Total number of MeetMe users: %d\n", total);
		return RESULT_SUCCESS;
	}
	if (argc < 3)
		return RESULT_SHOWUSAGE;
	ast_copy_string(cmdline, argv[2], sizeof(cmdline));	/* Argv 2: conference number */
	if (strstr(argv[1], "lock")) {	
		if (strcmp(argv[1], "lock") == 0) {
			/* Lock */
			strncat(cmdline, "|L", sizeof(cmdline) - strlen(cmdline) - 1);
		} else {
			/* Unlock */
			strncat(cmdline, "|l", sizeof(cmdline) - strlen(cmdline) - 1);
		}
	} else if (strstr(argv[1], "mute")) { 
		if (argc < 4)
			return RESULT_SHOWUSAGE;
		if (strcmp(argv[1], "mute") == 0) {
			/* Mute */
			if (strcmp(argv[3], "all") == 0) {
				strncat(cmdline, "|N", sizeof(cmdline) - strlen(cmdline) - 1);
			} else {
				strncat(cmdline, "|M|", sizeof(cmdline) - strlen(cmdline) - 1);	
				strncat(cmdline, argv[3], sizeof(cmdline) - strlen(cmdline) - 1);
			}
		} else {
			/* Unmute */
			if (strcmp(argv[3], "all") == 0) {
				strncat(cmdline, "|n", sizeof(cmdline) - strlen(cmdline) - 1);
			} else {
				strncat(cmdline, "|m|", sizeof(cmdline) - strlen(cmdline) - 1);
				strncat(cmdline, argv[3], sizeof(cmdline) - strlen(cmdline) - 1);
			}
		}
	} else if (strcmp(argv[1], "kick") == 0) {
		if (argc < 4)
			return RESULT_SHOWUSAGE;
		if (strcmp(argv[3], "all") == 0) {
			/* Kick all */
			strncat(cmdline, "|K", sizeof(cmdline) - strlen(cmdline) - 1);
		} else {
			/* Kick a single user */
			strncat(cmdline, "|k|", sizeof(cmdline) - strlen(cmdline) - 1);
			strncat(cmdline, argv[3], sizeof(cmdline) - strlen(cmdline) - 1);
		}	
	} else if(strcmp(argv[1], "list") == 0) {
		struct ao2_iterator user_iter;
		int concise = ( 4 == argc && ( !strcasecmp(argv[3], "concise") ) );
		/* List all the users in a conference */
		if (AST_LIST_EMPTY(&confs)) {
			if ( !concise )
				ast_cli(fd, "No active conferences.\n");
			return RESULT_SUCCESS;	
		}
		/* Find the right conference */
		AST_LIST_LOCK(&confs);
		AST_LIST_TRAVERSE(&confs, cnf, list) {
			if (strcmp(cnf->confno, argv[2]) == 0)
				break;
		}
		if (!cnf) {
			if ( !concise )
				ast_cli(fd, "No such conference: %s.\n",argv[2]);
			AST_LIST_UNLOCK(&confs);
			return RESULT_SUCCESS;
		}
		/* Show all the users */
		time(&now);
		user_iter = ao2_iterator_init(cnf->usercontainer, 0);
		while((user = ao2_iterator_next(&user_iter))) {
			hr = (now - user->jointime) / 3600;
			min = ((now - user->jointime) % 3600) / 60;
			sec = (now - user->jointime) % 60;
			if (!concise) {
				ast_cli(fd, "User #: %-2.2d %12.12s %-20.20s Channel: %s %s %s %s %s %02d:%02d:%02d\n",
					user->user_no,
					S_OR(user->chan->cid.cid_num, "<unknown>"),
					S_OR(user->chan->cid.cid_name, "<no name>"),
					user->chan->name,
					user->userflags & CONFFLAG_ADMIN ? "(Admin)" : "",
					user->userflags & CONFFLAG_MONITOR ? "(Listen only)" : "",
					user->adminflags & ADMINFLAG_MUTED ? "(Admin Muted)" : user->adminflags & ADMINFLAG_SELFMUTED ? "(Muted)" : "",
					istalking(user->talking), hr, min, sec); 
			} else {
				ast_cli(fd, "%d!%s!%s!%s!%s!%s!%s!%d!%02d:%02d:%02d\n",
					user->user_no,
					S_OR(user->chan->cid.cid_num, ""),
					S_OR(user->chan->cid.cid_name, ""),
					user->chan->name,
					user->userflags  & CONFFLAG_ADMIN   ? "1" : "",
					user->userflags  & CONFFLAG_MONITOR ? "1" : "",
					user->adminflags & (ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED)  ? "1" : "",
					user->talking, hr, min, sec);
			}
			ao2_ref(user, -1);
		}
		ao2_iterator_destroy(&user_iter);
		if ( !concise )
			ast_cli(fd,"%d users in that conference.\n",cnf->users);
		AST_LIST_UNLOCK(&confs);
		return RESULT_SUCCESS;
	} else 
		return RESULT_SHOWUSAGE;
	ast_log(LOG_DEBUG, "Cmdline: %s\n", cmdline);
	admin_exec(NULL, cmdline);

	return 0;
}

static char *complete_meetmecmd(const char *line, const char *word, int pos, int state)
{
	static char *cmds[] = {"lock", "unlock", "mute", "unmute", "kick", "list", NULL};

	int len = strlen(word);
	int which = 0;
	struct ast_conference *cnf = NULL;
	struct ast_conf_user *usr = NULL;
	char *confno = NULL;
	char usrno[50] = "";
	char *myline, *ret = NULL;
	
	if (pos == 1) {		/* Command */
		return ast_cli_complete(word, cmds, state);
	} else if (pos == 2) {	/* Conference Number */
		AST_LIST_LOCK(&confs);
		AST_LIST_TRAVERSE(&confs, cnf, list) {
			if (!strncasecmp(word, cnf->confno, len) && ++which > state) {
				ret = cnf->confno;
				break;
			}
		}
		ret = ast_strdup(ret); /* dup before releasing the lock */
		AST_LIST_UNLOCK(&confs);
		return ret;
	} else if (pos == 3) {
		/* User Number || Conf Command option*/
		if (strstr(line, "mute") || strstr(line, "kick")) {
			if (state == 0 && (strstr(line, "kick") || strstr(line,"mute")) && !strncasecmp(word, "all", len))
				return strdup("all");
			which++;
			AST_LIST_LOCK(&confs);

			/* TODO: Find the conf number from the cmdline (ignore spaces) <- test this and make it fail-safe! */
			myline = ast_strdupa(line);
			if (strsep(&myline, " ") && strsep(&myline, " ") && !confno) {
				while((confno = strsep(&myline, " ")) && (strcmp(confno, " ") == 0))
					;
			}
			
			AST_LIST_TRAVERSE(&confs, cnf, list) {
				if (!strcmp(confno, cnf->confno))
				    break;
			}

			if (cnf) {
				struct ao2_iterator user_iter;
				user_iter = ao2_iterator_init(cnf->usercontainer, 0);
				/* Search for the user */
				while((usr = ao2_iterator_next(&user_iter))) {
					snprintf(usrno, sizeof(usrno), "%d", usr->user_no);
					if (!strncasecmp(word, usrno, len) && ++which > state) {
						ao2_ref(usr, -1);
						break;
					}
					ao2_ref(usr, -1);
				}
				ao2_iterator_destroy(&user_iter);
				AST_LIST_UNLOCK(&confs);
				return usr ? strdup(usrno) : NULL;
			}
			AST_LIST_UNLOCK(&confs);
		} else if ( strstr(line, "list") && ( 0 == state ) )
			return strdup("concise");
	}

	return NULL;
}
	
static char meetme_usage[] =
"Usage: meetme (un)lock|(un)mute|kick|list [concise] <confno> <usernumber>\n"
"       Executes a command for the conference or on a conferee\n";

static const char *sla_hold_str(unsigned int hold_access)
{
	const char *hold = "Unknown";

	switch (hold_access) {
	case SLA_HOLD_OPEN:
		hold = "Open";
		break;
	case SLA_HOLD_PRIVATE:
		hold = "Private";
	default:
		break;
	}

	return hold;
}

static int sla_show_trunks(int fd, int argc, char **argv)
{
	const struct sla_trunk *trunk;

	ast_cli(fd, "\n"
	            "=============================================================\n"
	            "=== Configured SLA Trunks ===================================\n"
	            "=============================================================\n"
	            "===\n");
	AST_RWLIST_RDLOCK(&sla_trunks);
	AST_RWLIST_TRAVERSE(&sla_trunks, trunk, entry) {
		struct sla_station_ref *station_ref;
		char ring_timeout[16] = "(none)";
		if (trunk->ring_timeout)
			snprintf(ring_timeout, sizeof(ring_timeout), "%u Seconds", trunk->ring_timeout);
		ast_cli(fd, "=== ---------------------------------------------------------\n"
		            "=== Trunk Name:       %s\n"
		            "=== ==> Device:       %s\n"
		            "=== ==> AutoContext:  %s\n"
		            "=== ==> RingTimeout:  %s\n"
		            "=== ==> BargeAllowed: %s\n"
		            "=== ==> HoldAccess:   %s\n"
		            "=== ==> Stations ...\n",
		            trunk->name, trunk->device, 
		            S_OR(trunk->autocontext, "(none)"), 
		            ring_timeout,
		            trunk->barge_disabled ? "No" : "Yes",
		            sla_hold_str(trunk->hold_access));
		AST_RWLIST_RDLOCK(&sla_stations);
		AST_LIST_TRAVERSE(&trunk->stations, station_ref, entry)
			ast_cli(fd, "===    ==> Station name: %s\n", station_ref->station->name);
		AST_RWLIST_UNLOCK(&sla_stations);
		ast_cli(fd, "=== ---------------------------------------------------------\n"
		            "===\n");
	}
	AST_RWLIST_UNLOCK(&sla_trunks);
	ast_cli(fd, "=============================================================\n"
	            "\n");

	return RESULT_SUCCESS;
}

static const char *trunkstate2str(enum sla_trunk_state state)
{
#define S(e) case e: return # e;
	switch (state) {
	S(SLA_TRUNK_STATE_IDLE)
	S(SLA_TRUNK_STATE_RINGING)
	S(SLA_TRUNK_STATE_UP)
	S(SLA_TRUNK_STATE_ONHOLD)
	S(SLA_TRUNK_STATE_ONHOLD_BYME)
	}
	return "Uknown State";
#undef S
}

static const char sla_show_trunks_usage[] =
"Usage: sla show trunks\n"
"       This will list all trunks defined in sla.conf\n";

static int sla_show_stations(int fd, int argc, char **argv)
{
	const struct sla_station *station;

	ast_cli(fd, "\n" 
	            "=============================================================\n"
	            "=== Configured SLA Stations =================================\n"
	            "=============================================================\n"
	            "===\n");
	AST_RWLIST_RDLOCK(&sla_stations);
	AST_RWLIST_TRAVERSE(&sla_stations, station, entry) {
		struct sla_trunk_ref *trunk_ref;
		char ring_timeout[16] = "(none)";
		char ring_delay[16] = "(none)";
		if (station->ring_timeout) {
			snprintf(ring_timeout, sizeof(ring_timeout), 
				"%u", station->ring_timeout);
		}
		if (station->ring_delay) {
			snprintf(ring_delay, sizeof(ring_delay), 
				"%u", station->ring_delay);
		}
		ast_cli(fd, "=== ---------------------------------------------------------\n"
		            "=== Station Name:    %s\n"
		            "=== ==> Device:      %s\n"
		            "=== ==> AutoContext: %s\n"
		            "=== ==> RingTimeout: %s\n"
		            "=== ==> RingDelay:   %s\n"
		            "=== ==> HoldAccess:  %s\n"
		            "=== ==> Trunks ...\n",
		            station->name, station->device,
		            S_OR(station->autocontext, "(none)"), 
		            ring_timeout, ring_delay,
		            sla_hold_str(station->hold_access));
		AST_RWLIST_RDLOCK(&sla_trunks);
		AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
			if (trunk_ref->ring_timeout) {
				snprintf(ring_timeout, sizeof(ring_timeout),
					"%u", trunk_ref->ring_timeout);
			} else
				strcpy(ring_timeout, "(none)");
			if (trunk_ref->ring_delay) {
				snprintf(ring_delay, sizeof(ring_delay),
					"%u", trunk_ref->ring_delay);
			} else
				strcpy(ring_delay, "(none)");
			ast_cli(fd, "===    ==> Trunk Name: %s\n"
			            "===       ==> State:       %s\n"
			            "===       ==> RingTimeout: %s\n"
			            "===       ==> RingDelay:   %s\n",
			            trunk_ref->trunk->name,
			            trunkstate2str(trunk_ref->state),
			            ring_timeout, ring_delay);
		}
		AST_RWLIST_UNLOCK(&sla_trunks);
		ast_cli(fd, "=== ---------------------------------------------------------\n"
		            "===\n");
	}
	AST_RWLIST_UNLOCK(&sla_stations);
	ast_cli(fd, "============================================================\n"
	            "\n");

	return RESULT_SUCCESS;
}

static const char sla_show_stations_usage[] =
"Usage: sla show stations\n"
"       This will list all stations defined in sla.conf\n";

static struct ast_cli_entry cli_meetme[] = {
	{ { "meetme", NULL, NULL },
	meetme_cmd, "Execute a command on a conference or conferee",
	meetme_usage, complete_meetmecmd },

	{ { "sla", "show", "trunks", NULL },
	sla_show_trunks, "Show SLA Trunks",
	sla_show_trunks_usage, NULL },

	{ { "sla", "show", "stations", NULL },
	sla_show_stations, "Show SLA Stations",
	sla_show_stations_usage, NULL },
};

static void conf_flush(int fd, struct ast_channel *chan)
{
	int x;

	/* read any frames that may be waiting on the channel
	   and throw them away
	*/
	if (chan) {
		struct ast_frame *f;

		/* when no frames are available, this will wait
		   for 1 millisecond maximum
		*/
		while (ast_waitfor(chan, 1)) {
			f = ast_read(chan);
			if (f)
				ast_frfree(f);
			else /* channel was hung up or something else happened */
				break;
		}
	}

	/* flush any data sitting in the pseudo channel */
	x = DAHDI_FLUSH_ALL;
	if (ioctl(fd, DAHDI_FLUSH, &x))
		ast_log(LOG_WARNING, "Error flushing channel\n");

}

/* Remove the conference from the list and free it.
   We assume that this was called while holding conflock. */
static int conf_free(struct ast_conference *conf)
{
	int x;
	struct announce_listitem *item;
	
	AST_LIST_REMOVE(&confs, conf, list);

	if (conf->recording == MEETME_RECORD_ACTIVE) {
		conf->recording = MEETME_RECORD_TERMINATE;
		AST_LIST_UNLOCK(&confs);
		while (1) {
			usleep(1);
			AST_LIST_LOCK(&confs);
			if (conf->recording == MEETME_RECORD_OFF)
				break;
			AST_LIST_UNLOCK(&confs);
		}
	}

	for (x=0;x<AST_FRAME_BITS;x++) {
		if (conf->transframe[x])
			ast_frfree(conf->transframe[x]);
		if (conf->transpath[x])
			ast_translator_free_path(conf->transpath[x]);
	}
	if (conf->announcethread != AST_PTHREADT_NULL) {
		ast_mutex_lock(&conf->announcelistlock);
		conf->announcethread_stop = 1;
		ast_softhangup(conf->chan, AST_SOFTHANGUP_EXPLICIT);
		ast_cond_signal(&conf->announcelist_addition);
		ast_mutex_unlock(&conf->announcelistlock);
		pthread_join(conf->announcethread, NULL);
	
		while ((item = AST_LIST_REMOVE_HEAD(&conf->announcelist, entry))) {
			ast_filedelete(item->namerecloc, NULL);
			ao2_ref(item, -1);
		}
		ast_mutex_destroy(&conf->announcelistlock);
	}
	if (conf->origframe)
		ast_frfree(conf->origframe);
	if (conf->lchan)
		ast_hangup(conf->lchan);
	if (conf->chan)
		ast_hangup(conf->chan);
	if (conf->fd >= 0)
		close(conf->fd);
	if (conf->usercontainer) {
		ao2_ref(conf->usercontainer, -1);
	}

	ast_mutex_destroy(&conf->playlock);
	ast_mutex_destroy(&conf->listenlock);
	ast_mutex_destroy(&conf->recordthreadlock);
	ast_mutex_destroy(&conf->announcethreadlock);

	free(conf);

	return 0;
}

static void conf_queue_dtmf(const struct ast_conference *conf,
	const struct ast_conf_user *sender, struct ast_frame *f)
{
	struct ast_conf_user *user;
	struct ao2_iterator user_iter;

	user_iter = ao2_iterator_init(conf->usercontainer, 0);
	while ((user = ao2_iterator_next(&user_iter))) {
		if (user == sender) {
			ao2_ref(user, -1);
			continue;
		}
		if (ast_write(user->chan, f) < 0)
			ast_log(LOG_WARNING, "Error writing frame to channel %s\n", user->chan->name);
		ao2_ref(user, -1);
	}
	ao2_iterator_destroy(&user_iter);
}

static void sla_queue_event_full(enum sla_event_type type, 
	struct sla_trunk_ref *trunk_ref, struct sla_station *station, int lock)
{
	struct sla_event *event;

	if (sla.thread == AST_PTHREADT_NULL) {
		return;
	}

	if (!(event = ast_calloc(1, sizeof(*event))))
		return;

	event->type = type;
	event->trunk_ref = trunk_ref;
	event->station = station;

	if (!lock) {
		AST_LIST_INSERT_TAIL(&sla.event_q, event, entry);
		return;
	}

	ast_mutex_lock(&sla.lock);
	AST_LIST_INSERT_TAIL(&sla.event_q, event, entry);
	ast_cond_signal(&sla.cond);
	ast_mutex_unlock(&sla.lock);
}

static void sla_queue_event_nolock(enum sla_event_type type)
{
	sla_queue_event_full(type, NULL, NULL, 0);
}

static void sla_queue_event(enum sla_event_type type)
{
	sla_queue_event_full(type, NULL, NULL, 1);
}

/*! \brief Queue a SLA event from the conference */
static void sla_queue_event_conf(enum sla_event_type type, struct ast_channel *chan,
	struct ast_conference *conf)
{
	struct sla_station *station;
	struct sla_trunk_ref *trunk_ref = NULL;
	char *trunk_name;

	trunk_name = ast_strdupa(conf->confno);
	strsep(&trunk_name, "_");
	if (ast_strlen_zero(trunk_name)) {
		ast_log(LOG_ERROR, "Invalid conference name for SLA - '%s'!\n", conf->confno);
		return;
	}

	AST_RWLIST_RDLOCK(&sla_stations);
	AST_RWLIST_TRAVERSE(&sla_stations, station, entry) {
		AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
			if (trunk_ref->chan == chan && !strcmp(trunk_ref->trunk->name, trunk_name))
				break;
		}
		if (trunk_ref)
			break;
	}
	AST_RWLIST_UNLOCK(&sla_stations);

	if (!trunk_ref) {
		ast_log(LOG_DEBUG, "Trunk not found for event!\n");
		return;
	}

	sla_queue_event_full(type, trunk_ref, station, 1);
}

/* Decrement reference counts, as incremented by find_conf() */
static int dispose_conf(struct ast_conference *conf)
{
	int res = 0;
	int confno_int = 0;

	AST_LIST_LOCK(&confs);
	if (ast_atomic_dec_and_test(&conf->refcount)) {
		/* Take the conference room number out of an inuse state */
		if ((sscanf(conf->confno, "%30d", &confno_int) == 1) && (confno_int >= 0 && confno_int < 1024))
			conf_map[confno_int] = 0;
		conf_free(conf);
		res = 1;
	}
	AST_LIST_UNLOCK(&confs);

	return res;
}

static const char *get_announce_filename(enum announcetypes type)
{
	switch (type) {
	case CONF_HASLEFT:
		return "conf-hasleft";
		break;
	case CONF_HASJOIN:
		return "conf-hasjoin";
		break;
	default:
		return "";
	}
}

static void *announce_thread(void *data)
{
	struct announce_listitem *current;
	struct ast_conference *conf = data;
	int res;
	char filename[PATH_MAX] = "";
	AST_LIST_HEAD_NOLOCK(, announce_listitem) local_list;
	AST_LIST_HEAD_INIT_NOLOCK(&local_list);

	while (!conf->announcethread_stop) {
		ast_mutex_lock(&conf->announcelistlock);
		if (conf->announcethread_stop) {
			ast_mutex_unlock(&conf->announcelistlock);
			break;
		}
		if (AST_LIST_EMPTY(&conf->announcelist))
			ast_cond_wait(&conf->announcelist_addition, &conf->announcelistlock);

		AST_LIST_APPEND_LIST(&local_list, &conf->announcelist, entry);
		AST_LIST_HEAD_INIT_NOLOCK(&conf->announcelist);

		ast_mutex_unlock(&conf->announcelistlock);
		if (conf->announcethread_stop) {
			break;
		}

		for (res = 1; !conf->announcethread_stop && (current = AST_LIST_REMOVE_HEAD(&local_list, entry)); ao2_ref(current, -1)) {
			ast_log(LOG_DEBUG, "About to play %s\n", current->namerecloc);
			if (!ast_fileexists(current->namerecloc, NULL, NULL))
				continue;
			if ((current->confchan) && (current->confusers > 1) && !ast_check_hangup(current->confchan)) {
				if (!ast_streamfile(current->confchan, current->namerecloc, current->language))
					res = ast_waitstream(current->confchan, "");
				if (!res) {
					ast_copy_string(filename, get_announce_filename(current->announcetype), sizeof(filename));
					if (!ast_streamfile(current->confchan, filename, current->language))
						ast_waitstream(current->confchan, "");
				}
			}
			if (current->announcetype == CONF_HASLEFT) {
				ast_filedelete(current->namerecloc, NULL);
			}
		}
	}

	/* thread marked to stop, clean up */
	while ((current = AST_LIST_REMOVE_HEAD(&local_list, entry))) {
		ast_filedelete(current->namerecloc, NULL);
		ao2_ref(current, -1);
	}
	return NULL;
}

static int can_write(struct ast_channel *chan, int confflags)
{
	if (!(confflags & CONFFLAG_NO_AUDIO_UNTIL_UP)) {
		return 1;
	}

	return (chan->_state == AST_STATE_UP);
}

static void send_talking_event(struct ast_channel *chan, struct ast_conference *conf, struct ast_conf_user *user, int talking)
{
	manager_event(EVENT_FLAG_CALL, "MeetmeTalking",
	      "Channel: %s\r\n"
	      "Uniqueid: %s\r\n"
	      "Meetme: %s\r\n"
	      "Usernum: %d\r\n"
	      "Status: %s\r\n",
	      chan->name, chan->uniqueid, conf->confno, user->user_no, talking ? "on" : "off");
}

static void set_user_talking(struct ast_channel *chan, struct ast_conference *conf, struct ast_conf_user *user, int talking, int monitor)
{
	int last_talking = user->talking;
	if (last_talking == talking)
		return;

	user->talking = talking;

	if (monitor) {
		/* Check if talking state changed. Take care of -1 which means unmonitored */
		int was_talking = (last_talking > 0);
		int now_talking = (talking > 0);
		if (was_talking != now_talking) {
			send_talking_event(chan, conf, user, now_talking);
		}
	}
}

static int conf_run(struct ast_channel *chan, struct ast_conference *conf, int confflags, char *optargs[])
{
	struct ast_conf_user *user = NULL;
	int fd;
	struct dahdi_confinfo ztc, ztc_empty;
	struct ast_frame *f;
	struct ast_channel *c;
	struct ast_frame fr;
	int outfd;
	int ms;
	int nfds;
	int res;
	int retryzap;
	int origfd;
	int musiconhold = 0, mohtempstopped = 0;
	int firstpass = 0;
	int lastmarked = 0;
	int currentmarked = 0;
	int ret = -1;
	int x;
	int menu_active = 0;
	int using_pseudo = 0;
	int duration=20;
	int hr, min, sec;
	int sent_event = 0;
	time_t now;
	struct ast_dsp *dsp=NULL;
	struct ast_app *app;
	const char *agifile;
	const char *agifiledefault = "conf-background.agi";
	char meetmesecs[30] = "";
	char exitcontext[AST_MAX_CONTEXT] = "";
	char recordingtmp[AST_MAX_EXTENSION] = "";
	char members[10] = "";
	int dtmf, opt_waitmarked_timeout = 0;
	time_t timeout = 0;
	struct dahdi_bufferinfo bi;
	char __buf[CONF_SIZE + AST_FRIENDLY_OFFSET];
	char *buf = __buf + AST_FRIENDLY_OFFSET;
	int setusercount = 0;
	int confsilence = 0, totalsilence = 0;

	if (!(user = ao2_alloc(sizeof(*user), NULL))) {
		return ret;
	}

	/* Possible timeout waiting for marked user */
	if ((confflags & CONFFLAG_WAITMARKED) &&
		!ast_strlen_zero(optargs[OPT_ARG_WAITMARKED]) &&
		(sscanf(optargs[OPT_ARG_WAITMARKED], "%30d", &opt_waitmarked_timeout) == 1) &&
		(opt_waitmarked_timeout > 0)) {
		timeout = time(NULL) + opt_waitmarked_timeout;
	}

	if (confflags & CONFFLAG_RECORDCONF) {
		if (!conf->recordingfilename) {
			conf->recordingfilename = pbx_builtin_getvar_helper(chan, "MEETME_RECORDINGFILE");
			if (!conf->recordingfilename) {
				snprintf(recordingtmp, sizeof(recordingtmp), "meetme-conf-rec-%s-%s", conf->confno, chan->uniqueid);
				conf->recordingfilename = ast_strdupa(recordingtmp);
			}
			conf->recordingformat = pbx_builtin_getvar_helper(chan, "MEETME_RECORDINGFORMAT");
			if (!conf->recordingformat) {
				snprintf(recordingtmp, sizeof(recordingtmp), "wav");
				conf->recordingformat = ast_strdupa(recordingtmp);
			}
			ast_verbose(VERBOSE_PREFIX_4 "Starting recording of MeetMe Conference %s into file %s.%s.\n",
				    conf->confno, conf->recordingfilename, conf->recordingformat);
		}
	}

	ast_mutex_lock(&conf->recordthreadlock);
	if ((conf->recordthread == AST_PTHREADT_NULL) && (confflags & CONFFLAG_RECORDCONF) && ((conf->lchan = ast_request(dahdi_chan_name, AST_FORMAT_SLINEAR, "pseudo", NULL)))) {
		ast_set_read_format(conf->lchan, AST_FORMAT_SLINEAR);
		ast_set_write_format(conf->lchan, AST_FORMAT_SLINEAR);
		ztc.chan = 0;
		ztc.confno = conf->zapconf;
		ztc.confmode = DAHDI_CONF_CONFANN | DAHDI_CONF_CONFANNMON;
		if (ioctl(conf->lchan->fds[0], DAHDI_SETCONF, &ztc)) {
			ast_log(LOG_WARNING, "Error starting listen channel\n");
			ast_hangup(conf->lchan);
			conf->lchan = NULL;
		} else {
			pthread_attr_init(&conf->attr);
			pthread_attr_setdetachstate(&conf->attr, PTHREAD_CREATE_DETACHED);
			ast_pthread_create_background(&conf->recordthread, &conf->attr, recordthread, conf);
			pthread_attr_destroy(&conf->attr);
		}
	}
	ast_mutex_unlock(&conf->recordthreadlock);

	ast_mutex_lock(&conf->announcethreadlock);
	if ((conf->announcethread == AST_PTHREADT_NULL) && !(confflags & CONFFLAG_QUIET) && ((confflags & CONFFLAG_INTROUSER) || (confflags & CONFFLAG_INTROUSERNOREVIEW))) {
		ast_mutex_init(&conf->announcelistlock);
		AST_LIST_HEAD_INIT_NOLOCK(&conf->announcelist);
		ast_pthread_create_background(&conf->announcethread, NULL, announce_thread, conf);
	}
	ast_mutex_unlock(&conf->announcethreadlock);

	time(&user->jointime);

	if (conf->locked && (!(confflags & CONFFLAG_ADMIN))) {
		/* Sorry, but this confernce is locked! */	
		if (!ast_streamfile(chan, "conf-locked", chan->language))
			ast_waitstream(chan, "");
		goto outrun;
	}

   	ast_mutex_lock(&conf->playlock);
	ao2_lock(conf->usercontainer);
	ao2_callback(conf->usercontainer, OBJ_NODATA, user_max_cmp, &user->user_no);
	user->user_no++;
	ao2_link(conf->usercontainer, user);
	ao2_unlock(conf->usercontainer);

	user->chan = chan;
	user->userflags = confflags;
	user->adminflags = (confflags & CONFFLAG_STARTMUTED) ? ADMINFLAG_SELFMUTED : 0;
	user->talking = -1;

	ast_mutex_unlock(&conf->playlock);

	if (!(confflags & CONFFLAG_QUIET) && ((confflags & CONFFLAG_INTROUSER) || (confflags & CONFFLAG_INTROUSERNOREVIEW))) {
		char destdir[PATH_MAX];

		snprintf(destdir, sizeof(destdir), "%s/meetme", ast_config_AST_SPOOL_DIR);

		if (mkdir(destdir, 0777) && errno != EEXIST) {
			ast_log(LOG_WARNING, "mkdir '%s' failed: %s\n", destdir, strerror(errno));
			goto outrun;
		}

		snprintf(user->namerecloc, sizeof(user->namerecloc),
			 "%s/meetme-username-%s-%d", destdir,
			 conf->confno, user->user_no);
		if (confflags & CONFFLAG_INTROUSERNOREVIEW)
			res = ast_play_and_record(chan, "vm-rec-name", user->namerecloc, 10, "sln", &duration, 128, 0, NULL);
		else
			res = ast_record_review(chan, "vm-rec-name", user->namerecloc, 10, "sln", &duration, NULL);
		if (res == -1)
			goto outrun;
	}

	ast_mutex_lock(&conf->playlock);

	if (confflags & CONFFLAG_MARKEDUSER)
		conf->markedusers++;
	conf->users++;
	/* Update table */
	snprintf(members, sizeof(members), "%d", conf->users);
	ast_update_realtime("meetme", "confno", conf->confno, "members", members , NULL);
	setusercount = 1;

	/* This device changed state now - if this is the first user */
	if (conf->users == 1)
		ast_device_state_changed("meetme:%s", conf->confno);

	ast_mutex_unlock(&conf->playlock);

	if (confflags & CONFFLAG_EXIT_CONTEXT) {
		if ((agifile = pbx_builtin_getvar_helper(chan, "MEETME_EXIT_CONTEXT"))) 
			ast_copy_string(exitcontext, agifile, sizeof(exitcontext));
		else if (!ast_strlen_zero(chan->macrocontext)) 
			ast_copy_string(exitcontext, chan->macrocontext, sizeof(exitcontext));
		else
			ast_copy_string(exitcontext, chan->context, sizeof(exitcontext));
	}

	if ( !(confflags & (CONFFLAG_QUIET | CONFFLAG_NOONLYPERSON)) ) {
		if (conf->users == 1 && !(confflags & CONFFLAG_WAITMARKED))
			if (!ast_streamfile(chan, "conf-onlyperson", chan->language))
				ast_waitstream(chan, "");
		if ((confflags & CONFFLAG_WAITMARKED) && conf->markedusers == 0)
			if (!ast_streamfile(chan, "conf-waitforleader", chan->language))
				ast_waitstream(chan, "");
	}

	if (!(confflags & CONFFLAG_QUIET) && (confflags & CONFFLAG_ANNOUNCEUSERCOUNT) && conf->users > 1) {
		int keepplaying = 1;

		if (conf->users == 2) { 
			if (!ast_streamfile(chan,"conf-onlyone",chan->language)) {
				res = ast_waitstream(chan, AST_DIGIT_ANY);
				ast_stopstream(chan);
				if (res > 0)
					keepplaying=0;
				else if (res == -1)
					goto outrun;
			}
		} else { 
			if (!ast_streamfile(chan, "conf-thereare", chan->language)) {
				res = ast_waitstream(chan, AST_DIGIT_ANY);
				ast_stopstream(chan);
				if (res > 0)
					keepplaying=0;
				else if (res == -1)
					goto outrun;
			}
			if (keepplaying) {
				res = ast_say_number(chan, conf->users - 1, AST_DIGIT_ANY, chan->language, (char *) NULL);
				if (res > 0)
					keepplaying=0;
				else if (res == -1)
					goto outrun;
			}
			if (keepplaying && !ast_streamfile(chan, "conf-otherinparty", chan->language)) {
				res = ast_waitstream(chan, AST_DIGIT_ANY);
				ast_stopstream(chan);
				if (res > 0)
					keepplaying=0;
				else if (res == -1) 
					goto outrun;
			}
		}
	}

	if (!(confflags & CONFFLAG_NO_AUDIO_UNTIL_UP)) {
		/* We're leaving this alone until the state gets changed to up */
		ast_indicate(chan, -1);
	}

	if (ast_set_write_format(chan, AST_FORMAT_SLINEAR) < 0) {
		ast_log(LOG_WARNING, "Unable to set '%s' to write linear mode\n", chan->name);
		goto outrun;
	}

	if (ast_set_read_format(chan, AST_FORMAT_SLINEAR) < 0) {
		ast_log(LOG_WARNING, "Unable to set '%s' to read linear mode\n", chan->name);
		goto outrun;
	}

	retryzap = (strcasecmp(chan->tech->type, dahdi_chan_name) || (chan->audiohooks || chan->monitor) ? 1 : 0);
	user->zapchannel = !retryzap;

 zapretry:
	origfd = chan->fds[0];
	if (retryzap) {
		/* open pseudo in non-blocking mode */
		fd = open(DAHDI_FILE_PSEUDO, O_RDWR | O_NONBLOCK);
		if (fd < 0) {
			ast_log(LOG_WARNING, "Unable to open DAHDI pseudo channel: %s\n", strerror(errno));
			goto outrun;
		}
		using_pseudo = 1;
		/* Setup buffering information */
		memset(&bi, 0, sizeof(bi));
		bi.bufsize = CONF_SIZE/2;
		bi.txbufpolicy = DAHDI_POLICY_IMMEDIATE;
		bi.rxbufpolicy = DAHDI_POLICY_IMMEDIATE;
		bi.numbufs = audio_buffers;
		if (ioctl(fd, DAHDI_SET_BUFINFO, &bi)) {
			ast_log(LOG_WARNING, "Unable to set buffering information: %s\n", strerror(errno));
			close(fd);
			goto outrun;
		}
		x = 1;
		if (ioctl(fd, DAHDI_SETLINEAR, &x)) {
			ast_log(LOG_WARNING, "Unable to set linear mode: %s\n", strerror(errno));
			close(fd);
			goto outrun;
		}
		nfds = 1;
	} else {
		/* XXX Make sure we're not running on a pseudo channel XXX */
		fd = chan->fds[0];
		nfds = 0;
	}
	memset(&ztc, 0, sizeof(ztc));
	memset(&ztc_empty, 0, sizeof(ztc_empty));
	/* Check to see if we're in a conference... */
	ztc.chan = 0;	
	if (ioctl(fd, DAHDI_GETCONF, &ztc)) {
		ast_log(LOG_WARNING, "Error getting conference\n");
		close(fd);
		goto outrun;
	}
	if (ztc.confmode) {
		/* Whoa, already in a conference...  Retry... */
		if (!retryzap) {
			ast_log(LOG_DEBUG, "%s channel is in a conference already, retrying with pseudo\n", dahdi_chan_name);
			retryzap = 1;
			goto zapretry;
		}
	}
	memset(&ztc, 0, sizeof(ztc));
	/* Add us to the conference */
	ztc.chan = 0;	
	ztc.confno = conf->zapconf;

	if (!(confflags & CONFFLAG_QUIET) && ((confflags & CONFFLAG_INTROUSER) || (confflags & CONFFLAG_INTROUSERNOREVIEW)) && conf->users > 1) {
		struct announce_listitem *item;
		if (!(item = ao2_alloc(sizeof(*item), NULL)))
			goto outrun;
		ast_copy_string(item->namerecloc, user->namerecloc, sizeof(item->namerecloc));
		ast_copy_string(item->language, chan->language, sizeof(item->language));
		item->confchan = conf->chan;
		item->confusers = conf->users;
		item->announcetype = CONF_HASJOIN;
		ast_mutex_lock(&conf->announcelistlock);
		ao2_ref(item, +1); /* add one more so we can determine when announce_thread is done playing it */
		AST_LIST_INSERT_TAIL(&conf->announcelist, item, entry);
		ast_cond_signal(&conf->announcelist_addition);
		ast_mutex_unlock(&conf->announcelistlock);

		while (!ast_check_hangup(conf->chan) && ao2_ref(item, 0) == 2 && !ast_safe_sleep(chan, 1000)) {
			;
		}
		ao2_ref(item, -1);
	}

	if (confflags & CONFFLAG_WAITMARKED && !conf->markedusers)
		ztc.confmode = DAHDI_CONF_CONF;
	else if (confflags & CONFFLAG_MONITOR)
		ztc.confmode = DAHDI_CONF_CONFMON | DAHDI_CONF_LISTENER;
	else if (confflags & CONFFLAG_TALKER)
		ztc.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER;
	else 
		ztc.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER | DAHDI_CONF_LISTENER;

	if (ioctl(fd, DAHDI_SETCONF, &ztc)) {
		ast_log(LOG_WARNING, "Error setting conference\n");
		close(fd);
		goto outrun;
	}
	if (option_debug) {
		ast_log(LOG_DEBUG, "Placed channel %s in %s conf %d\n", chan->name, dahdi_chan_name, conf->zapconf);
	}

	if (!sent_event) {
		manager_event(EVENT_FLAG_CALL, "MeetmeJoin", 
			      "Channel: %s\r\n"
			      "Uniqueid: %s\r\n"
			      "Meetme: %s\r\n"
			      "Usernum: %d\r\n",
			      chan->name, chan->uniqueid, conf->confno, user->user_no);
		sent_event = 1;
	}

	if (!firstpass && !(confflags & CONFFLAG_MONITOR) && !(confflags & CONFFLAG_ADMIN)) {
		firstpass = 1;
		if (!(confflags & CONFFLAG_QUIET))
			if (!(confflags & CONFFLAG_WAITMARKED) || ((confflags & CONFFLAG_MARKEDUSER) && (conf->markedusers >= 1)))
				conf_play(chan, conf, ENTER);
	}

	conf_flush(fd, chan);

	if (dsp)
		ast_dsp_free(dsp);

	if (!(dsp = ast_dsp_new())) {
		ast_log(LOG_WARNING, "Unable to allocate DSP!\n");
		res = -1;
	}

	if (confflags & CONFFLAG_AGI) {
		/* Get name of AGI file to run from $(MEETME_AGI_BACKGROUND)
		   or use default filename of conf-background.agi */

		agifile = pbx_builtin_getvar_helper(chan, "MEETME_AGI_BACKGROUND");
		if (!agifile)
			agifile = agifiledefault;

		if (user->zapchannel) {
			/*  Set CONFMUTE mode on Zap channel to mute DTMF tones */
			x = 1;
			ast_channel_setoption(chan, AST_OPTION_TONE_VERIFY, &x, sizeof(char), 0);
		}
		/* Find a pointer to the agi app and execute the script */
		app = pbx_findapp("agi");
		if (app) {
			char *s = ast_strdupa(agifile);
			ret = pbx_exec(chan, app, s);
		} else {
			ast_log(LOG_WARNING, "Could not find application (agi)\n");
			ret = -2;
		}
		if (user->zapchannel) {
			/*  Remove CONFMUTE mode on Zap channel */
			x = 0;
			ast_channel_setoption(chan, AST_OPTION_TONE_VERIFY, &x, sizeof(char), 0);
		}
	} else {
		if (user->zapchannel && (confflags & CONFFLAG_STARMENU)) {
			/*  Set CONFMUTE mode on Zap channel to mute DTMF tones when the menu is enabled */
			x = 1;
			ast_channel_setoption(chan, AST_OPTION_TONE_VERIFY, &x, sizeof(char), 0);
		}	
		for(;;) {
			int menu_was_active = 0;

			outfd = -1;
			ms = -1;

			if (timeout && time(NULL) >= timeout)
				break;

			/* if we have just exited from the menu, and the user had a channel-driver
			   volume adjustment, restore it
			*/
			if (!menu_active && menu_was_active && user->listen.desired && !user->listen.actual)
				set_talk_volume(user, user->listen.desired);

			menu_was_active = menu_active;

			currentmarked = conf->markedusers;
			if (!(confflags & CONFFLAG_QUIET) &&
			    (confflags & CONFFLAG_MARKEDUSER) &&
			    (confflags & CONFFLAG_WAITMARKED) &&
			    lastmarked == 0) {
				if (currentmarked == 1 && conf->users > 1) {
					ast_say_number(chan, conf->users - 1, AST_DIGIT_ANY, chan->language, (char *) NULL);
					if (conf->users - 1 == 1) {
						if (!ast_streamfile(chan, "conf-userwilljoin", chan->language))
							ast_waitstream(chan, "");
					} else {
						if (!ast_streamfile(chan, "conf-userswilljoin", chan->language))
							ast_waitstream(chan, "");
					}
				}
				if (conf->users == 1 && ! (confflags & CONFFLAG_MARKEDUSER))
					if (!ast_streamfile(chan, "conf-onlyperson", chan->language))
						ast_waitstream(chan, "");
			}

			/* Update the struct with the actual confflags */
			user->userflags = confflags;

			if (confflags & CONFFLAG_WAITMARKED) {
				if(currentmarked == 0) {
					if (lastmarked != 0) {
						if (!(confflags & CONFFLAG_QUIET))
							if (!ast_streamfile(chan, "conf-leaderhasleft", chan->language))
								ast_waitstream(chan, "");
						if(confflags & CONFFLAG_MARKEDEXIT)
							break;
						else {
							ztc.confmode = DAHDI_CONF_CONF;
							if (ioctl(fd, DAHDI_SETCONF, &ztc)) {
								ast_log(LOG_WARNING, "Error setting conference\n");
								close(fd);
								goto outrun;
							}
						}
					}
					if (musiconhold == 0 && (confflags & CONFFLAG_MOH)) {
						ast_moh_start(chan, NULL, NULL);
						musiconhold = 1;
					}
				} else if(currentmarked >= 1 && lastmarked == 0) {
					/* Marked user entered, so cancel timeout */
					timeout = 0;
					if (confflags & CONFFLAG_MONITOR)
						ztc.confmode = DAHDI_CONF_CONFMON | DAHDI_CONF_LISTENER;
					else if (confflags & CONFFLAG_TALKER)
						ztc.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER;
					else
						ztc.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER | DAHDI_CONF_LISTENER;
					if (ioctl(fd, DAHDI_SETCONF, &ztc)) {
						ast_log(LOG_WARNING, "Error setting conference\n");
						close(fd);
						goto outrun;
					}
					if (musiconhold && (confflags & CONFFLAG_MOH)) {
						ast_moh_stop(chan);
						musiconhold = 0;
					}
					if ( !(confflags & CONFFLAG_QUIET) && !(confflags & CONFFLAG_MARKEDUSER)) {
						if (!ast_streamfile(chan, "conf-placeintoconf", chan->language))
							ast_waitstream(chan, "");
						conf_play(chan, conf, ENTER);
					}
				}
			}

			/* trying to add moh for single person conf */
			if ((confflags & CONFFLAG_MOH) && !(confflags & CONFFLAG_WAITMARKED)) {
				if (conf->users == 1) {
					if (musiconhold == 0) {
						ast_moh_start(chan, NULL, NULL);
						musiconhold = 1;
					} 
				} else {
					if (musiconhold) {
						ast_moh_stop(chan);
						musiconhold = 0;
					}
				}
			}
			
			/* Leave if the last marked user left */
			if (currentmarked == 0 && lastmarked != 0 && (confflags & CONFFLAG_MARKEDEXIT)) {
				ret = -1;
				break;
			}
	
			/* Check if my modes have changed */

			/* If I should be muted but am still talker, mute me */
			if ((user->adminflags & (ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED)) && (ztc.confmode & DAHDI_CONF_TALKER)) {
				ztc.confmode ^= DAHDI_CONF_TALKER;
				if (ioctl(fd, DAHDI_SETCONF, &ztc)) {
					ast_log(LOG_WARNING, "Error setting conference - Un/Mute \n");
					ret = -1;
					break;
				}

				/* Indicate user is not talking anymore - change him to unmonitored state */
				if ((confflags & (CONFFLAG_MONITORTALKER | CONFFLAG_OPTIMIZETALKER))) {
					set_user_talking(chan, conf, user, -1, confflags & CONFFLAG_MONITORTALKER);
				}

				manager_event(EVENT_FLAG_CALL, "MeetmeMute", 
						"Channel: %s\r\n"
						"Uniqueid: %s\r\n"
						"Meetme: %s\r\n"
						"Usernum: %i\r\n"
						"Status: on\r\n",
						chan->name, chan->uniqueid, conf->confno, user->user_no);
			}

			/* If I should be un-muted but am not talker, un-mute me */
			if (!(user->adminflags & (ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED)) && !(confflags & CONFFLAG_MONITOR) && !(ztc.confmode & DAHDI_CONF_TALKER)) {
				ztc.confmode |= DAHDI_CONF_TALKER;
				if (ioctl(fd, DAHDI_SETCONF, &ztc)) {
					ast_log(LOG_WARNING, "Error setting conference - Un/Mute \n");
					ret = -1;
					break;
				}

				manager_event(EVENT_FLAG_CALL, "MeetmeMute", 
						"Channel: %s\r\n"
						"Uniqueid: %s\r\n"
						"Meetme: %s\r\n"
						"Usernum: %i\r\n"
						"Status: off\r\n",
						chan->name, chan->uniqueid, conf->confno, user->user_no);
			}

			/* If I have been kicked, exit the conference */
			if (user->adminflags & ADMINFLAG_KICKME) {
				//You have been kicked.
				if (!(confflags & CONFFLAG_QUIET) && 
					!ast_streamfile(chan, "conf-kicked", chan->language)) {
					ast_waitstream(chan, "");
				}
				ret = 0;
				break;
			}

			c = ast_waitfor_nandfds(&chan, 1, &fd, nfds, NULL, &outfd, &ms);

			if (c) {
				char dtmfstr[2] = "";

				if (c->fds[0] != origfd || (user->zapchannel && (c->audiohooks || c->monitor))) {
					if (using_pseudo) {
						/* Kill old pseudo */
						close(fd);
						using_pseudo = 0;
					}
					ast_log(LOG_DEBUG, "Ooh, something swapped out under us, starting over\n");
					retryzap = (strcasecmp(c->tech->type, dahdi_chan_name) || (c->audiohooks || c->monitor) ? 1 : 0);
					user->zapchannel = !retryzap;
					goto zapretry;
				}
				if ((confflags & CONFFLAG_MONITOR) || (user->adminflags & (ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED)))
					f = ast_read_noaudio(c);
				else
					f = ast_read(c);
				if (!f)
					break;
				if (f->frametype == AST_FRAME_DTMF) {
					dtmfstr[0] = f->subclass;
					dtmfstr[1] = '\0';
				}

				if ((f->frametype == AST_FRAME_VOICE) && (f->subclass == AST_FORMAT_SLINEAR)) {
					if (user->talk.actual)
						ast_frame_adjust_volume(f, user->talk.actual);

					if (confflags & (CONFFLAG_MONITORTALKER | CONFFLAG_OPTIMIZETALKER)) {
						if (user->talking == -1)
							user->talking = 0;

						res = ast_dsp_silence(dsp, f, &totalsilence);
						if (totalsilence < MEETME_DELAYDETECTTALK) {
							set_user_talking(chan, conf, user, 1, confflags & CONFFLAG_MONITORTALKER);
						}
						if (totalsilence > MEETME_DELAYDETECTENDTALK) {
							set_user_talking(chan, conf, user, 0, confflags & CONFFLAG_MONITORTALKER);
						}
					}
					if (using_pseudo) {
						/* Absolutely do _not_ use careful_write here...
						   it is important that we read data from the channel
						   as fast as it arrives, and feed it into the conference.
						   The buffering in the pseudo channel will take care of any
						   timing differences, unless they are so drastic as to lose
						   audio frames (in which case carefully writing would only
						   have delayed the audio even further).
						*/
						/* As it turns out, we do want to use careful write.  We just
						   don't want to block, but we do want to at least *try*
						   to write out all the samples.
						 */
						if (user->talking || !(confflags & CONFFLAG_OPTIMIZETALKER))
							careful_write(fd, f->data, f->datalen, 0);
					}
				} else if ((f->frametype == AST_FRAME_DTMF) && (f->subclass == '#') && (confflags & CONFFLAG_POUNDEXIT)) {
					if (confflags & CONFFLAG_PASS_DTMF)
						conf_queue_dtmf(conf, user, f);
					ret = 0;
					ast_frfree(f);
					break;
				} else if (((f->frametype == AST_FRAME_DTMF) && (f->subclass == '*') && (confflags & CONFFLAG_STARMENU)) || ((f->frametype == AST_FRAME_DTMF) && menu_active)) {
					if (confflags & CONFFLAG_PASS_DTMF)
						conf_queue_dtmf(conf, user, f);
					if (ioctl(fd, DAHDI_SETCONF, &ztc_empty)) {
						ast_log(LOG_WARNING, "Error setting conference\n");
						close(fd);
						ast_frfree(f);
						goto outrun;
					}

					/* if we are entering the menu, and the user has a channel-driver
					   volume adjustment, clear it
					*/
					if (!menu_active && user->talk.desired && !user->talk.actual)
						set_talk_volume(user, 0);

					if (musiconhold) {
			   			ast_moh_stop(chan);
					}
					if ((confflags & CONFFLAG_ADMIN)) {
						/* Admin menu */
						if (!menu_active) {
							menu_active = 1;
							/* Record this sound! */
							if (!ast_streamfile(chan, "conf-adminmenu", chan->language)) {
								dtmf = ast_waitstream(chan, AST_DIGIT_ANY);
								ast_stopstream(chan);
							} else 
								dtmf = 0;
						} else 
							dtmf = f->subclass;
						if (dtmf) {
							switch(dtmf) {
							case '1': /* Un/Mute */
								menu_active = 0;

								/* for admin, change both admin and use flags */
								if (user->adminflags & (ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED))
									user->adminflags &= ~(ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED);
								else
									user->adminflags |= (ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED);

								if ((confflags & CONFFLAG_MONITOR) || (user->adminflags & (ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED))) {
									if (!ast_streamfile(chan, "conf-muted", chan->language))
										ast_waitstream(chan, "");
								} else {
									if (!ast_streamfile(chan, "conf-unmuted", chan->language))
										ast_waitstream(chan, "");
								}
								break;
							case '2': /* Un/Lock the Conference */
								menu_active = 0;
								if (conf->locked) {
									conf->locked = 0;
									if (!ast_streamfile(chan, "conf-unlockednow", chan->language))
										ast_waitstream(chan, "");
								} else {
									conf->locked = 1;
									if (!ast_streamfile(chan, "conf-lockednow", chan->language))
										ast_waitstream(chan, "");
								}
								break;
							case '3': /* Eject last user */
							{
								struct ast_conf_user *usr = NULL;
								int max_no = 0;
								ao2_callback(conf->usercontainer, OBJ_NODATA, user_max_cmp, &max_no);
								menu_active = 0;
								usr = ao2_find(conf->usercontainer, &max_no, 0);
								if ((usr->chan->name == chan->name)||(usr->userflags & CONFFLAG_ADMIN)) {
									if(!ast_streamfile(chan, "conf-errormenu", chan->language))
										ast_waitstream(chan, "");
								} else {
									usr->adminflags |= ADMINFLAG_KICKME;
								}
								ao2_ref(usr, -1);
								ast_stopstream(chan);
								break;	
							}
							case '4':
								tweak_listen_volume(user, VOL_DOWN);
								break;
							case '6':
								tweak_listen_volume(user, VOL_UP);
								break;
							case '7':
								tweak_talk_volume(user, VOL_DOWN);
								break;
							case '8':
								menu_active = 0;
								break;
							case '9':
								tweak_talk_volume(user, VOL_UP);
								break;
							default:
								menu_active = 0;
								/* Play an error message! */
								if (!ast_streamfile(chan, "conf-errormenu", chan->language))
									ast_waitstream(chan, "");
								break;
							}
						}
					} else {
						/* User menu */
						if (!menu_active) {
							menu_active = 1;
							if (!ast_streamfile(chan, "conf-usermenu", chan->language)) {
								dtmf = ast_waitstream(chan, AST_DIGIT_ANY);
								ast_stopstream(chan);
							} else
								dtmf = 0;
						} else 
							dtmf = f->subclass;
						if (dtmf) {
							switch(dtmf) {
							case '1': /* Un/Mute */
								menu_active = 0;

								/* user can only toggle the self-muted state */
								user->adminflags ^= ADMINFLAG_SELFMUTED;

								/* they can't override the admin mute state */
								if ((confflags & CONFFLAG_MONITOR) || (user->adminflags & (ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED))) {
									if (!ast_streamfile(chan, "conf-muted", chan->language))
										ast_waitstream(chan, "");
								} else {
									if (!ast_streamfile(chan, "conf-unmuted", chan->language))
										ast_waitstream(chan, "");
								}
								break;
							case '4':
								tweak_listen_volume(user, VOL_DOWN);
								break;
							case '6':
								tweak_listen_volume(user, VOL_UP);
								break;
							case '7':
								tweak_talk_volume(user, VOL_DOWN);
								break;
							case '8':
								menu_active = 0;
								break;
							case '9':
								tweak_talk_volume(user, VOL_UP);
								break;
							default:
								menu_active = 0;
								if (!ast_streamfile(chan, "conf-errormenu", chan->language))
									ast_waitstream(chan, "");
								break;
							}
						}
					}
					if (musiconhold) {
			   			ast_moh_start(chan, NULL, NULL);
					}

					if (ioctl(fd, DAHDI_SETCONF, &ztc)) {
						ast_log(LOG_WARNING, "Error setting conference\n");
						close(fd);
						ast_frfree(f);
						goto outrun;
					}

					conf_flush(fd, chan);
				/* Since this option could absorb dtmf for the previous, we have to check this one last */
				} else if ((f->frametype == AST_FRAME_DTMF) && (confflags & CONFFLAG_EXIT_CONTEXT) && ast_exists_extension(chan, exitcontext, dtmfstr, 1, "")) {
					if (confflags & CONFFLAG_PASS_DTMF)
						conf_queue_dtmf(conf, user, f);

					if (!ast_goto_if_exists(chan, exitcontext, dtmfstr, 1)) {
						ast_log(LOG_DEBUG, "Got DTMF %c, goto context %s\n", dtmfstr[0], exitcontext);
						ret = 0;
						ast_frfree(f);
						break;
					} else if (option_debug > 1)
						ast_log(LOG_DEBUG, "Exit by single digit did not work in meetme. Extension '%s' does not exist in context '%s'\n", dtmfstr, exitcontext);
				} else if ((f->frametype == AST_FRAME_DTMF_BEGIN || f->frametype == AST_FRAME_DTMF_END)
					&& confflags & CONFFLAG_PASS_DTMF) {
					conf_queue_dtmf(conf, user, f);
				} else if ((confflags & CONFFLAG_SLA_STATION) && f->frametype == AST_FRAME_CONTROL) {
					switch (f->subclass) {
					case AST_CONTROL_HOLD:
						sla_queue_event_conf(SLA_EVENT_HOLD, chan, conf);
						break;
					default:
						break;
					}
				} else if (f->frametype == AST_FRAME_NULL) {
					/* Ignore NULL frames. It is perfectly normal to get these if the person is muted. */
				} else if (f->frametype == AST_FRAME_CONTROL) {
					switch (f->subclass) {
					case AST_CONTROL_BUSY:
					case AST_CONTROL_CONGESTION:
						ast_frfree(f);
						goto outrun;
						break;
					default:
						if (option_debug) {
							ast_log(LOG_DEBUG, 
								"Got ignored control frame on channel %s, f->frametype=%d,f->subclass=%d\n",
								chan->name, f->frametype, f->subclass);
						}
					}
				} else if (option_debug) {
					ast_log(LOG_DEBUG,
						"Got unrecognized frame on channel %s, f->frametype=%d,f->subclass=%d\n",
						chan->name, f->frametype, f->subclass);
				}
				ast_frfree(f);
			} else if (outfd > -1) {
				res = read(outfd, buf, CONF_SIZE);
				if (res > 0) {
					memset(&fr, 0, sizeof(fr));
					fr.frametype = AST_FRAME_VOICE;
					fr.subclass = AST_FORMAT_SLINEAR;
					fr.datalen = res;
					fr.samples = res/2;
					fr.data = buf;
					fr.offset = AST_FRIENDLY_OFFSET;
					if (!user->listen.actual && 
						((confflags & CONFFLAG_MONITOR) || 
						 (user->adminflags & (ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED)) ||
						 (!user->talking && (confflags & CONFFLAG_OPTIMIZETALKER))
						 )) {
						int index;
						for (index=0;index<AST_FRAME_BITS;index++)
							if (chan->rawwriteformat & (1 << index))
								break;
						if (index >= AST_FRAME_BITS)
							goto bailoutandtrynormal;
						ast_mutex_lock(&conf->listenlock);
						if (!conf->transframe[index]) {
							if (conf->origframe) {
								if (musiconhold && !ast_dsp_silence(dsp, conf->origframe, &confsilence) && confsilence < MEETME_DELAYDETECTTALK) {
									ast_moh_stop(chan);
									mohtempstopped = 1;
								}
								if (!conf->transpath[index])
									conf->transpath[index] = ast_translator_build_path((1 << index), AST_FORMAT_SLINEAR);
								if (conf->transpath[index]) {
									conf->transframe[index] = ast_translate(conf->transpath[index], conf->origframe, 0);
									if (!conf->transframe[index])
										conf->transframe[index] = &ast_null_frame;
								}
							}
						}
						if (conf->transframe[index]) {
							if ((conf->transframe[index]->frametype != AST_FRAME_NULL) &&
							    can_write(chan, confflags)) {
								struct ast_frame *cur;
								/* the translator may have returned a list of frames, so
								   write each one onto the channel
								*/
								for (cur = conf->transframe[index]; cur; cur = AST_LIST_NEXT(cur, frame_list)) {
									if (ast_write(chan, cur)) {
										ast_log(LOG_WARNING, "Unable to write frame to channel %s\n", chan->name);
										break;
									}
								}
								if (musiconhold && mohtempstopped && confsilence > MEETME_DELAYDETECTENDTALK) {
									mohtempstopped = 0;
									ast_moh_start(chan, NULL, NULL);
								}
							}
						} else {
							ast_mutex_unlock(&conf->listenlock);
							goto bailoutandtrynormal;
						}
						ast_mutex_unlock(&conf->listenlock);
					} else {
bailoutandtrynormal:
						if (musiconhold && !ast_dsp_silence(dsp, &fr, &confsilence) && confsilence < MEETME_DELAYDETECTTALK) {
							ast_moh_stop(chan);
							mohtempstopped = 1;
						}
						if (user->listen.actual)
							ast_frame_adjust_volume(&fr, user->listen.actual);
						if (can_write(chan, confflags) && ast_write(chan, &fr) < 0) {
							ast_log(LOG_WARNING, "Unable to write frame to channel %s\n", chan->name);
						}
						if (musiconhold && mohtempstopped && confsilence > MEETME_DELAYDETECTENDTALK) {
							mohtempstopped = 0;
							ast_moh_start(chan, NULL, NULL);
						}
					}
				} else 
					ast_log(LOG_WARNING, "Failed to read frame: %s\n", strerror(errno));
			}
			lastmarked = currentmarked;
		}
	}

	if (musiconhold) {
		ast_moh_stop(chan);
	}
	
	if (using_pseudo)
		close(fd);
	else {
		/* Take out of conference */
		ztc.chan = 0;	
		ztc.confno = 0;
		ztc.confmode = 0;
		if (ioctl(fd, DAHDI_SETCONF, &ztc)) {
			ast_log(LOG_WARNING, "Error setting conference\n");
		}
	}

	reset_volumes(user);

	if (!(confflags & CONFFLAG_QUIET) && !(confflags & CONFFLAG_MONITOR) && !(confflags & CONFFLAG_ADMIN))
		conf_play(chan, conf, LEAVE);

	if (!(confflags & CONFFLAG_QUIET) && ((confflags & CONFFLAG_INTROUSER) || (confflags & CONFFLAG_INTROUSERNOREVIEW)) && conf->users > 1) {
		struct announce_listitem *item;
		if (!(item = ao2_alloc(sizeof(*item), NULL)))
			goto outrun;
		ast_copy_string(item->namerecloc, user->namerecloc, sizeof(item->namerecloc));
		ast_copy_string(item->language, chan->language, sizeof(item->language));
		item->confchan = conf->chan;
		item->confusers = conf->users;
		item->announcetype = CONF_HASLEFT;
		ast_mutex_lock(&conf->announcelistlock);
		AST_LIST_INSERT_TAIL(&conf->announcelist, item, entry);
		ast_cond_signal(&conf->announcelist_addition);
		ast_mutex_unlock(&conf->announcelistlock);
	} else if (!(confflags & CONFFLAG_QUIET) && ((confflags & CONFFLAG_INTROUSER) || (confflags & CONFFLAG_INTROUSERNOREVIEW)) && conf->users == 1) {
		/* Last person is leaving, so no reason to try and announce, but should delete the name recording */
		ast_filedelete(user->namerecloc, NULL);
	}

 outrun:
	AST_LIST_LOCK(&confs);

	if (dsp)
		ast_dsp_free(dsp);
	
	if (user->user_no) {
		/* Only cleanup users who really joined! */
		now = time(NULL);
		hr = (now - user->jointime) / 3600;
		min = ((now - user->jointime) % 3600) / 60;
		sec = (now - user->jointime) % 60;

		if (sent_event) {
			manager_event(EVENT_FLAG_CALL, "MeetmeLeave",
				      "Channel: %s\r\n"
				      "Uniqueid: %s\r\n"
				      "Meetme: %s\r\n"
				      "Usernum: %d\r\n"
				      "CallerIDnum: %s\r\n"
				      "CallerIDname: %s\r\n"
				      "Duration: %ld\r\n",
				      chan->name, chan->uniqueid, conf->confno, 
				      user->user_no,
				      S_OR(user->chan->cid.cid_num, "<unknown>"),
				      S_OR(user->chan->cid.cid_name, "<unknown>"),
				      (long)(now - user->jointime));
		}

		if (setusercount) {
			conf->users--;
			/* Update table */
			snprintf(members, sizeof(members), "%d", conf->users);
			ast_update_realtime("meetme", "confno", conf->confno, "members", members, NULL);
			if (confflags & CONFFLAG_MARKEDUSER) 
				conf->markedusers--;
		}
		/* Remove ourselves from the container */
		ao2_unlink(conf->usercontainer, user); 

		/* Change any states */
		if (!conf->users)
			ast_device_state_changed("meetme:%s", conf->confno);
		
		/* Return the number of seconds the user was in the conf */
		snprintf(meetmesecs, sizeof(meetmesecs), "%d", (int) (time(NULL) - user->jointime));
		pbx_builtin_setvar_helper(chan, "MEETMESECS", meetmesecs);
	}
	ao2_ref(user, -1);
	AST_LIST_UNLOCK(&confs);

	return ret;
}

static struct ast_conference *find_conf_realtime(struct ast_channel *chan, char *confno, int make, int dynamic,
						 char *dynamic_pin, size_t pin_buf_len, int refcount, struct ast_flags *confflags)
{
	struct ast_variable *var, *save;
	struct ast_conference *cnf;

	/* Check first in the conference list */
	AST_LIST_LOCK(&confs);
	AST_LIST_TRAVERSE(&confs, cnf, list) {
		if (!strcmp(confno, cnf->confno)) 
			break;
	}
	if (cnf) {
		cnf->refcount += refcount;
	}
	AST_LIST_UNLOCK(&confs);

	if (!cnf) {
		char *pin = NULL, *pinadmin = NULL; /* For temp use */
		
		var = ast_load_realtime("meetme", "confno", confno, NULL);

		if (!var)
			return NULL;

		save = var;
		while (var) {
			if (!strcasecmp(var->name, "pin")) {
				pin = ast_strdupa(var->value);
			} else if (!strcasecmp(var->name, "adminpin")) {
				pinadmin = ast_strdupa(var->value);
			}
			var = var->next;
		}
		ast_variables_destroy(save);
		
		cnf = build_conf(confno, pin ? pin : "", pinadmin ? pinadmin : "", make, dynamic, refcount);
	}

	if (cnf) {
		if (confflags && !cnf->chan &&
		    !ast_test_flag(confflags, CONFFLAG_QUIET) &&
		    ast_test_flag(confflags, CONFFLAG_INTROUSER | CONFFLAG_INTROUSERNOREVIEW)) {
			ast_log(LOG_WARNING, "No %s channel available for conference, user introduction disabled\n", dahdi_chan_name);
			ast_clear_flag(confflags, CONFFLAG_INTROUSER | CONFFLAG_INTROUSERNOREVIEW);
		}
		
		if (confflags && !cnf->chan &&
		    ast_test_flag(confflags, CONFFLAG_RECORDCONF)) {
			ast_log(LOG_WARNING, "No %s channel available for conference, conference recording disabled\n", dahdi_chan_name);
			ast_clear_flag(confflags, CONFFLAG_RECORDCONF);
		}
	}

	return cnf;
}


static struct ast_conference *find_conf(struct ast_channel *chan, char *confno, int make, int dynamic,
					char *dynamic_pin, size_t pin_buf_len, int refcount, struct ast_flags *confflags)
{
	struct ast_config *cfg;
	struct ast_variable *var;
	struct ast_conference *cnf;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(confno);
		AST_APP_ARG(pin);
		AST_APP_ARG(pinadmin);
	);

	/* Check first in the conference list */
	AST_LIST_LOCK(&confs);
	AST_LIST_TRAVERSE(&confs, cnf, list) {
		if (!strcmp(confno, cnf->confno)) 
			break;
	}
	if (cnf){
		cnf->refcount += refcount;
	}
	AST_LIST_UNLOCK(&confs);

	if (!cnf) {
		if (dynamic) {
			/* No need to parse meetme.conf */
			ast_log(LOG_DEBUG, "Building dynamic conference '%s'\n", confno);
			if (dynamic_pin) {
				if (dynamic_pin[0] == 'q') {
					/* Query the user to enter a PIN */
					if (ast_app_getdata(chan, "conf-getpin", dynamic_pin, pin_buf_len - 1, 0) < 0)
						return NULL;
				}
				cnf = build_conf(confno, dynamic_pin, "", make, dynamic, refcount);
			} else {
				cnf = build_conf(confno, "", "", make, dynamic, refcount);
			}
		} else {
			/* Check the config */
			cfg = ast_config_load(CONFIG_FILE_NAME);
			if (!cfg) {
				ast_log(LOG_WARNING, "No %s file :(\n", CONFIG_FILE_NAME);
				return NULL;
			}

			for (var = ast_variable_browse(cfg, "rooms"); var; var = var->next) {
				char parse[MAX_SETTINGS];

				if (strcasecmp(var->name, "conf"))
					continue;

				ast_copy_string(parse, var->value, sizeof(parse));

				AST_NONSTANDARD_APP_ARGS(args, parse, ',');
				if (!strcasecmp(args.confno, confno)) {
					/* Bingo it's a valid conference */
					cnf = build_conf(args.confno,
							S_OR(args.pin, ""),
							S_OR(args.pinadmin, ""),
							make, dynamic, refcount);
					break;
				}
			}
			if (!var) {
				ast_log(LOG_DEBUG, "%s isn't a valid conference\n", confno);
			}
			ast_config_destroy(cfg);
		}
	} else if (dynamic_pin) {
		/* Correct for the user selecting 'D' instead of 'd' to have
		   someone join into a conference that has already been created
		   with a pin. */
		if (dynamic_pin[0] == 'q')
			dynamic_pin[0] = '\0';
	}

	if (cnf) {
		if (confflags && !cnf->chan &&
		    !ast_test_flag(confflags, CONFFLAG_QUIET) &&
		    ast_test_flag(confflags, CONFFLAG_INTROUSER | CONFFLAG_INTROUSERNOREVIEW)) {
			ast_log(LOG_WARNING, "No %s channel available for conference, user introduction disabled\n", dahdi_chan_name);
			ast_clear_flag(confflags, CONFFLAG_INTROUSER | CONFFLAG_INTROUSERNOREVIEW);
		}
		
		if (confflags && !cnf->chan &&
		    ast_test_flag(confflags, CONFFLAG_RECORDCONF)) {
			ast_log(LOG_WARNING, "No %s channel available for conference, conference recording disabled\n", dahdi_chan_name);
			ast_clear_flag(confflags, CONFFLAG_RECORDCONF);
		}
	}

	return cnf;
}

/*! \brief The MeetmeCount application */
static int count_exec(struct ast_channel *chan, void *data)
{
	struct ast_module_user *u;
	int res = 0;
	struct ast_conference *conf;
	int count;
	char *localdata;
	char val[80] = "0"; 
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(confno);
		AST_APP_ARG(varname);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "MeetMeCount requires an argument (conference number)\n");
		return -1;
	}

	u = ast_module_user_add(chan);
	
	if (!(localdata = ast_strdupa(data))) {
		ast_module_user_remove(u);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, localdata);
	
	conf = find_conf(chan, args.confno, 0, 0, NULL, 0, 1, NULL);

	if (conf) {
		count = conf->users;
		dispose_conf(conf);
		conf = NULL;
	} else
		count = 0;

	if (!ast_strlen_zero(args.varname)){
		/* have var so load it and exit */
		snprintf(val, sizeof(val), "%d",count);
		pbx_builtin_setvar_helper(chan, args.varname, val);
	} else {
		if (chan->_state != AST_STATE_UP)
			ast_answer(chan);
		res = ast_say_number(chan, count, "", chan->language, (char *) NULL); /* Needs gender */
	}
	ast_module_user_remove(u);

	return res;
}

/*! \brief The meetme() application */
static int conf_exec(struct ast_channel *chan, void *data)
{
	int res=-1;
	struct ast_module_user *u;
	char confno[MAX_CONFNUM] = "";
	int allowretry = 0;
	int retrycnt = 0;
	struct ast_conference *cnf = NULL;
	struct ast_flags confflags = {0};
	int dynamic = 0;
	int empty = 0, empty_no_pin = 0;
	int always_prompt = 0;
	char *notdata, *info, the_pin[MAX_PIN] = "";
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(confno);
		AST_APP_ARG(options);
		AST_APP_ARG(pin);
	);
	char *optargs[OPT_ARG_ARRAY_SIZE] = { NULL, };

	u = ast_module_user_add(chan);

	if (ast_strlen_zero(data)) {
		allowretry = 1;
		notdata = "";
	} else {
		notdata = data;
	}
	
	if (chan->_state != AST_STATE_UP)
		ast_answer(chan);

	info = ast_strdupa(notdata);

	AST_STANDARD_APP_ARGS(args, info);	

	if (args.confno) {
		ast_copy_string(confno, args.confno, sizeof(confno));
		if (ast_strlen_zero(confno)) {
			allowretry = 1;
		}
	}
	
	if (args.pin)
		ast_copy_string(the_pin, args.pin, sizeof(the_pin));

	if (args.options) {
		ast_app_parse_options(meetme_opts, &confflags, optargs, args.options);
		dynamic = ast_test_flag(&confflags, CONFFLAG_DYNAMIC | CONFFLAG_DYNAMICPIN);
		if (ast_test_flag(&confflags, CONFFLAG_DYNAMICPIN) && ast_strlen_zero(args.pin))
			strcpy(the_pin, "q");

		empty = ast_test_flag(&confflags, CONFFLAG_EMPTY | CONFFLAG_EMPTYNOPIN);
		empty_no_pin = ast_test_flag(&confflags, CONFFLAG_EMPTYNOPIN);
		always_prompt = ast_test_flag(&confflags, CONFFLAG_ALWAYSPROMPT | CONFFLAG_DYNAMICPIN);
	}

	do {
		if (retrycnt > 3)
			allowretry = 0;
		if (empty) {
			int i;
			struct ast_config *cfg;
			struct ast_variable *var;
			int confno_int;

			/* We only need to load the config file for static and empty_no_pin (otherwise we don't care) */
			if ((empty_no_pin) || (!dynamic)) {
				cfg = ast_config_load(CONFIG_FILE_NAME);
				if (cfg) {
					var = ast_variable_browse(cfg, "rooms");
					while (var) {
						char parse[MAX_SETTINGS], *stringp = parse, *confno_tmp;
						if (!strcasecmp(var->name, "conf")) {
							int found = 0;
							ast_copy_string(parse, var->value, sizeof(parse));
							confno_tmp = strsep(&stringp, "|,");
							if (!dynamic) {
								/* For static:  run through the list and see if this conference is empty */
								AST_LIST_LOCK(&confs);
								AST_LIST_TRAVERSE(&confs, cnf, list) {
									if (!strcmp(confno_tmp, cnf->confno)) {
										/* The conference exists, therefore it's not empty */
										found = 1;
										break;
									}
								}
								AST_LIST_UNLOCK(&confs);
								if (!found) {
									/* At this point, we have a confno_tmp (static conference) that is empty */
									if ((empty_no_pin && ast_strlen_zero(stringp)) || (!empty_no_pin)) {
										/* Case 1:  empty_no_pin and pin is nonexistent (NULL)
										 * Case 2:  empty_no_pin and pin is blank (but not NULL)
										 * Case 3:  not empty_no_pin
										 */
										ast_copy_string(confno, confno_tmp, sizeof(confno));
										break;
										/* XXX the map is not complete (but we do have a confno) */
									}
								}
							}
						}
						var = var->next;
					}
					ast_config_destroy(cfg);
				}
			}

			/* Select first conference number not in use */
			if (ast_strlen_zero(confno) && dynamic) {
				AST_LIST_LOCK(&confs);
				for (i = 0; i < sizeof(conf_map) / sizeof(conf_map[0]); i++) {
					if (!conf_map[i]) {
						snprintf(confno, sizeof(confno), "%d", i);
						conf_map[i] = 1;
						break;
					}
				}
				AST_LIST_UNLOCK(&confs);
			}

			/* Not found? */
			if (ast_strlen_zero(confno)) {
				res = ast_streamfile(chan, "conf-noempty", chan->language);
				if (!res)
					ast_waitstream(chan, "");
			} else {
				if (sscanf(confno, "%30d", &confno_int) == 1) {
					res = ast_streamfile(chan, "conf-enteringno", chan->language);
					if (!res) {
						ast_waitstream(chan, "");
						res = ast_say_digits(chan, confno_int, "", chan->language);
					}
				} else {
					ast_log(LOG_ERROR, "Could not scan confno '%s'\n", confno);
				}
			}
		}

		while (allowretry && (ast_strlen_zero(confno)) && (++retrycnt < 4)) {
			/* Prompt user for conference number */
			res = ast_app_getdata(chan, "conf-getconfno", confno, sizeof(confno) - 1, 0);
			if (res < 0) {
				/* Don't try to validate when we catch an error */
				confno[0] = '\0';
				allowretry = 0;
				break;
			}
		}
		if (!ast_strlen_zero(confno)) {
			/* Check the validity of the conference */
			cnf = find_conf(chan, confno, 1, dynamic, the_pin, 
				sizeof(the_pin), 1, &confflags);
			if (!cnf) {
				cnf = find_conf_realtime(chan, confno, 1, dynamic, 
					the_pin, sizeof(the_pin), 1, &confflags);
			}

			if (!cnf) {
				res = ast_streamfile(chan, "conf-invalid", chan->language);
				if (!res)
					ast_waitstream(chan, "");
				res = -1;
				if (allowretry)
					confno[0] = '\0';
			} else {
				if (((!ast_strlen_zero(cnf->pin)       &&
					!ast_test_flag(&confflags, CONFFLAG_ADMIN)) ||
				     (!ast_strlen_zero(cnf->pinadmin)  &&
				     	 ast_test_flag(&confflags, CONFFLAG_ADMIN)) ||
			    	     (!ast_strlen_zero(cnf->pin) &&
			    	     	 ast_strlen_zero(cnf->pinadmin) &&
			    	     	 ast_test_flag(&confflags, CONFFLAG_ADMIN))) &&
				    (!(cnf->users == 0 && cnf->isdynamic))) {
					char pin[MAX_PIN] = "";
					int j;

					/* Allow the pin to be retried up to 3 times */
					for (j = 0; j < 3; j++) {
						if (*the_pin && (always_prompt == 0)) {
							ast_copy_string(pin, the_pin, sizeof(pin));
							res = 0;
						} else {
							/* Prompt user for pin if pin is required */
							res = ast_app_getdata(chan, "conf-getpin", pin + strlen(pin), sizeof(pin) - 1 - strlen(pin), 0);
						}
						if (res >= 0) {
							if ((!strcasecmp(pin, cnf->pin) &&
							     (ast_strlen_zero(cnf->pinadmin) ||
							      !ast_test_flag(&confflags, CONFFLAG_ADMIN))) ||
							     (!ast_strlen_zero(cnf->pinadmin) &&
							      !strcasecmp(pin, cnf->pinadmin))) {
								/* Pin correct */
								allowretry = 0;
								if (!ast_strlen_zero(cnf->pinadmin) && !strcasecmp(pin, cnf->pinadmin)) 
									ast_set_flag(&confflags, CONFFLAG_ADMIN);
								/* Run the conference */
								res = conf_run(chan, cnf, confflags.flags, optargs);
								break;
							} else {
								/* Pin invalid */
								if (!ast_streamfile(chan, "conf-invalidpin", chan->language)) {
									res = ast_waitstream(chan, AST_DIGIT_ANY);
									ast_stopstream(chan);
								}
								else {
									ast_log(LOG_WARNING, "Couldn't play invalid pin msg!\n");
									break;
								}
								if (res < 0)
									break;
								pin[0] = res;
								pin[1] = '\0';
								res = -1;
								if (allowretry)
									confno[0] = '\0';
							}
						} else {
							/* failed when getting the pin */
							res = -1;
							allowretry = 0;
							/* see if we need to get rid of the conference */
							break;
						}

						/* Don't retry pin with a static pin */
						if (*the_pin && (always_prompt==0)) {
							break;
						}
					}
				} else {
					/* No pin required */
					allowretry = 0;

					/* Run the conference */
					res = conf_run(chan, cnf, confflags.flags, optargs);
				}
				dispose_conf(cnf);
				cnf = NULL;
			}
		}
	} while (allowretry);

	if (cnf)
		dispose_conf(cnf);

	ast_module_user_remove(u);
	
	return res;
}

static struct ast_conf_user *find_user(struct ast_conference *conf, char *callerident) 
{
	struct ast_conf_user *user = NULL;
	int cid;
	
	sscanf(callerident, "%30i", &cid);
	if (conf && callerident) {
		user = ao2_find(conf->usercontainer, &cid, 0);
		/* reference decremented later in admin_exec */
		return user;
	}
	return NULL;
}

static int user_set_kickme_cb(void *obj, void *unused, int flags)
{
	struct ast_conf_user *user = obj;
	user->adminflags |= ADMINFLAG_KICKME;
	return 0;
}

static int user_set_muted_cb(void *obj, void *unused, int flags)
{
	struct ast_conf_user *user = obj;
	if (!(user->userflags & CONFFLAG_ADMIN)) {
		user->adminflags |= ADMINFLAG_MUTED;
	}
	return 0;
}

static int user_set_unmuted_cb(void *obj, void *unused, int flags)
{
	struct ast_conf_user *user = obj;
	user->adminflags &= ~(ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED);
	return 0;
}

static int user_listen_volup_cb(void *obj, void *unused, int flags)
{
	struct ast_conf_user *user = obj;
	tweak_listen_volume(user, VOL_UP);
	return 0;
}

static int user_listen_voldown_cb(void *obj, void *unused, int flags)
{
	struct ast_conf_user *user = obj;
	tweak_listen_volume(user, VOL_DOWN);
	return 0;
}

static int user_talk_volup_cb(void *obj, void *unused, int flags)
{
	struct ast_conf_user *user = obj;
	tweak_talk_volume(user, VOL_UP);
	return 0;
}

static int user_talk_voldown_cb(void *obj, void *unused, int flags)
{
	struct ast_conf_user *user = obj;
	tweak_talk_volume(user, VOL_DOWN);
	return 0;
}

static int user_reset_vol_cb(void *obj, void *unused, int flags)
{
	struct ast_conf_user *user = obj;
	reset_volumes(user);
	return 0;
}

/*! \brief The MeetMeadmin application */
/* MeetMeAdmin(confno, command, caller) */
static int admin_exec(struct ast_channel *chan, void *data) {
	char *params;
	struct ast_conference *cnf;
	struct ast_conf_user *user = NULL;
	struct ast_module_user *u;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(confno);
		AST_APP_ARG(command);
		AST_APP_ARG(user);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "MeetMeAdmin requires an argument!\n");
		return -1;
	}

	u = ast_module_user_add(chan);

	AST_LIST_LOCK(&confs);
	
	params = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, params);

	if (!args.command) {
		ast_log(LOG_WARNING, "MeetmeAdmin requires a command!\n");
		AST_LIST_UNLOCK(&confs);
		ast_module_user_remove(u);
		return -1;
	}
	AST_LIST_TRAVERSE(&confs, cnf, list) {
		if (!strcmp(cnf->confno, args.confno))
			break;
	}

	if (!cnf) {
		ast_log(LOG_WARNING, "Conference number '%s' not found!\n", args.confno);
		AST_LIST_UNLOCK(&confs);
		ast_module_user_remove(u);
		return 0;
	}

	ast_atomic_fetchadd_int(&cnf->refcount, 1);

	if (args.user) {
		user = find_user(cnf, args.user);
		if (!user) {
			ast_log(LOG_NOTICE, "Specified User not found!\n");
			goto usernotfound;
		}
	}

	switch (*args.command) {
	case 76: /* L: Lock */ 
		cnf->locked = 1;
		break;
	case 108: /* l: Unlock */ 
		cnf->locked = 0;
		break;
	case 75: /* K: kick all users */
		ao2_callback(cnf->usercontainer, 0, user_set_kickme_cb, NULL);
		break;
	case 101: /* e: Eject last user*/
	{
		int max_no = 0;
		ao2_callback(cnf->usercontainer, OBJ_NODATA, user_max_cmp, &max_no);
		user = ao2_find(cnf->usercontainer, &max_no, 0);
		if (!(user->userflags & CONFFLAG_ADMIN))
			user->adminflags |= ADMINFLAG_KICKME;
		else
			ast_log(LOG_NOTICE, "Not kicking last user, is an Admin!\n");
		ao2_ref(user, -1);
		break;
	}
	case 77: /* M: Mute */ 
		user->adminflags |= ADMINFLAG_MUTED;
		break;
	case 78: /* N: Mute all (non-admin) users */
		ao2_callback(cnf->usercontainer, 0, user_set_muted_cb, NULL);
		break;					
	case 109: /* m: Unmute */ 
		user->adminflags &= ~(ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED);
		break;
	case 110: /* n: Unmute all users */
		ao2_callback(cnf->usercontainer, 0, user_set_unmuted_cb, NULL);
		break;
	case 107: /* k: Kick user */ 
		user->adminflags |= ADMINFLAG_KICKME;
		break;
	case 118: /* v: Lower all users listen volume */
		ao2_callback(cnf->usercontainer, 0, user_listen_voldown_cb, NULL);
		break;
	case 86: /* V: Raise all users listen volume */
		ao2_callback(cnf->usercontainer, 0, user_listen_volup_cb, NULL);
		break;
	case 115: /* s: Lower all users speaking volume */
		ao2_callback(cnf->usercontainer, 0, user_talk_voldown_cb, NULL);
		break;
	case 83: /* S: Raise all users speaking volume */
		ao2_callback(cnf->usercontainer, 0, user_talk_volup_cb, NULL);
		break;
	case 82: /* R: Reset all volume levels */
		ao2_callback(cnf->usercontainer, 0, user_reset_vol_cb, NULL);
		break;
	case 114: /* r: Reset user's volume level */
		reset_volumes(user);
		break;
	case 85: /* U: Raise user's listen volume */
		tweak_listen_volume(user, VOL_UP);
		break;
	case 117: /* u: Lower user's listen volume */
		tweak_listen_volume(user, VOL_DOWN);
		break;
	case 84: /* T: Raise user's talk volume */
		tweak_talk_volume(user, VOL_UP);
		break;
	case 116: /* t: Lower user's talk volume */
		tweak_talk_volume(user, VOL_DOWN);
		break;
	}

	if (args.user) {
		/* decrement reference from find_user */
		ao2_ref(user, -1);
	}
usernotfound:
	AST_LIST_UNLOCK(&confs);

	dispose_conf(cnf);

	ast_module_user_remove(u);
	
	return 0;
}

static int meetmemute(struct mansession *s, const struct message *m, int mute)
{
	struct ast_conference *conf;
	struct ast_conf_user *user;
	const char *confid = astman_get_header(m, "Meetme");
	char *userid = ast_strdupa(astman_get_header(m, "Usernum"));
	int userno;

	if (ast_strlen_zero(confid)) {
		astman_send_error(s, m, "Meetme conference not specified");
		return 0;
	}

	if (ast_strlen_zero(userid)) {
		astman_send_error(s, m, "Meetme user number not specified");
		return 0;
	}

	userno = strtoul(userid, &userid, 10);

	if (*userid) {
		astman_send_error(s, m, "Invalid user number");
		return 0;
	}

	/* Look in the conference list */
	AST_LIST_LOCK(&confs);
	AST_LIST_TRAVERSE(&confs, conf, list) {
		if (!strcmp(confid, conf->confno))
			break;
	}

	if (!conf) {
		AST_LIST_UNLOCK(&confs);
		astman_send_error(s, m, "Meetme conference does not exist");
		return 0;
	}

	user = ao2_find(conf->usercontainer, &userno, 0);

	if (!user) {
		AST_LIST_UNLOCK(&confs);
		astman_send_error(s, m, "User number not found");
		return 0;
	}

	if (mute)
		user->adminflags |= ADMINFLAG_MUTED;	/* request user muting */
	else
		user->adminflags &= ~(ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED);	/* request user unmuting */

	AST_LIST_UNLOCK(&confs);

	ast_log(LOG_NOTICE, "Requested to %smute conf %s user %d userchan %s uniqueid %s\n", mute ? "" : "un", conf->confno, user->user_no, user->chan->name, user->chan->uniqueid);

	ao2_ref(user, -1);
	astman_send_ack(s, m, mute ? "User muted" : "User unmuted");
	return 0;
}

static int action_meetmemute(struct mansession *s, const struct message *m)
{
	return meetmemute(s, m, 1);
}

static int action_meetmeunmute(struct mansession *s, const struct message *m)
{
	return meetmemute(s, m, 0);
}

static void *recordthread(void *args)
{
	struct ast_conference *cnf = args;
	struct ast_frame *f=NULL;
	int flags;
	struct ast_filestream *s=NULL;
	int res=0;
	int x;
	const char *oldrecordingfilename = NULL;

	if (!cnf || !cnf->lchan) {
		pthread_exit(0);
	}

	ast_stopstream(cnf->lchan);
	flags = O_CREAT|O_TRUNC|O_WRONLY;


	cnf->recording = MEETME_RECORD_ACTIVE;
	while (ast_waitfor(cnf->lchan, -1) > -1) {
		if (cnf->recording == MEETME_RECORD_TERMINATE) {
			AST_LIST_LOCK(&confs);
			AST_LIST_UNLOCK(&confs);
			break;
		}
		if (!s && cnf->recordingfilename && (cnf->recordingfilename != oldrecordingfilename)) {
			s = ast_writefile(cnf->recordingfilename, cnf->recordingformat, NULL, flags, 0, 0644);
			oldrecordingfilename = cnf->recordingfilename;
		}
		
		f = ast_read(cnf->lchan);
		if (!f) {
			res = -1;
			break;
		}
		if (f->frametype == AST_FRAME_VOICE) {
			ast_mutex_lock(&cnf->listenlock);
			for (x=0;x<AST_FRAME_BITS;x++) {
				/* Free any translations that have occured */
				if (cnf->transframe[x]) {
					ast_frfree(cnf->transframe[x]);
					cnf->transframe[x] = NULL;
				}
			}
			if (cnf->origframe)
				ast_frfree(cnf->origframe);
			cnf->origframe = ast_frdup(f);
			ast_mutex_unlock(&cnf->listenlock);
			if (s)
				res = ast_writestream(s, f);
			if (res) {
				ast_frfree(f);
				break;
			}
		}
		ast_frfree(f);
	}
	cnf->recording = MEETME_RECORD_OFF;
	if (s)
		ast_closestream(s);
	
	pthread_exit(0);
}

/*! \brief Callback for devicestate providers */
static int meetmestate(const char *data)
{
	struct ast_conference *conf;

	/* Find conference */
	AST_LIST_LOCK(&confs);
	AST_LIST_TRAVERSE(&confs, conf, list) {
		if (!strcmp(data, conf->confno))
			break;
	}
	AST_LIST_UNLOCK(&confs);
	if (!conf)
		return AST_DEVICE_INVALID;


	/* SKREP to fill */
	if (!conf->users)
		return AST_DEVICE_NOT_INUSE;

	return AST_DEVICE_INUSE;
}

static void load_config_meetme(void)
{
	struct ast_config *cfg;
	const char *val;

	audio_buffers = DEFAULT_AUDIO_BUFFERS;

	if (!(cfg = ast_config_load(CONFIG_FILE_NAME)))
		return;

	if ((val = ast_variable_retrieve(cfg, "general", "audiobuffers"))) {
		if ((sscanf(val, "%30d", &audio_buffers) != 1)) {
			ast_log(LOG_WARNING, "audiobuffers setting must be a number, not '%s'\n", val);
			audio_buffers = DEFAULT_AUDIO_BUFFERS;
		} else if ((audio_buffers < DAHDI_DEFAULT_NUM_BUFS) || (audio_buffers > DAHDI_MAX_NUM_BUFS)) {
			ast_log(LOG_WARNING, "audiobuffers setting must be between %d and %d\n",
				DAHDI_DEFAULT_NUM_BUFS, DAHDI_MAX_NUM_BUFS);
			audio_buffers = DEFAULT_AUDIO_BUFFERS;
		}
		if (audio_buffers != DEFAULT_AUDIO_BUFFERS)
			ast_log(LOG_NOTICE, "Audio buffers per channel set to %d\n", audio_buffers);
	}

	ast_config_destroy(cfg);
}

/*! \brief Find an SLA trunk by name
 * \note This must be called with the sla_trunks container locked
 */
static struct sla_trunk *sla_find_trunk(const char *name)
{
	struct sla_trunk *trunk = NULL;

	AST_RWLIST_TRAVERSE(&sla_trunks, trunk, entry) {
		if (!strcasecmp(trunk->name, name))
			break;
	}

	return trunk;
}

/*! \brief Find an SLA station by name
 * \note This must be called with the sla_stations container locked
 */
static struct sla_station *sla_find_station(const char *name)
{
	struct sla_station *station = NULL;

	AST_RWLIST_TRAVERSE(&sla_stations, station, entry) {
		if (!strcasecmp(station->name, name))
			break;
	}

	return station;
}

static int sla_check_station_hold_access(const struct sla_trunk *trunk,
	const struct sla_station *station)
{
	struct sla_station_ref *station_ref;
	struct sla_trunk_ref *trunk_ref;

	/* For each station that has this call on hold, check for private hold. */
	AST_LIST_TRAVERSE(&trunk->stations, station_ref, entry) {
		AST_LIST_TRAVERSE(&station_ref->station->trunks, trunk_ref, entry) {
			if (trunk_ref->trunk != trunk || station_ref->station == station)
				continue;
			if (trunk_ref->state == SLA_TRUNK_STATE_ONHOLD_BYME &&
				station_ref->station->hold_access == SLA_HOLD_PRIVATE)
				return 1;
			return 0;
		}
	}

	return 0;
}

/*! \brief Find a trunk reference on a station by name
 * \param station the station
 * \param name the trunk's name
 * \return a pointer to the station's trunk reference.  If the trunk
 *         is not found, it is not idle and barge is disabled, or if
 *         it is on hold and private hold is set, then NULL will be returned.
 */
static struct sla_trunk_ref *sla_find_trunk_ref_byname(const struct sla_station *station,
	const char *name)
{
	struct sla_trunk_ref *trunk_ref = NULL;

	AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
		if (strcasecmp(trunk_ref->trunk->name, name))
			continue;

		if ( (trunk_ref->trunk->barge_disabled 
			&& trunk_ref->state == SLA_TRUNK_STATE_UP) ||
			(trunk_ref->trunk->hold_stations 
			&& trunk_ref->trunk->hold_access == SLA_HOLD_PRIVATE
			&& trunk_ref->state != SLA_TRUNK_STATE_ONHOLD_BYME) ||
			sla_check_station_hold_access(trunk_ref->trunk, station) ) 
		{
			trunk_ref = NULL;
		}

		break;
	}

	return trunk_ref;
}

static struct sla_station_ref *sla_create_station_ref(struct sla_station *station)
{
	struct sla_station_ref *station_ref;

	if (!(station_ref = ast_calloc(1, sizeof(*station_ref))))
		return NULL;

	station_ref->station = station;

	return station_ref;
}

static struct sla_ringing_station *sla_create_ringing_station(struct sla_station *station)
{
	struct sla_ringing_station *ringing_station;

	if (!(ringing_station = ast_calloc(1, sizeof(*ringing_station))))
		return NULL;

	ringing_station->station = station;
	ringing_station->ring_begin = ast_tvnow();

	return ringing_station;
}

static void sla_change_trunk_state(const struct sla_trunk *trunk, enum sla_trunk_state state, 
	enum sla_which_trunk_refs inactive_only, const struct sla_trunk_ref *exclude)
{
	struct sla_station *station;
	struct sla_trunk_ref *trunk_ref;

	AST_LIST_TRAVERSE(&sla_stations, station, entry) {
		AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
			if (trunk_ref->trunk != trunk || (inactive_only ? trunk_ref->chan : 0)
				|| trunk_ref == exclude)
				continue;
			trunk_ref->state = state;
			ast_device_state_changed("SLA:%s_%s", station->name, trunk->name);
			break;
		}
	}
}

struct run_station_args {
	struct sla_station *station;
	struct sla_trunk_ref *trunk_ref;
	ast_mutex_t *cond_lock;
	ast_cond_t *cond;
};

static void answer_trunk_chan(struct ast_channel *chan)
{
	ast_answer(chan);
	ast_indicate(chan, -1);
}

static void *run_station(void *data)
{
	struct sla_station *station;
	struct sla_trunk_ref *trunk_ref;
	char conf_name[MAX_CONFNUM];
	struct ast_flags conf_flags = { 0 };
	struct ast_conference *conf;

	{
		struct run_station_args *args = data;
		station = args->station;
		trunk_ref = args->trunk_ref;
		ast_mutex_lock(args->cond_lock);
		ast_cond_signal(args->cond);
		ast_mutex_unlock(args->cond_lock);
		/* args is no longer valid here. */
	}

	ast_atomic_fetchadd_int((int *) &trunk_ref->trunk->active_stations, 1);
	snprintf(conf_name, sizeof(conf_name), "SLA_%s", trunk_ref->trunk->name);
	ast_set_flag(&conf_flags, 
		CONFFLAG_QUIET | CONFFLAG_MARKEDEXIT | CONFFLAG_PASS_DTMF | CONFFLAG_SLA_STATION);
	answer_trunk_chan(trunk_ref->chan);
	conf = build_conf(conf_name, "", "", 0, 0, 1);
	if (conf) {
		conf_run(trunk_ref->chan, conf, conf_flags.flags, NULL);
		dispose_conf(conf);
		conf = NULL;
	}
	trunk_ref->chan = NULL;
	if (ast_atomic_dec_and_test((int *) &trunk_ref->trunk->active_stations) &&
		trunk_ref->state != SLA_TRUNK_STATE_ONHOLD_BYME) {
		strncat(conf_name, "|K", sizeof(conf_name) - strlen(conf_name) - 1);
		admin_exec(NULL, conf_name);
		trunk_ref->trunk->hold_stations = 0;
		sla_change_trunk_state(trunk_ref->trunk, SLA_TRUNK_STATE_IDLE, ALL_TRUNK_REFS, NULL);
	}

	ast_dial_join(station->dial);
	ast_dial_destroy(station->dial);
	station->dial = NULL;

	return NULL;
}

static void sla_stop_ringing_trunk(struct sla_ringing_trunk *ringing_trunk)
{
	char buf[80];
	struct sla_station_ref *station_ref;

	snprintf(buf, sizeof(buf), "SLA_%s|K", ringing_trunk->trunk->name);
	admin_exec(NULL, buf);
	sla_change_trunk_state(ringing_trunk->trunk, SLA_TRUNK_STATE_IDLE, ALL_TRUNK_REFS, NULL);

	while ((station_ref = AST_LIST_REMOVE_HEAD(&ringing_trunk->timed_out_stations, entry)))
		free(station_ref);

	free(ringing_trunk);
}

static void sla_stop_ringing_station(struct sla_ringing_station *ringing_station,
	enum sla_station_hangup hangup)
{
	struct sla_ringing_trunk *ringing_trunk;
	struct sla_trunk_ref *trunk_ref;
	struct sla_station_ref *station_ref;

	ast_dial_join(ringing_station->station->dial);
	ast_dial_destroy(ringing_station->station->dial);
	ringing_station->station->dial = NULL;

	if (hangup == SLA_STATION_HANGUP_NORMAL)
		goto done;

	/* If the station is being hung up because of a timeout, then add it to the
	 * list of timed out stations on each of the ringing trunks.  This is so
	 * that when doing further processing to figure out which stations should be
	 * ringing, which trunk to answer, determining timeouts, etc., we know which
	 * ringing trunks we should ignore. */
	AST_LIST_TRAVERSE(&sla.ringing_trunks, ringing_trunk, entry) {
		AST_LIST_TRAVERSE(&ringing_station->station->trunks, trunk_ref, entry) {
			if (ringing_trunk->trunk == trunk_ref->trunk)
				break;
		}
		if (!trunk_ref)
			continue;
		if (!(station_ref = sla_create_station_ref(ringing_station->station)))
			continue;
		AST_LIST_INSERT_TAIL(&ringing_trunk->timed_out_stations, station_ref, entry);
	}

done:
	free(ringing_station);
}

static void sla_dial_state_callback(struct ast_dial *dial)
{
	sla_queue_event(SLA_EVENT_DIAL_STATE);
}

/*! \brief Check to see if dialing this station already timed out for this ringing trunk
 * \note Assumes sla.lock is locked
 */
static int sla_check_timed_out_station(const struct sla_ringing_trunk *ringing_trunk,
	const struct sla_station *station)
{
	struct sla_station_ref *timed_out_station;

	AST_LIST_TRAVERSE(&ringing_trunk->timed_out_stations, timed_out_station, entry) {
		if (station == timed_out_station->station)
			return 1;
	}

	return 0;
}

/*! \brief Choose the highest priority ringing trunk for a station
 * \param station the station
 * \param remove remove the ringing trunk once selected
 * \param trunk_ref a place to store the pointer to this stations reference to
 *        the selected trunk
 * \return a pointer to the selected ringing trunk, or NULL if none found
 * \note Assumes that sla.lock is locked
 */
static struct sla_ringing_trunk *sla_choose_ringing_trunk(struct sla_station *station, 
	struct sla_trunk_ref **trunk_ref, int remove)
{
	struct sla_trunk_ref *s_trunk_ref;
	struct sla_ringing_trunk *ringing_trunk = NULL;

	AST_LIST_TRAVERSE(&station->trunks, s_trunk_ref, entry) {
		AST_LIST_TRAVERSE_SAFE_BEGIN(&sla.ringing_trunks, ringing_trunk, entry) {
			/* Make sure this is the trunk we're looking for */
			if (s_trunk_ref->trunk != ringing_trunk->trunk)
				continue;

			/* This trunk on the station is ringing.  But, make sure this station
			 * didn't already time out while this trunk was ringing. */
			if (sla_check_timed_out_station(ringing_trunk, station))
				continue;

			if (remove)
				AST_LIST_REMOVE_CURRENT(&sla.ringing_trunks, entry);

			if (trunk_ref)
				*trunk_ref = s_trunk_ref;

			break;
		}
		AST_LIST_TRAVERSE_SAFE_END
	
		if (ringing_trunk)
			break;
	}

	return ringing_trunk;
}

static void sla_handle_dial_state_event(void)
{
	struct sla_ringing_station *ringing_station;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&sla.ringing_stations, ringing_station, entry) {
		struct sla_trunk_ref *s_trunk_ref = NULL;
		struct sla_ringing_trunk *ringing_trunk = NULL;
		struct run_station_args args;
		enum ast_dial_result dial_res;
		pthread_attr_t attr;
		pthread_t dont_care;
		ast_mutex_t cond_lock;
		ast_cond_t cond;

		switch ((dial_res = ast_dial_state(ringing_station->station->dial))) {
		case AST_DIAL_RESULT_HANGUP:
		case AST_DIAL_RESULT_INVALID:
		case AST_DIAL_RESULT_FAILED:
		case AST_DIAL_RESULT_TIMEOUT:
		case AST_DIAL_RESULT_UNANSWERED:
			AST_LIST_REMOVE_CURRENT(&sla.ringing_stations, entry);
			sla_stop_ringing_station(ringing_station, SLA_STATION_HANGUP_NORMAL);
			break;
		case AST_DIAL_RESULT_ANSWERED:
			AST_LIST_REMOVE_CURRENT(&sla.ringing_stations, entry);
			/* Find the appropriate trunk to answer. */
			ast_mutex_lock(&sla.lock);
			ringing_trunk = sla_choose_ringing_trunk(ringing_station->station, &s_trunk_ref, 1);
			ast_mutex_unlock(&sla.lock);
			if (!ringing_trunk) {
				ast_log(LOG_DEBUG, "Found no ringing trunk for station '%s' to answer!\n",
					ringing_station->station->name);
				break;
			}
			/* Track the channel that answered this trunk */
			s_trunk_ref->chan = ast_dial_answered(ringing_station->station->dial);
			/* Actually answer the trunk */
			answer_trunk_chan(ringing_trunk->trunk->chan);
			sla_change_trunk_state(ringing_trunk->trunk, SLA_TRUNK_STATE_UP, ALL_TRUNK_REFS, NULL);
			/* Now, start a thread that will connect this station to the trunk.  The rest of
			 * the code here sets up the thread and ensures that it is able to save the arguments
			 * before they are no longer valid since they are allocated on the stack. */
			args.trunk_ref = s_trunk_ref;
			args.station = ringing_station->station;
			args.cond = &cond;
			args.cond_lock = &cond_lock;
			free(ringing_trunk);
			free(ringing_station);
			ast_mutex_init(&cond_lock);
			ast_cond_init(&cond, NULL);
			pthread_attr_init(&attr);
			pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
			ast_mutex_lock(&cond_lock);
			ast_pthread_create_background(&dont_care, &attr, run_station, &args);
			ast_cond_wait(&cond, &cond_lock);
			ast_mutex_unlock(&cond_lock);
			ast_mutex_destroy(&cond_lock);
			ast_cond_destroy(&cond);
			pthread_attr_destroy(&attr);
			break;
		case AST_DIAL_RESULT_TRYING:
		case AST_DIAL_RESULT_RINGING:
		case AST_DIAL_RESULT_PROGRESS:
		case AST_DIAL_RESULT_PROCEEDING:
			break;
		}
		if (dial_res == AST_DIAL_RESULT_ANSWERED) {
			/* Queue up reprocessing ringing trunks, and then ringing stations again */
			sla_queue_event(SLA_EVENT_RINGING_TRUNK);
			sla_queue_event(SLA_EVENT_DIAL_STATE);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END
}

/*! \brief Check to see if this station is already ringing 
 * \note Assumes sla.lock is locked 
 */
static int sla_check_ringing_station(const struct sla_station *station)
{
	struct sla_ringing_station *ringing_station;

	AST_LIST_TRAVERSE(&sla.ringing_stations, ringing_station, entry) {
		if (station == ringing_station->station)
			return 1;
	}

	return 0;
}

/*! \brief Check to see if this station has failed to be dialed in the past minute
 * \note assumes sla.lock is locked
 */
static int sla_check_failed_station(const struct sla_station *station)
{
	struct sla_failed_station *failed_station;
	int res = 0;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&sla.failed_stations, failed_station, entry) {
		if (station != failed_station->station)
			continue;
		if (ast_tvdiff_ms(ast_tvnow(), failed_station->last_try) > 1000) {
			AST_LIST_REMOVE_CURRENT(&sla.failed_stations, entry);
			free(failed_station);
			break;
		}
		res = 1;
	}
	AST_LIST_TRAVERSE_SAFE_END

	return res;
}

/*! \brief Ring a station
 * \note Assumes sla.lock is locked
 */
static int sla_ring_station(struct sla_ringing_trunk *ringing_trunk, struct sla_station *station)
{
	char *tech, *tech_data;
	struct ast_dial *dial;
	struct sla_ringing_station *ringing_station;
	const char *cid_name = NULL, *cid_num = NULL;
	enum ast_dial_result res;

	if (!(dial = ast_dial_create()))
		return -1;

	ast_dial_set_state_callback(dial, sla_dial_state_callback);
	tech_data = ast_strdupa(station->device);
	tech = strsep(&tech_data, "/");

	if (ast_dial_append(dial, tech, tech_data) == -1) {
		ast_dial_destroy(dial);
		return -1;
	}

	if (!sla.attempt_callerid && !ast_strlen_zero(ringing_trunk->trunk->chan->cid.cid_name)) {
		cid_name = ast_strdupa(ringing_trunk->trunk->chan->cid.cid_name);
		free(ringing_trunk->trunk->chan->cid.cid_name);
		ringing_trunk->trunk->chan->cid.cid_name = NULL;
	}
	if (!sla.attempt_callerid && !ast_strlen_zero(ringing_trunk->trunk->chan->cid.cid_num)) {
		cid_num = ast_strdupa(ringing_trunk->trunk->chan->cid.cid_num);
		free(ringing_trunk->trunk->chan->cid.cid_num);
		ringing_trunk->trunk->chan->cid.cid_num = NULL;
	}

	res = ast_dial_run(dial, ringing_trunk->trunk->chan, 1);
	
	if (cid_name)
		ringing_trunk->trunk->chan->cid.cid_name = ast_strdup(cid_name);
	if (cid_num)
		ringing_trunk->trunk->chan->cid.cid_num = ast_strdup(cid_num);
	
	if (res != AST_DIAL_RESULT_TRYING) {
		struct sla_failed_station *failed_station;
		ast_dial_destroy(dial);
		if (!(failed_station = ast_calloc(1, sizeof(*failed_station))))
			return -1;
		failed_station->station = station;
		failed_station->last_try = ast_tvnow();
		AST_LIST_INSERT_HEAD(&sla.failed_stations, failed_station, entry);
		return -1;
	}
	if (!(ringing_station = sla_create_ringing_station(station))) {
		ast_dial_join(dial);
		ast_dial_destroy(dial);
		return -1;
	}

	station->dial = dial;

	AST_LIST_INSERT_HEAD(&sla.ringing_stations, ringing_station, entry);

	return 0;
}

/*! \brief Check to see if a station is in use
 */
static int sla_check_inuse_station(const struct sla_station *station)
{
	struct sla_trunk_ref *trunk_ref;

	AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
		if (trunk_ref->chan)
			return 1;
	}

	return 0;
}

static struct sla_trunk_ref *sla_find_trunk_ref(const struct sla_station *station,
	const struct sla_trunk *trunk)
{
	struct sla_trunk_ref *trunk_ref = NULL;

	AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
		if (trunk_ref->trunk == trunk)
			break;
	}

	return trunk_ref;
}

/*! \brief Calculate the ring delay for a given ringing trunk on a station
 * \param station the station
 * \param trunk the trunk.  If NULL, the highest priority ringing trunk will be used
 * \return the number of ms left before the delay is complete, or INT_MAX if there is no delay
 */
static int sla_check_station_delay(struct sla_station *station, 
	struct sla_ringing_trunk *ringing_trunk)
{
	struct sla_trunk_ref *trunk_ref;
	unsigned int delay = UINT_MAX;
	int time_left, time_elapsed;

	if (!ringing_trunk)
		ringing_trunk = sla_choose_ringing_trunk(station, &trunk_ref, 0);
	else
		trunk_ref = sla_find_trunk_ref(station, ringing_trunk->trunk);

	if (!ringing_trunk || !trunk_ref)
		return delay;

	/* If this station has a ring delay specific to the highest priority
	 * ringing trunk, use that.  Otherwise, use the ring delay specified
	 * globally for the station. */
	delay = trunk_ref->ring_delay;
	if (!delay)
		delay = station->ring_delay;
	if (!delay)
		return INT_MAX;

	time_elapsed = ast_tvdiff_ms(ast_tvnow(), ringing_trunk->ring_begin);
	time_left = (delay * 1000) - time_elapsed;

	return time_left;
}

/*! \brief Ring stations based on current set of ringing trunks
 * \note Assumes that sla.lock is locked
 */
static void sla_ring_stations(void)
{
	struct sla_station_ref *station_ref;
	struct sla_ringing_trunk *ringing_trunk;

	/* Make sure that every station that uses at least one of the ringing
	 * trunks, is ringing. */
	AST_LIST_TRAVERSE(&sla.ringing_trunks, ringing_trunk, entry) {
		AST_LIST_TRAVERSE(&ringing_trunk->trunk->stations, station_ref, entry) {
			int time_left;

			/* Is this station already ringing? */
			if (sla_check_ringing_station(station_ref->station))
				continue;

			/* Is this station already in a call? */
			if (sla_check_inuse_station(station_ref->station))
				continue;

			/* Did we fail to dial this station earlier?  If so, has it been
 			 * a minute since we tried? */
			if (sla_check_failed_station(station_ref->station))
				continue;

			/* If this station already timed out while this trunk was ringing,
			 * do not dial it again for this ringing trunk. */
			if (sla_check_timed_out_station(ringing_trunk, station_ref->station))
				continue;

			/* Check for a ring delay in progress */
			time_left = sla_check_station_delay(station_ref->station, ringing_trunk);
			if (time_left != INT_MAX && time_left > 0)
				continue;

			/* It is time to make this station begin to ring.  Do it! */
			sla_ring_station(ringing_trunk, station_ref->station);
		}
	}
	/* Now, all of the stations that should be ringing, are ringing. */
}

static void sla_hangup_stations(void)
{
	struct sla_trunk_ref *trunk_ref;
	struct sla_ringing_station *ringing_station;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&sla.ringing_stations, ringing_station, entry) {
		AST_LIST_TRAVERSE(&ringing_station->station->trunks, trunk_ref, entry) {
			struct sla_ringing_trunk *ringing_trunk;
			ast_mutex_lock(&sla.lock);
			AST_LIST_TRAVERSE(&sla.ringing_trunks, ringing_trunk, entry) {
				if (trunk_ref->trunk == ringing_trunk->trunk)
					break;
			}
			ast_mutex_unlock(&sla.lock);
			if (ringing_trunk)
				break;
		}
		if (!trunk_ref) {
			AST_LIST_REMOVE_CURRENT(&sla.ringing_stations, entry);
			ast_dial_join(ringing_station->station->dial);
			ast_dial_destroy(ringing_station->station->dial);
			ringing_station->station->dial = NULL;
			free(ringing_station);
		}
	}
	AST_LIST_TRAVERSE_SAFE_END
}

static void sla_handle_ringing_trunk_event(void)
{
	ast_mutex_lock(&sla.lock);
	sla_ring_stations();
	ast_mutex_unlock(&sla.lock);

	/* Find stations that shouldn't be ringing anymore. */
	sla_hangup_stations();
}

static void sla_handle_hold_event(struct sla_event *event)
{
	ast_atomic_fetchadd_int((int *) &event->trunk_ref->trunk->hold_stations, 1);
	event->trunk_ref->state = SLA_TRUNK_STATE_ONHOLD_BYME;
	ast_device_state_changed("SLA:%s_%s", 
		event->station->name, event->trunk_ref->trunk->name);
	sla_change_trunk_state(event->trunk_ref->trunk, SLA_TRUNK_STATE_ONHOLD, 
		INACTIVE_TRUNK_REFS, event->trunk_ref);

	if (event->trunk_ref->trunk->active_stations == 1) {
		/* The station putting it on hold is the only one on the call, so start
		 * Music on hold to the trunk. */
		event->trunk_ref->trunk->on_hold = 1;
		ast_indicate(event->trunk_ref->trunk->chan, AST_CONTROL_HOLD);
	}

	ast_softhangup(event->trunk_ref->chan, AST_SOFTHANGUP_DEV);
	event->trunk_ref->chan = NULL;
}

/*! \brief Process trunk ring timeouts
 * \note Called with sla.lock locked
 * \return non-zero if a change to the ringing trunks was made
 */
static int sla_calc_trunk_timeouts(unsigned int *timeout)
{
	struct sla_ringing_trunk *ringing_trunk;
	int res = 0;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&sla.ringing_trunks, ringing_trunk, entry) {
		int time_left, time_elapsed;
		if (!ringing_trunk->trunk->ring_timeout)
			continue;
		time_elapsed = ast_tvdiff_ms(ast_tvnow(), ringing_trunk->ring_begin);
		time_left = (ringing_trunk->trunk->ring_timeout * 1000) - time_elapsed;
		if (time_left <= 0) {
			pbx_builtin_setvar_helper(ringing_trunk->trunk->chan, "SLATRUNK_STATUS", "RINGTIMEOUT");
			AST_LIST_REMOVE_CURRENT(&sla.ringing_trunks, entry);
			sla_stop_ringing_trunk(ringing_trunk);
			res = 1;
			continue;
		}
		if (time_left < *timeout)
			*timeout = time_left;
	}
	AST_LIST_TRAVERSE_SAFE_END

	return res;
}

/*! \brief Process station ring timeouts
 * \note Called with sla.lock locked
 * \return non-zero if a change to the ringing stations was made
 */
static int sla_calc_station_timeouts(unsigned int *timeout)
{
	struct sla_ringing_trunk *ringing_trunk;
	struct sla_ringing_station *ringing_station;
	int res = 0;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&sla.ringing_stations, ringing_station, entry) {
		unsigned int ring_timeout = 0;
		int time_elapsed, time_left = INT_MAX, final_trunk_time_left = INT_MIN;
		struct sla_trunk_ref *trunk_ref;

		/* If there are any ring timeouts specified for a specific trunk
		 * on the station, then use the highest per-trunk ring timeout.
		 * Otherwise, use the ring timeout set for the entire station. */
		AST_LIST_TRAVERSE(&ringing_station->station->trunks, trunk_ref, entry) {
			struct sla_station_ref *station_ref;
			int trunk_time_elapsed, trunk_time_left;

			AST_LIST_TRAVERSE(&sla.ringing_trunks, ringing_trunk, entry) {
				if (ringing_trunk->trunk == trunk_ref->trunk)
					break;
			}
			if (!ringing_trunk)
				continue;

			/* If there is a trunk that is ringing without a timeout, then the
			 * only timeout that could matter is a global station ring timeout. */
			if (!trunk_ref->ring_timeout)
				break;

			/* This trunk on this station is ringing and has a timeout.
			 * However, make sure this trunk isn't still ringing from a
			 * previous timeout.  If so, don't consider it. */
			AST_LIST_TRAVERSE(&ringing_trunk->timed_out_stations, station_ref, entry) {
				if (station_ref->station == ringing_station->station)
					break;
			}
			if (station_ref)
				continue;

			trunk_time_elapsed = ast_tvdiff_ms(ast_tvnow(), ringing_trunk->ring_begin);
			trunk_time_left = (trunk_ref->ring_timeout * 1000) - trunk_time_elapsed;
			if (trunk_time_left > final_trunk_time_left)
				final_trunk_time_left = trunk_time_left;
		}

		/* No timeout was found for ringing trunks, and no timeout for the entire station */
		if (final_trunk_time_left == INT_MIN && !ringing_station->station->ring_timeout)
			continue;

		/* Compute how much time is left for a global station timeout */
		if (ringing_station->station->ring_timeout) {
			ring_timeout = ringing_station->station->ring_timeout;
			time_elapsed = ast_tvdiff_ms(ast_tvnow(), ringing_station->ring_begin);
			time_left = (ring_timeout * 1000) - time_elapsed;
		}

		/* If the time left based on the per-trunk timeouts is smaller than the
		 * global station ring timeout, use that. */
		if (final_trunk_time_left > INT_MIN && final_trunk_time_left < time_left)
			time_left = final_trunk_time_left;

		/* If there is no time left, the station needs to stop ringing */
		if (time_left <= 0) {
			AST_LIST_REMOVE_CURRENT(&sla.ringing_stations, entry);
			sla_stop_ringing_station(ringing_station, SLA_STATION_HANGUP_TIMEOUT);
			res = 1;
			continue;
		}

		/* There is still some time left for this station to ring, so save that
		 * timeout if it is the first event scheduled to occur */
		if (time_left < *timeout)
			*timeout = time_left;
	}
	AST_LIST_TRAVERSE_SAFE_END

	return res;
}

/*! \brief Calculate the ring delay for a station
 * \note Assumes sla.lock is locked
 */
static int sla_calc_station_delays(unsigned int *timeout)
{
	struct sla_station *station;
	int res = 0;

	AST_LIST_TRAVERSE(&sla_stations, station, entry) {
		struct sla_ringing_trunk *ringing_trunk;
		int time_left;

		/* Ignore stations already ringing */
		if (sla_check_ringing_station(station))
			continue;

		/* Ignore stations already on a call */
		if (sla_check_inuse_station(station))
			continue;

		/* Ignore stations that don't have one of their trunks ringing */
		if (!(ringing_trunk = sla_choose_ringing_trunk(station, NULL, 0)))
			continue;

		if ((time_left = sla_check_station_delay(station, ringing_trunk)) == INT_MAX)
			continue;

		/* If there is no time left, then the station needs to start ringing.
		 * Return non-zero so that an event will be queued up an event to 
		 * make that happen. */
		if (time_left <= 0) {
			res = 1;
			continue;
		}

		if (time_left < *timeout)
			*timeout = time_left;
	}

	return res;
}

/*! \brief Calculate the time until the next known event
 *  \note Called with sla.lock locked */
static int sla_process_timers(struct timespec *ts)
{
	unsigned int timeout = UINT_MAX;
	struct timeval tv;
	unsigned int change_made = 0;

	/* Check for ring timeouts on ringing trunks */
	if (sla_calc_trunk_timeouts(&timeout))
		change_made = 1;

	/* Check for ring timeouts on ringing stations */
	if (sla_calc_station_timeouts(&timeout))
		change_made = 1;

	/* Check for station ring delays */
	if (sla_calc_station_delays(&timeout))
		change_made = 1;

	/* queue reprocessing of ringing trunks */
	if (change_made)
		sla_queue_event_nolock(SLA_EVENT_RINGING_TRUNK);

	/* No timeout */
	if (timeout == UINT_MAX)
		return 0;

	if (ts) {
		tv = ast_tvadd(ast_tvnow(), ast_samp2tv(timeout, 1000));
		ts->tv_sec = tv.tv_sec;
		ts->tv_nsec = tv.tv_usec * 1000;
	}

	return 1;
}

static void *sla_thread(void *data)
{
	struct sla_failed_station *failed_station;
	struct sla_ringing_station *ringing_station;

	ast_mutex_lock(&sla.lock);

	while (!sla.stop) {
		struct sla_event *event;
		struct timespec ts = { 0, };
		unsigned int have_timeout = 0;

		if (AST_LIST_EMPTY(&sla.event_q)) {
			if ((have_timeout = sla_process_timers(&ts)))
				ast_cond_timedwait(&sla.cond, &sla.lock, &ts);
			else
				ast_cond_wait(&sla.cond, &sla.lock);
			if (sla.stop)
				break;
		}

		if (have_timeout)
			sla_process_timers(NULL);

		while ((event = AST_LIST_REMOVE_HEAD(&sla.event_q, entry))) {
			ast_mutex_unlock(&sla.lock);
			switch (event->type) {
			case SLA_EVENT_HOLD:
				sla_handle_hold_event(event);
				break;
			case SLA_EVENT_DIAL_STATE:
				sla_handle_dial_state_event();
				break;
			case SLA_EVENT_RINGING_TRUNK:
				sla_handle_ringing_trunk_event();
				break;
			}
			free(event);
			ast_mutex_lock(&sla.lock);
		}
	}

	ast_mutex_unlock(&sla.lock);

	while ((ringing_station = AST_LIST_REMOVE_HEAD(&sla.ringing_stations, entry)))
		free(ringing_station);

	while ((failed_station = AST_LIST_REMOVE_HEAD(&sla.failed_stations, entry)))
		free(failed_station);

	return NULL;
}

struct dial_trunk_args {
	struct sla_trunk_ref *trunk_ref;
	struct sla_station *station;
	ast_mutex_t *cond_lock;
	ast_cond_t *cond;
};

static void *dial_trunk(void *data)
{
	struct dial_trunk_args *args = data;
	struct ast_dial *dial;
	char *tech, *tech_data;
	enum ast_dial_result dial_res;
	char conf_name[MAX_CONFNUM];
	struct ast_conference *conf;
	struct ast_flags conf_flags = { 0 };
	struct sla_trunk_ref *trunk_ref = args->trunk_ref;
	const char *cid_name = NULL, *cid_num = NULL;

	if (!(dial = ast_dial_create())) {
		ast_mutex_lock(args->cond_lock);
		ast_cond_signal(args->cond);
		ast_mutex_unlock(args->cond_lock);
		return NULL;
	}

	tech_data = ast_strdupa(trunk_ref->trunk->device);
	tech = strsep(&tech_data, "/");
	if (ast_dial_append(dial, tech, tech_data) == -1) {
		ast_mutex_lock(args->cond_lock);
		ast_cond_signal(args->cond);
		ast_mutex_unlock(args->cond_lock);
		ast_dial_destroy(dial);
		return NULL;
	}

	if (!sla.attempt_callerid && !ast_strlen_zero(trunk_ref->chan->cid.cid_name)) {
		cid_name = ast_strdupa(trunk_ref->chan->cid.cid_name);
		free(trunk_ref->chan->cid.cid_name);
		trunk_ref->chan->cid.cid_name = NULL;
	}
	if (!sla.attempt_callerid && !ast_strlen_zero(trunk_ref->chan->cid.cid_num)) {
		cid_num = ast_strdupa(trunk_ref->chan->cid.cid_num);
		free(trunk_ref->chan->cid.cid_num);
		trunk_ref->chan->cid.cid_num = NULL;
	}

	dial_res = ast_dial_run(dial, trunk_ref->chan, 1);

	if (cid_name)
		trunk_ref->chan->cid.cid_name = ast_strdup(cid_name);
	if (cid_num)
		trunk_ref->chan->cid.cid_num = ast_strdup(cid_num);

	if (dial_res != AST_DIAL_RESULT_TRYING) {
		ast_mutex_lock(args->cond_lock);
		ast_cond_signal(args->cond);
		ast_mutex_unlock(args->cond_lock);
		ast_dial_destroy(dial);
		return NULL;
	}

	for (;;) {
		unsigned int done = 0;
		switch ((dial_res = ast_dial_state(dial))) {
		case AST_DIAL_RESULT_ANSWERED:
			trunk_ref->trunk->chan = ast_dial_answered(dial);
		case AST_DIAL_RESULT_HANGUP:
		case AST_DIAL_RESULT_INVALID:
		case AST_DIAL_RESULT_FAILED:
		case AST_DIAL_RESULT_TIMEOUT:
		case AST_DIAL_RESULT_UNANSWERED:
			done = 1;
		case AST_DIAL_RESULT_TRYING:
		case AST_DIAL_RESULT_RINGING:
		case AST_DIAL_RESULT_PROGRESS:
		case AST_DIAL_RESULT_PROCEEDING:
			break;
		}
		if (done)
			break;
	}

	if (!trunk_ref->trunk->chan) {
		ast_mutex_lock(args->cond_lock);
		ast_cond_signal(args->cond);
		ast_mutex_unlock(args->cond_lock);
		ast_dial_join(dial);
		ast_dial_destroy(dial);
		return NULL;
	}

	snprintf(conf_name, sizeof(conf_name), "SLA_%s", trunk_ref->trunk->name);
	ast_set_flag(&conf_flags, 
		CONFFLAG_QUIET | CONFFLAG_MARKEDEXIT | CONFFLAG_MARKEDUSER | 
		CONFFLAG_PASS_DTMF | CONFFLAG_SLA_TRUNK);
	conf = build_conf(conf_name, "", "", 1, 1, 1);

	ast_mutex_lock(args->cond_lock);
	ast_cond_signal(args->cond);
	ast_mutex_unlock(args->cond_lock);

	if (conf) {
		conf_run(trunk_ref->trunk->chan, conf, conf_flags.flags, NULL);
		dispose_conf(conf);
		conf = NULL;
	}

	/* If the trunk is going away, it is definitely now IDLE. */
	sla_change_trunk_state(trunk_ref->trunk, SLA_TRUNK_STATE_IDLE, ALL_TRUNK_REFS, NULL);

	trunk_ref->trunk->chan = NULL;
	trunk_ref->trunk->on_hold = 0;

	ast_dial_join(dial);
	ast_dial_destroy(dial);

	return NULL;
}

/*! \brief For a given station, choose the highest priority idle trunk
 */
static struct sla_trunk_ref *sla_choose_idle_trunk(const struct sla_station *station)
{
	struct sla_trunk_ref *trunk_ref = NULL;

	AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
		if (trunk_ref->state == SLA_TRUNK_STATE_IDLE)
			break;
	}

	return trunk_ref;
}

static int sla_station_exec(struct ast_channel *chan, void *data)
{
	char *station_name, *trunk_name;
	struct sla_station *station;
	struct sla_trunk_ref *trunk_ref = NULL;
	char conf_name[MAX_CONFNUM];
	struct ast_flags conf_flags = { 0 };
	struct ast_conference *conf;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Invalid Arguments to SLAStation!\n");
		pbx_builtin_setvar_helper(chan, "SLASTATION_STATUS", "FAILURE");
		return 0;
	}

	trunk_name = ast_strdupa(data);
	station_name = strsep(&trunk_name, "_");

	if (ast_strlen_zero(station_name)) {
		ast_log(LOG_WARNING, "Invalid Arguments to SLAStation!\n");
		pbx_builtin_setvar_helper(chan, "SLASTATION_STATUS", "FAILURE");
		return 0;
	}

	AST_RWLIST_RDLOCK(&sla_stations);
	station = sla_find_station(station_name);
	AST_RWLIST_UNLOCK(&sla_stations);

	if (!station) {
		ast_log(LOG_WARNING, "Station '%s' not found!\n", station_name);
		pbx_builtin_setvar_helper(chan, "SLASTATION_STATUS", "FAILURE");
		return 0;
	}

	AST_RWLIST_RDLOCK(&sla_trunks);
	if (!ast_strlen_zero(trunk_name)) {
		trunk_ref = sla_find_trunk_ref_byname(station, trunk_name);
	} else
		trunk_ref = sla_choose_idle_trunk(station);
	AST_RWLIST_UNLOCK(&sla_trunks);

	if (!trunk_ref) {
		if (ast_strlen_zero(trunk_name))
			ast_log(LOG_NOTICE, "No trunks available for call.\n");
		else {
			ast_log(LOG_NOTICE, "Can't join existing call on trunk "
				"'%s' due to access controls.\n", trunk_name);
		}
		pbx_builtin_setvar_helper(chan, "SLASTATION_STATUS", "CONGESTION");
		return 0;
	}

	if (trunk_ref->state == SLA_TRUNK_STATE_ONHOLD_BYME) {
		if (ast_atomic_dec_and_test((int *) &trunk_ref->trunk->hold_stations) == 1)
			sla_change_trunk_state(trunk_ref->trunk, SLA_TRUNK_STATE_UP, ALL_TRUNK_REFS, NULL);
		else {
			trunk_ref->state = SLA_TRUNK_STATE_UP;
			ast_device_state_changed("SLA:%s_%s", station->name, trunk_ref->trunk->name);
		}
	} else if (trunk_ref->state == SLA_TRUNK_STATE_RINGING) {
		struct sla_ringing_trunk *ringing_trunk;

		ast_mutex_lock(&sla.lock);
		AST_LIST_TRAVERSE_SAFE_BEGIN(&sla.ringing_trunks, ringing_trunk, entry) {
			if (ringing_trunk->trunk == trunk_ref->trunk) {
				AST_LIST_REMOVE_CURRENT(&sla.ringing_trunks, entry);
				break;
			}
		}
		AST_LIST_TRAVERSE_SAFE_END
		ast_mutex_unlock(&sla.lock);

		if (ringing_trunk) {
			answer_trunk_chan(ringing_trunk->trunk->chan);
			sla_change_trunk_state(ringing_trunk->trunk, SLA_TRUNK_STATE_UP, ALL_TRUNK_REFS, NULL);

			free(ringing_trunk);

			/* Queue up reprocessing ringing trunks, and then ringing stations again */
			sla_queue_event(SLA_EVENT_RINGING_TRUNK);
			sla_queue_event(SLA_EVENT_DIAL_STATE);
		}
	}

	trunk_ref->chan = chan;

	if (!trunk_ref->trunk->chan) {
		ast_mutex_t cond_lock;
		ast_cond_t cond;
		pthread_t dont_care;
		pthread_attr_t attr;
		struct dial_trunk_args args = {
			.trunk_ref = trunk_ref,
			.station = station,
			.cond_lock = &cond_lock,
			.cond = &cond,
		};
		sla_change_trunk_state(trunk_ref->trunk, SLA_TRUNK_STATE_UP, ALL_TRUNK_REFS, NULL);
		/* Create a thread to dial the trunk and dump it into the conference.
		 * However, we want to wait until the trunk has been dialed and the
		 * conference is created before continuing on here. */
		ast_autoservice_start(chan);
		ast_mutex_init(&cond_lock);
		ast_cond_init(&cond, NULL);
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		ast_mutex_lock(&cond_lock);
		ast_pthread_create_background(&dont_care, &attr, dial_trunk, &args);
		ast_cond_wait(&cond, &cond_lock);
		ast_mutex_unlock(&cond_lock);
		ast_mutex_destroy(&cond_lock);
		ast_cond_destroy(&cond);
		pthread_attr_destroy(&attr);
		ast_autoservice_stop(chan);
		if (!trunk_ref->trunk->chan) {
			ast_log(LOG_DEBUG, "Trunk didn't get created. chan: %lx\n", (long) trunk_ref->trunk->chan);
			pbx_builtin_setvar_helper(chan, "SLASTATION_STATUS", "CONGESTION");
			sla_change_trunk_state(trunk_ref->trunk, SLA_TRUNK_STATE_IDLE, ALL_TRUNK_REFS, NULL);
			trunk_ref->chan = NULL;
			return 0;
		}
	}

	if (ast_atomic_fetchadd_int((int *) &trunk_ref->trunk->active_stations, 1) == 0 &&
		trunk_ref->trunk->on_hold) {
		trunk_ref->trunk->on_hold = 0;
		ast_indicate(trunk_ref->trunk->chan, AST_CONTROL_UNHOLD);
		sla_change_trunk_state(trunk_ref->trunk, SLA_TRUNK_STATE_UP, ALL_TRUNK_REFS, NULL);
	}

	snprintf(conf_name, sizeof(conf_name), "SLA_%s", trunk_ref->trunk->name);
	ast_set_flag(&conf_flags, 
		CONFFLAG_QUIET | CONFFLAG_MARKEDEXIT | CONFFLAG_PASS_DTMF | CONFFLAG_SLA_STATION);
	ast_answer(chan);
	conf = build_conf(conf_name, "", "", 0, 0, 1);
	if (conf) {
		conf_run(chan, conf, conf_flags.flags, NULL);
		dispose_conf(conf);
		conf = NULL;
	}
	trunk_ref->chan = NULL;
	if (ast_atomic_dec_and_test((int *) &trunk_ref->trunk->active_stations) &&
		trunk_ref->state != SLA_TRUNK_STATE_ONHOLD_BYME) {
		strncat(conf_name, "|K", sizeof(conf_name) - strlen(conf_name) - 1);
		admin_exec(NULL, conf_name);
		trunk_ref->trunk->hold_stations = 0;
		sla_change_trunk_state(trunk_ref->trunk, SLA_TRUNK_STATE_IDLE, ALL_TRUNK_REFS, NULL);
	}
	
	pbx_builtin_setvar_helper(chan, "SLASTATION_STATUS", "SUCCESS");

	return 0;
}

static struct sla_trunk_ref *create_trunk_ref(struct sla_trunk *trunk)
{
	struct sla_trunk_ref *trunk_ref;

	if (!(trunk_ref = ast_calloc(1, sizeof(*trunk_ref))))
		return NULL;

	trunk_ref->trunk = trunk;

	return trunk_ref;
}

static struct sla_ringing_trunk *queue_ringing_trunk(struct sla_trunk *trunk)
{
	struct sla_ringing_trunk *ringing_trunk;

	if (!(ringing_trunk = ast_calloc(1, sizeof(*ringing_trunk))))
		return NULL;
	
	ringing_trunk->trunk = trunk;
	ringing_trunk->ring_begin = ast_tvnow();

	sla_change_trunk_state(trunk, SLA_TRUNK_STATE_RINGING, ALL_TRUNK_REFS, NULL);

	ast_mutex_lock(&sla.lock);
	AST_LIST_INSERT_HEAD(&sla.ringing_trunks, ringing_trunk, entry);
	ast_mutex_unlock(&sla.lock);

	sla_queue_event(SLA_EVENT_RINGING_TRUNK);

	return ringing_trunk;
}

static int sla_trunk_exec(struct ast_channel *chan, void *data)
{
	const char *trunk_name = data;
	char conf_name[MAX_CONFNUM];
	struct ast_conference *conf;
	struct ast_flags conf_flags = { 0 };
	struct sla_trunk *trunk;
	struct sla_ringing_trunk *ringing_trunk;

	AST_RWLIST_RDLOCK(&sla_trunks);
	trunk = sla_find_trunk(trunk_name);
	AST_RWLIST_UNLOCK(&sla_trunks);
	if (!trunk) {
		ast_log(LOG_ERROR, "SLA Trunk '%s' not found!\n", trunk_name);
		pbx_builtin_setvar_helper(chan, "SLATRUNK_STATUS", "FAILURE");
		return 0;
	}
	if (trunk->chan) {
		ast_log(LOG_ERROR, "Call came in on %s, but the trunk is already in use!\n",
			trunk_name);
		pbx_builtin_setvar_helper(chan, "SLATRUNK_STATUS", "FAILURE");
		return 0;
	}
	trunk->chan = chan;

	if (!(ringing_trunk = queue_ringing_trunk(trunk))) {
		pbx_builtin_setvar_helper(chan, "SLATRUNK_STATUS", "FAILURE");
		return 0;
	}

	snprintf(conf_name, sizeof(conf_name), "SLA_%s", trunk_name);
	conf = build_conf(conf_name, "", "", 1, 1, 1);
	if (!conf) {
		pbx_builtin_setvar_helper(chan, "SLATRUNK_STATUS", "FAILURE");
		return 0;
	}
	ast_set_flag(&conf_flags, 
		CONFFLAG_QUIET | CONFFLAG_MARKEDEXIT | CONFFLAG_MARKEDUSER | CONFFLAG_PASS_DTMF | CONFFLAG_NO_AUDIO_UNTIL_UP);
	ast_indicate(chan, AST_CONTROL_RINGING);
	conf_run(chan, conf, conf_flags.flags, NULL);
	dispose_conf(conf);
	conf = NULL;
	trunk->chan = NULL;
	trunk->on_hold = 0;

	sla_change_trunk_state(trunk, SLA_TRUNK_STATE_IDLE, ALL_TRUNK_REFS, NULL);

	if (!pbx_builtin_getvar_helper(chan, "SLATRUNK_STATUS"))
		pbx_builtin_setvar_helper(chan, "SLATRUNK_STATUS", "SUCCESS");

	/* Remove the entry from the list of ringing trunks if it is still there. */
	ast_mutex_lock(&sla.lock);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&sla.ringing_trunks, ringing_trunk, entry) {
		if (ringing_trunk->trunk == trunk) {
			AST_LIST_REMOVE_CURRENT(&sla.ringing_trunks, entry);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END
	ast_mutex_unlock(&sla.lock);
	if (ringing_trunk) {
		free(ringing_trunk);
		pbx_builtin_setvar_helper(chan, "SLATRUNK_STATUS", "UNANSWERED");
		/* Queue reprocessing of ringing trunks to make stations stop ringing
		 * that shouldn't be ringing after this trunk stopped. */
		sla_queue_event(SLA_EVENT_RINGING_TRUNK);
	}

	return 0;
}

static int sla_state(const char *data)
{
	char *buf, *station_name, *trunk_name;
	struct sla_station *station;
	struct sla_trunk_ref *trunk_ref;
	int res = AST_DEVICE_INVALID;

	trunk_name = buf = ast_strdupa(data);
	station_name = strsep(&trunk_name, "_");

	AST_RWLIST_RDLOCK(&sla_stations);
	AST_LIST_TRAVERSE(&sla_stations, station, entry) {
		if (strcasecmp(station_name, station->name))
			continue;
		AST_RWLIST_RDLOCK(&sla_trunks);
		AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
			if (!strcasecmp(trunk_name, trunk_ref->trunk->name))
				break;
		}
		if (!trunk_ref) {
			AST_RWLIST_UNLOCK(&sla_trunks);
			break;
		}
		switch (trunk_ref->state) {
		case SLA_TRUNK_STATE_IDLE:
			res = AST_DEVICE_NOT_INUSE;
			break;
		case SLA_TRUNK_STATE_RINGING:
			res = AST_DEVICE_RINGING;
			break;
		case SLA_TRUNK_STATE_UP:
			res = AST_DEVICE_INUSE;
			break;
		case SLA_TRUNK_STATE_ONHOLD:
		case SLA_TRUNK_STATE_ONHOLD_BYME:
			res = AST_DEVICE_ONHOLD;
			break;
		}
		AST_RWLIST_UNLOCK(&sla_trunks);
	}
	AST_RWLIST_UNLOCK(&sla_stations);

	if (res == AST_DEVICE_INVALID) {
		ast_log(LOG_ERROR, "Could not determine state for trunk %s on station %s!\n",
			trunk_name, station_name);
	}

	return res;
}

static void destroy_trunk(struct sla_trunk *trunk)
{
	struct sla_station_ref *station_ref;

	if (!ast_strlen_zero(trunk->autocontext))
		ast_context_remove_extension(trunk->autocontext, "s", 1, sla_registrar);

	while ((station_ref = AST_LIST_REMOVE_HEAD(&trunk->stations, entry)))
		free(station_ref);

	ast_string_field_free_memory(trunk);
	free(trunk);
}

static void destroy_station(struct sla_station *station)
{
	struct sla_trunk_ref *trunk_ref;

	if (!ast_strlen_zero(station->autocontext)) {
		AST_RWLIST_RDLOCK(&sla_trunks);
		AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
			char exten[AST_MAX_EXTENSION];
			char hint[AST_MAX_APP];
			snprintf(exten, sizeof(exten), "%s_%s", station->name, trunk_ref->trunk->name);
			snprintf(hint, sizeof(hint), "SLA:%s", exten);
			ast_context_remove_extension(station->autocontext, exten, 
				1, sla_registrar);
			ast_context_remove_extension(station->autocontext, hint, 
				PRIORITY_HINT, sla_registrar);
		}
		AST_RWLIST_UNLOCK(&sla_trunks);
	}

	while ((trunk_ref = AST_LIST_REMOVE_HEAD(&station->trunks, entry)))
		free(trunk_ref);

	ast_string_field_free_memory(station);
	free(station);
}

static void sla_destroy(void)
{
	struct sla_trunk *trunk;
	struct sla_station *station;

	AST_RWLIST_WRLOCK(&sla_trunks);
	while ((trunk = AST_RWLIST_REMOVE_HEAD(&sla_trunks, entry)))
		destroy_trunk(trunk);
	AST_RWLIST_UNLOCK(&sla_trunks);

	AST_RWLIST_WRLOCK(&sla_stations);
	while ((station = AST_RWLIST_REMOVE_HEAD(&sla_stations, entry)))
		destroy_station(station);
	AST_RWLIST_UNLOCK(&sla_stations);

	if (sla.thread != AST_PTHREADT_NULL) {
		ast_mutex_lock(&sla.lock);
		sla.stop = 1;
		ast_cond_signal(&sla.cond);
		ast_mutex_unlock(&sla.lock);
		pthread_join(sla.thread, NULL);
	}

	/* Drop any created contexts from the dialplan */
	ast_context_destroy(NULL, sla_registrar);

	ast_mutex_destroy(&sla.lock);
	ast_cond_destroy(&sla.cond);
}

static int sla_check_device(const char *device)
{
	char *tech, *tech_data;

	tech_data = ast_strdupa(device);
	tech = strsep(&tech_data, "/");

	if (ast_strlen_zero(tech) || ast_strlen_zero(tech_data))
		return -1;

	return 0;
}

static int sla_build_trunk(struct ast_config *cfg, const char *cat)
{
	struct sla_trunk *trunk;
	struct ast_variable *var;
	const char *dev;

	if (!(dev = ast_variable_retrieve(cfg, cat, "device"))) {
		ast_log(LOG_ERROR, "SLA Trunk '%s' defined with no device!\n", cat);
		return -1;
	}

	if (sla_check_device(dev)) {
		ast_log(LOG_ERROR, "SLA Trunk '%s' define with invalid device '%s'!\n",
			cat, dev);
		return -1;
	}

	if (!(trunk = ast_calloc(1, sizeof(*trunk))))
		return -1;
	if (ast_string_field_init(trunk, 32)) {
		free(trunk);
		return -1;
	}

	ast_string_field_set(trunk, name, cat);
	ast_string_field_set(trunk, device, dev);

	for (var = ast_variable_browse(cfg, cat); var; var = var->next) {
		if (!strcasecmp(var->name, "autocontext"))
			ast_string_field_set(trunk, autocontext, var->value);
		else if (!strcasecmp(var->name, "ringtimeout")) {
			if (sscanf(var->value, "%30u", &trunk->ring_timeout) != 1) {
				ast_log(LOG_WARNING, "Invalid ringtimeout '%s' specified for trunk '%s'\n",
					var->value, trunk->name);
				trunk->ring_timeout = 0;
			}
		} else if (!strcasecmp(var->name, "barge"))
			trunk->barge_disabled = ast_false(var->value);
		else if (!strcasecmp(var->name, "hold")) {
			if (!strcasecmp(var->value, "private"))
				trunk->hold_access = SLA_HOLD_PRIVATE;
			else if (!strcasecmp(var->value, "open"))
				trunk->hold_access = SLA_HOLD_OPEN;
			else {
				ast_log(LOG_WARNING, "Invalid value '%s' for hold on trunk %s\n",
					var->value, trunk->name);
			}
		} else if (strcasecmp(var->name, "type") && strcasecmp(var->name, "device")) {
			ast_log(LOG_ERROR, "Invalid option '%s' specified at line %d of %s!\n",
				var->name, var->lineno, SLA_CONFIG_FILE);
		}
	}

	if (!ast_strlen_zero(trunk->autocontext)) {
		struct ast_context *context;
		context = ast_context_find_or_create(NULL, trunk->autocontext, sla_registrar);
		if (!context) {
			ast_log(LOG_ERROR, "Failed to automatically find or create "
				"context '%s' for SLA!\n", trunk->autocontext);
			destroy_trunk(trunk);
			return -1;
		}
		if (ast_add_extension2(context, 0 /* don't replace */, "s", 1,
			NULL, NULL, slatrunk_app, ast_strdup(trunk->name), ast_free_ptr, sla_registrar)) {
			ast_log(LOG_ERROR, "Failed to automatically create extension "
				"for trunk '%s'!\n", trunk->name);
			destroy_trunk(trunk);
			return -1;
		}
	}

	AST_RWLIST_WRLOCK(&sla_trunks);
	AST_RWLIST_INSERT_TAIL(&sla_trunks, trunk, entry);
	AST_RWLIST_UNLOCK(&sla_trunks);

	return 0;
}

static void sla_add_trunk_to_station(struct sla_station *station, struct ast_variable *var)
{
	struct sla_trunk *trunk;
	struct sla_trunk_ref *trunk_ref;
	struct sla_station_ref *station_ref;
	char *trunk_name, *options, *cur;

	options = ast_strdupa(var->value);
	trunk_name = strsep(&options, ",");
	
	AST_RWLIST_RDLOCK(&sla_trunks);
	AST_RWLIST_TRAVERSE(&sla_trunks, trunk, entry) {
		if (!strcasecmp(trunk->name, trunk_name))
			break;
	}

	AST_RWLIST_UNLOCK(&sla_trunks);
	if (!trunk) {
		ast_log(LOG_ERROR, "Trunk '%s' not found!\n", var->value);
		return;
	}
	if (!(trunk_ref = create_trunk_ref(trunk)))
		return;
	trunk_ref->state = SLA_TRUNK_STATE_IDLE;

	while ((cur = strsep(&options, ","))) {
		char *name, *value = cur;
		name = strsep(&value, "=");
		if (!strcasecmp(name, "ringtimeout")) {
			if (sscanf(value, "%30u", &trunk_ref->ring_timeout) != 1) {
				ast_log(LOG_WARNING, "Invalid ringtimeout value '%s' for "
					"trunk '%s' on station '%s'\n", value, trunk->name, station->name);
				trunk_ref->ring_timeout = 0;
			}
		} else if (!strcasecmp(name, "ringdelay")) {
			if (sscanf(value, "%30u", &trunk_ref->ring_delay) != 1) {
				ast_log(LOG_WARNING, "Invalid ringdelay value '%s' for "
					"trunk '%s' on station '%s'\n", value, trunk->name, station->name);
				trunk_ref->ring_delay = 0;
			}
		} else {
			ast_log(LOG_WARNING, "Invalid option '%s' for "
				"trunk '%s' on station '%s'\n", name, trunk->name, station->name);
		}
	}

	if (!(station_ref = sla_create_station_ref(station))) {
		free(trunk_ref);
		return;
	}
	ast_atomic_fetchadd_int((int *) &trunk->num_stations, 1);
	AST_RWLIST_WRLOCK(&sla_trunks);
	AST_LIST_INSERT_TAIL(&trunk->stations, station_ref, entry);
	AST_RWLIST_UNLOCK(&sla_trunks);
	AST_LIST_INSERT_TAIL(&station->trunks, trunk_ref, entry);
}

static int sla_build_station(struct ast_config *cfg, const char *cat)
{
	struct sla_station *station;
	struct ast_variable *var;
	const char *dev;

	if (!(dev = ast_variable_retrieve(cfg, cat, "device"))) {
		ast_log(LOG_ERROR, "SLA Station '%s' defined with no device!\n", cat);
		return -1;
	}

	if (!(station = ast_calloc(1, sizeof(*station))))
		return -1;
	if (ast_string_field_init(station, 32)) {
		free(station);
		return -1;
	}

	ast_string_field_set(station, name, cat);
	ast_string_field_set(station, device, dev);

	for (var = ast_variable_browse(cfg, cat); var; var = var->next) {
		if (!strcasecmp(var->name, "trunk"))
			sla_add_trunk_to_station(station, var);
		else if (!strcasecmp(var->name, "autocontext"))
			ast_string_field_set(station, autocontext, var->value);
		else if (!strcasecmp(var->name, "ringtimeout")) {
			if (sscanf(var->value, "%30u", &station->ring_timeout) != 1) {
				ast_log(LOG_WARNING, "Invalid ringtimeout '%s' specified for station '%s'\n",
					var->value, station->name);
				station->ring_timeout = 0;
			}
		} else if (!strcasecmp(var->name, "ringdelay")) {
			if (sscanf(var->value, "%30u", &station->ring_delay) != 1) {
				ast_log(LOG_WARNING, "Invalid ringdelay '%s' specified for station '%s'\n",
					var->value, station->name);
				station->ring_delay = 0;
			}
		} else if (!strcasecmp(var->name, "hold")) {
			if (!strcasecmp(var->value, "private"))
				station->hold_access = SLA_HOLD_PRIVATE;
			else if (!strcasecmp(var->value, "open"))
				station->hold_access = SLA_HOLD_OPEN;
			else {
				ast_log(LOG_WARNING, "Invalid value '%s' for hold on station %s\n",
					var->value, station->name);
			}

		} else if (strcasecmp(var->name, "type") && strcasecmp(var->name, "device")) {
			ast_log(LOG_ERROR, "Invalid option '%s' specified at line %d of %s!\n",
				var->name, var->lineno, SLA_CONFIG_FILE);
		}
	}

	if (!ast_strlen_zero(station->autocontext)) {
		struct ast_context *context;
		struct sla_trunk_ref *trunk_ref;
		context = ast_context_find_or_create(NULL, station->autocontext, sla_registrar);
		if (!context) {
			ast_log(LOG_ERROR, "Failed to automatically find or create "
				"context '%s' for SLA!\n", station->autocontext);
			destroy_station(station);
			return -1;
		}
		/* The extension for when the handset goes off-hook.
		 * exten => station1,1,SLAStation(station1) */
		if (ast_add_extension2(context, 0 /* don't replace */, station->name, 1,
			NULL, NULL, slastation_app, ast_strdup(station->name), ast_free_ptr, sla_registrar)) {
			ast_log(LOG_ERROR, "Failed to automatically create extension "
				"for trunk '%s'!\n", station->name);
			destroy_station(station);
			return -1;
		}
		AST_RWLIST_RDLOCK(&sla_trunks);
		AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
			char exten[AST_MAX_EXTENSION];
			char hint[AST_MAX_APP];
			snprintf(exten, sizeof(exten), "%s_%s", station->name, trunk_ref->trunk->name);
			snprintf(hint, sizeof(hint), "SLA:%s", exten);
			/* Extension for this line button 
			 * exten => station1_line1,1,SLAStation(station1_line1) */
			if (ast_add_extension2(context, 0 /* don't replace */, exten, 1,
				NULL, NULL, slastation_app, ast_strdup(exten), ast_free_ptr, sla_registrar)) {
				ast_log(LOG_ERROR, "Failed to automatically create extension "
					"for trunk '%s'!\n", station->name);
				destroy_station(station);
				return -1;
			}
			/* Hint for this line button 
			 * exten => station1_line1,hint,SLA:station1_line1 */
			if (ast_add_extension2(context, 0 /* don't replace */, exten, PRIORITY_HINT,
				NULL, NULL, hint, NULL, NULL, sla_registrar)) {
				ast_log(LOG_ERROR, "Failed to automatically create hint "
					"for trunk '%s'!\n", station->name);
				destroy_station(station);
				return -1;
			}
		}
		AST_RWLIST_UNLOCK(&sla_trunks);
	}

	AST_RWLIST_WRLOCK(&sla_stations);
	AST_RWLIST_INSERT_TAIL(&sla_stations, station, entry);
	AST_RWLIST_UNLOCK(&sla_stations);

	return 0;
}

static int sla_load_config(void)
{
	struct ast_config *cfg;
	const char *cat = NULL;
	int res = 0;
	const char *val;

	ast_mutex_init(&sla.lock);
	ast_cond_init(&sla.cond, NULL);

	if (!(cfg = ast_config_load(SLA_CONFIG_FILE)))
		return 0; /* Treat no config as normal */

	if ((val = ast_variable_retrieve(cfg, "general", "attemptcallerid")))
		sla.attempt_callerid = ast_true(val);

	while ((cat = ast_category_browse(cfg, cat)) && !res) {
		const char *type;
		if (!strcasecmp(cat, "general"))
			continue;
		if (!(type = ast_variable_retrieve(cfg, cat, "type"))) {
			ast_log(LOG_WARNING, "Invalid entry in %s defined with no type!\n",
				SLA_CONFIG_FILE);
			continue;
		}
		if (!strcasecmp(type, "trunk"))
			res = sla_build_trunk(cfg, cat);
		else if (!strcasecmp(type, "station"))
			res = sla_build_station(cfg, cat);
		else {
			ast_log(LOG_WARNING, "Entry in %s defined with invalid type '%s'!\n",
				SLA_CONFIG_FILE, type);
		}
	}

	ast_config_destroy(cfg);

	if (!AST_LIST_EMPTY(&sla_stations) || !AST_LIST_EMPTY(&sla_stations))
		ast_pthread_create(&sla.thread, NULL, sla_thread, NULL);

	return res;
}

static int load_config(int reload)
{
	int res = 0;

	load_config_meetme();
	if (!reload)
		res = sla_load_config();

	return res;
}

static int unload_module(void)
{
	int res = 0;
	
	ast_cli_unregister_multiple(cli_meetme, ARRAY_LEN(cli_meetme));
	res = ast_manager_unregister("MeetmeMute");
	res |= ast_manager_unregister("MeetmeUnmute");
	res |= ast_unregister_application(app3);
	res |= ast_unregister_application(app2);
	res |= ast_unregister_application(app);
	res |= ast_unregister_application(slastation_app);
	res |= ast_unregister_application(slatrunk_app);

	ast_devstate_prov_del("Meetme");
	ast_devstate_prov_del("SLA");

	ast_module_user_hangup_all();
	
	sla_destroy();

	return res;
}

static int load_module(void)
{
	int res = 0;

	res |= load_config(0);

	ast_cli_register_multiple(cli_meetme, ARRAY_LEN(cli_meetme));
	res |= ast_manager_register("MeetmeMute", EVENT_FLAG_CALL, 
				    action_meetmemute, "Mute a Meetme user");
	res |= ast_manager_register("MeetmeUnmute", EVENT_FLAG_CALL, 
				    action_meetmeunmute, "Unmute a Meetme user");
	res |= ast_register_application(app3, admin_exec, synopsis3, descrip3);
	res |= ast_register_application(app2, count_exec, synopsis2, descrip2);
	res |= ast_register_application(app, conf_exec, synopsis, descrip);
	res |= ast_register_application(slastation_app, sla_station_exec,
					slastation_synopsis, slastation_desc);
	res |= ast_register_application(slatrunk_app, sla_trunk_exec,
					slatrunk_synopsis, slatrunk_desc);

	res |= ast_devstate_prov_add("Meetme", meetmestate);
	res |= ast_devstate_prov_add("SLA", sla_state);

	return res;
}

static int reload(void)
{
	return load_config(1);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "MeetMe conference bridge",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );

