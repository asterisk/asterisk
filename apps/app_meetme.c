/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Meet me conference bridge
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */

/*** MODULEINFO
	<depend>zaptel</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <zaptel/zaptel.h>

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
#include "asterisk/devicestate.h"

#include "enter.h"
#include "leave.h"

#define CONFIG_FILE_NAME "meetme.conf"

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
	/*! If set, the user is a shared line appearance station */
	CONFFLAG_SLA_STATION = (1 << 25),
	/*! If set, the user is a shared line appearance trunk */
	CONFFLAG_SLA_TRUNK = (1 << 26),
	/*! If set, the user has put us on hold */
	CONFFLAG_HOLD = (1 << 27)
};

AST_APP_OPTIONS(meetme_opts, {
	AST_APP_OPTION('A', CONFFLAG_MARKEDUSER ),
	AST_APP_OPTION('a', CONFFLAG_ADMIN ),
	AST_APP_OPTION('b', CONFFLAG_AGI ),
	AST_APP_OPTION('c', CONFFLAG_ANNOUNCEUSERCOUNT ),
	AST_APP_OPTION('D', CONFFLAG_DYNAMICPIN ),
	AST_APP_OPTION('d', CONFFLAG_DYNAMIC ),
	AST_APP_OPTION('E', CONFFLAG_EMPTYNOPIN ),
	AST_APP_OPTION('e', CONFFLAG_EMPTY ),
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
	AST_APP_OPTION('w', CONFFLAG_WAITMARKED ),
	AST_APP_OPTION('X', CONFFLAG_EXIT_CONTEXT ),
	AST_APP_OPTION('x', CONFFLAG_MARKEDEXIT ),
	AST_APP_OPTION('1', CONFFLAG_NOONLYPERSON ),
});

AST_APP_OPTIONS(sla_opts, {
	/* Just a placeholder for now */
});
static const char *app = "MeetMe";
static const char *app2 = "MeetMeCount";
static const char *app3 = "MeetMeAdmin";
static const char *appslas = "SLAStation";
static const char *appslat = "SLATrunk";

static const char *synopsis = "MeetMe conference bridge";
static const char *synopsis2 = "MeetMe participant count";
static const char *synopsis3 = "MeetMe conference Administration";
static const char *synopslas = "Shared Line Appearance - Station";
static const char *synopslat = "Shared Line Appearance - Trunk";

static const char *descrip =
"  MeetMe([confno][,[options][,pin]]): Enters the user into a specified MeetMe\n"
"conference.  If the conference number is omitted, the user will be prompted\n"
"to enter one.  User can exit the conference by hangup, or if the 'p' option\n"
"is specified, by pressing '#'.\n"
"Please note: The Zaptel kernel modules and at least one hardware driver (or ztdummy)\n"
"             must be present for conferencing to operate properly. In addition, the chan_zap\n"
"             channel driver must be loaded for the 'i' and 'r' options to operate at all.\n\n"
"The option string may contain zero or more of the following characters:\n"
"      'a' -- set admin mode\n"
"      'A' -- set marked mode\n"
"      'b' -- run AGI script specified in ${MEETME_AGI_BACKGROUND}\n"
"             Default: conf-background.agi  (Note: This does not work with\n"
"             non-Zap channels in the same conference)\n"
"      'c' -- announce user(s) count on joining a conference\n"
"      'd' -- dynamically add conference\n"
"      'D' -- dynamically add conference, prompting for a PIN\n"
"      'e' -- select an empty conference\n"
"      'E' -- select an empty pinless conference\n"
"      'i' -- announce user join/leave with review\n"
"      'I' -- announce user join/leave without review\n"
"      'l' -- set listen only mode (Listen only, no talking)\n"
"      'm' -- set initially muted\n"
"      'M' -- enable music on hold when the conference has a single caller\n"
"      'o' -- set talker optimization - treats talkers who aren't speaking as\n"
"             being muted, meaning (a) No encode is done on transmission and\n"
"             (b) Received audio that is not registered as talking is omitted\n"
"             causing no buildup in background noise\n"
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
"      'w' -- wait until the marked user enters the conference\n"
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
"continue.\n"
"A ZAPTEL INTERFACE MUST BE INSTALLED FOR CONFERENCING FUNCTIONALITY.\n";

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
"      'T' -- Lower all users talk volume\n"
"      'u' -- Lower one user's listen volume\n"
"      'U' -- Lower all users listen volume\n"
"      'v' -- Lower entire conference listening volume\n"
"      'V' -- Raise entire conference listening volume\n"
"";

static const char *descripslas =
"  SLAStation(sla[,options]): Run Shared Line Appearance for station\n"
"Runs the share line appearance for a station calling in.  If there are no\n"
"other participants in the conference, the trunk is called and is dumped into\n"
"the bridge.\n";

static const char *descripslat =
"  SLATrunk(sla[,options]): Run Shared Line Appearance for trunk\n"
"Runs the share line appearance for a trunk calling in.  If there are no\n"
"other participants in the conference, all member stations are invited into\n"
"the bridge.\n";

#define CONFIG_FILE_NAME "meetme.conf"
#define CONFIG_FILE_NAME_SLA "sla.conf"

/*! \brief The MeetMe Conference object */
struct ast_conference {
	ast_mutex_t playlock;                   /*!< Conference specific lock (players) */
	ast_mutex_t listenlock;                 /*!< Conference specific lock (listeners) */
	char confno[AST_MAX_EXTENSION];         /*!< Conference */
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
	pthread_attr_t attr;                    /*!< thread attribute */
	const char *recordingfilename;          /*!< Filename to record the Conference into */
	const char *recordingformat;            /*!< Format to record the Conference in */
	char pin[AST_MAX_EXTENSION];            /*!< If protected by a PIN */
	char pinadmin[AST_MAX_EXTENSION];       /*!< If protected by a admin PIN */
	struct ast_frame *transframe[32];
	struct ast_frame *origframe;
	struct ast_trans_pvt *transpath[32];
	AST_LIST_HEAD_NOLOCK(, ast_conf_user) userlist;
	AST_LIST_ENTRY(ast_conference) list;
};

static AST_LIST_HEAD_STATIC(confs, ast_conference);

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
	int control;							/*! Queue Control for transmission */
	int dtmf;								/*! Queue DTMF for transmission */
	time_t jointime;                        /*!< Time the user joined the conference */
	struct volume talk;
	struct volume listen;
	AST_LIST_ENTRY(ast_conf_user) list;
};

/*! SLA station - one device in an SLA configuration */
struct ast_sla_station {
	ASTOBJ_COMPONENTS(struct ast_sla_station);
	char *dest;
	char tech[0];
};

struct ast_sla_station_box {
	ASTOBJ_CONTAINER_COMPONENTS(struct ast_sla_station);
};

/*! SLA - Shared Line Apperance object. These consist of one trunk (outbound line)
	and stations that receive incoming calls and place outbound calls over the trunk 
*/
struct ast_sla {
	ASTOBJ_COMPONENTS (struct ast_sla);
	struct ast_sla_station_box stations;	/*!< Stations connected to this SLA */
	char confname[80];	/*!< Name for this SLA bridge */
	char trunkdest[256];	/*!< Device (channel) identifier for the trunk line */
	char trunktech[20];	/*!< Technology used for the trunk (channel driver) */
};

struct ast_sla_box {
	ASTOBJ_CONTAINER_COMPONENTS(struct ast_sla);
} slas;

static int audio_buffers;			/*!< The number of audio buffers to be allocated on pseudo channels
						   when in a conference
						*/
/*! The number of audio buffers to be allocated on pseudo channels
 *  when in a conference */
static int audio_buffers;

/*! Map 'volume' levels from -5 through +5 into
 *  decibel (dB) settings for channel drivers
 *  Note: these are not a straight linear-to-dB
 *  conversion... the numbers have been modified
 *  to give the user a better level of adjustability
 */
static signed char gain_map[] = {
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
			x = ZT_IOMUX_WRITE | ZT_IOMUX_SIGEVENT;
			res = ioctl(fd, ZT_IOMUX, &x);
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
	signed char gain_adjust;

	/* attempt to make the adjustment in the channel driver;
	   if successful, don't adjust in the frame reading routine
	*/
	gain_adjust = gain_map[volume + 5];

	return ast_channel_setoption(user->chan, AST_OPTION_RXGAIN, &gain_adjust, sizeof(gain_adjust), 0);
}

static int set_listen_volume(struct ast_conf_user *user, int volume)
{
	signed char gain_adjust;

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

static void station_destroy(struct ast_sla_station *station)
{
	free(station);
}

static void sla_destroy(struct ast_sla *sla)
{
	ASTOBJ_CONTAINER_DESTROYALL(&sla->stations, station_destroy);
	ASTOBJ_CONTAINER_DESTROY(&sla->stations);
	free(sla);
}

static struct ast_conference *build_conf(char *confno, char *pin, char *pinadmin, int make, int dynamic, int refcount)
{
	struct ast_conference *cnf;
	struct zt_confinfo ztc;

	AST_LIST_LOCK(&confs);

	AST_LIST_TRAVERSE(&confs, cnf, list) {
		if (!strcmp(confno, cnf->confno)) 
			break;
	}

	if (!cnf && (make || dynamic)) {
		/* Make a new one */
		if ((cnf = ast_calloc(1, sizeof(*cnf)))) {
			ast_mutex_init(&cnf->playlock);
			ast_mutex_init(&cnf->listenlock);
			ast_copy_string(cnf->confno, confno, sizeof(cnf->confno));
			ast_copy_string(cnf->pin, pin, sizeof(cnf->pin));
			ast_copy_string(cnf->pinadmin, pinadmin, sizeof(cnf->pinadmin));
			cnf->refcount = 0;
			cnf->markedusers = 0;
			cnf->chan = ast_request("zap", AST_FORMAT_SLINEAR, "pseudo", NULL);
			if (cnf->chan) {
				ast_set_read_format(cnf->chan, AST_FORMAT_SLINEAR);
				ast_set_write_format(cnf->chan, AST_FORMAT_SLINEAR);
				cnf->fd = cnf->chan->fds[0];	/* for use by conf_play() */
			} else {
				ast_log(LOG_WARNING, "Unable to open pseudo channel - trying device\n");
				cnf->fd = open("/dev/zap/pseudo", O_RDWR);
				if (cnf->fd < 0) {
					ast_log(LOG_WARNING, "Unable to open pseudo device\n");
					free(cnf);
					cnf = NULL;
					goto cnfout;
				}
			}
			memset(&ztc, 0, sizeof(ztc));
			/* Setup a new zap conference */
			ztc.chan = 0;
			ztc.confno = -1;
			ztc.confmode = ZT_CONF_CONFANN | ZT_CONF_CONFANNMON;
			if (ioctl(cnf->fd, ZT_SETCONF, &ztc)) {
				ast_log(LOG_WARNING, "Error setting conference\n");
				if (cnf->chan)
					ast_hangup(cnf->chan);
				else
					close(cnf->fd);
				free(cnf);
				cnf = NULL;
				goto cnfout;
			}
			cnf->lchan = ast_request("zap", AST_FORMAT_SLINEAR, "pseudo", NULL);
			if (cnf->lchan) {
				ast_set_read_format(cnf->lchan, AST_FORMAT_SLINEAR);
				ast_set_write_format(cnf->lchan, AST_FORMAT_SLINEAR);
				ztc.chan = 0;
				ztc.confmode = ZT_CONF_CONFANN | ZT_CONF_CONFANNMON;
				if (ioctl(cnf->lchan->fds[0], ZT_SETCONF, &ztc)) {
					ast_log(LOG_WARNING, "Error setting conference\n");
					ast_hangup(cnf->lchan);
					cnf->lchan = NULL;
				}
			}
			/* Fill the conference struct */
			cnf->start = time(NULL);
			cnf->zapconf = ztc.confno;
			cnf->isdynamic = dynamic ? 1 : 0;
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Created MeetMe conference %d for conference '%s'\n", cnf->zapconf, cnf->confno);
			AST_LIST_INSERT_HEAD(&confs, cnf, list);
		} 
	}
 cnfout:
	if (cnf){ 
		cnf->refcount += refcount;
	}
	AST_LIST_UNLOCK(&confs);
	return cnf;
}

static int confs_show(int fd, int argc, char **argv)
{
	ast_cli(fd, "Deprecated! Please use 'meetme' instead.\n");

	return RESULT_SUCCESS;
}

/*! \brief CLI command for showing SLAs */
static int sla_show(int fd, int argc, char *argv[]) 
{
	struct ast_sla *sla;
	if (argc != 2)
		return RESULT_SHOWUSAGE;

	ast_cli(fd, "Shared line appearances:\n");
	ASTOBJ_CONTAINER_TRAVERSE(&slas, 1, {
		ASTOBJ_RDLOCK(iterator);
		ast_cli(fd, "SLA %s\n", iterator->name);
		if (ast_strlen_zero(iterator->trunkdest) || ast_strlen_zero(iterator->trunktech))
			ast_cli(fd, "   Trunk => <unspecified>\n");
		else
			ast_cli(fd, "   Trunk => %s/%s\n", iterator->trunktech, iterator->trunkdest);
		sla = iterator;
		ASTOBJ_CONTAINER_TRAVERSE(&sla->stations, 1, {
			ast_cli(fd, "   Station: %s/%s\n", iterator->tech, iterator->dest);
		});
		ASTOBJ_UNLOCK(iterator);
	});

	return RESULT_SUCCESS;
}

static char show_confs_usage[] =
"Deprecated! Please use 'meetme' instead.\n";

static struct ast_cli_entry cli_show_confs = {
	{ "show", "conferences", NULL }, confs_show,
	"Show status of conferences", show_confs_usage, NULL };


static char sla_show_usage[] =
"Usage: sla show\n"
"       Lists status of all shared line appearances\n";

static struct ast_cli_entry cli_sla_show = {
	{ "sla", "show", NULL }, sla_show,
	"Show status of Shared Line Appearances", sla_show_usage, NULL };

static int conf_cmd(int fd, int argc, char **argv) 
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
		if (AST_LIST_EMPTY(&confs)) {
			ast_cli(fd, "No active MeetMe conferences.\n");
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
		int concise = ( 4 == argc && ( !strcasecmp(argv[3], "concise") ) );
		/* List all the users in a conference */
		if (AST_LIST_EMPTY(&confs)) {
			if ( !concise )
				ast_cli(fd, "No active conferences.\n");
			return RESULT_SUCCESS;	
		}
		/* Find the right conference */
		AST_LIST_TRAVERSE(&confs, cnf, list) {
			if (strcmp(cnf->confno, argv[2]) == 0)
				break;
		}
		if (!cnf) {
			if ( !concise )
				ast_cli(fd, "No such conference: %s.\n",argv[2]);
			return RESULT_SUCCESS;
		}
		/* Show all the users */
		AST_LIST_TRAVERSE(&cnf->userlist, user, list) {
			now = time(NULL);
			hr = (now - user->jointime) / 3600;
			min = ((now - user->jointime) % 3600) / 60;
			sec = (now - user->jointime) % 60;
			if ( !concise )
				ast_cli(fd, "User #: %-2.2d %12.12s %-20.20s Channel: %s %s %s %s %s %s %02d:%02d:%02d\n",
					user->user_no,
					S_OR(user->chan->cid.cid_num, "<unknown>"),
					S_OR(user->chan->cid.cid_name, "<no name>"),
					user->chan->name,
					user->userflags & CONFFLAG_ADMIN ? "(Admin)" : "",
					user->userflags & CONFFLAG_MONITOR ? "(Listen only)" : "",
					user->adminflags & ADMINFLAG_MUTED ? "(Admin Muted)" : user->adminflags & ADMINFLAG_SELFMUTED ? "(Muted)" : "",
					istalking(user->talking), 
					user->userflags & CONFFLAG_HOLD ? " (On Hold) " : "", hr, min, sec);
			else 
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
		if ( !concise )
			ast_cli(fd,"%d users in that conference.\n",cnf->users);

		return RESULT_SUCCESS;
	} else 
		return RESULT_SHOWUSAGE;
	ast_log(LOG_DEBUG, "Cmdline: %s\n", cmdline);
	admin_exec(NULL, cmdline);

	return 0;
}

static char *complete_confcmd(const char *line, const char *word, int pos, int state)
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
				/* Search for the user */
				AST_LIST_TRAVERSE(&cnf->userlist, usr, list) {
					snprintf(usrno, sizeof(usrno), "%d", usr->user_no);
					if (!strncasecmp(word, usrno, len) && ++which > state)
						break;
				}
			}
			AST_LIST_UNLOCK(&confs);
			return usr ? strdup(usrno) : NULL;
		} else if ( strstr(line, "list") && ( 0 == state ) )
			return strdup("concise");
	}

	return NULL;
}
	
static char conf_usage[] =
"Usage: meetme  (un)lock|(un)mute|kick|list [concise] <confno> <usernumber>\n"
"       Executes a command for the conference or on a conferee\n";

static struct ast_cli_entry cli_conf = {
	{"meetme", NULL, NULL }, conf_cmd,
	"Execute a command on a conference or conferee", conf_usage, complete_confcmd};

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
	x = ZT_FLUSH_ALL;
	if (ioctl(fd, ZT_FLUSH, &x))
		ast_log(LOG_WARNING, "Error flushing channel\n");

}

/* Remove the conference from the list and free it.
   We assume that this was called while holding conflock. */
static int conf_free(struct ast_conference *conf)
{
	int x;
	
	AST_LIST_REMOVE(&confs, conf, list);

	if (conf->recording == MEETME_RECORD_ACTIVE) {
		conf->recording = MEETME_RECORD_TERMINATE;
		AST_LIST_UNLOCK(&confs);
		while (1) {
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
	if (conf->origframe)
		ast_frfree(conf->origframe);
	if (conf->lchan)
		ast_hangup(conf->lchan);
	if (conf->chan)
		ast_hangup(conf->chan);
	else
		close(conf->fd);
	
	free(conf);

	return 0;
}

static void conf_queue_dtmf(struct ast_conference *conf, int digit)
{
	struct ast_conf_user *user;
	AST_LIST_TRAVERSE(&conf->userlist, user, list) {
		user->dtmf = digit;
	}
}

static void conf_queue_control(struct ast_conference *conf, int control)
{
	struct ast_conf_user *user;
	AST_LIST_TRAVERSE(&conf->userlist, user, list) {
		user->control = control;
	}
}


static int conf_run(struct ast_channel *chan, struct ast_conference *conf, int confflags)
{
	struct ast_conf_user *user = NULL;
	struct ast_conf_user *usr = NULL;
	int fd;
	struct zt_confinfo ztc, ztc_empty;
	struct ast_frame *f;
	struct ast_channel *c;
	struct ast_frame fr;
	int outfd;
	int ms;
	int nfds;
	int res;
	int flags;
	int retryzap;
	int origfd;
	int musiconhold = 0;
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
	int dtmf;
	ZT_BUFFERINFO bi;
	char __buf[CONF_SIZE + AST_FRIENDLY_OFFSET];
	char *buf = __buf + AST_FRIENDLY_OFFSET;

	if (!(user = ast_calloc(1, sizeof(*user)))) {
		AST_LIST_LOCK(&confs);
		conf->refcount--;
		if (!conf->refcount){
			conf_free(conf);
		}
		AST_LIST_UNLOCK(&confs);
		return ret;
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

	if ((conf->recording == MEETME_RECORD_OFF) && ((confflags & CONFFLAG_RECORDCONF) || (conf->lchan))) {
		pthread_attr_init(&conf->attr);
		pthread_attr_setdetachstate(&conf->attr, PTHREAD_CREATE_DETACHED);
		ast_pthread_create(&conf->recordthread, &conf->attr, recordthread, conf);
	}

	time(&user->jointime);

	if (conf->locked && (!(confflags & CONFFLAG_ADMIN))) {
		/* Sorry, but this confernce is locked! */	
		if (!ast_streamfile(chan, "conf-locked", chan->language))
			ast_waitstream(chan, "");
		goto outrun;
	}

	if (confflags & CONFFLAG_MARKEDUSER)
		conf->markedusers++;
      
   	ast_mutex_lock(&conf->playlock);

	if (AST_LIST_EMPTY(&conf->userlist))
		user->user_no = 1;
	else
		user->user_no = AST_LIST_LAST(&conf->userlist)->user_no + 1;

	AST_LIST_INSERT_TAIL(&conf->userlist, user, list);

	user->chan = chan;
	user->userflags = confflags;
	user->adminflags = (confflags & CONFFLAG_STARTMUTED) ? ADMINFLAG_SELFMUTED : 0;
	user->talking = -1;
	conf->users++;
	/* Update table */
	snprintf(members, sizeof(members), "%d", conf->users);
	ast_update_realtime("meetme", "confno", conf->confno, "members", members , NULL);

	/* This device changed state now - if this is the first user */
	if (conf->users == 1)
		ast_device_state_changed("meetme:%s", conf->confno);
	if (confflags & (CONFFLAG_SLA_STATION|CONFFLAG_SLA_TRUNK))
		ast_device_state_changed("SLA:%s", conf->confno + 4);

	ast_mutex_unlock(&conf->playlock);

	if (confflags & CONFFLAG_EXIT_CONTEXT) {
		if ((agifile = pbx_builtin_getvar_helper(chan, "MEETME_EXIT_CONTEXT"))) 
			ast_copy_string(exitcontext, agifile, sizeof(exitcontext));
		else if (!ast_strlen_zero(chan->macrocontext)) 
			ast_copy_string(exitcontext, chan->macrocontext, sizeof(exitcontext));
		else
			ast_copy_string(exitcontext, chan->context, sizeof(exitcontext));
	}

	if (!(confflags & CONFFLAG_QUIET) && ((confflags & CONFFLAG_INTROUSER) || (confflags & CONFFLAG_INTROUSERNOREVIEW))) {
		snprintf(user->namerecloc, sizeof(user->namerecloc),
			 "%s/meetme/meetme-username-%s-%d", ast_config_AST_SPOOL_DIR,
			 conf->confno, user->user_no);
		if (confflags & CONFFLAG_INTROUSERNOREVIEW)
			res = ast_play_and_record(chan, "vm-rec-name", user->namerecloc, 10, "sln", &duration, 128, 0, NULL);
		else
			res = ast_record_review(chan, "vm-rec-name", user->namerecloc, 10, "sln", &duration, NULL);
		if (res == -1)
			goto outrun;
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
				if (res > 0)
					keepplaying=0;
				else if (res == -1)
					goto outrun;
			}
		} else { 
			if (!ast_streamfile(chan, "conf-thereare", chan->language)) {
				res = ast_waitstream(chan, AST_DIGIT_ANY);
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
				if (res > 0)
					keepplaying=0;
				else if (res == -1) 
					goto outrun;
			}
		}
	}

	ast_indicate(chan, -1);

	if (ast_set_write_format(chan, AST_FORMAT_SLINEAR) < 0) {
		ast_log(LOG_WARNING, "Unable to set '%s' to write linear mode\n", chan->name);
		goto outrun;
	}

	if (ast_set_read_format(chan, AST_FORMAT_SLINEAR) < 0) {
		ast_log(LOG_WARNING, "Unable to set '%s' to read linear mode\n", chan->name);
		goto outrun;
	}

	retryzap = strcasecmp(chan->tech->type, "Zap");
	user->zapchannel = !retryzap;

 zapretry:
	origfd = chan->fds[0];
	if (retryzap) {
		fd = open("/dev/zap/pseudo", O_RDWR);
		if (fd < 0) {
			ast_log(LOG_WARNING, "Unable to open pseudo channel: %s\n", strerror(errno));
			goto outrun;
		}
		using_pseudo = 1;
		/* Make non-blocking */
		flags = fcntl(fd, F_GETFL);
		if (flags < 0) {
			ast_log(LOG_WARNING, "Unable to get flags: %s\n", strerror(errno));
			close(fd);
			goto outrun;
		}
		if (fcntl(fd, F_SETFL, flags | O_NONBLOCK)) {
			ast_log(LOG_WARNING, "Unable to set flags: %s\n", strerror(errno));
			close(fd);
			goto outrun;
		}
		/* Setup buffering information */
		memset(&bi, 0, sizeof(bi));
		bi.bufsize = CONF_SIZE/2;
		bi.txbufpolicy = ZT_POLICY_IMMEDIATE;
		bi.rxbufpolicy = ZT_POLICY_IMMEDIATE;
		bi.numbufs = audio_buffers;
		if (ioctl(fd, ZT_SET_BUFINFO, &bi)) {
			ast_log(LOG_WARNING, "Unable to set buffering information: %s\n", strerror(errno));
			close(fd);
			goto outrun;
		}
		x = 1;
		if (ioctl(fd, ZT_SETLINEAR, &x)) {
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
	if (ioctl(fd, ZT_GETCONF, &ztc)) {
		ast_log(LOG_WARNING, "Error getting conference\n");
		close(fd);
		goto outrun;
	}
	if (ztc.confmode) {
		/* Whoa, already in a conference...  Retry... */
		if (!retryzap) {
			ast_log(LOG_DEBUG, "Zap channel is in a conference already, retrying with pseudo\n");
			retryzap = 1;
			goto zapretry;
		}
	}
	memset(&ztc, 0, sizeof(ztc));
	/* Add us to the conference */
	ztc.chan = 0;	
	ztc.confno = conf->zapconf;

	ast_mutex_lock(&conf->playlock);

	if (!(confflags & CONFFLAG_QUIET) && ((confflags & CONFFLAG_INTROUSER) || (confflags & CONFFLAG_INTROUSERNOREVIEW)) && conf->users > 1) {
		if (conf->chan && ast_fileexists(user->namerecloc, NULL, NULL)) {
			if (!ast_streamfile(conf->chan, user->namerecloc, chan->language))
				ast_waitstream(conf->chan, "");
			if (!ast_streamfile(conf->chan, "conf-hasjoin", chan->language))
				ast_waitstream(conf->chan, "");
		}
	}

	if (confflags & CONFFLAG_MONITOR)
		ztc.confmode = ZT_CONF_CONFMON | ZT_CONF_LISTENER;
	else if (confflags & CONFFLAG_TALKER)
		ztc.confmode = ZT_CONF_CONF | ZT_CONF_TALKER;
	else 
		ztc.confmode = ZT_CONF_CONF | ZT_CONF_TALKER | ZT_CONF_LISTENER;

	if (ioctl(fd, ZT_SETCONF, &ztc)) {
		ast_log(LOG_WARNING, "Error setting conference\n");
		close(fd);
		ast_mutex_unlock(&conf->playlock);
		goto outrun;
	}
	ast_log(LOG_DEBUG, "Placed channel %s in ZAP conf %d\n", chan->name, conf->zapconf);

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

	ast_mutex_unlock(&conf->playlock);

	conf_flush(fd, chan);

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
		if (confflags & (CONFFLAG_MONITORTALKER | CONFFLAG_OPTIMIZETALKER) && !(dsp = ast_dsp_new())) {
			ast_log(LOG_WARNING, "Unable to allocate DSP!\n");
			res = -1;
		}
		for(;;) {
			int menu_was_active = 0;

			outfd = -1;
			ms = -1;
			
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

			c = ast_waitfor_nandfds(&chan, 1, &fd, nfds, NULL, &outfd, &ms);
			
			
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
							ztc.confmode = ZT_CONF_CONF;
							if (ioctl(fd, ZT_SETCONF, &ztc)) {
								ast_log(LOG_WARNING, "Error setting conference\n");
								close(fd);
								goto outrun;
							}
						}
					}
					if (musiconhold == 0 && (confflags & CONFFLAG_MOH)) {
						ast_moh_start(chan, NULL, NULL);
						musiconhold = 1;
					} else {
						ztc.confmode = ZT_CONF_CONF;
						if (ioctl(fd, ZT_SETCONF, &ztc)) {
							ast_log(LOG_WARNING, "Error setting conference\n");
							close(fd);
							goto outrun;
						}
					}
				} else if(currentmarked >= 1 && lastmarked == 0) {
					if (confflags & CONFFLAG_MONITOR)
						ztc.confmode = ZT_CONF_CONFMON | ZT_CONF_LISTENER;
					else if (confflags & CONFFLAG_TALKER)
						ztc.confmode = ZT_CONF_CONF | ZT_CONF_TALKER;
					else
						ztc.confmode = ZT_CONF_CONF | ZT_CONF_TALKER | ZT_CONF_LISTENER;
					if (ioctl(fd, ZT_SETCONF, &ztc)) {
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
			if ((user->adminflags & (ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED)) && (ztc.confmode & ZT_CONF_TALKER)) {
				ztc.confmode ^= ZT_CONF_TALKER;
				if (ioctl(fd, ZT_SETCONF, &ztc)) {
					ast_log(LOG_WARNING, "Error setting conference - Un/Mute \n");
					ret = -1;
					break;
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
			if (!(user->adminflags & (ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED)) && !(confflags & CONFFLAG_MONITOR) && !(ztc.confmode & ZT_CONF_TALKER)) {
				ztc.confmode |= ZT_CONF_TALKER;
				if (ioctl(fd, ZT_SETCONF, &ztc)) {
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
				if (!ast_streamfile(chan, "conf-kicked", chan->language))
					ast_waitstream(chan, "");
				ret = 0;
				break;
			}

			if (c) {
				if (c->fds[0] != origfd) {
					if (using_pseudo) {
						/* Kill old pseudo */
						close(fd);
						using_pseudo = 0;
					}
					ast_log(LOG_DEBUG, "Ooh, something swapped out under us, starting over\n");
					retryzap = strcasecmp(c->tech->type, "Zap");
					user->zapchannel = !retryzap;
					goto zapretry;
				}
				if ((confflags & CONFFLAG_MONITOR) || (user->adminflags & (ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED)))
					f = ast_read_noaudio(c);
				else
					f = ast_read(c);
				if (!f)
					break;
				if ((f->frametype == AST_FRAME_VOICE) && (f->subclass == AST_FORMAT_SLINEAR)) {
					if (user->talk.actual)
						ast_frame_adjust_volume(f, user->talk.actual);

					if (confflags & (CONFFLAG_MONITORTALKER | CONFFLAG_OPTIMIZETALKER)) {
						int totalsilence;

						if (user->talking == -1)
							user->talking = 0;

						res = ast_dsp_silence(dsp, f, &totalsilence);
						if (!user->talking && totalsilence < MEETME_DELAYDETECTTALK) {
							user->talking = 1;
							if (confflags & CONFFLAG_MONITORTALKER)
								manager_event(EVENT_FLAG_CALL, "MeetmeTalking",
								      "Channel: %s\r\n"
								      "Uniqueid: %s\r\n"
								      "Meetme: %s\r\n"
								      "Usernum: %d\r\n"
								      "Status: on\r\n",
								      chan->name, chan->uniqueid, conf->confno, user->user_no);
						}
						if (user->talking && totalsilence > MEETME_DELAYDETECTENDTALK) {
							user->talking = 0;
							if (confflags & CONFFLAG_MONITORTALKER)
								manager_event(EVENT_FLAG_CALL, "MeetmeTalking",
								      "Channel: %s\r\n"
								      "Uniqueid: %s\r\n"
								      "Meetme: %s\r\n"
								      "Usernum: %d\r\n"
								      "Status: off\r\n",
								      chan->name, chan->uniqueid, conf->confno, user->user_no);
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
				} else if ((f->frametype == AST_FRAME_DTMF) && 
							(confflags & (CONFFLAG_SLA_STATION|CONFFLAG_SLA_TRUNK))) {
					conf_queue_dtmf(conf, f->subclass);
				} else if ((f->frametype == AST_FRAME_CONTROL) && 
							(confflags & (CONFFLAG_SLA_STATION|CONFFLAG_SLA_TRUNK))) {
					conf_queue_control(conf, f->subclass);
					if (f->subclass == AST_CONTROL_HOLD)
						confflags |= CONFFLAG_HOLD;
					else if (f->subclass == AST_CONTROL_UNHOLD)
						confflags &= ~CONFFLAG_HOLD;
					user->userflags = confflags;
				} else if ((f->frametype == AST_FRAME_DTMF) && (confflags & CONFFLAG_EXIT_CONTEXT)) {
					char tmp[2];

					tmp[0] = f->subclass;
					tmp[1] = '\0';
					if (!ast_goto_if_exists(chan, exitcontext, tmp, 1)) {
						ast_log(LOG_DEBUG, "Got DTMF %c, goto context %s\n", tmp[0], exitcontext);
						ret = 0;
						ast_frfree(f);
						break;
					} else if (option_debug > 1)
						ast_log(LOG_DEBUG, "Exit by single digit did not work in meetme. Extension %s does not exist in context %s\n", tmp, exitcontext);
				} else if ((f->frametype == AST_FRAME_DTMF) && (f->subclass == '#') && (confflags & CONFFLAG_POUNDEXIT)) {
					ret = 0;
					ast_frfree(f);
					break;
				} else if (((f->frametype == AST_FRAME_DTMF) && (f->subclass == '*') && (confflags & CONFFLAG_STARMENU)) || ((f->frametype == AST_FRAME_DTMF) && menu_active)) {
					if (ioctl(fd, ZT_SETCONF, &ztc_empty)) {
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
								menu_active = 0;
								usr = AST_LIST_LAST(&conf->userlist);
								if ((usr->chan->name == chan->name)||(usr->userflags & CONFFLAG_ADMIN)) {
									if(!ast_streamfile(chan, "conf-errormenu", chan->language))
										ast_waitstream(chan, "");
								} else 
									usr->adminflags |= ADMINFLAG_KICKME;
								ast_stopstream(chan);
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
					if (musiconhold)
			   			ast_moh_start(chan, NULL, NULL);

					if (ioctl(fd, ZT_SETCONF, &ztc)) {
						ast_log(LOG_WARNING, "Error setting conference\n");
						close(fd);
						ast_frfree(f);
						goto outrun;
					}

					conf_flush(fd, chan);
				} else if (option_debug) {
					ast_log(LOG_DEBUG,
						"Got unrecognized frame on channel %s, f->frametype=%d,f->subclass=%d\n",
						chan->name, f->frametype, f->subclass);
				}
				ast_frfree(f);
			} else if (outfd > -1) {
				if (user->control) {
					switch(user->control) {
					case AST_CONTROL_RINGING:
					case AST_CONTROL_PROGRESS:
					case AST_CONTROL_PROCEEDING:
						ast_indicate(chan, user->control);
						break;
					case AST_CONTROL_ANSWER:
						if (chan->_state != AST_STATE_UP)
							ast_answer(chan);
						break;
					}
					user->control = 0;
					if (confflags & (CONFFLAG_SLA_STATION|CONFFLAG_SLA_TRUNK))
						ast_device_state_changed("SLA:%s", conf->confno + 4);
					continue;
				}
				if (user->dtmf) {
					memset(&fr, 0, sizeof(fr));
					fr.frametype = AST_FRAME_DTMF;
					fr.subclass = user->dtmf;
					if (ast_write(chan, &fr) < 0) {
						ast_log(LOG_WARNING, "Unable to write frame to channel: %s\n", strerror(errno));
					}
					user->dtmf = 0;
					continue;
				}
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
 							if (conf->transframe[index]->frametype != AST_FRAME_NULL) {
	 							if (ast_write(chan, conf->transframe[index]))
									ast_log(LOG_WARNING, "Unable to write frame to channel: %s\n", strerror(errno));
							}
						} else {
							ast_mutex_unlock(&conf->listenlock);
							goto bailoutandtrynormal;
						}
						ast_mutex_unlock(&conf->listenlock);
					} else {
bailoutandtrynormal:					
						if (user->listen.actual)
							ast_frame_adjust_volume(&fr, user->listen.actual);
						if (ast_write(chan, &fr) < 0) {
							ast_log(LOG_WARNING, "Unable to write frame to channel: %s\n", strerror(errno));
						}
					}
				} else 
					ast_log(LOG_WARNING, "Failed to read frame: %s\n", strerror(errno));
			}
			lastmarked = currentmarked;
		}
	}

	if (musiconhold)
		ast_moh_stop(chan);
	
	if (using_pseudo)
		close(fd);
	else {
		/* Take out of conference */
		ztc.chan = 0;	
		ztc.confno = 0;
		ztc.confmode = 0;
		if (ioctl(fd, ZT_SETCONF, &ztc)) {
			ast_log(LOG_WARNING, "Error setting conference\n");
		}
	}

	reset_volumes(user);

	AST_LIST_LOCK(&confs);
	if (!(confflags & CONFFLAG_QUIET) && !(confflags & CONFFLAG_MONITOR) && !(confflags & CONFFLAG_ADMIN))
		conf_play(chan, conf, LEAVE);

	if (!(confflags & CONFFLAG_QUIET) && ((confflags & CONFFLAG_INTROUSER) || (confflags & CONFFLAG_INTROUSERNOREVIEW))) {
		if (ast_fileexists(user->namerecloc, NULL, NULL)) {
			if ((conf->chan) && (conf->users > 1)) {
				if (!ast_streamfile(conf->chan, user->namerecloc, chan->language))
					ast_waitstream(conf->chan, "");
				if (!ast_streamfile(conf->chan, "conf-hasleft", chan->language))
					ast_waitstream(conf->chan, "");
			}
			ast_filedelete(user->namerecloc, NULL);
		}
	}
	AST_LIST_UNLOCK(&confs);

 outrun:
	AST_LIST_LOCK(&confs);

	if (dsp)
		ast_dsp_free(dsp);
	
	if (user->user_no) { /* Only cleanup users who really joined! */
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
				      (now - user->jointime));
		}

		conf->users--;
		conf->refcount--;
		/* Update table */
		snprintf(members, sizeof(members), "%d", conf->users);
		ast_update_realtime("meetme", "confno", conf->confno, "members", members, NULL);
		if (confflags & CONFFLAG_MARKEDUSER) 
			conf->markedusers--;
		/* Remove ourselves from the list */
		AST_LIST_REMOVE(&conf->userlist, user, list);
		if (AST_LIST_EMPTY(&conf->userlist)) {
			/* close this one when no more users and no references*/
			if (!conf->refcount)
				conf_free(conf);
		}
		/* Return the number of seconds the user was in the conf */
		snprintf(meetmesecs, sizeof(meetmesecs), "%d", (int) (time(NULL) - user->jointime));
		pbx_builtin_setvar_helper(chan, "MEETMESECS", meetmesecs);

		/* This device changed state now */
		if (!conf->users)	/* If there are no more members */
			ast_device_state_changed("meetme:%s", conf->confno);
		if (confflags & (CONFFLAG_SLA_STATION|CONFFLAG_SLA_TRUNK))
			ast_device_state_changed("SLA:%s", conf->confno + 4);
	}
	free(user);
	AST_LIST_UNLOCK(&confs);

	return ret;
}

static struct ast_conference *find_conf_realtime(struct ast_channel *chan, char *confno, int make, int dynamic,
						 char *dynamic_pin, int refcount, struct ast_flags *confflags)
{
	struct ast_variable *var;
	struct ast_conference *cnf;

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
		char *pin = NULL, *pinadmin = NULL; /* For temp use */

		cnf = ast_calloc(1, sizeof(struct ast_conference));
		if (!cnf) {
			ast_log(LOG_ERROR, "Out of memory\n");
			return NULL;
		}

		var = ast_load_realtime("meetme", "confno", confno, NULL);
		while (var) {
			if (!strcasecmp(var->name, "confno")) {
				ast_copy_string(cnf->confno, var->value, sizeof(cnf->confno));
			} else if (!strcasecmp(var->name, "pin")) {
				pin = ast_strdupa(var->value);
			} else if (!strcasecmp(var->name, "adminpin")) {
				pinadmin = ast_strdupa(var->value);
			}
			var = var->next;
		}
		ast_variables_destroy(var);

		cnf = build_conf(confno, pin ? pin : "", pinadmin ? pinadmin : "", make, dynamic, refcount);
	}

	if (cnf) {
		if (confflags && !cnf->chan &&
		    !ast_test_flag(confflags, CONFFLAG_QUIET) &&
		    ast_test_flag(confflags, CONFFLAG_INTROUSER)) {
			ast_log(LOG_WARNING, "No Zap channel available for conference, user introduction disabled (is chan_zap loaded?)\n");
			ast_clear_flag(confflags, CONFFLAG_INTROUSER);
		}
		
		if (confflags && !cnf->chan &&
		    ast_test_flag(confflags, CONFFLAG_RECORDCONF)) {
			ast_log(LOG_WARNING, "No Zap channel available for conference, conference recording disabled (is chan_zap loaded?)\n");
			ast_clear_flag(confflags, CONFFLAG_RECORDCONF);
		}
	}

	return cnf;
}


static struct ast_conference *find_conf(struct ast_channel *chan, char *confno, int make, int dynamic,
					char *dynamic_pin, int refcount, struct ast_flags *confflags)
{
	struct ast_config *cfg;
	struct ast_variable *var;
	struct ast_conference *cnf;
	char *parse;
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
					if (ast_app_getdata(chan, "conf-getpin", dynamic_pin, AST_MAX_EXTENSION - 1, 0) < 0)
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
				if (strcasecmp(var->name, "conf"))
					continue;
				
				if (!(parse = ast_strdupa(var->value)))
					return NULL;
				
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
		    ast_test_flag(confflags, CONFFLAG_INTROUSER)) {
			ast_log(LOG_WARNING, "No Zap channel available for conference, user introduction disabled (is chan_zap loaded?)\n");
			ast_clear_flag(confflags, CONFFLAG_INTROUSER);
		}
		
		if (confflags && !cnf->chan &&
		    ast_test_flag(confflags, CONFFLAG_RECORDCONF)) {
			ast_log(LOG_WARNING, "No Zap channel available for conference, conference recording disabled (is chan_zap loaded?)\n");
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
	
	conf = find_conf(chan, args.confno, 0, 0, NULL, 0, NULL);

	if (conf)
		count = conf->users;
	else
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
	char confno[AST_MAX_EXTENSION] = "";
	int allowretry = 0;
	int retrycnt = 0;
	struct ast_conference *cnf;
	struct ast_flags confflags = {0};
	int dynamic = 0;
	int empty = 0, empty_no_pin = 0;
	int always_prompt = 0;
	char *notdata, *info, the_pin[AST_MAX_EXTENSION] = "";
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(confno);
		AST_APP_ARG(options);
		AST_APP_ARG(pin);
	);

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
		ast_app_parse_options(meetme_opts, &confflags, NULL, args.options);
		dynamic = ast_test_flag(&confflags, CONFFLAG_DYNAMIC | CONFFLAG_DYNAMICPIN);
		if (ast_test_flag(&confflags, CONFFLAG_DYNAMICPIN) && !args.pin)
			strcpy(the_pin, "q");

		empty = ast_test_flag(&confflags, CONFFLAG_EMPTY | CONFFLAG_EMPTYNOPIN);
		empty_no_pin = ast_test_flag(&confflags, CONFFLAG_EMPTYNOPIN);
		always_prompt = ast_test_flag(&confflags, CONFFLAG_ALWAYSPROMPT);
	}

	do {
		if (retrycnt > 3)
			allowretry = 0;
		if (empty) {
			int i, map[1024] = { 0, };
			struct ast_config *cfg;
			struct ast_variable *var;
			int confno_int;

			AST_LIST_LOCK(&confs);
			AST_LIST_TRAVERSE(&confs, cnf, list) {
				if (sscanf(cnf->confno, "%d", &confno_int) == 1) {
					/* Disqualify in use conference */
					if (confno_int >= 0 && confno_int < 1024)
						map[confno_int]++;
				}
			}
			AST_LIST_UNLOCK(&confs);

			/* We only need to load the config file for static and empty_no_pin (otherwise we don't care) */
			if ((empty_no_pin) || (!dynamic)) {
				cfg = ast_config_load(CONFIG_FILE_NAME);
				if (cfg) {
					var = ast_variable_browse(cfg, "rooms");
					while (var) {
						if (!strcasecmp(var->name, "conf")) {
							char *stringp = ast_strdupa(var->value);
							if (stringp) {
								char *confno_tmp = strsep(&stringp, "|,");
								int found = 0;
								if (sscanf(confno_tmp, "%d", &confno_int) == 1) {
									if ((confno_int >= 0) && (confno_int < 1024)) {
										if (stringp && empty_no_pin) {
											map[confno_int]++;
										}
									}
								}
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
						}
						var = var->next;
					}
					ast_config_destroy(cfg);
				}
			}

			/* Select first conference number not in use */
			if (ast_strlen_zero(confno) && dynamic) {
				for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
					if (!map[i]) {
						snprintf(confno, sizeof(confno), "%d", i);
						break;
					}
				}
			}

			/* Not found? */
			if (ast_strlen_zero(confno)) {
				res = ast_streamfile(chan, "conf-noempty", chan->language);
				if (!res)
					ast_waitstream(chan, "");
			} else {
				if (sscanf(confno, "%d", &confno_int) == 1) {
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
			cnf = find_conf(chan, confno, 1, dynamic, the_pin, 1, &confflags);
			if (!cnf)
				cnf = find_conf_realtime(chan, confno, 1, dynamic, the_pin, 1, &confflags);

			if (!cnf) {
				res = ast_streamfile(chan, "conf-invalid", chan->language);
				if (!res)
					ast_waitstream(chan, "");
				res = -1;
				if (allowretry)
					confno[0] = '\0';
			} else {
				if ((!ast_strlen_zero(cnf->pin) &&
				     !ast_test_flag(&confflags, CONFFLAG_ADMIN)) ||
				    (!ast_strlen_zero(cnf->pinadmin) &&
				     ast_test_flag(&confflags, CONFFLAG_ADMIN))) {
					char pin[AST_MAX_EXTENSION]="";
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
							if (!strcasecmp(pin, cnf->pin) ||
							    (!ast_strlen_zero(cnf->pinadmin) &&
							     !strcasecmp(pin, cnf->pinadmin))) {
								/* Pin correct */
								allowretry = 0;
								if (!ast_strlen_zero(cnf->pinadmin) && !strcasecmp(pin, cnf->pinadmin)) 
									ast_set_flag(&confflags, CONFFLAG_ADMIN);
								/* Run the conference */
								res = conf_run(chan, cnf, confflags.flags);
								break;
							} else {
								/* Pin invalid */
								if (!ast_streamfile(chan, "conf-invalidpin", chan->language))
									res = ast_waitstream(chan, AST_DIGIT_ANY);
								else {
									ast_log(LOG_WARNING, "Couldn't play invalid pin msg!\n");
									break;
								}
								if (res < 0) {
									AST_LIST_LOCK(&confs);
									cnf->refcount--;
									if (!cnf->refcount){
										conf_free(cnf);
									}
									AST_LIST_UNLOCK(&confs);
									break;
								}
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
							AST_LIST_LOCK(&confs);
							cnf->refcount--;
							if (!cnf->refcount) {
								conf_free(cnf);
							}
							AST_LIST_UNLOCK(&confs);
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
					res = conf_run(chan, cnf, confflags.flags);
				}
			}
		}
	} while (allowretry);
	
	ast_module_user_remove(u);
	
	return res;
}

struct sla_originate_helper {
	char tech[100];
	char data[200];
	char app[20];
	char appdata[100];
	char cid_name[100];
	char cid_num[100];
};

static void *sla_originate(void *data)
{
	struct sla_originate_helper *in = data;
	int reason = 0;
	struct ast_channel *chan = NULL;

	ast_pbx_outgoing_app(in->tech, AST_FORMAT_SLINEAR, in->data, 99999, in->app, in->appdata, &reason, 1, 
		S_OR(in->cid_num, NULL), 
		S_OR(in->cid_name, NULL),
		NULL, NULL, &chan);
	/* Locked by ast_pbx_outgoing_exten or ast_pbx_outgoing_app */
	if (chan)
		ast_channel_unlock(chan);
	free(in);
	return NULL;
}

/*! Call in stations and trunk to the SLA */
static void do_invite(struct ast_channel *orig, const char *tech, const char *dest, const char *app, const char *data)
{
	struct sla_originate_helper *slal;
	pthread_attr_t attr;
	pthread_t th;

	if (!(slal = ast_calloc(1, sizeof(*slal))))
		return;
	
	ast_copy_string(slal->tech, tech, sizeof(slal->tech));
   	ast_copy_string(slal->data, dest, sizeof(slal->data));
	ast_copy_string(slal->app, app, sizeof(slal->app));
	ast_copy_string(slal->appdata, data, sizeof(slal->appdata));
	if (orig->cid.cid_num)
		ast_copy_string(slal->cid_num, orig->cid.cid_num, sizeof(slal->cid_num));
	if (orig->cid.cid_name)
		ast_copy_string(slal->cid_name, orig->cid.cid_name, sizeof(slal->cid_name));
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	ast_pthread_create(&th, &attr, sla_originate, slal);
}

static void invite_stations(struct ast_channel *orig, struct ast_sla *sla)
{
	ASTOBJ_CONTAINER_TRAVERSE(&sla->stations, 1, {
		do_invite(orig, iterator->tech, iterator->dest, "SLAStation", sla->name);
	});
}

static void invite_trunk(struct ast_channel *orig, struct ast_sla *sla)
{
	do_invite(orig, sla->trunktech, sla->trunkdest, "SLATrunk", sla->name);
}


static int sla_checkforhold(struct ast_conference *conf, int hangup)
{
	struct ast_conf_user *user;
	struct ast_channel *onhold=NULL;
	int holdcount = 0;
	int stationcount = 0;
	int amonhold = 0;
	AST_LIST_TRAVERSE(&conf->userlist, user, list) {
		if (user->userflags & CONFFLAG_SLA_STATION) {
			stationcount++;
			if ((user->userflags & CONFFLAG_HOLD)) {
				holdcount++;
				onhold = user->chan;
			}
		}
	}
	if ((holdcount == 1) && (stationcount == 1)) {
		amonhold = 1;
		if (hangup)
			ast_softhangup(onhold, AST_SOFTHANGUP_EXPLICIT);
	} else if (holdcount && (stationcount == holdcount))
		amonhold = 1;
	return amonhold;
}


/*! \brief The slas()/slat() application */
static int sla_exec(struct ast_channel *chan, void *data, int trunk)
{
	int res=-1;
	struct ast_module_user *u;
	char confno[AST_MAX_EXTENSION] = "";
	struct ast_sla *sla;
	struct ast_conference *cnf;
	char *info;
	struct ast_flags confflags = {0};
	int dynamic = 1;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(confno);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SLA%c requires an argument (line)\n", trunk ? 'T' : 'S');
		return -1;
	}

	info = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, info);	

	if (ast_strlen_zero(args.confno)) {
		ast_log(LOG_WARNING, "SLA%c requires an SLA line number\n", trunk ? 'T' : 'S');
		return -1;
	}
	
	u = ast_module_user_add(chan);

	if (args.options)
		ast_app_parse_options(sla_opts, &confflags, NULL, args.options);
		
	ast_set_flag(&confflags, CONFFLAG_QUIET|CONFFLAG_DYNAMIC);
	if (trunk)
		ast_set_flag(&confflags, CONFFLAG_WAITMARKED|CONFFLAG_MARKEDEXIT|CONFFLAG_SLA_TRUNK);
	else
		ast_set_flag(&confflags, CONFFLAG_MARKEDUSER|CONFFLAG_SLA_STATION);

	sla = ASTOBJ_CONTAINER_FIND(&slas, args.confno);
	if (sla) {
		snprintf(confno, sizeof(confno), "sla-%s", args.confno);
		cnf = find_conf(chan, confno, 1, dynamic, "", 1, &confflags);
		if (cnf) {
			sla_checkforhold(cnf, 1);
			if (!cnf->users) {
				if (trunk) {
					ast_indicate(chan, AST_CONTROL_RINGING);
					invite_stations(chan, sla);
				} else
					invite_trunk(chan, sla);
			} else if (chan->_state != AST_STATE_UP)
				ast_answer(chan);

			/* Run the conference */
			res = conf_run(chan, cnf, confflags.flags);
		} else
			ast_log(LOG_WARNING, "SLA%c: Found SLA '%s' but unable to build conference!\n", trunk ? 'T' : 'S', args.confno);
		ASTOBJ_UNREF(sla, sla_destroy);
	} else {
		ast_log(LOG_WARNING, "SLA%c: SLA '%s' not found!\n", trunk ? 'T' : 'S', args.confno);
	}
	
	ast_module_user_remove(u);
	
	return res;
}

/*! \brief The slas() wrapper */
static int slas_exec(struct ast_channel *chan, void *data)
{
	return sla_exec(chan, data, 0);
}

/*! \brief The slat() wrapper */
static int slat_exec(struct ast_channel *chan, void *data)
{
	return sla_exec(chan, data, 1);
}

static struct ast_conf_user *find_user(struct ast_conference *conf, char *callerident) 
{
	struct ast_conf_user *user = NULL;
	int cid;
	
	sscanf(callerident, "%i", &cid);
	if (conf && callerident) {
		AST_LIST_TRAVERSE(&conf->userlist, user, list) {
			if (cid == user->user_no)
				return user;
		}
	}
	return NULL;
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

	if (args.user)
		user = find_user(cnf, args.user);

	switch (*args.command) {
	case 76: /* L: Lock */ 
		cnf->locked = 1;
		break;
	case 108: /* l: Unlock */ 
		cnf->locked = 0;
		break;
	case 75: /* K: kick all users */
		AST_LIST_TRAVERSE(&cnf->userlist, user, list)
			user->adminflags |= ADMINFLAG_KICKME;
		break;
	case 101: /* e: Eject last user*/
		user = AST_LIST_LAST(&cnf->userlist);
		if (!(user->userflags & CONFFLAG_ADMIN))
			user->adminflags |= ADMINFLAG_KICKME;
		else
			ast_log(LOG_NOTICE, "Not kicking last user, is an Admin!\n");
		break;
	case 77: /* M: Mute */ 
		if (user) {
			user->adminflags |= ADMINFLAG_MUTED;
		} else
			ast_log(LOG_NOTICE, "Specified User not found!\n");
		break;
	case 78: /* N: Mute all (non-admin) users */
		AST_LIST_TRAVERSE(&cnf->userlist, user, list) {
			if (!(user->userflags & CONFFLAG_ADMIN))
				user->adminflags |= ADMINFLAG_MUTED;
		}
		break;					
	case 109: /* m: Unmute */ 
		if (user) {
			user->adminflags &= ~(ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED);
		} else
			ast_log(LOG_NOTICE, "Specified User not found!\n");
		break;
	case 110: /* n: Unmute all users */
		AST_LIST_TRAVERSE(&cnf->userlist, user, list)
			user->adminflags &= ~(ADMINFLAG_MUTED | ADMINFLAG_SELFMUTED);
		break;
	case 107: /* k: Kick user */ 
		if (user)
			user->adminflags |= ADMINFLAG_KICKME;
		else
			ast_log(LOG_NOTICE, "Specified User not found!\n");
		break;
	case 118: /* v: Lower all users listen volume */
		AST_LIST_TRAVERSE(&cnf->userlist, user, list)
			tweak_listen_volume(user, VOL_DOWN);
		break;
	case 86: /* V: Raise all users listen volume */
		AST_LIST_TRAVERSE(&cnf->userlist, user, list)
			tweak_listen_volume(user, VOL_UP);
		break;
	case 115: /* s: Lower all users speaking volume */
		AST_LIST_TRAVERSE(&cnf->userlist, user, list)
			tweak_talk_volume(user, VOL_DOWN);
		break;
	case 83: /* S: Raise all users speaking volume */
		AST_LIST_TRAVERSE(&cnf->userlist, user, list)
			tweak_talk_volume(user, VOL_UP);
		break;
	case 82: /* R: Reset all volume levels */
		AST_LIST_TRAVERSE(&cnf->userlist, user, list)
			reset_volumes(user);
		break;
	case 114: /* r: Reset user's volume level */
		if (user)
			reset_volumes(user);
		else
			ast_log(LOG_NOTICE, "Specified User not found!\n");
		break;
	case 85: /* U: Raise user's listen volume */
		if (user)
			tweak_listen_volume(user, VOL_UP);
		else
			ast_log(LOG_NOTICE, "Specified User not found!\n");
		break;
	case 117: /* u: Lower user's listen volume */
		if (user)
			tweak_listen_volume(user, VOL_DOWN);
		else
			ast_log(LOG_NOTICE, "Specified User not found!\n");
		break;
	case 84: /* T: Raise user's talk volume */
		if (user)
			tweak_talk_volume(user, VOL_UP);
		else
			ast_log(LOG_NOTICE, "Specified User not found!\n");
		break;
	case 116: /* t: Lower user's talk volume */
		if (user) 
			tweak_talk_volume(user, VOL_DOWN);
		else 
			ast_log(LOG_NOTICE, "Specified User not found!\n");
		break;
	}

	AST_LIST_UNLOCK(&confs);

	ast_module_user_remove(u);
	
	return 0;
}

static int meetmemute(struct mansession *s, struct message *m, int mute)
{
	struct ast_conference *conf;
	struct ast_conf_user *user;
	char *confid = astman_get_header(m, "Meetme");
	char *userid = astman_get_header(m, "Usernum");
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

	AST_LIST_TRAVERSE(&conf->userlist, user, list)
		if (user->user_no == userno)
			break;

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

	astman_send_ack(s, m, mute ? "User muted" : "User unmuted");
	return 0;
}

static int action_meetmemute(struct mansession *s, struct message *m)
{
	return meetmemute(s, m, 1);
}

static int action_meetmeunmute(struct mansession *s, struct message *m)
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
			cnf->origframe = f;
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

/*! \brief Callback for devicestate providers */
static int slastate(const char *data)
{
	struct ast_conference *conf;
	struct ast_sla *sla, *sla2;

	ast_log(LOG_DEBUG, "asked for sla state for '%s'\n", data);

	/* Find conference */
	AST_LIST_LOCK(&confs);
	AST_LIST_TRAVERSE(&confs, conf, list) {
		if (!strncmp(conf->confno, "sla-", 4) && !strcmp(data, conf->confno + 4))
			break;
	}
	AST_LIST_UNLOCK(&confs);

	/* Find conference */
	sla = sla2 = ASTOBJ_CONTAINER_FIND(&slas, data);
	ASTOBJ_UNREF(sla2, sla_destroy);

	ast_log(LOG_DEBUG, "for '%s' conf = %p, sla = %p\n", data, conf, sla);

	if (!conf && !sla)
		return AST_DEVICE_INVALID;

	/* SKREP to fill */
	if (!conf || !conf->users)
		return AST_DEVICE_NOT_INUSE;
	
	if (conf && sla_checkforhold(conf, 0))
		return AST_DEVICE_ONHOLD;

	if ((conf->users == 1) && (AST_LIST_FIRST(&conf->userlist)->userflags & CONFFLAG_SLA_TRUNK))
		return AST_DEVICE_RINGING;

	return AST_DEVICE_INUSE;
}

static void load_config_meetme(void)
{
	struct ast_config *cfg;
	char *val;

	audio_buffers = DEFAULT_AUDIO_BUFFERS;

	if (!(cfg = ast_config_load(CONFIG_FILE_NAME)))
		return;

	if ((val = ast_variable_retrieve(cfg, "general", "audiobuffers"))) {
		if ((sscanf(val, "%d", &audio_buffers) != 1)) {
			ast_log(LOG_WARNING, "audiobuffers setting must be a number, not '%s'\n", val);
			audio_buffers = DEFAULT_AUDIO_BUFFERS;
		} else if ((audio_buffers < ZT_DEFAULT_NUM_BUFS) || (audio_buffers > ZT_MAX_NUM_BUFS)) {
			ast_log(LOG_WARNING, "audiobuffers setting must be between %d and %d\n",
				ZT_DEFAULT_NUM_BUFS, ZT_MAX_NUM_BUFS);
			audio_buffers = DEFAULT_AUDIO_BUFFERS;
		}
		if (audio_buffers != DEFAULT_AUDIO_BUFFERS)
			ast_log(LOG_NOTICE, "Audio buffers per channel set to %d\n", audio_buffers);
	}

	ast_config_destroy(cfg);
}

/*! Append SLA station to station list */
static void append_station(struct ast_sla *sla, const char *station)
{
	struct ast_sla_station *s;
	char *c;

	s = ast_calloc(1, sizeof(struct ast_sla_station) + strlen(station) + 2);
	if (s) {
		ASTOBJ_INIT(s);
		strcpy(s->tech, station);
		c = strchr(s->tech, '/');
		if (c) {
			*c = '\0';
			s->dest = c + 1;
			ASTOBJ_CONTAINER_LINK(&sla->stations, s);
		} else {
			ast_log(LOG_WARNING, "station '%s' should be in tech/destination format! Ignoring!\n", station);
			free(s);
		}
	}
}

/*! Parse SLA configuration file and create objects */
static void parse_sla(const char *cat, struct ast_variable *v)
{
	struct ast_sla *sla;

	sla = ASTOBJ_CONTAINER_FIND(&slas, cat);
	if (!sla) {
		sla = ast_calloc(1, sizeof(struct ast_sla));
		if (sla) {
			ASTOBJ_INIT(sla);
			ast_copy_string(sla->name, cat, sizeof(sla->name));
			snprintf(sla->confname, sizeof(sla->confname), "sla-%s", sla->name);
			ASTOBJ_CONTAINER_LINK(&slas, sla);
		}
	}
	if (sla) {
		ASTOBJ_UNMARK(sla);
		ASTOBJ_WRLOCK(sla);
		ASTOBJ_CONTAINER_DESTROYALL(&sla->stations, station_destroy);
		while (v) {
			if (!strcasecmp(v->name, "trunk")) {
				char *c;
				c = strchr(v->value, '/');
				if (c) {
					ast_copy_string(sla->trunktech, v->value, (c - v->value) + 1);
					ast_copy_string(sla->trunkdest, c + 1, sizeof(sla->trunkdest));
				}
			} else if (!strcasecmp(v->name, "station")) {
				append_station(sla, v->value);
			}
			v = v->next;
		}
		ASTOBJ_UNLOCK(sla);
		ast_device_state_changed("SLA:%s", cat);
	}
}

/*! If there is a SLA configuration file, parse it */
static void load_config_sla(void)
{
	char *cat;
	struct ast_config *cfg;
	if (!(cfg = ast_config_load(CONFIG_FILE_NAME_SLA)))
		return;

	ASTOBJ_CONTAINER_MARKALL(&slas);
	cat = ast_category_browse(cfg, NULL);
	while(cat) {
		if (strcasecmp(cat, "general")) 
			parse_sla(cat, ast_variable_browse(cfg, cat));
		cat = ast_category_browse(cfg, cat);
	}
	ast_config_destroy(cfg);
	ASTOBJ_CONTAINER_PRUNE_MARKED(&slas, sla_destroy);
}

static void load_config(void)
{
	load_config_meetme();
	load_config_sla();
}

static int unload_module(void)
{
	int res = 0;
	
	res |= ast_cli_unregister(&cli_show_confs);
	res |= ast_cli_unregister(&cli_sla_show);
	res |= ast_cli_unregister(&cli_conf);
	res |= ast_manager_unregister("MeetmeMute");
	res |= ast_manager_unregister("MeetmeUnmute");
	res |= ast_unregister_application(app3);
	res |= ast_unregister_application(app2);
	res |= ast_unregister_application(app);
	res |= ast_unregister_application(appslas);
	res |= ast_unregister_application(appslat);

	ast_module_user_hangup_all();
	ast_devstate_prov_del("Meetme");
	ast_devstate_prov_del("SLA");

	return res;
}

static int load_module(void)
{
	int res;

	ASTOBJ_CONTAINER_INIT(&slas);
	res = ast_cli_register(&cli_show_confs);
	res |= ast_cli_register(&cli_sla_show);
	res |= ast_cli_register(&cli_conf);
	res |= ast_manager_register("MeetmeMute", EVENT_FLAG_CALL, action_meetmemute, "Mute a Meetme user");
	res |= ast_manager_register("MeetmeUnmute", EVENT_FLAG_CALL, action_meetmeunmute, "Unmute a Meetme user");
	res |= ast_register_application(app3, admin_exec, synopsis3, descrip3);
	res |= ast_register_application(app2, count_exec, synopsis2, descrip2);
	res |= ast_register_application(app, conf_exec, synopsis, descrip);
	res |= ast_register_application(appslas, slas_exec, synopslas, descripslas);
	res |= ast_register_application(appslat, slat_exec, synopslat, descripslat);

	res |= ast_devstate_prov_add("Meetme", meetmestate);
	res |= ast_devstate_prov_add("SLA", slastate);
	load_config();
	return res;
}

static int reload(void)
{
	load_config();

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "MeetMe conference bridge",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );

