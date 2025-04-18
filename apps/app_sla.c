/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2022, Naveen Albert
 *
 * Based on previous MeetMe-based SLA Implementation by:
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
 * \brief Shared Line Appearances
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 *
 * \ingroup applications
 */

/*! \li \ref app_sla.c uses configuration file \ref sla.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page sla.conf sla.conf
 * \verbinclude sla.conf.sample
 */

/*** MODULEINFO
	<depend>app_confbridge</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/app.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/astobj2.h"
#include "asterisk/devicestate.h"
#include "asterisk/dial.h"
#include "asterisk/causes.h"
#include "asterisk/format_compatibility.h"

/*** DOCUMENTATION
	<application name="SLAStation" language="en_US">
		<since>
			<version>21.0.0</version>
		</since>
		<synopsis>
			Shared Line Appearance Station.
		</synopsis>
		<syntax>
			<parameter name="station" required="true">
				<para>Station name</para>
			</parameter>
		</syntax>
		<description>
			<para>This application should be executed by an SLA station. The argument depends
			on how the call was initiated. If the phone was just taken off hook, then the argument
			<replaceable>station</replaceable> should be just the station name. If the call was
			initiated by pressing a line key, then the station name should be preceded by an underscore
			and the trunk name associated with that line button.</para>
			<para>For example: <literal>station1_line1</literal></para>
			<para>On exit, this application will set the variable <variable>SLASTATION_STATUS</variable> to
			one of the following values:</para>
			<variablelist>
				<variable name="SLASTATION_STATUS">
					<value name="FAILURE" />
					<value name="CONGESTION" />
					<value name="SUCCESS" />
				</variable>
			</variablelist>
		</description>
	</application>
	<application name="SLATrunk" language="en_US">
		<since>
			<version>21.0.0</version>
		</since>
		<synopsis>
			Shared Line Appearance Trunk.
		</synopsis>
		<syntax>
			<parameter name="trunk" required="true">
				<para>Trunk name</para>
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="M" hasparams="optional">
						<para>Play back the specified MOH <replaceable>class</replaceable>
						instead of ringing</para>
						<argument name="class" required="true" />
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>This application should be executed by an SLA trunk on an inbound call. The channel calling
			this application should correspond to the SLA trunk with the name <replaceable>trunk</replaceable>
			that is being passed as an argument.</para>
			<para>On exit, this application will set the variable <variable>SLATRUNK_STATUS</variable> to
			one of the following values:</para>
			<variablelist>
				<variable name="SLATRUNK_STATUS">
					<value name="FAILURE" />
					<value name="SUCCESS" />
					<value name="UNANSWERED" />
					<value name="RINGTIMEOUT" />
				</variable>
			</variablelist>
		</description>
	</application>
 ***/

#define SLA_CONFIG_FILE		"sla.conf"
#define MAX_CONFNUM 80

static const char * const slastation_app = "SLAStation";
static const char * const slatrunk_app = "SLATrunk";

enum {
	/*! If set there will be no enter or leave sounds */
	CONFFLAG_QUIET = (1 << 0),
	/*! Set to have music on hold when user is alone in conference */
	CONFFLAG_MOH = (1 << 1),
	/*! If set, the channel will leave the conference if all marked users leave */
	CONFFLAG_MARKEDEXIT = (1 << 2),
	/*! If set, the user will be marked */
	CONFFLAG_MARKEDUSER = (1 << 3),
	/*! Pass DTMF through the conference */
	CONFFLAG_PASS_DTMF = (1 << 4),
	CONFFLAG_SLA_STATION = (1 << 5),
	CONFFLAG_SLA_TRUNK = (1 << 6),
};

enum {
	SLA_TRUNK_OPT_MOH = (1 << 0),
};

enum {
	SLA_TRUNK_OPT_ARG_MOH_CLASS = 0,
	SLA_TRUNK_OPT_ARG_ARRAY_SIZE = 1,
};

AST_APP_OPTIONS(sla_trunk_opts, BEGIN_OPTIONS
	AST_APP_OPTION_ARG('M', SLA_TRUNK_OPT_MOH, SLA_TRUNK_OPT_ARG_MOH_CLASS),
END_OPTIONS );

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
	/*! Mark used during reload processing */
	unsigned int mark:1;
};

/*!
 * \brief A reference to a station
 *
 * This struct looks near useless at first glance.  However, its existence
 * in the list of stations in sla_trunk means that this station references
 * that trunk.  We use the mark to keep track of whether it needs to be
 * removed from the sla_trunk's list of stations during a reload.
 */
struct sla_station_ref {
	AST_LIST_ENTRY(sla_station_ref) entry;
	struct sla_station *station;
	/*! Mark used during reload processing */
	unsigned int mark:1;
};

struct sla_trunk {
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
	/*! Mark used during reload processing */
	unsigned int mark:1;
};

/*!
 * \brief A station's reference to a trunk
 *
 * An sla_station keeps a list of trunk_refs.  This holds metadata about the
 * stations usage of the trunk.
 */
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
	/*! Mark used during reload processing */
	unsigned int mark:1;
};

static struct ao2_container *sla_stations;
static struct ao2_container *sla_trunks;

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

static char *sla_show_trunks(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator i;
	struct sla_trunk *trunk;

	switch (cmd) {
	case CLI_INIT:
		e->command = "sla show trunks";
		e->usage =
			"Usage: sla show trunks\n"
			"       This will list all trunks defined in sla.conf\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, "\n"
	            "=============================================================\n"
	            "=== Configured SLA Trunks ===================================\n"
	            "=============================================================\n"
	            "===\n");
	i = ao2_iterator_init(sla_trunks, 0);
	for (; (trunk = ao2_iterator_next(&i)); ao2_ref(trunk, -1)) {
		struct sla_station_ref *station_ref;
		char ring_timeout[23] = "(none)";

		ao2_lock(trunk);

		if (trunk->ring_timeout) {
			snprintf(ring_timeout, sizeof(ring_timeout), "%u Seconds", trunk->ring_timeout);
		}

		ast_cli(a->fd, "=== ---------------------------------------------------------\n"
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

		AST_LIST_TRAVERSE(&trunk->stations, station_ref, entry) {
			ast_cli(a->fd, "===    ==> Station name: %s\n", station_ref->station->name);
		}

		ast_cli(a->fd, "=== ---------------------------------------------------------\n===\n");

		ao2_unlock(trunk);
	}
	ao2_iterator_destroy(&i);
	ast_cli(a->fd, "=============================================================\n\n");

	return CLI_SUCCESS;
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
	return "Unknown State";
#undef S
}

static char *sla_show_stations(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator i;
	struct sla_station *station;

	switch (cmd) {
	case CLI_INIT:
		e->command = "sla show stations";
		e->usage =
			"Usage: sla show stations\n"
			"       This will list all stations defined in sla.conf\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, "\n"
	            "=============================================================\n"
	            "=== Configured SLA Stations =================================\n"
	            "=============================================================\n"
	            "===\n");
	i = ao2_iterator_init(sla_stations, 0);
	for (; (station = ao2_iterator_next(&i)); ao2_ref(station, -1)) {
		struct sla_trunk_ref *trunk_ref;
		char ring_timeout[16] = "(none)";
		char ring_delay[16] = "(none)";

		ao2_lock(station);

		if (station->ring_timeout) {
			snprintf(ring_timeout, sizeof(ring_timeout), "%u", station->ring_timeout);
		}
		if (station->ring_delay) {
			snprintf(ring_delay, sizeof(ring_delay), "%u", station->ring_delay);
		}
		ast_cli(a->fd, "=== ---------------------------------------------------------\n"
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
		AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
			if (trunk_ref->ring_timeout) {
				snprintf(ring_timeout, sizeof(ring_timeout), "%u", trunk_ref->ring_timeout);
			} else {
				strcpy(ring_timeout, "(none)");
			}
			if (trunk_ref->ring_delay) {
				snprintf(ring_delay, sizeof(ring_delay), "%u", trunk_ref->ring_delay);
			} else {
				strcpy(ring_delay, "(none)");
			}

			ast_cli(a->fd, "===    ==> Trunk Name: %s\n"
	            "===       ==> State:       %s\n"
	            "===       ==> RingTimeout: %s\n"
	            "===       ==> RingDelay:   %s\n",
	            trunk_ref->trunk->name,
	            trunkstate2str(trunk_ref->state),
	            ring_timeout, ring_delay);
		}
		ast_cli(a->fd, "=== ---------------------------------------------------------\n"
		            "===\n");

		ao2_unlock(station);
	}
	ao2_iterator_destroy(&i);
	ast_cli(a->fd, "============================================================\n"
	            "\n");

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_sla[] = {
	AST_CLI_DEFINE(sla_show_trunks, "Show SLA Trunks"),
	AST_CLI_DEFINE(sla_show_stations, "Show SLA Stations"),
};

static void sla_queue_event_full(enum sla_event_type type, struct sla_trunk_ref *trunk_ref, struct sla_station *station, int lock)
{
	struct sla_event *event;

	if (sla.thread == AST_PTHREADT_NULL) {
		ao2_ref(station, -1);
		ao2_ref(trunk_ref, -1);
		return;
	}

	if (!(event = ast_calloc(1, sizeof(*event)))) {
		ao2_ref(station, -1);
		ao2_ref(trunk_ref, -1);
		return;
	}

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
static void sla_queue_event_conf(enum sla_event_type type, struct ast_channel *chan, const char *confname)
{
	struct sla_station *station;
	struct sla_trunk_ref *trunk_ref = NULL;
	char *trunk_name;
	struct ao2_iterator i;

	trunk_name = ast_strdupa(confname);
	strsep(&trunk_name, "_");
	if (ast_strlen_zero(trunk_name)) {
		ast_log(LOG_ERROR, "Invalid conference name for SLA - '%s'!\n", confname);
		return;
	}

	i = ao2_iterator_init(sla_stations, 0);
	while ((station = ao2_iterator_next(&i))) {
		ao2_lock(station);
		AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
			if (trunk_ref->chan == chan && !strcmp(trunk_ref->trunk->name, trunk_name)) {
				ao2_ref(trunk_ref, 1);
				break;
			}
		}
		ao2_unlock(station);
		if (trunk_ref) {
			/* station reference given to sla_queue_event_full() */
			break;
		}
		ao2_ref(station, -1);
	}
	ao2_iterator_destroy(&i);

	if (!trunk_ref) {
		ast_debug(1, "Trunk not found for event!\n");
		return;
	}

	sla_queue_event_full(type, trunk_ref, station, 1);
}

/*!
 * \brief Framehook to support HOLD within the conference
 */

struct sla_framehook_data {
	int framehook_id;
	char *confname;
};

static const struct ast_datastore_info sla_framehook_datastore = {
	.type = "app_sla",
};

static int remove_framehook(struct ast_channel *chan)
{
	struct ast_datastore *datastore = NULL;
	struct sla_framehook_data *data;
	SCOPED_CHANNELLOCK(chan_lock, chan);

	datastore = ast_channel_datastore_find(chan, &sla_framehook_datastore, NULL);
	if (!datastore) {
		ast_log(AST_LOG_WARNING, "Cannot remove framehook from %s: HOLD_INTERCEPT not currently enabled\n", ast_channel_name(chan));
		return -1;
	}
	data = datastore->data;

	ast_free(data->confname);

	if (ast_framehook_detach(chan, data->framehook_id)) {
		ast_log(AST_LOG_WARNING, "Failed to remove framehook from channel %s\n", ast_channel_name(chan));
		return -1;
	}
	if (ast_channel_datastore_remove(chan, datastore)) {
		ast_log(AST_LOG_WARNING, "Failed to remove datastore from channel %s\n", ast_channel_name(chan));
		return -1;
	}
	ast_datastore_free(datastore);

	return 0;
}

static struct ast_frame *sla_framehook(struct ast_channel *chan, struct ast_frame *f, enum ast_framehook_event event, void *data)
{
	struct sla_framehook_data *sla_data = data;
	if (!f || (event != AST_FRAMEHOOK_EVENT_WRITE)) {
		return f;
	}
	if (f->frametype == AST_FRAME_CONTROL && f->subclass.integer == AST_CONTROL_HOLD) {
		sla_queue_event_conf(SLA_EVENT_HOLD, chan, sla_data->confname);
	}
	return f;
}

/*! \brief Callback function which informs upstream if we are consuming a frame of a specific type */
static int sla_framehook_consume(void *data, enum ast_frame_type type)
{
	return (type == AST_FRAME_CONTROL ? 1 : 0);
}

static int attach_framehook(struct ast_channel *chan, const char *confname)
{
	struct ast_datastore *datastore;
	struct sla_framehook_data *data;
	static struct ast_framehook_interface sla_framehook_interface = {
		.version = AST_FRAMEHOOK_INTERFACE_VERSION,
		.event_cb = sla_framehook,
		.consume_cb = sla_framehook_consume,
		.disable_inheritance = 1,
	};
	SCOPED_CHANNELLOCK(chan_lock, chan);

	datastore = ast_channel_datastore_find(chan, &sla_framehook_datastore, NULL);
	if (datastore) {
		ast_log(AST_LOG_WARNING, "SLA framehook already set on '%s'\n", ast_channel_name(chan));
		return 0;
	}

	datastore = ast_datastore_alloc(&sla_framehook_datastore, NULL);
	if (!datastore) {
		return -1;
	}

	data = ast_calloc(1, sizeof(*data));
	if (!data) {
		ast_datastore_free(datastore);
		return -1;
	}

	data->framehook_id = ast_framehook_attach(chan, &sla_framehook_interface);
	data->confname = ast_strdup(confname);
	if (!data->confname || data->framehook_id < 0) {
		ast_log(AST_LOG_WARNING, "Failed to attach SLA framehook to '%s'\n", ast_channel_name(chan));
		ast_datastore_free(datastore);
		ast_free(data);
		return -1;
	}
	datastore->data = data;

	ast_channel_datastore_add(chan, datastore);
	return 0;
}

/*!
 * \internal
 * \brief Find an SLA trunk by name
 */
static struct sla_trunk *sla_find_trunk(const char *name)
{
	struct sla_trunk tmp_trunk = {
		.name = name,
	};

	return ao2_find(sla_trunks, &tmp_trunk, OBJ_POINTER);
}

/*!
 * \internal
 * \brief Find an SLA station by name
 */
static struct sla_station *sla_find_station(const char *name)
{
	struct sla_station tmp_station = {
		.name = name,
	};

	return ao2_find(sla_stations, &tmp_station, OBJ_POINTER);
}

static int sla_check_station_hold_access(const struct sla_trunk *trunk, const struct sla_station *station)
{
	struct sla_station_ref *station_ref;
	struct sla_trunk_ref *trunk_ref;

	/* For each station that has this call on hold, check for private hold. */
	AST_LIST_TRAVERSE(&trunk->stations, station_ref, entry) {
		AST_LIST_TRAVERSE(&station_ref->station->trunks, trunk_ref, entry) {
			if (trunk_ref->trunk != trunk || station_ref->station == station) {
				continue;
			}
			if (trunk_ref->state == SLA_TRUNK_STATE_ONHOLD_BYME && station_ref->station->hold_access == SLA_HOLD_PRIVATE) {
				return 1;
			}
			return 0;
		}
	}

	return 0;
}

/*!
 * \brief Find a trunk reference on a station by name
 * \param station the station
 * \param name the trunk's name
 * \pre sla_station is locked
 * \return a pointer to the station's trunk reference.  If the trunk
 *         is not found, it is not idle and barge is disabled, or if
 *         it is on hold and private hold is set, then NULL will be returned.
 */
static struct sla_trunk_ref *sla_find_trunk_ref_byname(const struct sla_station *station, const char *name)
{
	struct sla_trunk_ref *trunk_ref = NULL;

	AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
		if (strcasecmp(trunk_ref->trunk->name, name)) {
			continue;
		}

		if (trunk_ref->trunk->barge_disabled && trunk_ref->state == SLA_TRUNK_STATE_UP) {
			ast_debug(2, "Barge disabled, trunk not available\n");
			trunk_ref = NULL;
		} else if (trunk_ref->trunk->hold_stations && trunk_ref->trunk->hold_access == SLA_HOLD_PRIVATE && trunk_ref->state != SLA_TRUNK_STATE_ONHOLD_BYME) {
			ast_debug(2, "Private hold by another station\n");
			trunk_ref = NULL;
		} else if (sla_check_station_hold_access(trunk_ref->trunk, station)) {
			ast_debug(2, "No hold access\n");
			trunk_ref = NULL;
		}

		break;
	}

	if (trunk_ref) {
		ao2_ref(trunk_ref, 1);
	}

	return trunk_ref;
}

static void sla_station_ref_destructor(void *obj)
{
	struct sla_station_ref *station_ref = obj;

	if (station_ref->station) {
		ao2_ref(station_ref->station, -1);
		station_ref->station = NULL;
	}
}

static struct sla_station_ref *sla_create_station_ref(struct sla_station *station)
{
	struct sla_station_ref *station_ref;

	if (!(station_ref = ao2_alloc(sizeof(*station_ref), sla_station_ref_destructor))) {
		return NULL;
	}

	ao2_ref(station, 1);
	station_ref->station = station;

	return station_ref;
}

static struct sla_ringing_station *sla_create_ringing_station(struct sla_station *station)
{
	struct sla_ringing_station *ringing_station;

	if (!(ringing_station = ast_calloc(1, sizeof(*ringing_station)))) {
		return NULL;
	}

	ao2_ref(station, 1);
	ringing_station->station = station;
	ringing_station->ring_begin = ast_tvnow();

	return ringing_station;
}

static void sla_ringing_station_destroy(struct sla_ringing_station *ringing_station)
{
	if (ringing_station->station) {
		ao2_ref(ringing_station->station, -1);
		ringing_station->station = NULL;
	}

	ast_free(ringing_station);
}

static struct sla_failed_station *sla_create_failed_station(struct sla_station *station)
{
	struct sla_failed_station *failed_station;

	if (!(failed_station = ast_calloc(1, sizeof(*failed_station)))) {
		return NULL;
	}

	ao2_ref(station, 1);
	failed_station->station = station;
	failed_station->last_try = ast_tvnow();

	return failed_station;
}

static void sla_failed_station_destroy(struct sla_failed_station *failed_station)
{
	if (failed_station->station) {
		ao2_ref(failed_station->station, -1);
		failed_station->station = NULL;
	}

	ast_free(failed_station);
}

static enum ast_device_state sla_state_to_devstate(enum sla_trunk_state state)
{
	switch (state) {
	case SLA_TRUNK_STATE_IDLE:
		return AST_DEVICE_NOT_INUSE;
	case SLA_TRUNK_STATE_RINGING:
		return AST_DEVICE_RINGING;
	case SLA_TRUNK_STATE_UP:
		return AST_DEVICE_INUSE;
	case SLA_TRUNK_STATE_ONHOLD:
	case SLA_TRUNK_STATE_ONHOLD_BYME:
		return AST_DEVICE_ONHOLD;
	}

	return AST_DEVICE_UNKNOWN;
}

static void sla_change_trunk_state(const struct sla_trunk *trunk, enum sla_trunk_state state,
	enum sla_which_trunk_refs inactive_only, const struct sla_trunk_ref *exclude)
{
	struct sla_station *station;
	struct sla_trunk_ref *trunk_ref;
	struct ao2_iterator i;

	i = ao2_iterator_init(sla_stations, 0);
	while ((station = ao2_iterator_next(&i))) {
		ao2_lock(station);
		AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
			if (trunk_ref->trunk != trunk || (inactive_only ? trunk_ref->chan : 0) || trunk_ref == exclude) {
				continue;
			}
			trunk_ref->state = state;
			ast_devstate_changed(sla_state_to_devstate(state), AST_DEVSTATE_CACHABLE, "SLA:%s_%s", station->name, trunk->name);
			break;
		}
		ao2_unlock(station);
		ao2_ref(station, -1);
	}
	ao2_iterator_destroy(&i);
}

struct run_station_args {
	struct sla_station *station;
	struct sla_trunk_ref *trunk_ref;
	ast_mutex_t *cond_lock;
	ast_cond_t *cond;
};

static void answer_trunk_chan(struct ast_channel *chan)
{
	ast_raw_answer(chan); /* Do NOT use ast_answer since that waits for media using ast_waitfor_nandfds. */
	ast_indicate(chan, -1);
}

static int conf_run(struct ast_channel *chan, const char *confname, struct ast_flags *confflags, char *optargs[])
{
	char confbridge_args[256];
	int res = 0;

	snprintf(confbridge_args, sizeof(confbridge_args), "%s", confname);

	res |= ast_func_write(chan, "CONFBRIDGE(user,quiet)", ast_test_flag(confflags, CONFFLAG_QUIET) ? "1" : "0");
	res |= ast_func_write(chan, "CONFBRIDGE(user,dtmf_passthrough)", ast_test_flag(confflags, CONFFLAG_PASS_DTMF) ? "1" : "0");
	res |= ast_func_write(chan, "CONFBRIDGE(user,marked)", ast_test_flag(confflags, CONFFLAG_MARKEDUSER) ? "1" : "0");
	res |= ast_func_write(chan, "CONFBRIDGE(user,end_marked)", ast_test_flag(confflags, CONFFLAG_MARKEDEXIT) ? "1" : "0");
	res |= ast_func_write(chan, "CONFBRIDGE(user,music_on_hold_when_empty)", ast_test_flag(confflags, CONFFLAG_MOH) ? "1" : "0");
	if (ast_test_flag(confflags, CONFFLAG_MOH) && !ast_strlen_zero(optargs[SLA_TRUNK_OPT_ARG_MOH_CLASS])) {
		res |= ast_func_write(chan, "CONFBRIDGE(user,music_on_hold_class)", optargs[SLA_TRUNK_OPT_ARG_MOH_CLASS]);
	}

	if (res) {
		ast_log(LOG_ERROR, "Failed to set up conference, aborting\n");
		return -1;
	}

	/* Attach a framehook that we'll use to process HOLD from stations. */
	if (ast_test_flag(confflags, CONFFLAG_SLA_STATION) && attach_framehook(chan, confname)) {
		return -1;
	}

	ast_debug(2, "Channel %s is running ConfBridge(%s)\n", ast_channel_name(chan), confbridge_args);
	res = ast_pbx_exec_application(chan, "ConfBridge", confbridge_args);

	if (ast_test_flag(confflags, CONFFLAG_SLA_STATION)) {
		remove_framehook(chan);
	}
	return res;
}

static int conf_kick_all(struct ast_channel *chan, const char *confname)
{
	char confkick_args[256];

	snprintf(confkick_args, sizeof(confkick_args), "%s,all", confname);
	ast_debug(2, "Kicking all participants from conference %s\n", confname);

	if (chan) {
		return ast_pbx_exec_application(chan, "ConfKick", confkick_args);
	} else {
		/* We might not have a channel available to us, use a dummy channel in that case. */
		chan = ast_dummy_channel_alloc();
		if (!chan) {
			ast_log(LOG_WARNING, "Failed to allocate dummy channel\n");
			return -1;
		} else {
			int res = ast_pbx_exec_application(chan, "ConfKick", confkick_args);
			ast_channel_unref(chan);
			return res;
		}
	}
}

static void *run_station(void *data)
{
	RAII_VAR(struct sla_station *, station, NULL, ao2_cleanup);
	RAII_VAR(struct sla_trunk_ref *, trunk_ref, NULL, ao2_cleanup);
	struct ast_str *conf_name = ast_str_create(16);
	struct ast_flags conf_flags = { 0 };

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
	ast_str_set(&conf_name, 0, "SLA_%s", trunk_ref->trunk->name);
	ast_set_flag(&conf_flags, CONFFLAG_QUIET | CONFFLAG_MARKEDEXIT | CONFFLAG_PASS_DTMF | CONFFLAG_SLA_STATION);
	answer_trunk_chan(trunk_ref->chan);

	ast_debug(2, "Station %s joining conference %s\n", station->name, ast_str_buffer(conf_name));
	conf_run(trunk_ref->chan, ast_str_buffer(conf_name), &conf_flags, NULL);

	trunk_ref->chan = NULL;
	if (ast_atomic_dec_and_test((int *) &trunk_ref->trunk->active_stations) &&
		trunk_ref->state != SLA_TRUNK_STATE_ONHOLD_BYME) {
		conf_kick_all(NULL, ast_str_buffer(conf_name));
		trunk_ref->trunk->hold_stations = 0;
		sla_change_trunk_state(trunk_ref->trunk, SLA_TRUNK_STATE_IDLE, ALL_TRUNK_REFS, NULL);
	}

	ast_dial_join(station->dial);
	ast_dial_destroy(station->dial);
	station->dial = NULL;
	ast_free(conf_name);

	return NULL;
}

static void sla_ringing_trunk_destroy(struct sla_ringing_trunk *ringing_trunk);

static void sla_stop_ringing_trunk(struct sla_ringing_trunk *ringing_trunk)
{
	struct sla_station_ref *station_ref;

	conf_kick_all(ringing_trunk->trunk->chan, ringing_trunk->trunk->name);
	sla_change_trunk_state(ringing_trunk->trunk, SLA_TRUNK_STATE_IDLE, ALL_TRUNK_REFS, NULL);

	while ((station_ref = AST_LIST_REMOVE_HEAD(&ringing_trunk->timed_out_stations, entry))) {
		ao2_ref(station_ref, -1);
	}

	sla_ringing_trunk_destroy(ringing_trunk);
}

static void sla_stop_ringing_station(struct sla_ringing_station *ringing_station, enum sla_station_hangup hangup)
{
	struct sla_ringing_trunk *ringing_trunk;
	struct sla_trunk_ref *trunk_ref;
	struct sla_station_ref *station_ref;

	ast_dial_join(ringing_station->station->dial);
	ast_dial_destroy(ringing_station->station->dial);
	ringing_station->station->dial = NULL;

	if (hangup == SLA_STATION_HANGUP_NORMAL) {
		goto done;
	}

	/* If the station is being hung up because of a timeout, then add it to the
	 * list of timed out stations on each of the ringing trunks.  This is so
	 * that when doing further processing to figure out which stations should be
	 * ringing, which trunk to answer, determining timeouts, etc., we know which
	 * ringing trunks we should ignore. */
	AST_LIST_TRAVERSE(&sla.ringing_trunks, ringing_trunk, entry) {
		AST_LIST_TRAVERSE(&ringing_station->station->trunks, trunk_ref, entry) {
			if (ringing_trunk->trunk == trunk_ref->trunk) {
				break;
			}
		}
		if (!trunk_ref) {
			continue;
		}
		if (!(station_ref = sla_create_station_ref(ringing_station->station))) {
			continue;
		}
		AST_LIST_INSERT_TAIL(&ringing_trunk->timed_out_stations, station_ref, entry);
	}

done:
	sla_ringing_station_destroy(ringing_station);
}

static void sla_dial_state_callback(struct ast_dial *dial)
{
	sla_queue_event(SLA_EVENT_DIAL_STATE);
}

/*! \brief Check to see if dialing this station already timed out for this ringing trunk
 * \note Assumes sla.lock is locked
 */
static int sla_check_timed_out_station(const struct sla_ringing_trunk *ringing_trunk, const struct sla_station *station)
{
	struct sla_station_ref *timed_out_station;

	AST_LIST_TRAVERSE(&ringing_trunk->timed_out_stations, timed_out_station, entry) {
		if (station == timed_out_station->station) {
			return 1;
		}
	}

	return 0;
}

/*! \brief Choose the highest priority ringing trunk for a station
 * \param station the station
 * \param rm remove the ringing trunk once selected
 * \param trunk_ref a place to store the pointer to this stations reference to
 *        the selected trunk
 * \return a pointer to the selected ringing trunk, or NULL if none found
 * \note Assumes that sla.lock is locked
 */
static struct sla_ringing_trunk *sla_choose_ringing_trunk(struct sla_station *station, struct sla_trunk_ref **trunk_ref, int rm)
{
	struct sla_trunk_ref *s_trunk_ref;
	struct sla_ringing_trunk *ringing_trunk = NULL;

	AST_LIST_TRAVERSE(&station->trunks, s_trunk_ref, entry) {
		AST_LIST_TRAVERSE_SAFE_BEGIN(&sla.ringing_trunks, ringing_trunk, entry) {
			/* Make sure this is the trunk we're looking for */
			if (s_trunk_ref->trunk != ringing_trunk->trunk) {
				continue;
			}

			/* This trunk on the station is ringing.  But, make sure this station
			 * didn't already time out while this trunk was ringing. */
			if (sla_check_timed_out_station(ringing_trunk, station)) {
				continue;
			}

			if (rm) {
				AST_LIST_REMOVE_CURRENT(entry);
			}

			if (trunk_ref) {
				ao2_ref(s_trunk_ref, 1);
				*trunk_ref = s_trunk_ref;
			}

			break;
		}
		AST_LIST_TRAVERSE_SAFE_END;

		if (ringing_trunk) {
			break;
		}
	}

	return ringing_trunk;
}

static void sla_handle_dial_state_event(void)
{
	struct sla_ringing_station *ringing_station;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&sla.ringing_stations, ringing_station, entry) {
		RAII_VAR(struct sla_trunk_ref *, s_trunk_ref, NULL, ao2_cleanup);
		struct sla_ringing_trunk *ringing_trunk = NULL;
		struct run_station_args args;
		enum ast_dial_result dial_res;
		pthread_t dont_care;
		ast_mutex_t cond_lock;
		ast_cond_t cond;

		switch ((dial_res = ast_dial_state(ringing_station->station->dial))) {
		case AST_DIAL_RESULT_HANGUP:
		case AST_DIAL_RESULT_INVALID:
		case AST_DIAL_RESULT_FAILED:
		case AST_DIAL_RESULT_TIMEOUT:
		case AST_DIAL_RESULT_UNANSWERED:
			AST_LIST_REMOVE_CURRENT(entry);
			sla_stop_ringing_station(ringing_station, SLA_STATION_HANGUP_NORMAL);
			break;
		case AST_DIAL_RESULT_ANSWERED:
			AST_LIST_REMOVE_CURRENT(entry);
			/* Find the appropriate trunk to answer. */
			ast_mutex_lock(&sla.lock);
			ringing_trunk = sla_choose_ringing_trunk(ringing_station->station, &s_trunk_ref, 1);
			ast_mutex_unlock(&sla.lock);
			if (!ringing_trunk) {
				/* This case happens in a bit of a race condition.  If two stations answer
				 * the outbound call at the same time, the first one will get connected to
				 * the trunk.  When the second one gets here, it will not see any trunks
				 * ringing so we have no idea what to conect it to.  So, we just hang up
				 * on it. */
				ast_debug(1, "Found no ringing trunk for station '%s' to answer!\n", ringing_station->station->name);
				ast_dial_join(ringing_station->station->dial);
				ast_dial_destroy(ringing_station->station->dial);
				ringing_station->station->dial = NULL;
				sla_ringing_station_destroy(ringing_station);
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
			ao2_ref(s_trunk_ref, 1);
			args.trunk_ref = s_trunk_ref;
			ao2_ref(ringing_station->station, 1);
			args.station = ringing_station->station;
			args.cond = &cond;
			args.cond_lock = &cond_lock;
			sla_ringing_trunk_destroy(ringing_trunk);
			sla_ringing_station_destroy(ringing_station);
			ast_mutex_init(&cond_lock);
			ast_cond_init(&cond, NULL);
			ast_mutex_lock(&cond_lock);
			ast_pthread_create_detached_background(&dont_care, NULL, run_station, &args);
			ast_cond_wait(&cond, &cond_lock);
			ast_mutex_unlock(&cond_lock);
			ast_mutex_destroy(&cond_lock);
			ast_cond_destroy(&cond);
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
	AST_LIST_TRAVERSE_SAFE_END;
}

/*! \brief Check to see if this station is already ringing
 * \note Assumes sla.lock is locked
 */
static int sla_check_ringing_station(const struct sla_station *station)
{
	struct sla_ringing_station *ringing_station;

	AST_LIST_TRAVERSE(&sla.ringing_stations, ringing_station, entry) {
		if (station == ringing_station->station) {
			return 1;
		}
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
		if (station != failed_station->station) {
			continue;
		}
		if (ast_tvdiff_ms(ast_tvnow(), failed_station->last_try) > 1000) {
			AST_LIST_REMOVE_CURRENT(entry);
			sla_failed_station_destroy(failed_station);
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
	enum ast_dial_result res;
	int caller_is_saved;
	struct ast_party_caller caller;

	if (!(dial = ast_dial_create())) {
		return -1;
	}

	ast_dial_set_state_callback(dial, sla_dial_state_callback);
	tech_data = ast_strdupa(station->device);
	tech = strsep(&tech_data, "/");

	if (ast_dial_append(dial, tech, tech_data, NULL) == -1) {
		ast_dial_destroy(dial);
		return -1;
	}

	/* Do we need to save off the caller ID data? */
	caller_is_saved = 0;
	if (!sla.attempt_callerid) {
		caller_is_saved = 1;
		caller = *ast_channel_caller(ringing_trunk->trunk->chan);
		ast_party_caller_init(ast_channel_caller(ringing_trunk->trunk->chan));
	}

	res = ast_dial_run(dial, ringing_trunk->trunk->chan, 1);

	/* Restore saved caller ID */
	if (caller_is_saved) {
		ast_party_caller_free(ast_channel_caller(ringing_trunk->trunk->chan));
		ast_channel_caller_set(ringing_trunk->trunk->chan, &caller);
	}

	if (res != AST_DIAL_RESULT_TRYING) {
		struct sla_failed_station *failed_station;
		ast_dial_destroy(dial);
		if ((failed_station = sla_create_failed_station(station))) {
			AST_LIST_INSERT_HEAD(&sla.failed_stations, failed_station, entry);
		}
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
		if (trunk_ref->chan) {
			return 1;
		}
	}

	return 0;
}

static struct sla_trunk_ref *sla_find_trunk_ref(const struct sla_station *station, const struct sla_trunk *trunk)
{
	struct sla_trunk_ref *trunk_ref = NULL;

	AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
		if (trunk_ref->trunk == trunk) {
			break;
		}
	}

	ao2_ref(trunk_ref, 1);

	return trunk_ref;
}

/*! \brief Calculate the ring delay for a given ringing trunk on a station
 * \param station the station
 * \param ringing_trunk the trunk.  If NULL, the highest priority ringing trunk will be used
 * \return the number of ms left before the delay is complete, or INT_MAX if there is no delay
 */
static int sla_check_station_delay(struct sla_station *station, struct sla_ringing_trunk *ringing_trunk)
{
	RAII_VAR(struct sla_trunk_ref *, trunk_ref, NULL, ao2_cleanup);
	unsigned int delay = UINT_MAX;
	int time_left, time_elapsed;

	if (!ringing_trunk) {
		ringing_trunk = sla_choose_ringing_trunk(station, &trunk_ref, 0);
	} else {
		trunk_ref = sla_find_trunk_ref(station, ringing_trunk->trunk);
	}

	if (!ringing_trunk || !trunk_ref) {
		return delay;
	}

	/* If this station has a ring delay specific to the highest priority
	 * ringing trunk, use that.  Otherwise, use the ring delay specified
	 * globally for the station. */
	delay = trunk_ref->ring_delay;
	if (!delay) {
		delay = station->ring_delay;
	}
	if (!delay) {
		return INT_MAX;
	}

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
			if (sla_check_ringing_station(station_ref->station)) {
				continue;
			}

			/* Is this station already in a call? */
			if (sla_check_inuse_station(station_ref->station)) {
				continue;
			}

			/* Did we fail to dial this station earlier?  If so, has it been
			 * a minute since we tried? */
			if (sla_check_failed_station(station_ref->station)) {
				continue;
			}

			/* If this station already timed out while this trunk was ringing,
			 * do not dial it again for this ringing trunk. */
			if (sla_check_timed_out_station(ringing_trunk, station_ref->station)) {
				continue;
			}

			/* Check for a ring delay in progress */
			time_left = sla_check_station_delay(station_ref->station, ringing_trunk);
			if (time_left != INT_MAX && time_left > 0) {
				continue;
			}

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
				if (trunk_ref->trunk == ringing_trunk->trunk) {
					break;
				}
			}
			ast_mutex_unlock(&sla.lock);
			if (ringing_trunk) {
				break;
			}
		}
		if (!trunk_ref) {
			AST_LIST_REMOVE_CURRENT(entry);
			ast_dial_join(ringing_station->station->dial);
			ast_dial_destroy(ringing_station->station->dial);
			ringing_station->station->dial = NULL;
			sla_ringing_station_destroy(ringing_station);
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
	ast_devstate_changed(AST_DEVICE_ONHOLD, AST_DEVSTATE_CACHABLE, "SLA:%s_%s", event->station->name, event->trunk_ref->trunk->name);
	sla_change_trunk_state(event->trunk_ref->trunk, SLA_TRUNK_STATE_ONHOLD, INACTIVE_TRUNK_REFS, event->trunk_ref);

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
		if (!ringing_trunk->trunk->ring_timeout) {
			continue;
		}
		time_elapsed = ast_tvdiff_ms(ast_tvnow(), ringing_trunk->ring_begin);
		time_left = (ringing_trunk->trunk->ring_timeout * 1000) - time_elapsed;
		if (time_left <= 0) {
			pbx_builtin_setvar_helper(ringing_trunk->trunk->chan, "SLATRUNK_STATUS", "RINGTIMEOUT");
			AST_LIST_REMOVE_CURRENT(entry);
			sla_stop_ringing_trunk(ringing_trunk);
			res = 1;
			continue;
		}
		if (time_left < *timeout) {
			*timeout = time_left;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

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
				if (ringing_trunk->trunk == trunk_ref->trunk) {
					break;
				}
			}
			if (!ringing_trunk) {
				continue;
			}

			/* If there is a trunk that is ringing without a timeout, then the
			 * only timeout that could matter is a global station ring timeout. */
			if (!trunk_ref->ring_timeout) {
				break;
			}

			/* This trunk on this station is ringing and has a timeout.
			 * However, make sure this trunk isn't still ringing from a
			 * previous timeout.  If so, don't consider it. */
			AST_LIST_TRAVERSE(&ringing_trunk->timed_out_stations, station_ref, entry) {
				if (station_ref->station == ringing_station->station) {
					break;
				}
			}
			if (station_ref) {
				continue;
			}

			trunk_time_elapsed = ast_tvdiff_ms(ast_tvnow(), ringing_trunk->ring_begin);
			trunk_time_left = (trunk_ref->ring_timeout * 1000) - trunk_time_elapsed;
			if (trunk_time_left > final_trunk_time_left) {
				final_trunk_time_left = trunk_time_left;
			}
		}

		/* No timeout was found for ringing trunks, and no timeout for the entire station */
		if (final_trunk_time_left == INT_MIN && !ringing_station->station->ring_timeout) {
			continue;
		}

		/* Compute how much time is left for a global station timeout */
		if (ringing_station->station->ring_timeout) {
			ring_timeout = ringing_station->station->ring_timeout;
			time_elapsed = ast_tvdiff_ms(ast_tvnow(), ringing_station->ring_begin);
			time_left = (ring_timeout * 1000) - time_elapsed;
		}

		/* If the time left based on the per-trunk timeouts is smaller than the
		 * global station ring timeout, use that. */
		if (final_trunk_time_left > INT_MIN && final_trunk_time_left < time_left) {
			time_left = final_trunk_time_left;
		}

		/* If there is no time left, the station needs to stop ringing */
		if (time_left <= 0) {
			AST_LIST_REMOVE_CURRENT(entry);
			sla_stop_ringing_station(ringing_station, SLA_STATION_HANGUP_TIMEOUT);
			res = 1;
			continue;
		}

		/* There is still some time left for this station to ring, so save that
		 * timeout if it is the first event scheduled to occur */
		if (time_left < *timeout) {
			*timeout = time_left;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	return res;
}

/*! \brief Calculate the ring delay for a station
 * \note Assumes sla.lock is locked
 */
static int sla_calc_station_delays(unsigned int *timeout)
{
	struct sla_station *station;
	int res = 0;
	struct ao2_iterator i;

	i = ao2_iterator_init(sla_stations, 0);
	for (; (station = ao2_iterator_next(&i)); ao2_ref(station, -1)) {
		struct sla_ringing_trunk *ringing_trunk;
		int time_left;

		/* Ignore stations already ringing */
		if (sla_check_ringing_station(station)) {
			continue;
		}

		/* Ignore stations already on a call */
		if (sla_check_inuse_station(station)) {
			continue;
		}

		/* Ignore stations that don't have one of their trunks ringing */
		if (!(ringing_trunk = sla_choose_ringing_trunk(station, NULL, 0))) {
			continue;
		}

		if ((time_left = sla_check_station_delay(station, ringing_trunk)) == INT_MAX) {
			continue;
		}

		/* If there is no time left, then the station needs to start ringing.
		 * Return non-zero so that an event will be queued up an event to
		 * make that happen. */
		if (time_left <= 0) {
			res = 1;
			continue;
		}

		if (time_left < *timeout) {
			*timeout = time_left;
		}
	}
	ao2_iterator_destroy(&i);

	return res;
}

/*! \brief Calculate the time until the next known event
 *  \note Called with sla.lock locked */
static int sla_process_timers(struct timespec *ts)
{
	unsigned int timeout = UINT_MAX;
	struct timeval wait;
	unsigned int change_made = 0;

	/* Check for ring timeouts on ringing trunks */
	if (sla_calc_trunk_timeouts(&timeout)) {
		change_made = 1;
	}

	/* Check for ring timeouts on ringing stations */
	if (sla_calc_station_timeouts(&timeout)) {
		change_made = 1;
	}

	/* Check for station ring delays */
	if (sla_calc_station_delays(&timeout)) {
		change_made = 1;
	}

	/* queue reprocessing of ringing trunks */
	if (change_made) {
		sla_queue_event_nolock(SLA_EVENT_RINGING_TRUNK);
	}

	/* No timeout */
	if (timeout == UINT_MAX) {
		return 0;
	}

	if (ts) {
		wait = ast_tvadd(ast_tvnow(), ast_samp2tv(timeout, 1000));
		ts->tv_sec = wait.tv_sec;
		ts->tv_nsec = wait.tv_usec * 1000;
	}

	return 1;
}

static void sla_event_destroy(struct sla_event *event)
{
	if (event->trunk_ref) {
		ao2_ref(event->trunk_ref, -1);
		event->trunk_ref = NULL;
	}

	if (event->station) {
		ao2_ref(event->station, -1);
		event->station = NULL;
	}

	ast_free(event);
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
			if ((have_timeout = sla_process_timers(&ts))) {
				ast_cond_timedwait(&sla.cond, &sla.lock, &ts);
			} else {
				ast_cond_wait(&sla.cond, &sla.lock);
			}
			if (sla.stop) {
				break;
			}
		}

		if (have_timeout) {
			sla_process_timers(NULL);
		}

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
			sla_event_destroy(event);
			ast_mutex_lock(&sla.lock);
		}
	}

	ast_mutex_unlock(&sla.lock);

	while ((ringing_station = AST_LIST_REMOVE_HEAD(&sla.ringing_stations, entry))) {
		sla_ringing_station_destroy(ringing_station);
	}

	while ((failed_station = AST_LIST_REMOVE_HEAD(&sla.failed_stations, entry))) {
		sla_failed_station_destroy(failed_station);
	}

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
	struct ast_flags conf_flags = { 0 };
	RAII_VAR(struct sla_trunk_ref *, trunk_ref, args->trunk_ref, ao2_cleanup);
	RAII_VAR(struct sla_station *, station, args->station, ao2_cleanup);
	int caller_is_saved;
	struct ast_party_caller caller;
	int last_state = 0;
	int current_state = 0;

	if (!(dial = ast_dial_create())) {
		ast_mutex_lock(args->cond_lock);
		ast_cond_signal(args->cond);
		ast_mutex_unlock(args->cond_lock);
		return NULL;
	}

	tech_data = ast_strdupa(trunk_ref->trunk->device);
	tech = strsep(&tech_data, "/");
	if (ast_dial_append(dial, tech, tech_data, NULL) == -1) {
		ast_mutex_lock(args->cond_lock);
		ast_cond_signal(args->cond);
		ast_mutex_unlock(args->cond_lock);
		ast_dial_destroy(dial);
		return NULL;
	}

	/* Do we need to save of the caller ID data? */
	caller_is_saved = 0;
	if (!sla.attempt_callerid) {
		caller_is_saved = 1;
		caller = *ast_channel_caller(trunk_ref->chan);
		ast_party_caller_init(ast_channel_caller(trunk_ref->chan));
	}

	dial_res = ast_dial_run(dial, trunk_ref->chan, 1);

	/* Restore saved caller ID */
	if (caller_is_saved) {
		ast_party_caller_free(ast_channel_caller(trunk_ref->chan));
		ast_channel_caller_set(trunk_ref->chan, &caller);
	}

	if (dial_res != AST_DIAL_RESULT_TRYING) {
		ast_mutex_lock(args->cond_lock);
		ast_cond_signal(args->cond);
		ast_mutex_unlock(args->cond_lock);
		ast_dial_destroy(dial);
		return NULL;
	}

	/* Wait for dial to end, while servicing the channel */
	while (ast_waitfor(trunk_ref->chan, 100)) {
		unsigned int done = 0;
		struct ast_frame *fr = ast_read(trunk_ref->chan);

		if (!fr) {
			ast_debug(1, "Channel %s did not return a frame, must have hung up\n", ast_channel_name(trunk_ref->chan));
			done = 1;
			break;
		}
		ast_frfree(fr); /* Ignore while dialing */

		switch ((dial_res = ast_dial_state(dial))) {
		case AST_DIAL_RESULT_ANSWERED:
			trunk_ref->trunk->chan = ast_dial_answered(dial);
		case AST_DIAL_RESULT_HANGUP:
		case AST_DIAL_RESULT_INVALID:
		case AST_DIAL_RESULT_FAILED:
		case AST_DIAL_RESULT_TIMEOUT:
		case AST_DIAL_RESULT_UNANSWERED:
			done = 1;
			break;
		case AST_DIAL_RESULT_TRYING:
			current_state = AST_CONTROL_PROGRESS;
			break;
		case AST_DIAL_RESULT_RINGING:
		case AST_DIAL_RESULT_PROGRESS:
		case AST_DIAL_RESULT_PROCEEDING:
			current_state = AST_CONTROL_RINGING;
			break;
		}
		if (done) {
			break;
		}

		/* check that SLA station that originated trunk call is still alive */
		if (station && ast_device_state(station->device) == AST_DEVICE_NOT_INUSE) {
			ast_debug(3, "Originating station device %s no longer active\n", station->device);
			trunk_ref->trunk->chan = NULL;
			break;
		}

		/* If trunk line state changed, send indication back to originating SLA Station channel */
		if (current_state != last_state) {
			ast_debug(3, "Indicating State Change %d to channel %s\n", current_state, ast_channel_name(trunk_ref->chan));
			ast_indicate(trunk_ref->chan, current_state);
			last_state = current_state;
		}
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
	ast_set_flag(&conf_flags, CONFFLAG_QUIET | CONFFLAG_MARKEDEXIT | CONFFLAG_MARKEDUSER | CONFFLAG_PASS_DTMF | CONFFLAG_SLA_TRUNK);

	ast_mutex_lock(args->cond_lock);
	ast_cond_signal(args->cond);
	ast_mutex_unlock(args->cond_lock);

	ast_debug(2, "Trunk dial %s joining conference %s\n", trunk_ref->trunk->name, conf_name);
	conf_run(trunk_ref->trunk->chan, conf_name, &conf_flags, NULL);

	/* If the trunk is going away, it is definitely now IDLE. */
	sla_change_trunk_state(trunk_ref->trunk, SLA_TRUNK_STATE_IDLE, ALL_TRUNK_REFS, NULL);

	trunk_ref->trunk->chan = NULL;
	trunk_ref->trunk->on_hold = 0;

	ast_dial_join(dial);
	ast_dial_destroy(dial);

	return NULL;
}

/*!
 * \brief For a given station, choose the highest priority idle trunk
 * \pre sla_station is locked
 */
static struct sla_trunk_ref *sla_choose_idle_trunk(const struct sla_station *station)
{
	struct sla_trunk_ref *trunk_ref = NULL;

	AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
		if (trunk_ref->state == SLA_TRUNK_STATE_IDLE) {
			ao2_ref(trunk_ref, 1);
			break;
		}
	}

	return trunk_ref;
}

static int sla_station_exec(struct ast_channel *chan, const char *data)
{
	char *station_name, *trunk_name;
	RAII_VAR(struct sla_station *, station, NULL, ao2_cleanup);
	RAII_VAR(struct sla_trunk_ref *, trunk_ref, NULL, ao2_cleanup);
	char conf_name[MAX_CONFNUM];
	struct ast_flags conf_flags = { 0 };

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

	station = sla_find_station(station_name);

	if (!station) {
		ast_log(LOG_WARNING, "Station '%s' not found!\n", station_name);
		pbx_builtin_setvar_helper(chan, "SLASTATION_STATUS", "FAILURE");
		return 0;
	}

	ao2_lock(station);
	if (!ast_strlen_zero(trunk_name)) {
		trunk_ref = sla_find_trunk_ref_byname(station, trunk_name);
	} else {
		trunk_ref = sla_choose_idle_trunk(station);
	}
	ao2_unlock(station);

	if (!trunk_ref) {
		if (ast_strlen_zero(trunk_name)) {
			ast_log(LOG_NOTICE, "No trunks available for call.\n");
		} else {
			ast_log(LOG_NOTICE, "Can't join existing call on trunk '%s' due to access controls.\n", trunk_name);
		}
		pbx_builtin_setvar_helper(chan, "SLASTATION_STATUS", "CONGESTION");
		return 0;
	}

	if (trunk_ref->state == SLA_TRUNK_STATE_ONHOLD_BYME) {
		if (ast_atomic_dec_and_test((int *) &trunk_ref->trunk->hold_stations) == 1) {
			sla_change_trunk_state(trunk_ref->trunk, SLA_TRUNK_STATE_UP, ALL_TRUNK_REFS, NULL);
		} else {
			trunk_ref->state = SLA_TRUNK_STATE_UP;
			ast_devstate_changed(AST_DEVICE_INUSE, AST_DEVSTATE_CACHABLE, "SLA:%s_%s", station->name, trunk_ref->trunk->name);
		}
	} else if (trunk_ref->state == SLA_TRUNK_STATE_RINGING) {
		struct sla_ringing_trunk *ringing_trunk;

		ast_mutex_lock(&sla.lock);
		AST_LIST_TRAVERSE_SAFE_BEGIN(&sla.ringing_trunks, ringing_trunk, entry) {
			if (ringing_trunk->trunk == trunk_ref->trunk) {
				AST_LIST_REMOVE_CURRENT(entry);
				break;
			}
		}
		AST_LIST_TRAVERSE_SAFE_END
		ast_mutex_unlock(&sla.lock);

		if (ringing_trunk) {
			answer_trunk_chan(ringing_trunk->trunk->chan);
			sla_change_trunk_state(ringing_trunk->trunk, SLA_TRUNK_STATE_UP, ALL_TRUNK_REFS, NULL);

			sla_ringing_trunk_destroy(ringing_trunk);

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
		struct dial_trunk_args args = {
			.trunk_ref = trunk_ref,
			.station = station,
			.cond_lock = &cond_lock,
			.cond = &cond,
		};
		ao2_ref(trunk_ref, 1);
		ao2_ref(station, 1);
		sla_change_trunk_state(trunk_ref->trunk, SLA_TRUNK_STATE_UP, ALL_TRUNK_REFS, NULL);
		/* Create a thread to dial the trunk and dump it into the conference.
		 * However, we want to wait until the trunk has been dialed and the
		 * conference is created before continuing on here.
		 * Don't autoservice the channel or we'll have multiple threads
		 * handling it. dial_trunk services the channel.
		 */
		ast_mutex_init(&cond_lock);
		ast_cond_init(&cond, NULL);
		ast_mutex_lock(&cond_lock);
		ast_pthread_create_detached_background(&dont_care, NULL, dial_trunk, &args);
		ast_cond_wait(&cond, &cond_lock);
		ast_mutex_unlock(&cond_lock);
		ast_mutex_destroy(&cond_lock);
		ast_cond_destroy(&cond);

		if (!trunk_ref->trunk->chan) {
			ast_debug(1, "Trunk didn't get created. chan: %lx\n", (unsigned long) trunk_ref->trunk->chan);
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
	ast_set_flag(&conf_flags, CONFFLAG_QUIET | CONFFLAG_MARKEDEXIT | CONFFLAG_PASS_DTMF | CONFFLAG_SLA_STATION);
	ast_answer(chan);

	ast_debug(2, "Station %s joining conference %s\n", station->name, conf_name);
	conf_run(chan, conf_name, &conf_flags, NULL);

	trunk_ref->chan = NULL;
	if (ast_atomic_dec_and_test((int *) &trunk_ref->trunk->active_stations) &&
		trunk_ref->state != SLA_TRUNK_STATE_ONHOLD_BYME) {
		conf_kick_all(chan, conf_name);
		trunk_ref->trunk->hold_stations = 0;
		sla_change_trunk_state(trunk_ref->trunk, SLA_TRUNK_STATE_IDLE, ALL_TRUNK_REFS, NULL);
	}

	pbx_builtin_setvar_helper(chan, "SLASTATION_STATUS", "SUCCESS");

	return 0;
}

static void sla_trunk_ref_destructor(void *obj)
{
	struct sla_trunk_ref *trunk_ref = obj;

	if (trunk_ref->trunk) {
		ao2_ref(trunk_ref->trunk, -1);
		trunk_ref->trunk = NULL;
	}
}

static struct sla_trunk_ref *create_trunk_ref(struct sla_trunk *trunk)
{
	struct sla_trunk_ref *trunk_ref;

	if (!(trunk_ref = ao2_alloc(sizeof(*trunk_ref), sla_trunk_ref_destructor))) {
		return NULL;
	}

	ao2_ref(trunk, 1);
	trunk_ref->trunk = trunk;

	return trunk_ref;
}

static struct sla_ringing_trunk *queue_ringing_trunk(struct sla_trunk *trunk)
{
	struct sla_ringing_trunk *ringing_trunk;

	if (!(ringing_trunk = ast_calloc(1, sizeof(*ringing_trunk)))) {
		return NULL;
	}

	ao2_ref(trunk, 1);
	ringing_trunk->trunk = trunk;
	ringing_trunk->ring_begin = ast_tvnow();

	sla_change_trunk_state(trunk, SLA_TRUNK_STATE_RINGING, ALL_TRUNK_REFS, NULL);

	ast_mutex_lock(&sla.lock);
	AST_LIST_INSERT_HEAD(&sla.ringing_trunks, ringing_trunk, entry);
	ast_mutex_unlock(&sla.lock);

	sla_queue_event(SLA_EVENT_RINGING_TRUNK);

	return ringing_trunk;
}

static void sla_ringing_trunk_destroy(struct sla_ringing_trunk *ringing_trunk)
{
	if (ringing_trunk->trunk) {
		ao2_ref(ringing_trunk->trunk, -1);
		ringing_trunk->trunk = NULL;
	}

	ast_free(ringing_trunk);
}

static int sla_trunk_exec(struct ast_channel *chan, const char *data)
{
	char conf_name[MAX_CONFNUM];
	struct ast_flags conf_flags = { 0 };
	RAII_VAR(struct sla_trunk *, trunk, NULL, ao2_cleanup);
	struct sla_ringing_trunk *ringing_trunk;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(trunk_name);
		AST_APP_ARG(options);
	);
	char *opts[SLA_TRUNK_OPT_ARG_ARRAY_SIZE] = { NULL, };
	struct ast_flags opt_flags = { 0 };
	char *parse;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "The SLATrunk application requires an argument, the trunk name\n");
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);
	if (args.argc == 2) {
		if (ast_app_parse_options(sla_trunk_opts, &opt_flags, opts, args.options)) {
			ast_log(LOG_ERROR, "Error parsing options for SLATrunk\n");
			return -1;
		}
	}

	trunk = sla_find_trunk(args.trunk_name);

	if (!trunk) {
		ast_log(LOG_ERROR, "SLA Trunk '%s' not found!\n", args.trunk_name);
		pbx_builtin_setvar_helper(chan, "SLATRUNK_STATUS", "FAILURE");
		return 0;
	}

	if (trunk->chan) {
		ast_log(LOG_ERROR, "Call came in on %s, but the trunk is already in use!\n", args.trunk_name);
		pbx_builtin_setvar_helper(chan, "SLATRUNK_STATUS", "FAILURE");
		return 0;
	}

	trunk->chan = chan;

	if (!(ringing_trunk = queue_ringing_trunk(trunk))) {
		pbx_builtin_setvar_helper(chan, "SLATRUNK_STATUS", "FAILURE");
		return 0;
	}

	snprintf(conf_name, sizeof(conf_name), "SLA_%s", args.trunk_name);
	ast_set_flag(&conf_flags, CONFFLAG_QUIET | CONFFLAG_MARKEDEXIT | CONFFLAG_MARKEDUSER | CONFFLAG_PASS_DTMF);

	if (ast_test_flag(&opt_flags, SLA_TRUNK_OPT_MOH)) {
		ast_indicate(chan, -1);
		ast_set_flag(&conf_flags, CONFFLAG_MOH);
	} else {
		ast_indicate(chan, AST_CONTROL_RINGING);
	}

	ast_debug(2, "Trunk %s joining conference %s\n", args.trunk_name, conf_name);
	conf_run(chan, conf_name, &conf_flags, opts);
	trunk->chan = NULL;
	trunk->on_hold = 0;

	sla_change_trunk_state(trunk, SLA_TRUNK_STATE_IDLE, ALL_TRUNK_REFS, NULL);

	if (!pbx_builtin_getvar_helper(chan, "SLATRUNK_STATUS")) {
		pbx_builtin_setvar_helper(chan, "SLATRUNK_STATUS", "SUCCESS");
	}

	/* Remove the entry from the list of ringing trunks if it is still there. */
	ast_mutex_lock(&sla.lock);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&sla.ringing_trunks, ringing_trunk, entry) {
		if (ringing_trunk->trunk == trunk) {
			AST_LIST_REMOVE_CURRENT(entry);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	ast_mutex_unlock(&sla.lock);
	if (ringing_trunk) {
		sla_ringing_trunk_destroy(ringing_trunk);
		pbx_builtin_setvar_helper(chan, "SLATRUNK_STATUS", "UNANSWERED");
		/* Queue reprocessing of ringing trunks to make stations stop ringing
		 * that shouldn't be ringing after this trunk stopped. */
		sla_queue_event(SLA_EVENT_RINGING_TRUNK);
	}

	return 0;
}

static enum ast_device_state sla_state(const char *data)
{
	char *buf, *station_name, *trunk_name;
	RAII_VAR(struct sla_station *, station, NULL, ao2_cleanup);
	struct sla_trunk_ref *trunk_ref;
	enum ast_device_state res = AST_DEVICE_INVALID;

	trunk_name = buf = ast_strdupa(data);
	station_name = strsep(&trunk_name, "_");

	station = sla_find_station(station_name);
	if (station) {
		ao2_lock(station);
		AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
			if (!strcasecmp(trunk_name, trunk_ref->trunk->name)) {
				res = sla_state_to_devstate(trunk_ref->state);
				break;
			}
		}
		ao2_unlock(station);
	}

	if (res == AST_DEVICE_INVALID) {
		ast_log(LOG_ERROR, "Could not determine state for trunk %s on station %s!\n", trunk_name, station_name);
	}

	return res;
}

static int sla_trunk_release_refs(void *obj, void *arg, int flags)
{
	struct sla_trunk *trunk = obj;
	struct sla_station_ref *station_ref;

	while ((station_ref = AST_LIST_REMOVE_HEAD(&trunk->stations, entry))) {
		ao2_ref(station_ref, -1);
	}

	return 0;
}

static int sla_station_release_refs(void *obj, void *arg, int flags)
{
	struct sla_station *station = obj;
	struct sla_trunk_ref *trunk_ref;

	while ((trunk_ref = AST_LIST_REMOVE_HEAD(&station->trunks, entry))) {
		ao2_ref(trunk_ref, -1);
	}

	return 0;
}

static void sla_station_destructor(void *obj)
{
	struct sla_station *station = obj;

	ast_debug(1, "sla_station destructor for '%s'\n", station->name);

	if (!ast_strlen_zero(station->autocontext)) {
		struct sla_trunk_ref *trunk_ref;

		AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
			char exten[AST_MAX_EXTENSION];
			char hint[AST_MAX_EXTENSION + 5];
			snprintf(exten, sizeof(exten), "%s_%s", station->name, trunk_ref->trunk->name);
			snprintf(hint, sizeof(hint), "SLA:%s", exten);
			ast_context_remove_extension(station->autocontext, exten, 1, sla_registrar);
			ast_context_remove_extension(station->autocontext, hint, PRIORITY_HINT, sla_registrar);
		}
	}

	sla_station_release_refs(station, NULL, 0);

	ast_string_field_free_memory(station);
}

static int sla_trunk_cmp(void *obj, void *arg, int flags)
{
	struct sla_trunk *trunk = obj, *trunk2 = arg;

	return !strcasecmp(trunk->name, trunk2->name) ? CMP_MATCH | CMP_STOP : 0;
}

static int sla_station_cmp(void *obj, void *arg, int flags)
{
	struct sla_station *station = obj, *station2 = arg;

	return !strcasecmp(station->name, station2->name) ? CMP_MATCH | CMP_STOP : 0;
}

static void sla_destroy(void)
{
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

	ao2_callback(sla_trunks, 0, sla_trunk_release_refs, NULL);
	ao2_callback(sla_stations, 0, sla_station_release_refs, NULL);

	ao2_ref(sla_trunks, -1);
	sla_trunks = NULL;

	ao2_ref(sla_stations, -1);
	sla_stations = NULL;
}

static int sla_check_device(const char *device)
{
	char *tech, *tech_data;

	tech_data = ast_strdupa(device);
	tech = strsep(&tech_data, "/");

	if (ast_strlen_zero(tech) || ast_strlen_zero(tech_data)) {
		return -1;
	}

	return 0;
}

static void sla_trunk_destructor(void *obj)
{
	struct sla_trunk *trunk = obj;

	ast_debug(1, "sla_trunk destructor for '%s'\n", trunk->name);

	if (!ast_strlen_zero(trunk->autocontext)) {
		ast_context_remove_extension(trunk->autocontext, "s", 1, sla_registrar);
	}

	sla_trunk_release_refs(trunk, NULL, 0);

	ast_string_field_free_memory(trunk);
}

static int sla_build_trunk(struct ast_config *cfg, const char *cat)
{
	RAII_VAR(struct sla_trunk *, trunk, NULL, ao2_cleanup);
	struct ast_variable *var;
	const char *dev;
	int existing_trunk = 0;

	if (!(dev = ast_variable_retrieve(cfg, cat, "device"))) {
		ast_log(LOG_ERROR, "SLA Trunk '%s' defined with no device!\n", cat);
		return -1;
	}

	if (sla_check_device(dev)) {
		ast_log(LOG_ERROR, "SLA Trunk '%s' defined with invalid device '%s'!\n", cat, dev);
		return -1;
	}

	if ((trunk = sla_find_trunk(cat))) {
		trunk->mark = 0;
		existing_trunk = 1;
	} else if ((trunk = ao2_alloc(sizeof(*trunk), sla_trunk_destructor))) {
		if (ast_string_field_init(trunk, 32)) {
			return -1;
		}
		ast_string_field_set(trunk, name, cat);
	} else {
		return -1;
	}

	ao2_lock(trunk);

	ast_string_field_set(trunk, device, dev);

	for (var = ast_variable_browse(cfg, cat); var; var = var->next) {
		if (!strcasecmp(var->name, "autocontext")) {
			ast_string_field_set(trunk, autocontext, var->value);
		} else if (!strcasecmp(var->name, "ringtimeout")) {
			if (sscanf(var->value, "%30u", &trunk->ring_timeout) != 1) {
				ast_log(LOG_WARNING, "Invalid ringtimeout '%s' specified for trunk '%s'\n", var->value, trunk->name);
				trunk->ring_timeout = 0;
			}
		} else if (!strcasecmp(var->name, "barge")) {
			trunk->barge_disabled = ast_false(var->value);
		} else if (!strcasecmp(var->name, "hold")) {
			if (!strcasecmp(var->value, "private")) {
				trunk->hold_access = SLA_HOLD_PRIVATE;
			} else if (!strcasecmp(var->value, "open")) {
				trunk->hold_access = SLA_HOLD_OPEN;
			} else {
				ast_log(LOG_WARNING, "Invalid value '%s' for hold on trunk %s\n", var->value, trunk->name);
			}
		} else if (strcasecmp(var->name, "type") && strcasecmp(var->name, "device")) {
			ast_log(LOG_ERROR, "Invalid option '%s' specified at line %d of %s!\n", var->name, var->lineno, SLA_CONFIG_FILE);
		}
	}

	ao2_unlock(trunk);

	if (!ast_strlen_zero(trunk->autocontext)) {
		if (!ast_context_find_or_create(NULL, NULL, trunk->autocontext, sla_registrar)) {
			ast_log(LOG_ERROR, "Failed to automatically find or create context '%s' for SLA!\n", trunk->autocontext);
			return -1;
		}

		if (ast_add_extension(trunk->autocontext, 0 /* don't replace */, "s", 1,
			NULL, NULL, slatrunk_app, ast_strdup(trunk->name), ast_free_ptr, sla_registrar)) {
			ast_log(LOG_ERROR, "Failed to automatically create extension for trunk '%s'!\n", trunk->name);
			return -1;
		}
	}

	if (!existing_trunk) {
		ao2_link(sla_trunks, trunk);
	}

	return 0;
}

/*!
 * \internal
 * \pre station is not locked
 */
static void sla_add_trunk_to_station(struct sla_station *station, struct ast_variable *var)
{
	RAII_VAR(struct sla_trunk *, trunk, NULL, ao2_cleanup);
	struct sla_trunk_ref *trunk_ref = NULL;
	struct sla_station_ref *station_ref;
	char *trunk_name, *options, *cur;
	int existing_trunk_ref = 0;
	int existing_station_ref = 0;

	options = ast_strdupa(var->value);
	trunk_name = strsep(&options, ",");

	trunk = sla_find_trunk(trunk_name);
	if (!trunk) {
		ast_log(LOG_ERROR, "Trunk '%s' not found!\n", var->value);
		return;
	}

	AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
		if (trunk_ref->trunk == trunk) {
			trunk_ref->mark = 0;
			existing_trunk_ref = 1;
			break;
		}
	}

	if (!trunk_ref && !(trunk_ref = create_trunk_ref(trunk))) {
		return;
	}

	trunk_ref->state = SLA_TRUNK_STATE_IDLE;

	while ((cur = strsep(&options, ","))) {
		char *name, *value = cur;
		name = strsep(&value, "=");
		if (!strcasecmp(name, "ringtimeout")) {
			if (sscanf(value, "%30u", &trunk_ref->ring_timeout) != 1) {
				ast_log(LOG_WARNING, "Invalid ringtimeout value '%s' for trunk '%s' on station '%s'\n", value, trunk->name, station->name);
				trunk_ref->ring_timeout = 0;
			}
		} else if (!strcasecmp(name, "ringdelay")) {
			if (sscanf(value, "%30u", &trunk_ref->ring_delay) != 1) {
				ast_log(LOG_WARNING, "Invalid ringdelay value '%s' for trunk '%s' on station '%s'\n", value, trunk->name, station->name);
				trunk_ref->ring_delay = 0;
			}
		} else {
			ast_log(LOG_WARNING, "Invalid option '%s' for trunk '%s' on station '%s'\n", name, trunk->name, station->name);
		}
	}

	AST_LIST_TRAVERSE(&trunk->stations, station_ref, entry) {
		if (station_ref->station == station) {
			station_ref->mark = 0;
			existing_station_ref = 1;
			break;
		}
	}

	if (!station_ref && !(station_ref = sla_create_station_ref(station))) {
		if (!existing_trunk_ref) {
			ao2_ref(trunk_ref, -1);
		} else {
			trunk_ref->mark = 1;
		}
		return;
	}

	if (!existing_station_ref) {
		ao2_lock(trunk);
		AST_LIST_INSERT_TAIL(&trunk->stations, station_ref, entry);
		ast_atomic_fetchadd_int((int *) &trunk->num_stations, 1);
		ao2_unlock(trunk);
	}

	if (!existing_trunk_ref) {
		ao2_lock(station);
		AST_LIST_INSERT_TAIL(&station->trunks, trunk_ref, entry);
		ao2_unlock(station);
	}
}

static int sla_build_station(struct ast_config *cfg, const char *cat)
{
	RAII_VAR(struct sla_station *, station, NULL, ao2_cleanup);
	struct ast_variable *var;
	const char *dev;
	int existing_station = 0;

	if (!(dev = ast_variable_retrieve(cfg, cat, "device"))) {
		ast_log(LOG_ERROR, "SLA Station '%s' defined with no device!\n", cat);
		return -1;
	}

	if ((station = sla_find_station(cat))) {
		station->mark = 0;
		existing_station = 1;
	} else if ((station = ao2_alloc(sizeof(*station), sla_station_destructor))) {
		if (ast_string_field_init(station, 32)) {
			return -1;
		}
		ast_string_field_set(station, name, cat);
	} else {
		return -1;
	}

	ao2_lock(station);

	ast_string_field_set(station, device, dev);

	for (var = ast_variable_browse(cfg, cat); var; var = var->next) {
		if (!strcasecmp(var->name, "trunk")) {
			ao2_unlock(station);
			sla_add_trunk_to_station(station, var);
			ao2_lock(station);
		} else if (!strcasecmp(var->name, "autocontext")) {
			ast_string_field_set(station, autocontext, var->value);
		} else if (!strcasecmp(var->name, "ringtimeout")) {
			if (sscanf(var->value, "%30u", &station->ring_timeout) != 1) {
				ast_log(LOG_WARNING, "Invalid ringtimeout '%s' specified for station '%s'\n", var->value, station->name);
				station->ring_timeout = 0;
			}
		} else if (!strcasecmp(var->name, "ringdelay")) {
			if (sscanf(var->value, "%30u", &station->ring_delay) != 1) {
				ast_log(LOG_WARNING, "Invalid ringdelay '%s' specified for station '%s'\n", var->value, station->name);
				station->ring_delay = 0;
			}
		} else if (!strcasecmp(var->name, "hold")) {
			if (!strcasecmp(var->value, "private")) {
				station->hold_access = SLA_HOLD_PRIVATE;
			} else if (!strcasecmp(var->value, "open")) {
				station->hold_access = SLA_HOLD_OPEN;
			} else {
				ast_log(LOG_WARNING, "Invalid value '%s' for hold on station %s\n", var->value, station->name);
			}
		} else if (strcasecmp(var->name, "type") && strcasecmp(var->name, "device")) {
			ast_log(LOG_ERROR, "Invalid option '%s' specified at line %d of %s!\n", var->name, var->lineno, SLA_CONFIG_FILE);
		}
	}

	ao2_unlock(station);

	if (!ast_strlen_zero(station->autocontext)) {
		struct sla_trunk_ref *trunk_ref;

		if (!ast_context_find_or_create(NULL, NULL, station->autocontext, sla_registrar)) {
			ast_log(LOG_ERROR, "Failed to automatically find or create context '%s' for SLA!\n", station->autocontext);
			return -1;
		}
		/* The extension for when the handset goes off-hook.
		 * exten => station1,1,SLAStation(station1) */
		if (ast_add_extension(station->autocontext, 0 /* don't replace */, station->name, 1,
			NULL, NULL, slastation_app, ast_strdup(station->name), ast_free_ptr, sla_registrar)) {
			ast_log(LOG_ERROR, "Failed to automatically create extension for trunk '%s'!\n", station->name);
			return -1;
		}
		AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
			char exten[AST_MAX_EXTENSION];
			char hint[AST_MAX_EXTENSION + 5];
			snprintf(exten, sizeof(exten), "%s_%s", station->name, trunk_ref->trunk->name);
			snprintf(hint, sizeof(hint), "SLA:%s", exten);
			/* Extension for this line button
			 * exten => station1_line1,1,SLAStation(station1_line1) */
			if (ast_add_extension(station->autocontext, 0 /* don't replace */, exten, 1,
				NULL, NULL, slastation_app, ast_strdup(exten), ast_free_ptr, sla_registrar)) {
				ast_log(LOG_ERROR, "Failed to automatically create extension for trunk '%s'!\n", station->name);
				return -1;
			}
			/* Hint for this line button
			 * exten => station1_line1,hint,SLA:station1_line1 */
			if (ast_add_extension(station->autocontext, 0 /* don't replace */, exten, PRIORITY_HINT,
				NULL, NULL, hint, NULL, NULL, sla_registrar)) {
				ast_log(LOG_ERROR, "Failed to automatically create hint for trunk '%s'!\n", station->name);
				return -1;
			}
		}
	}

	if (!existing_station) {
		ao2_link(sla_stations, station);
	}

	return 0;
}

static int sla_trunk_mark(void *obj, void *arg, int flags)
{
	struct sla_trunk *trunk = obj;
	struct sla_station_ref *station_ref;

	ao2_lock(trunk);

	trunk->mark = 1;

	AST_LIST_TRAVERSE(&trunk->stations, station_ref, entry) {
		station_ref->mark = 1;
	}

	ao2_unlock(trunk);

	return 0;
}

static int sla_station_mark(void *obj, void *arg, int flags)
{
	struct sla_station *station = obj;
	struct sla_trunk_ref *trunk_ref;

	ao2_lock(station);

	station->mark = 1;

	AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
		trunk_ref->mark = 1;
	}

	ao2_unlock(station);

	return 0;
}

static int sla_trunk_is_marked(void *obj, void *arg, int flags)
{
	struct sla_trunk *trunk = obj;

	ao2_lock(trunk);

	if (trunk->mark) {
		/* Only remove all of the station references if the trunk itself is going away */
		sla_trunk_release_refs(trunk, NULL, 0);
	} else {
		struct sla_station_ref *station_ref;

		/* Otherwise only remove references to stations no longer in the config */
		AST_LIST_TRAVERSE_SAFE_BEGIN(&trunk->stations, station_ref, entry) {
			if (!station_ref->mark) {
				continue;
			}
			AST_LIST_REMOVE_CURRENT(entry);
			ao2_ref(station_ref, -1);
		}
		AST_LIST_TRAVERSE_SAFE_END
	}

	ao2_unlock(trunk);

	return trunk->mark ? CMP_MATCH : 0;
}

static int sla_station_is_marked(void *obj, void *arg, int flags)
{
	struct sla_station *station = obj;

	ao2_lock(station);

	if (station->mark) {
		/* Only remove all of the trunk references if the station itself is going away */
		sla_station_release_refs(station, NULL, 0);
	} else {
		struct sla_trunk_ref *trunk_ref;

		/* Otherwise only remove references to trunks no longer in the config */
		AST_LIST_TRAVERSE_SAFE_BEGIN(&station->trunks, trunk_ref, entry) {
			if (!trunk_ref->mark) {
				continue;
			}
			AST_LIST_REMOVE_CURRENT(entry);
			ao2_ref(trunk_ref, -1);
		}
		AST_LIST_TRAVERSE_SAFE_END
	}

	ao2_unlock(station);

	return station->mark ? CMP_MATCH : 0;
}

static int sla_in_use(void)
{
	return ao2_container_count(sla_trunks) || ao2_container_count(sla_stations);
}

static int sla_load_config(int reload)
{
	struct ast_config *cfg;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	const char *cat = NULL;
	int res = 0;
	const char *val;

	if (!reload) {
		ast_mutex_init(&sla.lock);
		ast_cond_init(&sla.cond, NULL);
		sla_trunks = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL, sla_trunk_cmp);
		sla_stations = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL, sla_station_cmp);
	}

	if (!(cfg = ast_config_load(SLA_CONFIG_FILE, config_flags))) {
		return 0; /* Treat no config as normal */
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file " SLA_CONFIG_FILE " is in an invalid format.  Aborting.\n");
		return 0;
	}

	if (reload) {
		ao2_callback(sla_trunks, 0, sla_trunk_mark, NULL);
		ao2_callback(sla_stations, 0, sla_station_mark, NULL);
	}

	if ((val = ast_variable_retrieve(cfg, "general", "attemptcallerid"))) {
		sla.attempt_callerid = ast_true(val);
	}

	while ((cat = ast_category_browse(cfg, cat)) && !res) {
		const char *type;
		if (!strcasecmp(cat, "general")) {
			continue;
		}
		if (!(type = ast_variable_retrieve(cfg, cat, "type"))) {
			ast_log(LOG_WARNING, "Invalid entry in %s defined with no type!\n", SLA_CONFIG_FILE);
			continue;
		}
		if (!strcasecmp(type, "trunk")) {
			res = sla_build_trunk(cfg, cat);
		} else if (!strcasecmp(type, "station")) {
			res = sla_build_station(cfg, cat);
		} else {
			ast_log(LOG_WARNING, "Entry in %s defined with invalid type '%s'!\n", SLA_CONFIG_FILE, type);
		}
	}

	ast_config_destroy(cfg);

	if (reload) {
		ao2_callback(sla_trunks, OBJ_NODATA | OBJ_UNLINK | OBJ_MULTIPLE, sla_trunk_is_marked, NULL);
		ao2_callback(sla_stations, OBJ_NODATA | OBJ_UNLINK | OBJ_MULTIPLE, sla_station_is_marked, NULL);
	}

	/* Start SLA event processing thread once SLA has been configured. */
	if (sla.thread == AST_PTHREADT_NULL && sla_in_use()) {
		ast_pthread_create(&sla.thread, NULL, sla_thread, NULL);
	}

	return res;
}

static int load_config(int reload)
{
	return sla_load_config(reload);
}

static int unload_module(void)
{
	int res = 0;

	ast_cli_unregister_multiple(cli_sla, ARRAY_LEN(cli_sla));
	res |= ast_unregister_application(slastation_app);
	res |= ast_unregister_application(slatrunk_app);

	ast_devstate_prov_del("SLA");

	sla_destroy();

	return res;
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

	res |= load_config(0);

	ast_cli_register_multiple(cli_sla, ARRAY_LEN(cli_sla));
	res |= ast_register_application_xml(slastation_app, sla_station_exec);
	res |= ast_register_application_xml(slatrunk_app, sla_trunk_exec);

	res |= ast_devstate_prov_add("SLA", sla_state);

	return res;
}

static int reload(void)
{
	return load_config(1);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Shared Line Appearances",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_DEVSTATE_PROVIDER,
);
