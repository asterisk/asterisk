/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2008, Digium, Inc.
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
 * \brief Routines implementing call features as call pickup, parking and transfer
 *
 * \author Mark Spencer <markster@digium.com> 
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/_private.h"

#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <netinet/in.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/causes.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/app.h"
#include "asterisk/say.h"
#include "asterisk/features.h"
#include "asterisk/musiconhold.h"
#include "asterisk/config.h"
#include "asterisk/cli.h"
#include "asterisk/manager.h"
#include "asterisk/utils.h"
#include "asterisk/adsi.h"
#include "asterisk/devicestate.h"
#include "asterisk/monitor.h"
#include "asterisk/audiohook.h"
#include "asterisk/global_datastores.h"

#define DEFAULT_PARK_TIME 45000
#define DEFAULT_TRANSFER_DIGIT_TIMEOUT 3000
#define DEFAULT_FEATURE_DIGIT_TIMEOUT 1000
#define DEFAULT_NOANSWER_TIMEOUT_ATTENDED_TRANSFER 15000
#define DEFAULT_ATXFER_DROP_CALL 0
#define DEFAULT_ATXFER_LOOP_DELAY 10000
#define DEFAULT_ATXFER_CALLBACK_RETRIES 2

#define AST_MAX_WATCHERS 256
#define MAX_DIAL_FEATURE_OPTIONS 30

#define FEATURE_RETURN_HANGUP                  -1
#define FEATURE_RETURN_SUCCESSBREAK             0
#define FEATURE_RETURN_PASSDIGITS               21
#define FEATURE_RETURN_STOREDIGITS              22
#define FEATURE_RETURN_SUCCESS                  23
#define FEATURE_RETURN_KEEPTRYING               24
#define FEATURE_RETURN_PARKFAILED               25

enum {
	AST_FEATURE_FLAG_NEEDSDTMF = (1 << 0),
	AST_FEATURE_FLAG_ONPEER =    (1 << 1),
	AST_FEATURE_FLAG_ONSELF =    (1 << 2),
	AST_FEATURE_FLAG_BYCALLEE =  (1 << 3),
	AST_FEATURE_FLAG_BYCALLER =  (1 << 4),
	AST_FEATURE_FLAG_BYBOTH	 =   (3 << 3),
};

struct feature_group_exten {
	AST_LIST_ENTRY(feature_group_exten) entry;
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(exten);
	);
	struct ast_call_feature *feature;
};

struct feature_group {
	AST_LIST_ENTRY(feature_group) entry;
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(gname);
	);
	AST_LIST_HEAD_NOLOCK(, feature_group_exten) features;
};

static AST_RWLIST_HEAD_STATIC(feature_groups, feature_group);

static char *parkedcall = "ParkedCall";

static int parkaddhints = 0;                               /*!< Add parking hints automatically */
static int parkedcalltransfers = 0;                        /*!< Enable DTMF based transfers on bridge when picking up parked calls */
static int parkedcallreparking = 0;                        /*!< Enable DTMF based parking on bridge when picking up parked calls */
static int parkedcallhangup = 0;                           /*!< Enable DTMF based disconnect on bridge when picking up parked calls */
static int parkedcallrecording = 0;                        /*!< Enable DTMF based recording on bridge when picking up parked calls */
static int parkingtime = DEFAULT_PARK_TIME;                /*!< No more than 45 seconds parked before you do something with them */
static char parking_con[AST_MAX_EXTENSION];                /*!< Context for which parking is made accessible */
static char parking_con_dial[AST_MAX_EXTENSION];           /*!< Context for dialback for parking (KLUDGE) */
static char parking_ext[AST_MAX_EXTENSION];                /*!< Extension you type to park the call */
static char pickup_ext[AST_MAX_EXTENSION];                 /*!< Call pickup extension */
static char parkmohclass[MAX_MUSICCLASS];                  /*!< Music class used for parking */
static int parking_start;                                  /*!< First available extension for parking */
static int parking_stop;                                   /*!< Last available extension for parking */

static char courtesytone[256];                             /*!< Courtesy tone */
static int parkedplay = 0;                                 /*!< Who to play the courtesy tone to */
static char xfersound[256];                                /*!< Call transfer sound */
static char xferfailsound[256];                            /*!< Call transfer failure sound */

static int parking_offset;
static int parkfindnext;

static int adsipark;

static int transferdigittimeout;
static int featuredigittimeout;
static int comebacktoorigin = 1;

static int atxfernoanswertimeout;
static unsigned int atxferdropcall;
static unsigned int atxferloopdelay;
static unsigned int atxfercallbackretries;

static char *registrar = "features";		   /*!< Registrar for operations */

/* module and CLI command definitions */
static char *synopsis = "Answer a parked call";

static char *descrip = "ParkedCall(exten): "
"Used to connect to a parked call.  This application is always\n"
"registered internally and does not need to be explicitly added\n"
"into the dialplan, although you should include the 'parkedcalls'\n"
"context.  If no extension is provided, then the first available\n"
"parked call will be acquired.\n";

static char *parkcall = "Park";

static char *synopsis2 = "Park yourself";

static char *descrip2 = "Park(): "
"Used to park yourself (typically in combination with a supervised\n"
"transfer to know the parking space). This application is always\n"
"registered internally and does not need to be explicitly added\n"
"into the dialplan, although you should include the 'parkedcalls'\n"
"context (or the context specified in features.conf).\n\n"
"If you set the PARKINGEXTEN variable to an extension in your\n"
"parking context, Park() will park the call on that extension, unless\n"
"it already exists. In that case, execution will continue at next\n"
"priority.\n" ;

static struct ast_app *monitor_app = NULL;
static int monitor_ok = 1;

static struct ast_app *mixmonitor_app = NULL;
static int mixmonitor_ok = 1;

static struct ast_app *stopmixmonitor_app = NULL;
static int stopmixmonitor_ok = 1;

struct parkeduser {
	struct ast_channel *chan;                   /*!< Parking channel */
	struct timeval start;                       /*!< Time the parking started */
	int parkingnum;                             /*!< Parking lot */
	char parkingexten[AST_MAX_EXTENSION];       /*!< If set beforehand, parking extension used for this call */
	char context[AST_MAX_CONTEXT];              /*!< Where to go if our parking time expires */
	char exten[AST_MAX_EXTENSION];
	int priority;
	int parkingtime;                            /*!< Maximum length in parking lot before return */
	int notquiteyet;
	char peername[1024];
	unsigned char moh_trys;
	AST_LIST_ENTRY(parkeduser) list;
};
struct ast_dial_features {
	struct ast_flags features_caller;
	struct ast_flags features_callee;
	int is_caller;
};

static AST_LIST_HEAD_STATIC(parkinglot, parkeduser);

static void *dial_features_duplicate(void *data)
{
	struct ast_dial_features *df = data, *df_copy;
 
 	if (!(df_copy = ast_calloc(1, sizeof(*df)))) {
 		return NULL;
 	}
 
 	memcpy(df_copy, df, sizeof(*df));
 
 	return df_copy;
 }
 
 static void dial_features_destroy(void *data)
 {
 	struct ast_dial_features *df = data;
 	if (df) {
 		ast_free(df);
 	}
 }
 
 const struct ast_datastore_info dial_features_info = {
 	.type = "dial-features",
 	.destroy = dial_features_destroy,
 	.duplicate = dial_features_duplicate,
 };
 
static pthread_t parking_thread;

const char *ast_parking_ext(void)
{
	return parking_ext;
}

const char *ast_pickup_ext(void)
{
	return pickup_ext;
}

struct ast_bridge_thread_obj 
{
	struct ast_bridge_config bconfig;
	struct ast_channel *chan;
	struct ast_channel *peer;
	unsigned int return_to_pbx:1;
};



/*!
 * \brief store context, extension and priority 
 * \param chan, context, ext, pri
*/
static void set_c_e_p(struct ast_channel *chan, const char *context, const char *ext, int pri)
{
	ast_copy_string(chan->context, context, sizeof(chan->context));
	ast_copy_string(chan->exten, ext, sizeof(chan->exten));
	chan->priority = pri;
}

/*!
 * \brief Check goto on transfer
 * \param chan
 *
 * Check if channel has 'GOTO_ON_BLINDXFR' set, if not exit.
 * When found make sure the types are compatible. Check if channel is valid
 * if so start the new channel else hangup the call. 
*/
static void check_goto_on_transfer(struct ast_channel *chan) 
{
	struct ast_channel *xferchan;
	const char *val = pbx_builtin_getvar_helper(chan, "GOTO_ON_BLINDXFR");
	char *x, *goto_on_transfer;
	struct ast_frame *f;

	if (ast_strlen_zero(val))
		return;

	goto_on_transfer = ast_strdupa(val);

	if (!(xferchan = ast_channel_alloc(0, AST_STATE_DOWN, 0, 0, "", "", "", 0, "%s", chan->name)))
		return;

	for (x = goto_on_transfer; x && *x; x++) {
		if (*x == '^')
			*x = '|';
	}
	/* Make formats okay */
	xferchan->readformat = chan->readformat;
	xferchan->writeformat = chan->writeformat;
	ast_channel_masquerade(xferchan, chan);
	ast_parseable_goto(xferchan, goto_on_transfer);
	xferchan->_state = AST_STATE_UP;
	ast_clear_flag(xferchan, AST_FLAGS_ALL);	
	xferchan->_softhangup = 0;
	if ((f = ast_read(xferchan))) {
		ast_frfree(f);
		f = NULL;
		ast_pbx_start(xferchan);
	} else {
		ast_hangup(xferchan);
	}
}

static struct ast_channel *ast_feature_request_and_dial(struct ast_channel *caller, struct ast_channel *transferee, const char *type, int format, void *data, int timeout, int *outstate, const char *cid_num, const char *cid_name, int igncallerstate, const char *language);

/*!
 * \brief bridge the call 
 * \param data thread bridge.
 *
 * Set Last Data for respective channels, reset cdr for channels
 * bridge call, check if we're going back to dialplan
 * if not hangup both legs of the call
*/
static void *ast_bridge_call_thread(void *data)
{
	struct ast_bridge_thread_obj *tobj = data;
	int res;

	tobj->chan->appl = !tobj->return_to_pbx ? "Transferred Call" : "ManagerBridge";
	tobj->chan->data = tobj->peer->name;
	tobj->peer->appl = !tobj->return_to_pbx ? "Transferred Call" : "ManagerBridge";
	tobj->peer->data = tobj->chan->name;

	ast_bridge_call(tobj->peer, tobj->chan, &tobj->bconfig);

	if (tobj->return_to_pbx) {
		if (!ast_check_hangup(tobj->peer)) {
			ast_log(LOG_VERBOSE, "putting peer %s into PBX again\n", tobj->peer->name);
			res = ast_pbx_start(tobj->peer);
			if (res != AST_PBX_SUCCESS)
				ast_log(LOG_WARNING, "FAILED continuing PBX on peer %s\n", tobj->peer->name);
		} else
			ast_hangup(tobj->peer);
		if (!ast_check_hangup(tobj->chan)) {
			ast_log(LOG_VERBOSE, "putting chan %s into PBX again\n", tobj->chan->name);
			res = ast_pbx_start(tobj->chan);
			if (res != AST_PBX_SUCCESS)
				ast_log(LOG_WARNING, "FAILED continuing PBX on chan %s\n", tobj->chan->name);
		} else
			ast_hangup(tobj->chan);
	} else {
		ast_hangup(tobj->chan);
		ast_hangup(tobj->peer);
	}

	ast_free(tobj);

	return NULL;
}

/*!
 * \brief create thread for the parked call
 * \param data
 *
 * Create thread and attributes, call ast_bridge_call_thread
*/
static void ast_bridge_call_thread_launch(void *data) 
{
	pthread_t thread;
	pthread_attr_t attr;
	struct sched_param sched;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	ast_pthread_create(&thread, &attr,ast_bridge_call_thread, data);
	pthread_attr_destroy(&attr);
	memset(&sched, 0, sizeof(sched));
	pthread_setschedparam(thread, SCHED_RR, &sched);
}

/*!
 * \brief Announce call parking by ADSI
 * \param chan .
 * \param parkingexten .
 * Create message to show for ADSI, display message.
 * \retval 0 on success.
 * \retval -1 on failure.
*/
static int adsi_announce_park(struct ast_channel *chan, char *parkingexten)
{
	int res;
	int justify[5] = {ADSI_JUST_CENT, ADSI_JUST_CENT, ADSI_JUST_CENT, ADSI_JUST_CENT};
	char tmp[256];
	char *message[5] = {NULL, NULL, NULL, NULL, NULL};

	snprintf(tmp, sizeof(tmp), "Parked on %s", parkingexten);
	message[0] = tmp;
	res = ast_adsi_load_session(chan, NULL, 0, 1);
	if (res == -1)
		return res;
	return ast_adsi_print(chan, message, justify, 1);
}

/*! \brief Notify metermaids that we've changed an extension */
static void notify_metermaids(const char *exten, char *context, enum ast_device_state state)
{
	ast_debug(4, "Notification of state change to metermaids %s@%s\n to state '%s'", 
		exten, context, devstate2str(state));

	ast_devstate_changed(state, "park:%s@%s", exten, context);
}

/*! \brief metermaids callback from devicestate.c */
static enum ast_device_state metermaidstate(const char *data)
{
	char *context;
	char *exten;

	context = ast_strdupa(data);

	exten = strsep(&context, "@");
	if (!context)
		return AST_DEVICE_INVALID;
	
	ast_debug(4, "Checking state of exten %s in context %s\n", exten, context);

	if (!ast_exists_extension(NULL, context, exten, 1, NULL))
		return AST_DEVICE_NOT_INUSE;

	return AST_DEVICE_INUSE;
}

static struct parkeduser *park_space_reserve(struct ast_channel *chan)
{
	struct parkeduser *pu, *cur;
	int i, parking_space = -1, parking_range;
	const char *parkingexten;
	
	/* Allocate memory for parking data */
	if (!(pu = ast_calloc(1, sizeof(*pu)))) 
		return NULL;

	/* Lock parking lot */
	AST_LIST_LOCK(&parkinglot);
	/* Check for channel variable PARKINGEXTEN */
	parkingexten = ast_strdupa(S_OR(pbx_builtin_getvar_helper(chan, "PARKINGEXTEN"), ""));
	if (!ast_strlen_zero(parkingexten)) {
		/*!\note The API forces us to specify a numeric parking slot, even
		 * though the architecture would tend to support non-numeric extensions
		 * (as are possible with SIP, for example).  Hence, we enforce that
		 * limitation here.  If extout was not numeric, we could permit
		 * arbitrary non-numeric extensions.
		 */
        if (sscanf(parkingexten, "%30d", &parking_space) != 1 || parking_space < 0) {
			AST_LIST_UNLOCK(&parkinglot);
            ast_free(pu);
            ast_log(LOG_WARNING, "PARKINGEXTEN does not indicate a valid parking slot: '%s'.\n", parkingexten);
            return NULL;
        }
        snprintf(pu->parkingexten, sizeof(pu->parkingexten), "%d", parking_space);

		if (ast_exists_extension(NULL, parking_con, pu->parkingexten, 1, NULL)) {
			AST_LIST_UNLOCK(&parkinglot);
			ast_free(pu);
			ast_log(LOG_WARNING, "Requested parking extension already exists: %s@%s\n", parkingexten, parking_con);
			return NULL;
		}
	} else {
		/* Select parking space within range */
		parking_range = parking_stop - parking_start+1;
		for (i = 0; i < parking_range; i++) {
			parking_space = (i + parking_offset) % parking_range + parking_start;
			AST_LIST_TRAVERSE(&parkinglot, cur, list) {
				if (cur->parkingnum == parking_space)
					break;
			}
			if (!cur)
				break;
		}

		if (!(i < parking_range)) {
			ast_log(LOG_WARNING, "No more parking spaces\n");
			ast_free(pu);
			AST_LIST_UNLOCK(&parkinglot);
			return NULL;
		}
		/* Set pointer for next parking */
		if (parkfindnext) 
			parking_offset = parking_space - parking_start + 1;
		snprintf(pu->parkingexten, sizeof(pu->parkingexten), "%d", parking_space);
	}

	pu->notquiteyet = 1;
	pu->parkingnum = parking_space;
	AST_LIST_INSERT_TAIL(&parkinglot, pu, list);	
	AST_LIST_UNLOCK(&parkinglot);

	return pu;
}

/* Park a call */
static int park_call_full(struct ast_channel *chan, struct ast_channel *peer, int timeout, int *extout, const char *orig_chan_name, struct parkeduser *pu)
{	
	struct ast_context *con;
	const char *event_from;

	/* Get a valid space if not already done */
	if (pu == NULL)
		pu = park_space_reserve(chan);
	if (pu == NULL)
		return 1; /* Continue execution if possible */

	snprintf(pu->parkingexten, sizeof(pu->parkingexten), "%d", pu->parkingnum);

	chan->appl = "Parked Call";
	chan->data = NULL; 

	pu->chan = chan;
	
	/* Put the parked channel on hold if we have two different channels */
	if (chan != peer) {
		ast_indicate_data(pu->chan, AST_CONTROL_HOLD, 
			S_OR(parkmohclass, NULL),
			!ast_strlen_zero(parkmohclass) ? strlen(parkmohclass) + 1 : 0);
	}
	
	pu->start = ast_tvnow();
	pu->parkingtime = (timeout > 0) ? timeout : parkingtime;
	if (extout)
		*extout = pu->parkingnum;

	if (peer) { 
		/* This is so ugly that it hurts, but implementing get_base_channel() on local channels
			could have ugly side effects.  We could have transferer<->local,1<->local,2<->parking
			and we need the callback name to be that of transferer.  Since local,1/2 have the same
			name we can be tricky and just grab the bridged channel from the other side of the local
		*/
		if (!strcasecmp(peer->tech->type, "Local")) {
			struct ast_channel *tmpchan, *base_peer;
			char other_side[AST_CHANNEL_NAME];
			char *c;
			ast_copy_string(other_side, S_OR(orig_chan_name, peer->name), sizeof(other_side));
			if ((c = strrchr(other_side, ';'))) {
				*++c = '1';
			}
			if ((tmpchan = ast_get_channel_by_name_locked(other_side))) {
				if ((base_peer = ast_bridged_channel(tmpchan))) {
					ast_copy_string(pu->peername, base_peer->name, sizeof(pu->peername));
				}
				ast_channel_unlock(tmpchan);
			}
		} else {
			ast_copy_string(pu->peername, S_OR(orig_chan_name, peer->name), sizeof(pu->peername));
		}
	}

	/* Remember what had been dialed, so that if the parking
	   expires, we try to come back to the same place */
	ast_copy_string(pu->context, S_OR(chan->macrocontext, chan->context), sizeof(pu->context));
	ast_copy_string(pu->exten, S_OR(chan->macroexten, chan->exten), sizeof(pu->exten));
	pu->priority = chan->macropriority ? chan->macropriority : chan->priority;

	/* If parking a channel directly, don't quiet yet get parking running on it.
	 * All parking lot entries are put into the parking lot with notquiteyet on. */
	if (peer != chan) 
		pu->notquiteyet = 0;

	/* Wake up the (presumably select()ing) thread */
	pthread_kill(parking_thread, SIGURG);
	ast_verb(2, "Parked %s on %d@%s. Will timeout back to extension [%s] %s, %d in %d seconds\n", pu->chan->name, pu->parkingnum, parking_con, pu->context, pu->exten, pu->priority, (pu->parkingtime/1000));

	if (peer) {
		event_from = peer->name;
	} else {
		event_from = pbx_builtin_getvar_helper(chan, "BLINDTRANSFER");
	}

	manager_event(EVENT_FLAG_CALL, "ParkedCall",
		"Exten: %s\r\n"
		"Channel: %s\r\n"
		"From: %s\r\n"
		"Timeout: %ld\r\n"
		"CallerIDNum: %s\r\n"
		"CallerIDName: %s\r\n"
		"Uniqueid: %s\r\n",
		pu->parkingexten, pu->chan->name, event_from ? event_from : "",
		(long)pu->start.tv_sec + (long)(pu->parkingtime/1000) - (long)time(NULL),
		S_OR(pu->chan->cid.cid_num, "<unknown>"),
		S_OR(pu->chan->cid.cid_name, "<unknown>"),
		pu->chan->uniqueid
		);

	if (peer && adsipark && ast_adsi_available(peer)) {
		adsi_announce_park(peer, pu->parkingexten);	/* Only supports parking numbers */
		ast_adsi_unload_session(peer);
	}

	con = ast_context_find_or_create(NULL, NULL, parking_con, registrar);
	if (!con)	/* Still no context? Bad */
		ast_log(LOG_ERROR, "Parking context '%s' does not exist and unable to create\n", parking_con);
	if (con) {
		if (!ast_add_extension2(con, 1, pu->parkingexten, 1, NULL, NULL, parkedcall, ast_strdup(pu->parkingexten), ast_free_ptr, registrar))
			notify_metermaids(pu->parkingexten, parking_con, AST_DEVICE_INUSE);
	}
	/* Tell the peer channel the number of the parking space */
	if (peer && (ast_strlen_zero(orig_chan_name) || !strcasecmp(peer->name, orig_chan_name))) { /* Only say number if it's a number and the channel hasn't been masqueraded away */
		/* If a channel is masqueraded into peer while playing back the parking slot number do not continue playing it back. This is the case if an attended transfer occurs. */
		ast_set_flag(peer, AST_FLAG_MASQ_NOSTREAM);
		ast_say_digits(peer, pu->parkingnum, "", peer->language);
		ast_clear_flag(peer, AST_FLAG_MASQ_NOSTREAM);
	}
	if (pu->notquiteyet) {
		/* Wake up parking thread if we're really done */
		ast_indicate_data(pu->chan, AST_CONTROL_HOLD, 
			S_OR(parkmohclass, NULL),
			!ast_strlen_zero(parkmohclass) ? strlen(parkmohclass) + 1 : 0);
		pu->notquiteyet = 0;
		pthread_kill(parking_thread, SIGURG);
	}
	return 0;
}

/*! \brief Park a call */
int ast_park_call(struct ast_channel *chan, struct ast_channel *peer, int timeout, int *extout)
{
	return park_call_full(chan, peer, timeout, extout, NULL, NULL);
}

static int masq_park_call(struct ast_channel *rchan, struct ast_channel *peer, int timeout, int *extout, int play_announcement, const char *orig_chan_name)
{
	struct ast_channel *chan;
	struct ast_frame *f;
	struct parkeduser *pu;
	int park_status;

	if ((pu = park_space_reserve(rchan)) == NULL) {
		if (peer)
			ast_stream_and_wait(peer, "beeperr", "");
		return FEATURE_RETURN_PARKFAILED;
	}

	/* Make a new, fake channel that we'll use to masquerade in the real one */
	if (!(chan = ast_channel_alloc(0, AST_STATE_DOWN, 0, 0, rchan->accountcode, rchan->exten, rchan->context, rchan->amaflags, "Parked/%s",rchan->name))) {
		ast_log(LOG_WARNING, "Unable to create parked channel\n");
		return -1;
	}

	/* Make formats okay */
	chan->readformat = rchan->readformat;
	chan->writeformat = rchan->writeformat;
	ast_channel_masquerade(chan, rchan);

	/* Setup the extensions and such */
	set_c_e_p(chan, rchan->context, rchan->exten, rchan->priority);

	/* Setup the macro extension and such */
	ast_copy_string(chan->macrocontext,rchan->macrocontext,sizeof(chan->macrocontext));
	ast_copy_string(chan->macroexten,rchan->macroexten,sizeof(chan->macroexten));
	chan->macropriority = rchan->macropriority;

	/* Make the masq execute */
	if ((f = ast_read(chan)))
		ast_frfree(f);

	if (peer == rchan) {
		peer = chan;
	}

	if (peer && (!play_announcement && !orig_chan_name)) {
		orig_chan_name = ast_strdupa(peer->name);
	}

	park_status = park_call_full(chan, peer, timeout, extout, orig_chan_name, pu);
	if (park_status == 1) {
	/* would be nice to play "invalid parking extension" */
		ast_hangup(chan);
		return -1;
	}
	return 0;
}

/* Park call via masquraded channel */
int ast_masq_park_call(struct ast_channel *rchan, struct ast_channel *peer, int timeout, int *extout)
{
	return masq_park_call(rchan, peer, timeout, extout, 0, NULL);
}

static int masq_park_call_announce(struct ast_channel *rchan, struct ast_channel *peer, int timeout, int *extout, const char *orig_chan_name)
{
	return masq_park_call(rchan, peer, timeout, extout, 1, orig_chan_name);
}

/*! 
 * \brief set caller and callee according to the direction 
 * \param caller, callee, peer, chan, sense
 *
 * Detect who triggered feature and set callee/caller variables accordingly
*/
static void set_peers(struct ast_channel **caller, struct ast_channel **callee,
	struct ast_channel *peer, struct ast_channel *chan, int sense)
{
	if (sense == FEATURE_SENSE_PEER) {
		*caller = peer;
		*callee = chan;
	} else {
		*callee = peer;
		*caller = chan;
	}
}

/*! 
 * \brief support routing for one touch call parking
 * \param chan channel parking call
 * \param peer channel to be parked
 * \param config unsed
 * \param code unused
 * \param sense feature options
 *
 * \param data
 * Setup channel, set return exten,priority to 's,1'
 * answer chan, sleep chan, park call
*/
static int builtin_parkcall(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, char *code, int sense, void *data)
{
	struct ast_channel *parker;
	struct ast_channel *parkee;
	int res = 0;

	set_peers(&parker, &parkee, peer, chan, sense);
	/* we used to set chan's exten and priority to "s" and 1
	   here, but this generates (in some cases) an invalid
	   extension, and if "s" exists, could errantly
	   cause execution of extensions you don't expect. It
	   makes more sense to let nature take its course
	   when chan finishes, and let the pbx do its thing
	   and hang up when the park is over.
	*/
	if (chan->_state != AST_STATE_UP)
		res = ast_answer(chan);
	if (!res)
		res = ast_safe_sleep(chan, 1000);

	if (!res) { /* one direction used to call park_call.... */
		res = masq_park_call_announce(parkee, parker, 0, NULL, NULL);
		/* PBX should hangup zombie channel if a masquerade actually occurred (res=0) */
	}
	return res;
}

/*!
 * \brief Monitor a channel by DTMF
 * \param chan channel requesting monitor
 * \param peer channel to be monitored
 * \param config
 * \param code
 * \param sense feature options
 *
 * \param data
 * Check monitor app enabled, setup channels, both caller/callee chans not null
 * get TOUCH_MONITOR variable for filename if exists, exec monitor app.
 * \retval FEATURE_RETURN_SUCCESS on success.
 * \retval -1 on error.
*/
static int builtin_automonitor(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, char *code, int sense, void *data)
{
	char *caller_chan_id = NULL, *callee_chan_id = NULL, *args = NULL, *touch_filename = NULL;
	int x = 0;
	size_t len;
	struct ast_channel *caller_chan, *callee_chan;

	if (!monitor_ok) {
		ast_log(LOG_ERROR,"Cannot record the call. The monitor application is disabled.\n");
		return -1;
	}

	if (!monitor_app && !(monitor_app = pbx_findapp("Monitor"))) {
		monitor_ok = 0;
		ast_log(LOG_ERROR,"Cannot record the call. The monitor application is disabled.\n");
		return -1;
	}

	set_peers(&caller_chan, &callee_chan, peer, chan, sense);

	if (!ast_strlen_zero(courtesytone)) {
		if (ast_autoservice_start(callee_chan))
			return -1;
		if (ast_stream_and_wait(caller_chan, courtesytone, "")) {
			ast_log(LOG_WARNING, "Failed to play courtesy tone!\n");
			ast_autoservice_stop(callee_chan);
			return -1;
		}
		if (ast_autoservice_stop(callee_chan))
			return -1;
	}
	
	if (callee_chan->monitor) {
		ast_verb(4, "User hit '%s' to stop recording call.\n", code);
		callee_chan->monitor->stop(callee_chan, 1);
		return FEATURE_RETURN_SUCCESS;
	}

	if (caller_chan && callee_chan) {
		const char *touch_format = pbx_builtin_getvar_helper(caller_chan, "TOUCH_MONITOR_FORMAT");
		const char *touch_monitor = pbx_builtin_getvar_helper(caller_chan, "TOUCH_MONITOR");
		const char *touch_monitor_prefix = pbx_builtin_getvar_helper(caller_chan, "TOUCH_MONITOR_PREFIX");

		if (!touch_format)
			touch_format = pbx_builtin_getvar_helper(callee_chan, "TOUCH_MONITOR_FORMAT");

		if (!touch_monitor)
			touch_monitor = pbx_builtin_getvar_helper(callee_chan, "TOUCH_MONITOR");
	
		if (!touch_monitor_prefix)
			touch_monitor_prefix = pbx_builtin_getvar_helper(callee_chan, "TOUCH_MONITOR_PREFIX");
	
		if (touch_monitor) {
			len = strlen(touch_monitor) + 50;
			args = alloca(len);
			touch_filename = alloca(len);
			snprintf(touch_filename, len, "%s-%ld-%s", S_OR(touch_monitor_prefix, "auto"), (long)time(NULL), touch_monitor);
			snprintf(args, len, "%s,%s,m", S_OR(touch_format, "wav"), touch_filename);
		} else {
			caller_chan_id = ast_strdupa(S_OR(caller_chan->cid.cid_num, caller_chan->name));
			callee_chan_id = ast_strdupa(S_OR(callee_chan->cid.cid_num, callee_chan->name));
			len = strlen(caller_chan_id) + strlen(callee_chan_id) + 50;
			args = alloca(len);
			touch_filename = alloca(len);
			snprintf(touch_filename, len, "%s-%ld-%s-%s", S_OR(touch_monitor_prefix, "auto"), (long)time(NULL), caller_chan_id, callee_chan_id);
			snprintf(args, len, "%s,%s,m", S_OR(touch_format, "wav"), touch_filename);
		}

		for(x = 0; x < strlen(args); x++) {
			if (args[x] == '/')
				args[x] = '-';
		}
		
		ast_verb(4, "User hit '%s' to record call. filename: %s\n", code, args);

		pbx_exec(callee_chan, monitor_app, args);
		pbx_builtin_setvar_helper(callee_chan, "TOUCH_MONITOR_OUTPUT", touch_filename);
		pbx_builtin_setvar_helper(caller_chan, "TOUCH_MONITOR_OUTPUT", touch_filename);
	
		return FEATURE_RETURN_SUCCESS;
	}
	
	ast_log(LOG_NOTICE,"Cannot record the call. One or both channels have gone away.\n");	
	return -1;
}

static int builtin_automixmonitor(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, char *code, int sense, void *data)
{
	char *caller_chan_id = NULL, *callee_chan_id = NULL, *args = NULL, *touch_filename = NULL;
	int x = 0;
	size_t len;
	struct ast_channel *caller_chan, *callee_chan;
	const char *mixmonitor_spy_type = "MixMonitor";
	int count = 0;

	if (!mixmonitor_ok) {
		ast_log(LOG_ERROR,"Cannot record the call. The mixmonitor application is disabled.\n");
		return -1;
	}

	if (!(mixmonitor_app = pbx_findapp("MixMonitor"))) {
		mixmonitor_ok = 0;
		ast_log(LOG_ERROR,"Cannot record the call. The mixmonitor application is disabled.\n");
		return -1;
	}

	set_peers(&caller_chan, &callee_chan, peer, chan, sense);

	if (!ast_strlen_zero(courtesytone)) {
		if (ast_autoservice_start(callee_chan))
			return -1;
		if (ast_stream_and_wait(caller_chan, courtesytone, "")) {
			ast_log(LOG_WARNING, "Failed to play courtesy tone!\n");
			ast_autoservice_stop(callee_chan);
			return -1;
		}
		if (ast_autoservice_stop(callee_chan))
			return -1;
	}

	ast_channel_lock(callee_chan);
	count = ast_channel_audiohook_count_by_source(callee_chan, mixmonitor_spy_type, AST_AUDIOHOOK_TYPE_SPY);
	ast_channel_unlock(callee_chan);

	// This means a mixmonitor is attached to the channel, running or not is unknown.
	if (count > 0) {
		
		ast_verb(3, "User hit '%s' to stop recording call.\n", code);

		//Make sure they are running
		ast_channel_lock(callee_chan);
		count = ast_channel_audiohook_count_by_source_running(callee_chan, mixmonitor_spy_type, AST_AUDIOHOOK_TYPE_SPY);
		ast_channel_unlock(callee_chan);
		if (count > 0) {
			if (!stopmixmonitor_ok) {
				ast_log(LOG_ERROR,"Cannot stop recording the call. The stopmixmonitor application is disabled.\n");
				return -1;
			}
			if (!(stopmixmonitor_app = pbx_findapp("StopMixMonitor"))) {
				stopmixmonitor_ok = 0;
				ast_log(LOG_ERROR,"Cannot stop recording the call. The stopmixmonitor application is disabled.\n");
				return -1;
			} else {
				pbx_exec(callee_chan, stopmixmonitor_app, "");
				return FEATURE_RETURN_SUCCESS;
			}
		}
		
		ast_log(LOG_WARNING,"Stopped MixMonitors are attached to the channel.\n");	
	}			

	if (caller_chan && callee_chan) {
		const char *touch_format = pbx_builtin_getvar_helper(caller_chan, "TOUCH_MIXMONITOR_FORMAT");
		const char *touch_monitor = pbx_builtin_getvar_helper(caller_chan, "TOUCH_MIXMONITOR");

		if (!touch_format)
			touch_format = pbx_builtin_getvar_helper(callee_chan, "TOUCH_MIXMONITOR_FORMAT");

		if (!touch_monitor)
			touch_monitor = pbx_builtin_getvar_helper(callee_chan, "TOUCH_MIXMONITOR");

		if (touch_monitor) {
			len = strlen(touch_monitor) + 50;
			args = alloca(len);
			touch_filename = alloca(len);
			snprintf(touch_filename, len, "auto-%ld-%s", (long)time(NULL), touch_monitor);
			snprintf(args, len, "%s.%s,b", touch_filename, (touch_format) ? touch_format : "wav");
		} else {
			caller_chan_id = ast_strdupa(S_OR(caller_chan->cid.cid_num, caller_chan->name));
			callee_chan_id = ast_strdupa(S_OR(callee_chan->cid.cid_num, callee_chan->name));
			len = strlen(caller_chan_id) + strlen(callee_chan_id) + 50;
			args = alloca(len);
			touch_filename = alloca(len);
			snprintf(touch_filename, len, "auto-%ld-%s-%s", (long)time(NULL), caller_chan_id, callee_chan_id);
			snprintf(args, len, "%s.%s,b", touch_filename, S_OR(touch_format, "wav"));
		}

		for( x = 0; x < strlen(args); x++) {
			if (args[x] == '/')
				args[x] = '-';
		}

		ast_verb(3, "User hit '%s' to record call. filename: %s\n", code, touch_filename);

		pbx_exec(callee_chan, mixmonitor_app, args);
		pbx_builtin_setvar_helper(callee_chan, "TOUCH_MIXMONITOR_OUTPUT", touch_filename);
		pbx_builtin_setvar_helper(caller_chan, "TOUCH_MIXMONITOR_OUTPUT", touch_filename);
		return FEATURE_RETURN_SUCCESS;
	
	}

	ast_log(LOG_NOTICE,"Cannot record the call. One or both channels have gone away.\n");
	return -1;

}

static int builtin_disconnect(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, char *code, int sense, void *data)
{
	ast_verb(4, "User hit '%s' to disconnect call.\n", code);
	return FEATURE_RETURN_HANGUP;
}

static int finishup(struct ast_channel *chan)
{
	ast_indicate(chan, AST_CONTROL_UNHOLD);

	return ast_autoservice_stop(chan);
}

/*!
 * \brief Find the context for the transfer
 * \param transferer
 * \param transferee
 * 
 * Grab the TRANSFER_CONTEXT, if fails try grabbing macrocontext.
 * \return a context string
*/
static const char *real_ctx(struct ast_channel *transferer, struct ast_channel *transferee)
{
	const char *s = pbx_builtin_getvar_helper(transferer, "TRANSFER_CONTEXT");
	if (ast_strlen_zero(s)) {
		s = pbx_builtin_getvar_helper(transferee, "TRANSFER_CONTEXT");
	}
	if (ast_strlen_zero(s)) { /* Use the non-macro context to transfer the call XXX ? */
		s = transferer->macrocontext;
	}
	if (ast_strlen_zero(s)) {
		s = transferer->context;
	}
	return s;  
}

/*!
 * \brief Blind transfer user to another extension
 * \param chan channel to be transfered
 * \param peer channel initiated blind transfer
 * \param config
 * \param code
 * \param data
 * \param sense  feature options
 * 
 * Place chan on hold, check if transferred to parkinglot extension,
 * otherwise check extension exists and transfer caller.
 * \retval FEATURE_RETURN_SUCCESS.
 * \retval -1 on failure.
*/
static int builtin_blindtransfer(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, char *code, int sense, void *data)
{
	struct ast_channel *transferer;
	struct ast_channel *transferee;
	const char *transferer_real_context;
	char xferto[256];
	int res;
	int parkstatus = 0;

	set_peers(&transferer, &transferee, peer, chan, sense);
	transferer_real_context = real_ctx(transferer, transferee);
	/* Start autoservice on chan while we talk to the originator */
	ast_autoservice_start(transferee);
	ast_indicate(transferee, AST_CONTROL_HOLD);

	memset(xferto, 0, sizeof(xferto));

	/* Transfer */
	res = ast_stream_and_wait(transferer, "pbx-transfer", AST_DIGIT_ANY);
	if (res < 0) {
		finishup(transferee);
		return -1; /* error ? */
	}
	if (res > 0)	/* If they've typed a digit already, handle it */
		xferto[0] = (char) res;

	ast_stopstream(transferer);
	res = ast_app_dtget(transferer, transferer_real_context, xferto, sizeof(xferto), 100, transferdigittimeout);
	if (res < 0) {  /* hangup, would be 0 for invalid and 1 for valid */
		finishup(transferee);
		return res;
	}
	if (!strcmp(xferto, ast_parking_ext())) {
		res = finishup(transferee);
		if (res)
			res = -1;
		else if (!(parkstatus = masq_park_call_announce(transferee, transferer, 0, NULL, NULL))) {	/* success */
			/* We return non-zero, but tell the PBX not to hang the channel when
			   the thread dies -- We have to be careful now though.  We are responsible for 
			   hanging up the channel, else it will never be hung up! */

			return 0;
		} else {
			ast_log(LOG_WARNING, "Unable to park call %s, parkstatus = %d\n", transferee->name, parkstatus);
		}
		/*! \todo XXX Maybe we should have another message here instead of invalid extension XXX */
	} else if (ast_exists_extension(transferee, transferer_real_context, xferto, 1, transferer->cid.cid_num)) {
		pbx_builtin_setvar_helper(transferer, "BLINDTRANSFER", transferee->name);
		pbx_builtin_setvar_helper(transferee, "BLINDTRANSFER", transferer->name);
		res=finishup(transferee);
		if (!transferer->cdr) { /* this code should never get called (in a perfect world) */
			transferer->cdr=ast_cdr_alloc();
			if (transferer->cdr) {
				ast_cdr_init(transferer->cdr, transferer); /* initialize our channel's cdr */
				ast_cdr_start(transferer->cdr);
			}
		}
		if (transferer->cdr) {
			struct ast_cdr *swap = transferer->cdr;
			ast_log(LOG_DEBUG,"transferer=%s; transferee=%s; lastapp=%s; lastdata=%s; chan=%s; dstchan=%s\n",
					transferer->name, transferee->name, transferer->cdr->lastapp, transferer->cdr->lastdata, 
					transferer->cdr->channel, transferer->cdr->dstchannel);
			ast_log(LOG_DEBUG,"TRANSFEREE; lastapp=%s; lastdata=%s, chan=%s; dstchan=%s\n",
					transferee->cdr->lastapp, transferee->cdr->lastdata, transferee->cdr->channel, transferee->cdr->dstchannel);
			ast_log(LOG_DEBUG,"transferer_real_context=%s; xferto=%s\n", transferer_real_context, xferto);
			/* swap cdrs-- it will save us some time & work */
			transferer->cdr = transferee->cdr;
			transferee->cdr = swap;
		}
		if (!transferee->pbx) {
			/* Doh!  Use our handy async_goto functions */
			ast_verb(3, "Transferring %s to '%s' (context %s) priority 1\n"
								,transferee->name, xferto, transferer_real_context);
			if (ast_async_goto(transferee, transferer_real_context, xferto, 1))
				ast_log(LOG_WARNING, "Async goto failed :-(\n");
		} else {
			/* Set the channel's new extension, since it exists, using transferer context */
			ast_set_flag(transferee, AST_FLAG_BRIDGE_HANGUP_DONT); /* don't let the after-bridge code run the h-exten */
			ast_log(LOG_DEBUG,"ABOUT TO AST_ASYNC_GOTO, have a pbx... set HANGUP_DONT on chan=%s\n", transferee->name);
			set_c_e_p(transferee, transferer_real_context, xferto, 0);
		}
		check_goto_on_transfer(transferer);
		return res;
	} else {
		ast_verb(3, "Unable to find extension '%s' in context '%s'\n", xferto, transferer_real_context);
	}
	if (parkstatus != FEATURE_RETURN_PARKFAILED && ast_stream_and_wait(transferer, xferfailsound, AST_DIGIT_ANY) < 0) {
		finishup(transferee);
		return -1;
	}
	ast_stopstream(transferer);
	res = finishup(transferee);
	if (res) {
		ast_verb(2, "Hungup during autoservice stop on '%s'\n", transferee->name);
		return res;
	}
	return FEATURE_RETURN_SUCCESS;
}

/*!
 * \brief make channels compatible
 * \param c
 * \param newchan
 * \retval 0 on success.
 * \retval -1 on failure.
*/
static int check_compat(struct ast_channel *c, struct ast_channel *newchan)
{
	if (ast_channel_make_compatible(c, newchan) < 0) {
		ast_log(LOG_WARNING, "Had to drop call because I couldn't make %s compatible with %s\n",
			c->name, newchan->name);
		ast_hangup(newchan);
		return -1;
	}
	return 0;
}

/*!
 * \brief Attended transfer
 * \param chan transfered user
 * \param peer person transfering call
 * \param config
 * \param code
 * \param sense feature options
 * 
 * \param data
 * Get extension to transfer to, if you cannot generate channel (or find extension) 
 * return to host channel. After called channel answered wait for hangup of transferer,
 * bridge call between transfer peer (taking them off hold) to attended transfer channel.
 *
 * \return -1 on failure
*/
static int builtin_atxfer(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, char *code, int sense, void *data)
{
	struct ast_channel *transferer;
	struct ast_channel *transferee;
	const char *transferer_real_context;
	char xferto[256] = "";
	int res;
	int outstate=0;
	struct ast_channel *newchan;
	struct ast_channel *xferchan;
	struct ast_bridge_thread_obj *tobj;
	struct ast_bridge_config bconfig;
	struct ast_frame *f;
	int l;
	struct ast_datastore *features_datastore;
	struct ast_dial_features *dialfeatures = NULL;

	ast_debug(1, "Executing Attended Transfer %s, %s (sense=%d) \n", chan->name, peer->name, sense);
	set_peers(&transferer, &transferee, peer, chan, sense);
	transferer_real_context = real_ctx(transferer, transferee);
	/* Start autoservice on chan while we talk to the originator */
	ast_autoservice_start(transferee);
	ast_indicate(transferee, AST_CONTROL_HOLD);
	
	/* Transfer */
	res = ast_stream_and_wait(transferer, "pbx-transfer", AST_DIGIT_ANY);
	if (res < 0) {
		finishup(transferee);
		return res;
	}
	if (res > 0) /* If they've typed a digit already, handle it */
		xferto[0] = (char) res;

	/* this is specific of atxfer */
	res = ast_app_dtget(transferer, transferer_real_context, xferto, sizeof(xferto), 100, transferdigittimeout);
	if (res < 0) {  /* hangup, would be 0 for invalid and 1 for valid */
		finishup(transferee);
		return res;
	}
	if (res == 0) {
		ast_log(LOG_WARNING, "Did not read data.\n");
		finishup(transferee);
		if (ast_stream_and_wait(transferer, "beeperr", ""))
			return -1;
		return FEATURE_RETURN_SUCCESS;
	}

	/* valid extension, res == 1 */
	if (!ast_exists_extension(transferer, transferer_real_context, xferto, 1, transferer->cid.cid_num)) {
		ast_log(LOG_WARNING, "Extension %s does not exist in context %s\n",xferto,transferer_real_context);
		finishup(transferee);
		if (ast_stream_and_wait(transferer, "beeperr", ""))
			return -1;
		return FEATURE_RETURN_SUCCESS;
	}

 	/* If we are attended transfering to parking, just use builtin_parkcall instead of trying to track all of
 	 * the different variables for handling this properly with a builtin_atxfer */
 	if (!strcmp(xferto, ast_parking_ext())) {
 		finishup(transferee);
 		return builtin_parkcall(chan, peer, config, code, sense, data);
 	}

	l = strlen(xferto);
	snprintf(xferto + l, sizeof(xferto) - l, "@%s/n", transferer_real_context);	/* append context */
	newchan = ast_feature_request_and_dial(transferer, transferee, "Local", ast_best_codec(transferer->nativeformats),
		xferto, atxfernoanswertimeout, &outstate, transferer->cid.cid_num, transferer->cid.cid_name, 1, transferer->language);

	if (!ast_check_hangup(transferer)) {
		/* Transferer is up - old behaviour */
		ast_indicate(transferer, -1);
		if (!newchan) {
			finishup(transferee);
			/* any reason besides user requested cancel and busy triggers the failed sound */
			if (outstate != AST_CONTROL_UNHOLD && outstate != AST_CONTROL_BUSY &&
				ast_stream_and_wait(transferer, xferfailsound, ""))
				return -1;
			if (ast_stream_and_wait(transferer, xfersound, ""))
				ast_log(LOG_WARNING, "Failed to play transfer sound!\n");
			return FEATURE_RETURN_SUCCESS;
		}

		if (check_compat(transferer, newchan)) {
			/* we do mean transferee here, NOT transferer */
			finishup(transferee);
			return -1;
		}
		memset(&bconfig,0,sizeof(struct ast_bridge_config));
		ast_set_flag(&(bconfig.features_caller), AST_FEATURE_DISCONNECT);
		ast_set_flag(&(bconfig.features_callee), AST_FEATURE_DISCONNECT);
		res = ast_bridge_call(transferer, newchan, &bconfig);
		if (ast_check_hangup(newchan) || !ast_check_hangup(transferer)) {
			ast_hangup(newchan);
			if (ast_stream_and_wait(transferer, xfersound, ""))
				ast_log(LOG_WARNING, "Failed to play transfer sound!\n");
			finishup(transferee);
			transferer->_softhangup = 0;
			return FEATURE_RETURN_SUCCESS;
		}
		if (check_compat(transferee, newchan)) {
			finishup(transferee);
			return -1;
		}
		ast_indicate(transferee, AST_CONTROL_UNHOLD);

		if ((ast_autoservice_stop(transferee) < 0)
		 || (ast_waitfordigit(transferee, 100) < 0)
		 || (ast_waitfordigit(newchan, 100) < 0)
		 || ast_check_hangup(transferee)
		 || ast_check_hangup(newchan)) {
			ast_hangup(newchan);
			return -1;
		}
		xferchan = ast_channel_alloc(0, AST_STATE_DOWN, 0, 0, "", "", "", 0, "Transfered/%s", transferee->name);
		if (!xferchan) {
			ast_hangup(newchan);
			return -1;
		}
		/* Make formats okay */
		xferchan->visible_indication = transferer->visible_indication;
		xferchan->readformat = transferee->readformat;
		xferchan->writeformat = transferee->writeformat;
		ast_channel_masquerade(xferchan, transferee);
		ast_explicit_goto(xferchan, transferee->context, transferee->exten, transferee->priority);
		xferchan->_state = AST_STATE_UP;
		ast_clear_flag(xferchan, AST_FLAGS_ALL);
		xferchan->_softhangup = 0;
		if ((f = ast_read(xferchan)))
			ast_frfree(f);
		newchan->_state = AST_STATE_UP;
		ast_clear_flag(newchan, AST_FLAGS_ALL);
		newchan->_softhangup = 0;
		if (!(tobj = ast_calloc(1, sizeof(*tobj)))) {
			ast_hangup(xferchan);
			ast_hangup(newchan);
			return -1;
		}

		ast_channel_lock(newchan);
		if ((features_datastore = ast_channel_datastore_find(newchan, &dial_features_info, NULL))) {
				dialfeatures = features_datastore->data;
		}
		ast_channel_unlock(newchan);

		if (dialfeatures) {
			/* newchan should always be the callee and shows up as callee in dialfeatures, but for some reason
			   I don't currently understand, the abilities of newchan seem to be stored on the caller side */
			ast_copy_flags(&(config->features_callee), &(dialfeatures->features_caller), AST_FLAGS_ALL);
			dialfeatures = NULL;
		}

		ast_channel_lock(xferchan);
		if ((features_datastore = ast_channel_datastore_find(xferchan, &dial_features_info, NULL))) {
			dialfeatures = features_datastore->data;
		}
		ast_channel_unlock(xferchan);
	 
		if (dialfeatures) {
			ast_copy_flags(&(config->features_caller), &(dialfeatures->features_caller), AST_FLAGS_ALL);
		}
	 
		tobj->chan = newchan;
		tobj->peer = xferchan;
		tobj->bconfig = *config;

		if (tobj->bconfig.end_bridge_callback_data_fixup) {
			tobj->bconfig.end_bridge_callback_data_fixup(&tobj->bconfig, tobj->peer, tobj->chan);
		}

		if (ast_stream_and_wait(newchan, xfersound, ""))
			ast_log(LOG_WARNING, "Failed to play transfer sound!\n");
		ast_bridge_call_thread_launch(tobj);
		return -1;      /* XXX meaning the channel is bridged ? */
	} else if (!ast_check_hangup(transferee)) {
		/* act as blind transfer */
		if (ast_autoservice_stop(transferee) < 0) {
			ast_hangup(newchan);
			return -1;
		}

		if (!newchan) {
			unsigned int tries = 0;
			char *transferer_tech, *transferer_name = ast_strdupa(transferer->name);

			transferer_tech = strsep(&transferer_name, "/");
			transferer_name = strsep(&transferer_name, "-");

			if (ast_strlen_zero(transferer_name) || ast_strlen_zero(transferer_tech)) {
				ast_log(LOG_WARNING, "Transferer has invalid channel name: '%s'\n", transferer->name);
				if (ast_stream_and_wait(transferee, "beeperr", ""))
					return -1;
				return FEATURE_RETURN_SUCCESS;
			}

			ast_log(LOG_NOTICE, "We're trying to call %s/%s\n", transferer_tech, transferer_name);
			newchan = ast_feature_request_and_dial(transferee, NULL, transferer_tech, ast_best_codec(transferee->nativeformats),
				transferer_name, atxfernoanswertimeout, &outstate, transferee->cid.cid_num, transferee->cid.cid_name, 0, transferer->language);
			while (!newchan && !atxferdropcall && tries < atxfercallbackretries) {
				/* Trying to transfer again */
				ast_autoservice_start(transferee);
				ast_indicate(transferee, AST_CONTROL_HOLD);

				newchan = ast_feature_request_and_dial(transferer, transferee, "Local", ast_best_codec(transferer->nativeformats),
					xferto, atxfernoanswertimeout, &outstate, transferer->cid.cid_num, transferer->cid.cid_name, 1, transferer->language);
				if (ast_autoservice_stop(transferee) < 0) {
					if (newchan)
						ast_hangup(newchan);
					return -1;
				}
				if (!newchan) {
					/* Transfer failed, sleeping */
					ast_debug(1, "Sleeping for %d ms before callback.\n", atxferloopdelay);
					ast_safe_sleep(transferee, atxferloopdelay);
					ast_debug(1, "Trying to callback...\n");
					newchan = ast_feature_request_and_dial(transferee, NULL, transferer_tech, ast_best_codec(transferee->nativeformats),
						transferer_name, atxfernoanswertimeout, &outstate, transferee->cid.cid_num, transferee->cid.cid_name, 0, transferer->language);
				}
				tries++;
			}
		}
		if (!newchan)
			return -1;

		/* newchan is up, we should prepare transferee and bridge them */
		if (check_compat(transferee, newchan)) {
			finishup(transferee);
			return -1;
		}
		ast_indicate(transferee, AST_CONTROL_UNHOLD);

		if ((ast_waitfordigit(transferee, 100) < 0)
		   || (ast_waitfordigit(newchan, 100) < 0)
		   || ast_check_hangup(transferee)
		   || ast_check_hangup(newchan)) {
			ast_hangup(newchan);
			return -1;
		}

		xferchan = ast_channel_alloc(0, AST_STATE_DOWN, 0, 0, "", "", "", 0, "Transfered/%s", transferee->name);
		if (!xferchan) {
			ast_hangup(newchan);
			return -1;
		}
		/* Make formats okay */
		xferchan->visible_indication = transferer->visible_indication;
		xferchan->readformat = transferee->readformat;
		xferchan->writeformat = transferee->writeformat;
		ast_channel_masquerade(xferchan, transferee);
		ast_explicit_goto(xferchan, transferee->context, transferee->exten, transferee->priority);
		xferchan->_state = AST_STATE_UP;
		ast_clear_flag(xferchan, AST_FLAGS_ALL);
		xferchan->_softhangup = 0;
		if ((f = ast_read(xferchan)))
			ast_frfree(f);
		newchan->_state = AST_STATE_UP;
		ast_clear_flag(newchan, AST_FLAGS_ALL);
		newchan->_softhangup = 0;
		if (!(tobj = ast_calloc(1, sizeof(*tobj)))) {
			ast_hangup(xferchan);
			ast_hangup(newchan);
			return -1;
		}
		tobj->chan = newchan;
		tobj->peer = xferchan;
		tobj->bconfig = *config;

		if (tobj->bconfig.end_bridge_callback_data_fixup) {
			tobj->bconfig.end_bridge_callback_data_fixup(&tobj->bconfig, tobj->peer, tobj->chan);
		}

		if (ast_stream_and_wait(newchan, xfersound, ""))
			ast_log(LOG_WARNING, "Failed to play transfer sound!\n");
		ast_bridge_call_thread_launch(tobj);
		return -1;      /* XXX meaning the channel is bridged ? */
	} else {
		/* Transferee hung up */
		finishup(transferee);
		return -1;
	}
}

/* add atxfer and automon as undefined so you can only use em if you configure them */
#define FEATURES_COUNT ARRAY_LEN(builtin_features)

AST_RWLOCK_DEFINE_STATIC(features_lock);

static struct ast_call_feature builtin_features[] = 
{
	{ AST_FEATURE_REDIRECT, "Blind Transfer", "blindxfer", "#", "#", builtin_blindtransfer, AST_FEATURE_FLAG_NEEDSDTMF, "" },
	{ AST_FEATURE_REDIRECT, "Attended Transfer", "atxfer", "", "", builtin_atxfer, AST_FEATURE_FLAG_NEEDSDTMF, "" },
	{ AST_FEATURE_AUTOMON, "One Touch Monitor", "automon", "", "", builtin_automonitor, AST_FEATURE_FLAG_NEEDSDTMF, "" },
	{ AST_FEATURE_DISCONNECT, "Disconnect Call", "disconnect", "*", "*", builtin_disconnect, AST_FEATURE_FLAG_NEEDSDTMF, "" },
	{ AST_FEATURE_PARKCALL, "Park Call", "parkcall", "", "", builtin_parkcall, AST_FEATURE_FLAG_NEEDSDTMF, "" },
	{ AST_FEATURE_AUTOMIXMON, "One Touch MixMonitor", "automixmon", "", "", builtin_automixmonitor, AST_FEATURE_FLAG_NEEDSDTMF, "" },
};


static AST_RWLIST_HEAD_STATIC(feature_list, ast_call_feature);

/*! \brief register new feature into feature_list*/
void ast_register_feature(struct ast_call_feature *feature)
{
	if (!feature) {
		ast_log(LOG_NOTICE,"You didn't pass a feature!\n");
		return;
	}
  
	AST_RWLIST_WRLOCK(&feature_list);
	AST_RWLIST_INSERT_HEAD(&feature_list,feature,feature_entry);
	AST_RWLIST_UNLOCK(&feature_list);

	ast_verb(2, "Registered Feature '%s'\n",feature->sname);
}

/*! 
 * \brief Add new feature group
 * \param fgname feature group name.
 *
 * Add new feature group to the feature group list insert at head of list.
 * \note This function MUST be called while feature_groups is locked.
*/
static struct feature_group* register_group(const char *fgname)
{
	struct feature_group *fg;

	if (!fgname) {
		ast_log(LOG_NOTICE, "You didn't pass a new group name!\n");
		return NULL;
	}

	if (!(fg = ast_calloc(1, sizeof(*fg))))
		return NULL;

	if (ast_string_field_init(fg, 128)) {
		ast_free(fg);
		return NULL;
	}

	ast_string_field_set(fg, gname, fgname);

	AST_LIST_INSERT_HEAD(&feature_groups, fg, entry);

	ast_verb(2, "Registered group '%s'\n", fg->gname);

	return fg;
}

/*! 
 * \brief Add feature to group
 * \param fg feature group
 * \param exten
 * \param feature feature to add.
 *
 * Check fg and feature specified, add feature to list
 * \note This function MUST be called while feature_groups is locked. 
*/
static void register_group_feature(struct feature_group *fg, const char *exten, struct ast_call_feature *feature) 
{
	struct feature_group_exten *fge;

	if (!fg) {
		ast_log(LOG_NOTICE, "You didn't pass a group!\n");
		return;
	}

	if (!feature) {
		ast_log(LOG_NOTICE, "You didn't pass a feature!\n");
		return;
	}

	if (!(fge = ast_calloc(1, sizeof(*fge))))
		return;

	if (ast_string_field_init(fge, 128)) {
		ast_free(fge);
		return;
	}

	ast_string_field_set(fge, exten, S_OR(exten, feature->exten));

	fge->feature = feature;

	AST_LIST_INSERT_HEAD(&fg->features, fge, entry);		

	ast_verb(2, "Registered feature '%s' for group '%s' at exten '%s'\n",
					feature->sname, fg->gname, exten);
}

void ast_unregister_feature(struct ast_call_feature *feature)
{
	if (!feature) {
		return;
	}

	AST_RWLIST_WRLOCK(&feature_list);
	AST_RWLIST_REMOVE(&feature_list, feature, feature_entry);
	AST_RWLIST_UNLOCK(&feature_list);

	ast_free(feature);
}

/*! \brief Remove all features in the list */
static void ast_unregister_features(void)
{
	struct ast_call_feature *feature;

	AST_RWLIST_WRLOCK(&feature_list);
	while ((feature = AST_RWLIST_REMOVE_HEAD(&feature_list, feature_entry))) {
		ast_free(feature);
	}
	AST_RWLIST_UNLOCK(&feature_list);
}

/*! \brief find a call feature by name */
static struct ast_call_feature *find_dynamic_feature(const char *name)
{
	struct ast_call_feature *tmp;

	AST_RWLIST_TRAVERSE(&feature_list, tmp, feature_entry) {
		if (!strcasecmp(tmp->sname, name)) {
			break;
		}
	}

	return tmp;
}

/*! \brief Remove all feature groups in the list */
static void ast_unregister_groups(void)
{
	struct feature_group *fg;
	struct feature_group_exten *fge;

	AST_RWLIST_WRLOCK(&feature_groups);
	while ((fg = AST_LIST_REMOVE_HEAD(&feature_groups, entry))) {
		while ((fge = AST_LIST_REMOVE_HEAD(&fg->features, entry))) {
			ast_string_field_free_memory(fge);
			ast_free(fge);
		}

		ast_string_field_free_memory(fg);
		ast_free(fg);
	}
	AST_RWLIST_UNLOCK(&feature_groups);
}

/*! 
 * \brief Find a group by name 
 * \param name feature name
 * \retval feature group on success.
 * \retval NULL on failure.
*/
static struct feature_group *find_group(const char *name) {
	struct feature_group *fg = NULL;

	AST_LIST_TRAVERSE(&feature_groups, fg, entry) {
		if (!strcasecmp(fg->gname, name))
			break;
	}

	return fg;
}

void ast_rdlock_call_features(void)
{
	ast_rwlock_rdlock(&features_lock);
}

void ast_unlock_call_features(void)
{
	ast_rwlock_unlock(&features_lock);
}

struct ast_call_feature *ast_find_call_feature(const char *name)
{
	int x;
	for (x = 0; x < FEATURES_COUNT; x++) {
		if (!strcasecmp(name, builtin_features[x].sname))
			return &builtin_features[x];
	}
	return NULL;
}

/*!
 * \brief exec an app by feature 
 * \param chan,peer,config,code,sense,data
 *
 * Find a feature, determine which channel activated
 * \retval -1 error.
 * \retval -2 when an application cannot be found.
*/
static int feature_exec_app(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, char *code, int sense, void *data)
{
	struct ast_app *app;
	struct ast_call_feature *feature = data;
	struct ast_channel *work, *idle;
	int res;

	if (!feature) { /* shouldn't ever happen! */
		ast_log(LOG_NOTICE, "Found feature before, but at execing we've lost it??\n");
		return -1; 
	}

	if (sense == FEATURE_SENSE_CHAN) {
		if (!ast_test_flag(feature, AST_FEATURE_FLAG_BYCALLER))
			return FEATURE_RETURN_KEEPTRYING;
		if (ast_test_flag(feature, AST_FEATURE_FLAG_ONSELF)) {
			work = chan;
			idle = peer;
		} else {
			work = peer;
			idle = chan;
		}
	} else {
		if (!ast_test_flag(feature, AST_FEATURE_FLAG_BYCALLEE))
			return FEATURE_RETURN_KEEPTRYING;
		if (ast_test_flag(feature, AST_FEATURE_FLAG_ONSELF)) {
			work = peer;
			idle = chan;
		} else {
			work = chan;
			idle = peer;
		}
	}

	if (!(app = pbx_findapp(feature->app))) {
		ast_log(LOG_WARNING, "Could not find application (%s)\n", feature->app);
		return -2;
	}

	ast_autoservice_start(idle);
	
	if (!ast_strlen_zero(feature->moh_class))
		ast_moh_start(idle, feature->moh_class, NULL);

	res = pbx_exec(work, app, feature->app_args);

	if (!ast_strlen_zero(feature->moh_class))
		ast_moh_stop(idle);

	ast_autoservice_stop(idle);

	if (res) {
		return FEATURE_RETURN_SUCCESSBREAK;
	}
	return FEATURE_RETURN_SUCCESS;	/*! \todo XXX should probably return res */
}

static void unmap_features(void)
{
	int x;

	ast_rwlock_wrlock(&features_lock);
	for (x = 0; x < FEATURES_COUNT; x++)
		strcpy(builtin_features[x].exten, builtin_features[x].default_exten);
	ast_rwlock_unlock(&features_lock);
}

static int remap_feature(const char *name, const char *value)
{
	int x, res = -1;

	ast_rwlock_wrlock(&features_lock);
	for (x = 0; x < FEATURES_COUNT; x++) {
		if (strcasecmp(builtin_features[x].sname, name))
			continue;

		ast_copy_string(builtin_features[x].exten, value, sizeof(builtin_features[x].exten));
		res = 0;
		break;
	}
	ast_rwlock_unlock(&features_lock);

	return res;
}

/*!
 * \brief Helper function for feature_interpret and ast_feature_detect
 * \param chan,peer,config,code,sense,dynamic_features char buf,feature flags,operation,feature
 *
 * Lock features list, browse for code, unlock list
 * If a feature is found and the operation variable is set, that feature's
 * operation is executed.  The first feature found is copied to the feature parameter.
 * \retval res on success.
 * \retval -1 on failure.
*/
static int feature_interpret_helper(struct ast_channel *chan, struct ast_channel *peer,
	struct ast_bridge_config *config, char *code, int sense, char *dynamic_features_buf,
	struct ast_flags *features, int operation, struct ast_call_feature *feature)
{
	int x;
	struct feature_group *fg = NULL;
	struct feature_group_exten *fge;
	struct ast_call_feature *tmpfeature;
	char *tmp, *tok;
	int res = AST_FEATURE_RETURN_PASSDIGITS;
	int feature_detected = 0;

	if (!(peer && chan && config) && operation) {
		return -1; /* can not run feature operation */
	}

	ast_rwlock_rdlock(&features_lock);
	for (x = 0; x < FEATURES_COUNT; x++) {
		if ((ast_test_flag(features, builtin_features[x].feature_mask)) &&
		    !ast_strlen_zero(builtin_features[x].exten)) {
			/* Feature is up for consideration */
			if (!strcmp(builtin_features[x].exten, code)) {
				ast_debug(3, "Feature detected: fname=%s sname=%s exten=%s\n", builtin_features[x].fname, builtin_features[x].sname, builtin_features[x].exten);
				if (operation) {
					res = builtin_features[x].operation(chan, peer, config, code, sense, NULL);
				}
				memcpy(feature, &builtin_features[x], sizeof(feature));
				feature_detected = 1;
				break;
			} else if (!strncmp(builtin_features[x].exten, code, strlen(code))) {
				if (res == AST_FEATURE_RETURN_PASSDIGITS)
					res = AST_FEATURE_RETURN_STOREDIGITS;
			}
		}
	}
	ast_rwlock_unlock(&features_lock);

	if (ast_strlen_zero(dynamic_features_buf) || feature_detected) {
		return res;
	}

	tmp = dynamic_features_buf;

	while ((tok = strsep(&tmp, "#"))) {
		AST_RWLIST_RDLOCK(&feature_groups);

		fg = find_group(tok);

		if (fg) {
			AST_LIST_TRAVERSE(&fg->features, fge, entry) {
				if (strcasecmp(fge->exten, code))
					continue;
				if (operation) {
					res = fge->feature->operation(chan, peer, config, code, sense, fge->feature);
				}
				memcpy(feature, fge->feature, sizeof(feature));
				if (res != AST_FEATURE_RETURN_KEEPTRYING) {
					AST_RWLIST_UNLOCK(&feature_groups);
					break;
				}
				res = AST_FEATURE_RETURN_PASSDIGITS;
			}
			if (fge)
				break;
		}

		AST_RWLIST_UNLOCK(&feature_groups);

		AST_RWLIST_RDLOCK(&feature_list);

		if (!(tmpfeature = find_dynamic_feature(tok))) {
			AST_RWLIST_UNLOCK(&feature_list);
			continue;
		}

		/* Feature is up for consideration */
		if (!strcmp(tmpfeature->exten, code)) {
			ast_verb(3, " Feature Found: %s exten: %s\n",tmpfeature->sname, tok);
			if (operation) {
				res = tmpfeature->operation(chan, peer, config, code, sense, tmpfeature);
			}
			memcpy(feature, tmpfeature, sizeof(feature));
			if (res != AST_FEATURE_RETURN_KEEPTRYING) {
				AST_RWLIST_UNLOCK(&feature_list);
				break;
			}
			res = AST_FEATURE_RETURN_PASSDIGITS;
		} else if (!strncmp(tmpfeature->exten, code, strlen(code)))
			res = AST_FEATURE_RETURN_STOREDIGITS;

		AST_RWLIST_UNLOCK(&feature_list);
	}

	return res;
}

/*!
 * \brief Check the dynamic features
 * \param chan,peer,config,code,sense
 *
 * \retval res on success.
 * \retval -1 on failure.
*/
static int ast_feature_interpret(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, char *code, int sense) {

	char dynamic_features_buf[128];
	const char *peer_dynamic_features, *chan_dynamic_features;
	struct ast_flags features;
	struct ast_call_feature feature;
	if (sense == FEATURE_SENSE_CHAN) {
		ast_copy_flags(&features, &(config->features_caller), AST_FLAGS_ALL);
	}
	else {
		ast_copy_flags(&features, &(config->features_callee), AST_FLAGS_ALL);
	}

	ast_channel_lock(peer);
	peer_dynamic_features = ast_strdupa(S_OR(pbx_builtin_getvar_helper(peer, "DYNAMIC_FEATURES"),""));
	ast_channel_unlock(peer);

	ast_channel_lock(chan);
	chan_dynamic_features = ast_strdupa(S_OR(pbx_builtin_getvar_helper(chan, "DYNAMIC_FEATURES"),""));
	ast_channel_unlock(chan);

	snprintf(dynamic_features_buf, sizeof(dynamic_features_buf), "%s%s%s", S_OR(chan_dynamic_features, ""), chan_dynamic_features && peer_dynamic_features ? "#" : "", S_OR(peer_dynamic_features,""));

	ast_debug(3, "Feature interpret: chan=%s, peer=%s, code=%s, sense=%d, features=%d, dynamic=%s\n", chan->name, peer->name, code, sense, features.flags, dynamic_features_buf);

	return feature_interpret_helper(chan, peer, config, code, sense, dynamic_features_buf, &features, 1, &feature);
}


int ast_feature_detect(struct ast_channel *chan, struct ast_flags *features, char *code, struct ast_call_feature *feature) {

	return feature_interpret_helper(chan, NULL, NULL, code, 0, NULL, features, 0, feature);
}

static void set_config_flags(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config)
{
	int x;
	
	ast_clear_flag(config, AST_FLAGS_ALL);

	ast_rwlock_rdlock(&features_lock);
	for (x = 0; x < FEATURES_COUNT; x++) {
		if (!ast_test_flag(builtin_features + x, AST_FEATURE_FLAG_NEEDSDTMF))
			continue;

		if (ast_test_flag(&(config->features_caller), builtin_features[x].feature_mask))
			ast_set_flag(config, AST_BRIDGE_DTMF_CHANNEL_0);

		if (ast_test_flag(&(config->features_callee), builtin_features[x].feature_mask))
			ast_set_flag(config, AST_BRIDGE_DTMF_CHANNEL_1);
	}
	ast_rwlock_unlock(&features_lock);
	
	if (chan && peer && !(ast_test_flag(config, AST_BRIDGE_DTMF_CHANNEL_0) && ast_test_flag(config, AST_BRIDGE_DTMF_CHANNEL_1))) {
		const char *dynamic_features = pbx_builtin_getvar_helper(chan, "DYNAMIC_FEATURES");

		if (dynamic_features) {
			char *tmp = ast_strdupa(dynamic_features);
			char *tok;
			struct ast_call_feature *feature;

			/* while we have a feature */
			while ((tok = strsep(&tmp, "#"))) {
				AST_RWLIST_RDLOCK(&feature_list);
				if ((feature = find_dynamic_feature(tok)) && ast_test_flag(feature, AST_FEATURE_FLAG_NEEDSDTMF)) {
					if (ast_test_flag(feature, AST_FEATURE_FLAG_BYCALLER))
						ast_set_flag(config, AST_BRIDGE_DTMF_CHANNEL_0);
					if (ast_test_flag(feature, AST_FEATURE_FLAG_BYCALLEE))
						ast_set_flag(config, AST_BRIDGE_DTMF_CHANNEL_1);
				}
				AST_RWLIST_UNLOCK(&feature_list);
			}
		}
	}
}

/*! 
 * \brief Get feature and dial
 * \param caller,transferee,type,format,data,timeout,outstate,cid_num,cid_name,igncallerstate
 *
 * Request channel, set channel variables, initiate call,check if they want to disconnect
 * go into loop, check if timeout has elapsed, check if person to be transfered hung up,
 * check for answer break loop, set cdr return channel.
 *
 * \todo XXX Check - this is very similar to the code in channel.c 
 * \return always a channel
*/
static struct ast_channel *ast_feature_request_and_dial(struct ast_channel *caller, struct ast_channel *transferee, const char *type, int format, void *data, int timeout, int *outstate, const char *cid_num, const char *cid_name, int igncallerstate, const char *language)
{
	int state = 0;
	int cause = 0;
	int to;
	struct ast_channel *chan;
	struct ast_channel *monitor_chans[2];
	struct ast_channel *active_channel;
	int res = 0, ready = 0;

	if ((chan = ast_request(type, format, data, &cause))) {
		ast_set_callerid(chan, cid_num, cid_name, cid_num);
		ast_string_field_set(chan, language, language);
		ast_channel_inherit_variables(caller, chan);	
		pbx_builtin_setvar_helper(chan, "TRANSFERERNAME", caller->name);
			
		if (!ast_call(chan, data, timeout)) {
			struct timeval started;
			int x, len = 0;
			char *disconnect_code = NULL, *dialed_code = NULL;

			ast_indicate(caller, AST_CONTROL_RINGING);
			/* support dialing of the featuremap disconnect code while performing an attended tranfer */
			ast_rwlock_rdlock(&features_lock);
			for (x = 0; x < FEATURES_COUNT; x++) {
				if (strcasecmp(builtin_features[x].sname, "disconnect"))
					continue;

				disconnect_code = builtin_features[x].exten;
				len = strlen(disconnect_code) + 1;
				dialed_code = alloca(len);
				memset(dialed_code, 0, len);
				break;
			}
			ast_rwlock_unlock(&features_lock);
			x = 0;
			started = ast_tvnow();
			to = timeout;

			ast_poll_channel_add(caller, chan);

			while (!((transferee && ast_check_hangup(transferee)) && (!igncallerstate && ast_check_hangup(caller))) && timeout && (chan->_state != AST_STATE_UP)) {
				struct ast_frame *f = NULL;

				monitor_chans[0] = caller;
				monitor_chans[1] = chan;
				active_channel = ast_waitfor_n(monitor_chans, 2, &to);

				/* see if the timeout has been violated */
				if(ast_tvdiff_ms(ast_tvnow(), started) > timeout) {
					state = AST_CONTROL_UNHOLD;
					ast_log(LOG_NOTICE, "We exceeded our AT-timeout\n");
					break; /*doh! timeout*/
				}

				if (!active_channel)
					continue;

				if (chan && (chan == active_channel)) {
					if (!ast_strlen_zero(chan->call_forward)) {
						if (!(chan = ast_call_forward(caller, chan, &to, format, NULL, outstate))) {
							return NULL;
						}
						continue;
					}
					f = ast_read(chan);
					if (f == NULL) { /*doh! where'd he go?*/
						state = AST_CONTROL_HANGUP;
						res = 0;
						break;
					}
					
					if (f->frametype == AST_FRAME_CONTROL || f->frametype == AST_FRAME_DTMF || f->frametype == AST_FRAME_TEXT) {
						if (f->subclass == AST_CONTROL_RINGING) {
							state = f->subclass;
							ast_verb(3, "%s is ringing\n", chan->name);
							ast_indicate(caller, AST_CONTROL_RINGING);
						} else if ((f->subclass == AST_CONTROL_BUSY) || (f->subclass == AST_CONTROL_CONGESTION)) {
							state = f->subclass;
							ast_verb(3, "%s is busy\n", chan->name);
							ast_indicate(caller, AST_CONTROL_BUSY);
							ast_frfree(f);
							f = NULL;
							break;
						} else if (f->subclass == AST_CONTROL_ANSWER) {
							/* This is what we are hoping for */
							state = f->subclass;
							ast_frfree(f);
							f = NULL;
							ready=1;
							break;
						} else if (f->subclass != -1 && f->subclass != AST_CONTROL_PROGRESS) {
							ast_log(LOG_NOTICE, "Don't know what to do about control frame: %d\n", f->subclass);
						}
						/* else who cares */
					} else if (f->frametype == AST_FRAME_VOICE || f->frametype == AST_FRAME_VIDEO) {
						ast_write(caller, f);
					}

				} else if (caller && (active_channel == caller)) {
					f = ast_read(caller);
					if (f == NULL) { /*doh! where'd he go?*/
						if (!igncallerstate) {
							if (ast_check_hangup(caller) && !ast_check_hangup(chan)) {
								/* make this a blind transfer */
								ready = 1;
								break;
							}
							state = AST_CONTROL_HANGUP;
							res = 0;
							break;
						}
					} else {
					
						if (f->frametype == AST_FRAME_DTMF) {
							dialed_code[x++] = f->subclass;
							dialed_code[x] = '\0';
							if (strlen(dialed_code) == len) {
								x = 0;
							} else if (x && strncmp(dialed_code, disconnect_code, x)) {
								x = 0;
								dialed_code[x] = '\0';
							}
							if (*dialed_code && !strcmp(dialed_code, disconnect_code)) {
								/* Caller Canceled the call */
								state = AST_CONTROL_UNHOLD;
								ast_frfree(f);
								f = NULL;
								break;
							}
						}
					}
				} else if (f->frametype == AST_FRAME_VOICE || f->frametype == AST_FRAME_VIDEO) {
					ast_write(chan, f);
				}
				if (f)
					ast_frfree(f);
			} /* end while */

			ast_poll_channel_del(caller, chan);

		} else
			ast_log(LOG_NOTICE, "Unable to call channel %s/%s\n", type, (char *)data);
	} else {
		ast_log(LOG_NOTICE, "Unable to request channel %s/%s\n", type, (char *)data);
		switch(cause) {
		case AST_CAUSE_BUSY:
			state = AST_CONTROL_BUSY;
			break;
		case AST_CAUSE_CONGESTION:
			state = AST_CONTROL_CONGESTION;
			break;
		}
	}
	
	ast_indicate(caller, -1);
	if (chan && ready) {
		if (chan->_state == AST_STATE_UP) 
			state = AST_CONTROL_ANSWER;
		res = 0;
	} else if(chan) {
		res = -1;
		ast_hangup(chan);
		chan = NULL;
	} else {
		res = -1;
	}
	
	if (outstate)
		*outstate = state;

	return chan;
}

/*!
 * \brief return the first unlocked cdr in a possible chain
*/
static struct ast_cdr *pick_unlocked_cdr(struct ast_cdr *cdr)
{
	struct ast_cdr *cdr_orig = cdr;
	while (cdr) {
		if (!ast_test_flag(cdr,AST_CDR_FLAG_LOCKED))
			return cdr;
		cdr = cdr->next;
	}
	return cdr_orig; /* everybody LOCKED or some other weirdness, like a NULL */
}

static void set_bridge_features_on_config(struct ast_bridge_config *config, const char *features)
{
	const char *feature;

	if (ast_strlen_zero(features)) {
		return;
	}

	for (feature = features; *feature; feature++) {
		switch (*feature) {
		case 'T' :
		case 't' :
			ast_set_flag(&(config->features_caller), AST_FEATURE_REDIRECT);
			break;
		case 'K' :
		case 'k' :
			ast_set_flag(&(config->features_caller), AST_FEATURE_PARKCALL);
			break;
		case 'H' :
		case 'h' :
			ast_set_flag(&(config->features_caller), AST_FEATURE_DISCONNECT);
			break;
		case 'W' :
		case 'w' :
			ast_set_flag(&(config->features_caller), AST_FEATURE_AUTOMON);
			break;
		default :
			ast_log(LOG_WARNING, "Skipping unknown feature code '%c'\n", *feature);
		}
	}
}

static void add_features_datastores(struct ast_channel *caller, struct ast_channel *callee, struct ast_bridge_config *config)
{
	struct ast_datastore *ds_callee_features = NULL, *ds_caller_features = NULL;
	struct ast_dial_features *callee_features = NULL, *caller_features = NULL;

	ast_channel_lock(caller);
	ds_caller_features = ast_channel_datastore_find(caller, &dial_features_info, NULL);
	ast_channel_unlock(caller);
	if (!ds_caller_features) {
		if (!(ds_caller_features = ast_channel_datastore_alloc(&dial_features_info, NULL))) {
			ast_log(LOG_WARNING, "Unable to create channel datastore for caller features. Aborting!\n");
			return;
		}
		if (!(caller_features = ast_calloc(1, sizeof(*caller_features)))) {
			ast_log(LOG_WARNING, "Unable to allocate memory for callee feature flags. Aborting!\n");
			ast_channel_datastore_free(ds_caller_features);
			return;
		}
		ds_caller_features->inheritance = DATASTORE_INHERIT_FOREVER;
		caller_features->is_caller = 1;
		ast_copy_flags(&(caller_features->features_callee), &(config->features_callee), AST_FLAGS_ALL);
		ast_copy_flags(&(caller_features->features_caller), &(config->features_caller), AST_FLAGS_ALL);
		ds_caller_features->data = caller_features;
		ast_channel_lock(caller);
		ast_channel_datastore_add(caller, ds_caller_features);
		ast_channel_unlock(caller);
	} else {
		/* If we don't return here, then when we do a builtin_atxfer we will copy the disconnect
		 * flags over from the atxfer to the caller */
		return;
	}

	ast_channel_lock(callee);
	ds_callee_features = ast_channel_datastore_find(callee, &dial_features_info, NULL);
	ast_channel_unlock(callee);
	if (!ds_callee_features) {
		if (!(ds_callee_features = ast_channel_datastore_alloc(&dial_features_info, NULL))) {
			ast_log(LOG_WARNING, "Unable to create channel datastore for callee features. Aborting!\n");
			return;
		}
		if (!(callee_features = ast_calloc(1, sizeof(*callee_features)))) {
			ast_log(LOG_WARNING, "Unable to allocate memory for callee feature flags. Aborting!\n");
			ast_channel_datastore_free(ds_callee_features);
			return;
		}
		ds_callee_features->inheritance = DATASTORE_INHERIT_FOREVER;
		callee_features->is_caller = 0;
		ast_copy_flags(&(callee_features->features_callee), &(config->features_caller), AST_FLAGS_ALL);
		ast_copy_flags(&(callee_features->features_caller), &(config->features_callee), AST_FLAGS_ALL);
		ds_callee_features->data = callee_features;
		ast_channel_lock(callee);
		ast_channel_datastore_add(callee, ds_callee_features);
		ast_channel_unlock(callee);
	}

	return;
}

/*!
 * \brief bridge the call and set CDR
 * \param chan,peer,config
 * 
 * Set start time, check for two channels,check if monitor on
 * check for feature activation, create new CDR
 * \retval res on success.
 * \retval -1 on failure to bridge.
*/
int ast_bridge_call(struct ast_channel *chan,struct ast_channel *peer,struct ast_bridge_config *config)
{
	/* Copy voice back and forth between the two channels.  Give the peer
	   the ability to transfer calls with '#<extension' syntax. */
	struct ast_frame *f;
	struct ast_channel *who;
	char chan_featurecode[FEATURE_MAX_LEN + 1]="";
	char peer_featurecode[FEATURE_MAX_LEN + 1]="";
	char orig_channame[AST_MAX_EXTENSION];
	char orig_peername[AST_MAX_EXTENSION];
	int res;
	int diff;
	int hasfeatures=0;
	int hadfeatures=0;
	int autoloopflag;
	struct ast_option_header *aoh;
	struct ast_bridge_config backup_config;
	struct ast_cdr *bridge_cdr = NULL;
	struct ast_cdr *orig_peer_cdr = NULL;
	struct ast_cdr *chan_cdr = chan->cdr; /* the proper chan cdr, if there are forked cdrs */
	struct ast_cdr *peer_cdr = peer->cdr; /* the proper chan cdr, if there are forked cdrs */
	struct ast_cdr *new_chan_cdr = NULL; /* the proper chan cdr, if there are forked cdrs */
	struct ast_cdr *new_peer_cdr = NULL; /* the proper chan cdr, if there are forked cdrs */

	memset(&backup_config, 0, sizeof(backup_config));

	config->start_time = ast_tvnow();

	if (chan && peer) {
		pbx_builtin_setvar_helper(chan, "BRIDGEPEER", peer->name);
		pbx_builtin_setvar_helper(peer, "BRIDGEPEER", chan->name);
	} else if (chan) {
		pbx_builtin_setvar_helper(chan, "BLINDTRANSFER", NULL);
	}

	set_bridge_features_on_config(config, pbx_builtin_getvar_helper(chan, "BRIDGE_FEATURES"));
	add_features_datastores(chan, peer, config);

	/* This is an interesting case.  One example is if a ringing channel gets redirected to
	 * an extension that picks up a parked call.  This will make sure that the call taken
	 * out of parking gets told that the channel it just got bridged to is still ringing. */
	if (chan->_state == AST_STATE_RINGING && peer->visible_indication != AST_CONTROL_RINGING) {
		ast_indicate(peer, AST_CONTROL_RINGING);
	}

	if (monitor_ok) {
		const char *monitor_exec;
		struct ast_channel *src = NULL;
		if (!monitor_app) { 
			if (!(monitor_app = pbx_findapp("Monitor")))
				monitor_ok=0;
		}
		if ((monitor_exec = pbx_builtin_getvar_helper(chan, "AUTO_MONITOR"))) 
			src = chan;
		else if ((monitor_exec = pbx_builtin_getvar_helper(peer, "AUTO_MONITOR")))
			src = peer;
		if (monitor_app && src) {
			char *tmp = ast_strdupa(monitor_exec);
			pbx_exec(src, monitor_app, tmp);
		}
	}

	set_config_flags(chan, peer, config);
	config->firstpass = 1;

	/* Answer if need be */
	if (chan->_state != AST_STATE_UP) {
		if (ast_raw_answer(chan, 1)) {
			return -1;
		}
	}

	ast_copy_string(orig_channame,chan->name,sizeof(orig_channame));
	ast_copy_string(orig_peername,peer->name,sizeof(orig_peername));
	orig_peer_cdr = peer_cdr;
	
	if (!chan_cdr || (chan_cdr && !ast_test_flag(chan_cdr, AST_CDR_FLAG_POST_DISABLED))) {
		
		if (chan_cdr) {
			ast_set_flag(chan_cdr, AST_CDR_FLAG_MAIN);
			ast_cdr_update(chan);
			bridge_cdr = ast_cdr_dup(chan_cdr);
			/* rip any forked CDR's off of the chan_cdr and attach
			 * them to the bridge_cdr instead */
			bridge_cdr->next = chan_cdr->next;
			chan_cdr->next = NULL;
			ast_copy_string(bridge_cdr->lastapp, S_OR(chan->appl, ""), sizeof(bridge_cdr->lastapp));
			ast_copy_string(bridge_cdr->lastdata, S_OR(chan->data, ""), sizeof(bridge_cdr->lastdata));
			if (peer_cdr && !ast_strlen_zero(peer_cdr->userfield)) {
				ast_copy_string(bridge_cdr->userfield, peer_cdr->userfield, sizeof(bridge_cdr->userfield));
			}
			ast_cdr_setaccount(peer, chan->accountcode);

		} else {
			/* better yet, in a xfer situation, find out why the chan cdr got zapped (pun unintentional) */
			bridge_cdr = ast_cdr_alloc(); /* this should be really, really rare/impossible? */
			ast_copy_string(bridge_cdr->channel, chan->name, sizeof(bridge_cdr->channel));
			ast_copy_string(bridge_cdr->dstchannel, peer->name, sizeof(bridge_cdr->dstchannel));
			ast_copy_string(bridge_cdr->uniqueid, chan->uniqueid, sizeof(bridge_cdr->uniqueid));
			ast_copy_string(bridge_cdr->lastapp, S_OR(chan->appl, ""), sizeof(bridge_cdr->lastapp));
			ast_copy_string(bridge_cdr->lastdata, S_OR(chan->data, ""), sizeof(bridge_cdr->lastdata));
			ast_cdr_setcid(bridge_cdr, chan);
			bridge_cdr->disposition = (chan->_state == AST_STATE_UP) ?  AST_CDR_ANSWERED : AST_CDR_NULL;
			bridge_cdr->amaflags = chan->amaflags ? chan->amaflags :  ast_default_amaflags;
			ast_copy_string(bridge_cdr->accountcode, chan->accountcode, sizeof(bridge_cdr->accountcode));
			/* Destination information */
			ast_copy_string(bridge_cdr->dst, chan->exten, sizeof(bridge_cdr->dst));
			ast_copy_string(bridge_cdr->dcontext, chan->context, sizeof(bridge_cdr->dcontext));
			if (peer_cdr) {
				bridge_cdr->start = peer_cdr->start;
				ast_copy_string(bridge_cdr->userfield, peer_cdr->userfield, sizeof(bridge_cdr->userfield));
			} else {
				ast_cdr_start(bridge_cdr);
			}
		}
		ast_debug(4,"bridge answer set, chan answer set\n");
		/* peer_cdr->answer will be set when a macro runs on the peer;
		   in that case, the bridge answer will be delayed while the
		   macro plays on the peer channel. The peer answered the call
		   before the macro started playing. To the phone system,
		   this is billable time for the call, even tho the caller
		   hears nothing but ringing while the macro does its thing. */

		/* Another case where the peer cdr's time will be set, is when
		   A self-parks by pickup up phone and dialing 700, then B
		   picks up A by dialing its parking slot; there may be more 
		   practical paths that get the same result, tho... in which
		   case you get the previous answer time from the Park... which
		   is before the bridge's start time, so I added in the 
		   tvcmp check to the if below */

		if (peer_cdr && !ast_tvzero(peer_cdr->answer) && ast_tvcmp(peer_cdr->answer, bridge_cdr->start) >= 0) {
			ast_cdr_setanswer(bridge_cdr, peer_cdr->answer);
			ast_cdr_setdisposition(bridge_cdr, peer_cdr->disposition);
			if (chan_cdr) {
				ast_cdr_setanswer(chan_cdr, peer_cdr->answer);
				ast_cdr_setdisposition(chan_cdr, peer_cdr->disposition);
			}
		} else {
			ast_cdr_answer(bridge_cdr);
			if (chan_cdr) {
				ast_cdr_answer(chan_cdr); /* for the sake of cli status checks */
			}
		}
		if (ast_test_flag(chan,AST_FLAG_BRIDGE_HANGUP_DONT) && (chan_cdr || peer_cdr)) {
			if (chan_cdr) {
				ast_set_flag(chan_cdr, AST_CDR_FLAG_BRIDGED);
			}
			if (peer_cdr) {
				ast_set_flag(peer_cdr, AST_CDR_FLAG_BRIDGED);
			}
		}
	}
	for (;;) {
		struct ast_channel *other;	/* used later */
	
		res = ast_channel_bridge(chan, peer, config, &f, &who);
		
		/* When frame is not set, we are probably involved in a situation
		   where we've timed out.
		   When frame is set, we'll come this code twice; once for DTMF_BEGIN
		   and also for DTMF_END. If we flow into the following 'if' for both, then 
		   our wait times are cut in half, as both will subtract from the
		   feature_timer. Not good!
		*/
		if (config->feature_timer && (!f || f->frametype == AST_FRAME_DTMF_END)) {
			/* Update time limit for next pass */
			diff = ast_tvdiff_ms(ast_tvnow(), config->start_time);
			if (res == AST_BRIDGE_RETRY) {
				/* The feature fully timed out but has not been updated. Skip
				 * the potential round error from the diff calculation and
				 * explicitly set to expired. */
				config->feature_timer = -1;
			} else {
				config->feature_timer -= diff;
			}

			if (hasfeatures) {
				/* Running on backup config, meaning a feature might be being
				   activated, but that's no excuse to keep things going 
				   indefinitely! */
				if (backup_config.feature_timer && ((backup_config.feature_timer -= diff) <= 0)) {
					ast_debug(1, "Timed out, realtime this time!\n");
					config->feature_timer = 0;
					who = chan;
					if (f)
						ast_frfree(f);
					f = NULL;
					res = 0;
				} else if (config->feature_timer <= 0) {
					/* Not *really* out of time, just out of time for
					   digits to come in for features. */
					ast_debug(1, "Timed out for feature!\n");
					if (!ast_strlen_zero(peer_featurecode)) {
						ast_dtmf_stream(chan, peer, peer_featurecode, 0, 0);
						memset(peer_featurecode, 0, sizeof(peer_featurecode));
					}
					if (!ast_strlen_zero(chan_featurecode)) {
						ast_dtmf_stream(peer, chan, chan_featurecode, 0, 0);
						memset(chan_featurecode, 0, sizeof(chan_featurecode));
					}
					if (f)
						ast_frfree(f);
					hasfeatures = !ast_strlen_zero(chan_featurecode) || !ast_strlen_zero(peer_featurecode);
					if (!hasfeatures) {
						/* Restore original (possibly time modified) bridge config */
						memcpy(config, &backup_config, sizeof(struct ast_bridge_config));
						memset(&backup_config, 0, sizeof(backup_config));
					}
					hadfeatures = hasfeatures;
					/* Continue as we were */
					continue;
				} else if (!f) {
					/* The bridge returned without a frame and there is a feature in progress.
					 * However, we don't think the feature has quite yet timed out, so just
					 * go back into the bridge. */
					continue;
 				}
			} else {
				if (config->feature_timer <=0) {
					/* We ran out of time */
					config->feature_timer = 0;
					who = chan;
					if (f)
						ast_frfree(f);
					f = NULL;
					res = 0;
				}
			}
		}
		if (res < 0) {
			if (!ast_test_flag(chan, AST_FLAG_ZOMBIE) && !ast_test_flag(peer, AST_FLAG_ZOMBIE) && !ast_check_hangup(chan) && !ast_check_hangup(peer))
				ast_log(LOG_WARNING, "Bridge failed on channels %s and %s\n", chan->name, peer->name);
			goto before_you_go;
		}
		
		if (!f || (f->frametype == AST_FRAME_CONTROL &&
				(f->subclass == AST_CONTROL_HANGUP || f->subclass == AST_CONTROL_BUSY || 
					f->subclass == AST_CONTROL_CONGESTION))) {
			res = -1;
			break;
		}
		/* many things should be sent to the 'other' channel */
		other = (who == chan) ? peer : chan;
		if (f->frametype == AST_FRAME_CONTROL) {
			switch (f->subclass) {
			case AST_CONTROL_RINGING:
			case AST_CONTROL_FLASH:
			case -1:
				ast_indicate(other, f->subclass);
				break;
			case AST_CONTROL_HOLD:
			case AST_CONTROL_UNHOLD:
				ast_indicate_data(other, f->subclass, f->data, f->datalen);
				break;
			case AST_CONTROL_OPTION:
				aoh = f->data;
				/* Forward option Requests */
				if (aoh && aoh->flag == AST_OPTION_FLAG_REQUEST) {
					ast_channel_setoption(other, ntohs(aoh->option), aoh->data, 
						f->datalen - sizeof(struct ast_option_header), 0);
				}
				break;
			}
		} else if (f->frametype == AST_FRAME_DTMF_BEGIN) {
			/* eat it */
		} else if (f->frametype == AST_FRAME_DTMF) {
			char *featurecode;
			int sense;

			hadfeatures = hasfeatures;
			/* This cannot overrun because the longest feature is one shorter than our buffer */
			if (who == chan) {
				sense = FEATURE_SENSE_CHAN;
				featurecode = chan_featurecode;
			} else  {
				sense = FEATURE_SENSE_PEER;
				featurecode = peer_featurecode;
			}
			/*! append the event to featurecode. we rely on the string being zero-filled, and
			 * not overflowing it. 
			 * \todo XXX how do we guarantee the latter ?
			 */
			featurecode[strlen(featurecode)] = f->subclass;
			/* Get rid of the frame before we start doing "stuff" with the channels */
			ast_frfree(f);
			f = NULL;
			config->feature_timer = backup_config.feature_timer;
			res = ast_feature_interpret(chan, peer, config, featurecode, sense);
			switch(res) {
			case FEATURE_RETURN_PASSDIGITS:
				ast_dtmf_stream(other, who, featurecode, 0, 0);
				/* Fall through */
			case FEATURE_RETURN_SUCCESS:
				memset(featurecode, 0, sizeof(chan_featurecode));
				break;
			}
			if (res >= FEATURE_RETURN_PASSDIGITS) {
				res = 0;
			} else 
				break;
			hasfeatures = !ast_strlen_zero(chan_featurecode) || !ast_strlen_zero(peer_featurecode);
			if (hadfeatures && !hasfeatures) {
				/* Restore backup */
				memcpy(config, &backup_config, sizeof(struct ast_bridge_config));
				memset(&backup_config, 0, sizeof(struct ast_bridge_config));
			} else if (hasfeatures) {
				if (!hadfeatures) {
					/* Backup configuration */
					memcpy(&backup_config, config, sizeof(struct ast_bridge_config));
					/* Setup temporary config options */
					config->play_warning = 0;
					ast_clear_flag(&(config->features_caller), AST_FEATURE_PLAY_WARNING);
					ast_clear_flag(&(config->features_callee), AST_FEATURE_PLAY_WARNING);
					config->warning_freq = 0;
					config->warning_sound = NULL;
					config->end_sound = NULL;
					config->start_sound = NULL;
					config->firstpass = 0;
				}
				config->start_time = ast_tvnow();
				config->feature_timer = featuredigittimeout;
				ast_debug(1, "Set time limit to %ld\n", config->feature_timer);
			}
		}
		if (f)
			ast_frfree(f);

	}
   before_you_go:

	if (ast_test_flag(chan,AST_FLAG_BRIDGE_HANGUP_DONT)) {
		ast_clear_flag(chan,AST_FLAG_BRIDGE_HANGUP_DONT); /* its job is done */
		if (bridge_cdr) {
			ast_cdr_discard(bridge_cdr);
			/* QUESTION: should we copy bridge_cdr fields to the peer before we throw it away? */
		}
		return res; /* if we shouldn't do the h-exten, we shouldn't do the bridge cdr, either! */
	}

	if (config->end_bridge_callback) {
		config->end_bridge_callback(config->end_bridge_callback_data);
	}

	if (!ast_test_flag(&(config->features_caller),AST_FEATURE_NO_H_EXTEN) &&
		ast_exists_extension(chan, chan->context, "h", 1, chan->cid.cid_num)) {
		struct ast_cdr *swapper = NULL;
		char savelastapp[AST_MAX_EXTENSION];
		char savelastdata[AST_MAX_EXTENSION];
		char save_exten[AST_MAX_EXTENSION];
		int  save_prio;
		int  found = 0;	/* set if we find at least one match */
		int  spawn_error = 0;
		
		autoloopflag = ast_test_flag(chan, AST_FLAG_IN_AUTOLOOP);
		ast_set_flag(chan, AST_FLAG_IN_AUTOLOOP);
		if (bridge_cdr && ast_opt_end_cdr_before_h_exten) {
			ast_cdr_end(bridge_cdr);
		}
		/* swap the bridge cdr and the chan cdr for a moment, and let the endbridge
		   dialplan code operate on it */
		ast_channel_lock(chan);
		if (bridge_cdr) {
			swapper = chan->cdr;
			ast_copy_string(savelastapp, bridge_cdr->lastapp, sizeof(bridge_cdr->lastapp));
			ast_copy_string(savelastdata, bridge_cdr->lastdata, sizeof(bridge_cdr->lastdata));
			chan->cdr = bridge_cdr;
		}
		ast_copy_string(save_exten, chan->exten, sizeof(save_exten));
		save_prio = chan->priority;
		ast_copy_string(chan->exten, "h", sizeof(chan->exten));
		chan->priority = 1;
		ast_channel_unlock(chan);
		while ((spawn_error = ast_spawn_extension(chan, chan->context, chan->exten, chan->priority, chan->cid.cid_num, &found, 1)) == 0) {
			chan->priority++;
		}
		if (spawn_error && (!ast_exists_extension(chan, chan->context, chan->exten, chan->priority, chan->cid.cid_num) || ast_check_hangup(chan))) {
			/* if the extension doesn't exist or a hangup occurred, this isn't really a spawn error */
			spawn_error = 0;
		}
		if (found && spawn_error) {
			/* Something bad happened, or a hangup has been requested. */
			ast_debug(1, "Spawn extension (%s,%s,%d) exited non-zero on '%s'\n", chan->context, chan->exten, chan->priority, chan->name);
			ast_verb(2, "Spawn extension (%s, %s, %d) exited non-zero on '%s'\n", chan->context, chan->exten, chan->priority, chan->name);
		}
		/* swap it back */
		ast_channel_lock(chan);
		ast_copy_string(chan->exten, save_exten, sizeof(chan->exten));
		chan->priority = save_prio;
		if (bridge_cdr) {
			if (chan->cdr == bridge_cdr) {
				chan->cdr = swapper;
			} else {
				bridge_cdr = NULL;
			}
		}
		if (!spawn_error) {
			ast_set_flag(chan, AST_FLAG_BRIDGE_HANGUP_RUN);
		}
		ast_channel_unlock(chan);
		/* protect the lastapp/lastdata against the effects of the hangup/dialplan code */
		if (bridge_cdr) {
			ast_copy_string(bridge_cdr->lastapp, savelastapp, sizeof(bridge_cdr->lastapp));
			ast_copy_string(bridge_cdr->lastdata, savelastdata, sizeof(bridge_cdr->lastdata));
		}
		ast_set2_flag(chan, autoloopflag, AST_FLAG_IN_AUTOLOOP);
	}
	
	/* obey the NoCDR() wishes. -- move the DISABLED flag to the bridge CDR if it was set on the channel during the bridge... */
	new_chan_cdr = pick_unlocked_cdr(chan->cdr); /* the proper chan cdr, if there are forked cdrs */
	if (bridge_cdr && new_chan_cdr && ast_test_flag(new_chan_cdr, AST_CDR_FLAG_POST_DISABLED))
		ast_set_flag(bridge_cdr, AST_CDR_FLAG_POST_DISABLED);

	/* we can post the bridge CDR at this point */
	if (bridge_cdr) {
		ast_cdr_end(bridge_cdr);
		ast_cdr_detach(bridge_cdr);
	}
	
	/* do a specialized reset on the beginning channel
	   CDR's, if they still exist, so as not to mess up
	   issues in future bridges;
	   
	   Here are the rules of the game:
	   1. The chan and peer channel pointers will not change
	      during the life of the bridge.
	   2. But, in transfers, the channel names will change.
	      between the time the bridge is started, and the
	      time the channel ends. 
	      Usually, when a channel changes names, it will
	      also change CDR pointers.
	   3. Usually, only one of the two channels (chan or peer)
	      will change names.
	   4. Usually, if a channel changes names during a bridge,
	      it is because of a transfer. Usually, in these situations,
	      it is normal to see 2 bridges running simultaneously, and
	      it is not unusual to see the two channels that change
	      swapped between bridges.
	   5. After a bridge occurs, we have 2 or 3 channels' CDRs
	      to attend to; if the chan or peer changed names,
	      we have the before and after attached CDR's.
	*/
	
	if (new_chan_cdr) {
		struct ast_channel *chan_ptr = NULL;
 
 		if (strcasecmp(orig_channame, chan->name) != 0) { 
 			/* old channel */
 			chan_ptr = ast_get_channel_by_name_locked(orig_channame);
 			if (chan_ptr) {
 				if (!ast_bridged_channel(chan_ptr)) {
 					struct ast_cdr *cur;
 					for (cur = chan_ptr->cdr; cur; cur = cur->next) {
 						if (cur == chan_cdr) {
 							break;
 						}
 					}
 					if (cur)
 						ast_cdr_specialized_reset(chan_cdr,0);
 				}
 				ast_channel_unlock(chan_ptr);
 			}
 			/* new channel */
 			ast_cdr_specialized_reset(new_chan_cdr,0);
 		} else {
 			ast_cdr_specialized_reset(chan_cdr,0); /* nothing changed, reset the chan_cdr  */
 		}
	}
	
	{
		struct ast_channel *chan_ptr = NULL;
		new_peer_cdr = pick_unlocked_cdr(peer->cdr); /* the proper chan cdr, if there are forked cdrs */
		if (new_chan_cdr && ast_test_flag(new_chan_cdr, AST_CDR_FLAG_POST_DISABLED) && new_peer_cdr && !ast_test_flag(new_peer_cdr, AST_CDR_FLAG_POST_DISABLED))
			ast_set_flag(new_peer_cdr, AST_CDR_FLAG_POST_DISABLED); /* DISABLED is viral-- it will propagate across a bridge */
		if (strcasecmp(orig_peername, peer->name) != 0) { 
			/* old channel */
			chan_ptr = ast_get_channel_by_name_locked(orig_peername);
			if (chan_ptr) {
				if (!ast_bridged_channel(chan_ptr)) {
					struct ast_cdr *cur;
					for (cur = chan_ptr->cdr; cur; cur = cur->next) {
						if (cur == peer_cdr) {
 							break;
 						}
 					}
 					if (cur)
 						ast_cdr_specialized_reset(peer_cdr,0);
 				}
 				ast_channel_unlock(chan_ptr);
 			}
 			/* new channel */
 			ast_cdr_specialized_reset(new_peer_cdr,0);
 		} else {
 			ast_cdr_specialized_reset(peer_cdr,0); /* nothing changed, reset the peer_cdr  */
 		}
 	}
	
	return res;
}

/*! \brief Output parking event to manager */
static void post_manager_event(const char *s, struct parkeduser *pu)
{
	manager_event(EVENT_FLAG_CALL, s,
		"Exten: %s\r\n"
		"Channel: %s\r\n"
		"CallerIDNum: %s\r\n"
		"CallerIDName: %s\r\n\r\n",
		pu->parkingexten, 
		pu->chan->name,
		S_OR(pu->chan->cid.cid_num, "<unknown>"),
		S_OR(pu->chan->cid.cid_name, "<unknown>")
		);
}

static char *callback_dialoptions(struct ast_flags *features_callee, struct ast_flags *features_caller, char *options, size_t len)
{
	int i = 0;
	enum {
		OPT_CALLEE_REDIRECT   = 't',
		OPT_CALLER_REDIRECT   = 'T',
		OPT_CALLEE_AUTOMON    = 'w',
		OPT_CALLER_AUTOMON    = 'W',
		OPT_CALLEE_DISCONNECT = 'h',
		OPT_CALLER_DISCONNECT = 'H',
		OPT_CALLEE_PARKCALL   = 'k',
		OPT_CALLER_PARKCALL   = 'K',
	};

	memset(options, 0, len);
	if (ast_test_flag(features_caller, AST_FEATURE_REDIRECT) && i < len) {
		options[i++] = OPT_CALLER_REDIRECT;
	}
	if (ast_test_flag(features_caller, AST_FEATURE_AUTOMON) && i < len) {
		options[i++] = OPT_CALLER_AUTOMON;
	}
	if (ast_test_flag(features_caller, AST_FEATURE_DISCONNECT) && i < len) {
		options[i++] = OPT_CALLER_DISCONNECT;
	}
	if (ast_test_flag(features_caller, AST_FEATURE_PARKCALL) && i < len) {
		options[i++] = OPT_CALLER_PARKCALL;
	}

	if (ast_test_flag(features_callee, AST_FEATURE_REDIRECT) && i < len) {
		options[i++] = OPT_CALLEE_REDIRECT;
	}
	if (ast_test_flag(features_callee, AST_FEATURE_AUTOMON) && i < len) {
		options[i++] = OPT_CALLEE_AUTOMON;
	}
	if (ast_test_flag(features_callee, AST_FEATURE_DISCONNECT) && i < len) {
		options[i++] = OPT_CALLEE_DISCONNECT;
	}
	if (ast_test_flag(features_callee, AST_FEATURE_PARKCALL) && i < len) {
		options[i++] = OPT_CALLEE_PARKCALL;
	}

	return options;
}

/*! 
 * \brief Take care of parked calls and unpark them if needed 
 * \param ignore unused var.
 * 
 * Start inf loop, lock parking lot, check if any parked channels have gone above timeout
 * if so, remove channel from parking lot and return it to the extension that parked it.
 * Check if parked channel decided to hangup, wait until next FD via select().
*/
static void *do_parking_thread(void *ignore)
{
	char parkingslot[AST_MAX_EXTENSION];
	fd_set rfds, efds;	/* results from previous select, to be preserved across loops. */

	FD_ZERO(&rfds);
	FD_ZERO(&efds);

	for (;;) {
		struct parkeduser *pu;
		int ms = -1;	/* select timeout, uninitialized */
		int max = -1;	/* max fd, none there yet */
		fd_set nrfds, nefds;	/* args for the next select */
		FD_ZERO(&nrfds);
		FD_ZERO(&nefds);

		AST_LIST_LOCK(&parkinglot);
		AST_LIST_TRAVERSE_SAFE_BEGIN(&parkinglot, pu, list) {
			struct ast_channel *chan = pu->chan;	/* shorthand */
			int tms;        /* timeout for this item */
			int x;          /* fd index in channel */
			struct ast_context *con;

			if (pu->notquiteyet) /* Pretend this one isn't here yet */
				continue;
			tms = ast_tvdiff_ms(ast_tvnow(), pu->start);
			if (tms > pu->parkingtime) {
				ast_indicate(chan, AST_CONTROL_UNHOLD);
				/* Get chan, exten from derived kludge */
				if (pu->peername[0]) {
					char *peername = ast_strdupa(pu->peername);
					char *cp = strrchr(peername, '-');
					char peername_flat[AST_MAX_EXTENSION]; /* using something like DAHDI/52 for an extension name is NOT a good idea */
					int i;

					if (cp) 
						*cp = 0;
					ast_copy_string(peername_flat,peername,sizeof(peername_flat));
					for(i=0; peername_flat[i] && i < AST_MAX_EXTENSION; i++) {
						if (peername_flat[i] == '/') 
							peername_flat[i]= '0';
					}
					con = ast_context_find_or_create(NULL, NULL, parking_con_dial, registrar);
					if (!con)
						ast_log(LOG_ERROR, "Parking dial context '%s' does not exist and unable to create\n", parking_con_dial);
					if (con) {
						char returnexten[AST_MAX_EXTENSION];
						struct ast_datastore *features_datastore;
						struct ast_dial_features *dialfeatures = NULL;

						ast_channel_lock(chan);

						if ((features_datastore = ast_channel_datastore_find(chan, &dial_features_info, NULL)))
							dialfeatures = features_datastore->data;

						ast_channel_unlock(chan);

						if (!strncmp(peername, "Parked/", 7)) {
							peername += 7;
						}

						if (dialfeatures) {
							char buf[MAX_DIAL_FEATURE_OPTIONS] = {0,};
							snprintf(returnexten, sizeof(returnexten), "%s,30,%s", peername, callback_dialoptions(&(dialfeatures->features_callee), &(dialfeatures->features_caller), buf, sizeof(buf)));
						} else { /* Existing default */
							snprintf(returnexten, sizeof(returnexten), "%s,30,t", peername);
						}
						ast_add_extension2(con, 1, peername_flat, 1, NULL, NULL, "Dial", ast_strdup(returnexten), ast_free_ptr, registrar);
					}
					if (comebacktoorigin) {
						set_c_e_p(chan, parking_con_dial, peername_flat, 1);
					} else {
						ast_log(LOG_WARNING, "now going to parkedcallstimeout,s,1 | ps is %d\n",pu->parkingnum);
						snprintf(parkingslot, sizeof(parkingslot), "%d", pu->parkingnum);
						pbx_builtin_setvar_helper(pu->chan, "PARKINGSLOT", parkingslot);
						set_c_e_p(chan, "parkedcallstimeout", peername_flat, 1);
					}
				} else {
					/* They've been waiting too long, send them back to where they came.  Theoretically they
					   should have their original extensions and such, but we copy to be on the safe side */
					set_c_e_p(chan, pu->context, pu->exten, pu->priority);
				}

				post_manager_event("ParkedCallTimeOut", pu);

				ast_verb(2, "Timeout for %s parked on %d. Returning to %s,%s,%d\n", chan->name, pu->parkingnum, chan->context, chan->exten, chan->priority);
				/* Start up the PBX, or hang them up */
				if (ast_pbx_start(chan))  {
					ast_log(LOG_WARNING, "Unable to restart the PBX for user on '%s', hanging them up...\n", chan->name);
					ast_hangup(chan);
				}
				/* And take them out of the parking lot */
				AST_LIST_REMOVE_CURRENT(list);
				con = ast_context_find(parking_con);
				if (con) {
					if (ast_context_remove_extension2(con, pu->parkingexten, 1, NULL, 0))
						ast_log(LOG_WARNING, "Whoa, failed to remove the extension!\n");
					else
						notify_metermaids(pu->parkingexten, parking_con, AST_DEVICE_NOT_INUSE);
				} else
					ast_log(LOG_WARNING, "Whoa, no parking context?\n");
				ast_free(pu);
			} else {	/* still within parking time, process descriptors */
				for (x = 0; x < AST_MAX_FDS; x++) {
					struct ast_frame *f;

					if (chan->fds[x] == -1 || (!FD_ISSET(chan->fds[x], &rfds) && !FD_ISSET(chan->fds[x], &efds)))
						continue;	/* nothing on this descriptor */

					if (FD_ISSET(chan->fds[x], &efds))
						ast_set_flag(chan, AST_FLAG_EXCEPTION);
					else
						ast_clear_flag(chan, AST_FLAG_EXCEPTION);
					chan->fdno = x;

					/* See if they need servicing */
					f = ast_read(chan);
					if (!f || (f->frametype == AST_FRAME_CONTROL && f->subclass ==  AST_CONTROL_HANGUP)) {
						if (f)
							ast_frfree(f);
						post_manager_event("ParkedCallGiveUp", pu);

						/* There's a problem, hang them up*/
						ast_verb(2, "%s got tired of being parked\n", chan->name);
						ast_hangup(chan);
						/* And take them out of the parking lot */
						AST_LIST_REMOVE_CURRENT(list);
						con = ast_context_find(parking_con);
						if (con) {
							if (ast_context_remove_extension2(con, pu->parkingexten, 1, NULL, 0))
								ast_log(LOG_WARNING, "Whoa, failed to remove the extension!\n");
							else
								notify_metermaids(pu->parkingexten, parking_con, AST_DEVICE_NOT_INUSE);
						} else
							ast_log(LOG_WARNING, "Whoa, no parking context?\n");
						ast_free(pu);
						break;
					} else {
						/*! \todo XXX Maybe we could do something with packets, like dial "0" for operator or something XXX */
						ast_frfree(f);
						if (pu->moh_trys < 3 && !chan->generatordata) {
							ast_debug(1, "MOH on parked call stopped by outside source.  Restarting.\n");
							ast_indicate_data(chan, AST_CONTROL_HOLD, 
								S_OR(parkmohclass, NULL),
								!ast_strlen_zero(parkmohclass) ? strlen(parkmohclass) + 1 : 0);
							pu->moh_trys++;
						}
						goto std;	/*! \todo XXX Ick: jumping into an else statement??? XXX */
					}

				} /* end for */
				if (x >= AST_MAX_FDS) {
std:					for (x=0; x<AST_MAX_FDS; x++) {	/* mark fds for next round */
						if (chan->fds[x] > -1) {
							FD_SET(chan->fds[x], &nrfds);
							FD_SET(chan->fds[x], &nefds);
							if (chan->fds[x] > max)
								max = chan->fds[x];
						}
					}
					/* Keep track of our shortest wait */
					if (tms < ms || ms < 0)
						ms = tms;
				}
			}
		} /* end while */
		AST_LIST_TRAVERSE_SAFE_END
		AST_LIST_UNLOCK(&parkinglot);
		rfds = nrfds;
		efds = nefds;
		{
			struct timeval tv = ast_samp2tv(ms, 1000);
			/* Wait for something to happen */
			ast_select(max + 1, &rfds, NULL, &efds, (ms > -1) ? &tv : NULL);
		}
		pthread_testcancel();
	}
	return NULL;	/* Never reached */
}

/*! \brief Park a call */
static int park_call_exec(struct ast_channel *chan, void *data)
{
 	char *orig_chan_name = ast_strdupa(chan->name);
	char orig_exten[AST_MAX_EXTENSION];
	int orig_priority = chan->priority;

	/* Data is unused at the moment but could contain a parking
	   lot context eventually */
	int res = 0;

	ast_copy_string(orig_exten, chan->exten, sizeof(orig_exten));

	/* Setup the exten/priority to be s/1 since we don't know
	   where this call should return */
	strcpy(chan->exten, "s");
	chan->priority = 1;
	/* Answer if call is not up */
	if (chan->_state != AST_STATE_UP)
		res = ast_answer(chan);
	/* Sleep to allow VoIP streams to settle down */
	if (!res)
		res = ast_safe_sleep(chan, 1000);
	/* Park the call */
	if (!res) {
 		res = masq_park_call_announce(chan, chan, 0, NULL, orig_chan_name);
		/* Continue on in the dialplan */
		if (res == 1) {
			ast_copy_string(chan->exten, orig_exten, sizeof(chan->exten));
			chan->priority = orig_priority;
			res = 0;
		} else if (!res) {
			res = 1;
		}
	}

	return res;
}

/*! \brief Pickup parked call */
static int park_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct ast_channel *peer=NULL;
	struct parkeduser *pu;
	struct ast_context *con;
	int park = 0;
	struct ast_bridge_config config;

	if (data)
		park = atoi((char *)data);

	AST_LIST_LOCK(&parkinglot);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&parkinglot, pu, list) {
		if (!data || pu->parkingnum == park) {
			AST_LIST_REMOVE_CURRENT(list);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END
	AST_LIST_UNLOCK(&parkinglot);

	if (pu) {
		peer = pu->chan;
		con = ast_context_find(parking_con);
		if (con) {
			if (ast_context_remove_extension2(con, pu->parkingexten, 1, NULL, 0))
				ast_log(LOG_WARNING, "Whoa, failed to remove the extension!\n");
			else
				notify_metermaids(pu->parkingexten, parking_con, AST_DEVICE_NOT_INUSE);
		} else
			ast_log(LOG_WARNING, "Whoa, no parking context?\n");

		manager_event(EVENT_FLAG_CALL, "UnParkedCall",
			"Exten: %s\r\n"
			"Channel: %s\r\n"
			"From: %s\r\n"
			"CallerIDNum: %s\r\n"
			"CallerIDName: %s\r\n",
			pu->parkingexten, pu->chan->name, chan->name,
			S_OR(pu->chan->cid.cid_num, "<unknown>"),
			S_OR(pu->chan->cid.cid_name, "<unknown>")
			);

		ast_free(pu);
	}
	/* JK02: it helps to answer the channel if not already up */
	if (chan->_state != AST_STATE_UP)
		ast_answer(chan);

	if (peer) {
		struct ast_datastore *features_datastore;
		struct ast_dial_features *dialfeatures = NULL;

		/* Play a courtesy to the source(s) configured to prefix the bridge connecting */

		if (!ast_strlen_zero(courtesytone)) {
			int error = 0;
			ast_indicate(peer, AST_CONTROL_UNHOLD);
			if (parkedplay == 0) {
				error = ast_stream_and_wait(chan, courtesytone, "");
			} else if (parkedplay == 1) {
				error = ast_stream_and_wait(peer, courtesytone, "");
			} else if (parkedplay == 2) {
				if (!ast_streamfile(chan, courtesytone, chan->language) &&
						!ast_streamfile(peer, courtesytone, chan->language)) {
					/*! \todo XXX we would like to wait on both! */
					res = ast_waitstream(chan, "");
					if (res >= 0)
						res = ast_waitstream(peer, "");
					if (res < 0)
						error = 1;
				}
			}
			if (error) {
				ast_log(LOG_WARNING, "Failed to play courtesy tone!\n");
				ast_hangup(peer);
				return -1;
			}
		} else
			ast_indicate(peer, AST_CONTROL_UNHOLD);

		res = ast_channel_make_compatible(chan, peer);
		if (res < 0) {
			ast_log(LOG_WARNING, "Could not make channels %s and %s compatible for bridge\n", chan->name, peer->name);
			ast_hangup(peer);
			return -1;
		}
		/* This runs sorta backwards, since we give the incoming channel control, as if it
		   were the person called. */
		ast_verb(3, "Channel %s connected to parked call %d\n", chan->name, park);

		pbx_builtin_setvar_helper(chan, "PARKEDCHANNEL", peer->name);
		ast_cdr_setdestchan(chan->cdr, peer->name);
		memset(&config, 0, sizeof(struct ast_bridge_config));

		/* Get datastore for peer and apply it's features to the callee side of the bridge config */
		ast_channel_lock(peer);
		if ((features_datastore = ast_channel_datastore_find(peer, &dial_features_info, NULL))) {
			dialfeatures = features_datastore->data;
		}
		ast_channel_unlock(peer);

		/* When the datastores for both caller and callee are created, both the callee and caller channels
		 * use the features_caller flag variable to represent themselves. With that said, the config.features_callee
		 * flags should be copied from the datastore's caller feature flags regardless if peer was a callee
		 * or caller. */
		if (dialfeatures) {
			ast_copy_flags(&(config.features_callee), &(dialfeatures->features_caller), AST_FLAGS_ALL);
		}

		if ((parkedcalltransfers == AST_FEATURE_FLAG_BYCALLEE) || (parkedcalltransfers == AST_FEATURE_FLAG_BYBOTH)) {
			ast_set_flag(&(config.features_callee), AST_FEATURE_REDIRECT);
		}
		if ((parkedcalltransfers == AST_FEATURE_FLAG_BYCALLER) || (parkedcalltransfers == AST_FEATURE_FLAG_BYBOTH)) {
			ast_set_flag(&(config.features_caller), AST_FEATURE_REDIRECT);
		}
		if ((parkedcallreparking == AST_FEATURE_FLAG_BYCALLEE) || (parkedcallreparking == AST_FEATURE_FLAG_BYBOTH)) {
			ast_set_flag(&(config.features_callee), AST_FEATURE_PARKCALL);
		}
		if ((parkedcallreparking == AST_FEATURE_FLAG_BYCALLER) || (parkedcallreparking == AST_FEATURE_FLAG_BYBOTH)) {
			ast_set_flag(&(config.features_caller), AST_FEATURE_PARKCALL);
		}
		if ((parkedcallhangup == AST_FEATURE_FLAG_BYCALLEE) || (parkedcallhangup == AST_FEATURE_FLAG_BYBOTH)) {
			ast_set_flag(&(config.features_callee), AST_FEATURE_DISCONNECT);
		}
		if ((parkedcallhangup == AST_FEATURE_FLAG_BYCALLER) || (parkedcallhangup == AST_FEATURE_FLAG_BYBOTH)) {
			ast_set_flag(&(config.features_caller), AST_FEATURE_DISCONNECT);
		}
		if ((parkedcallrecording == AST_FEATURE_FLAG_BYCALLEE) || (parkedcallrecording == AST_FEATURE_FLAG_BYBOTH)) {
			ast_set_flag(&(config.features_callee), AST_FEATURE_AUTOMON);
		}
		if ((parkedcallrecording == AST_FEATURE_FLAG_BYCALLER) || (parkedcallrecording == AST_FEATURE_FLAG_BYBOTH)) {
			ast_set_flag(&(config.features_caller), AST_FEATURE_AUTOMON);
		}

		res = ast_bridge_call(chan, peer, &config);

		pbx_builtin_setvar_helper(chan, "PARKEDCHANNEL", peer->name);
		ast_cdr_setdestchan(chan->cdr, peer->name);

		/* Simulate the PBX hanging up */
		ast_hangup(peer);
		return -1;
	} else {
		/*! \todo XXX Play a message XXX */
		if (ast_stream_and_wait(chan, "pbx-invalidpark", ""))
			ast_log(LOG_WARNING, "ast_streamfile of %s failed on %s\n", "pbx-invalidpark", chan->name);
		ast_verb(3, "Channel %s tried to talk to nonexistent parked call %d\n", chan->name, park);
		res = -1;
	}

	return -1;
}

/*! 
 * \brief Add parking hints for all defined parking lots 
 * \param context
 * \param start starting parkinglot number
 * \param stop ending parkinglot number
*/
static void park_add_hints(char *context, int start, int stop)
{
	int numext;
	char device[AST_MAX_EXTENSION];
	char exten[10];

	for (numext = start; numext <= stop; numext++) {
		snprintf(exten, sizeof(exten), "%d", numext);
		snprintf(device, sizeof(device), "park:%s@%s", exten, context);
		ast_add_extension(context, 1, exten, PRIORITY_HINT, NULL, NULL, device, NULL, NULL, registrar);
	}
}

static int load_config(void) 
{
	int start = 0, end = 0;
	int res;
	int i;
	struct ast_context *con = NULL;
	struct ast_config *cfg = NULL;
	struct ast_variable *var = NULL;
	struct feature_group *fg = NULL;
	struct ast_flags config_flags = { 0 };
	char old_parking_ext[AST_MAX_EXTENSION];
	char old_parking_con[AST_MAX_EXTENSION] = "";
	char *ctg; 
	static const char *categories[] = { 
		/* Categories in features.conf that are not
		 * to be parsed as group categories
		 */
		"general",
		"featuremap",
		"applicationmap"
	};

	if (!ast_strlen_zero(parking_con)) {
		strcpy(old_parking_ext, parking_ext);
		strcpy(old_parking_con, parking_con);
	} 

	/* Reset to defaults */
	strcpy(parking_con, "parkedcalls");
	strcpy(parking_con_dial, "park-dial");
	strcpy(parking_ext, "700");
	strcpy(pickup_ext, "*8");
	strcpy(parkmohclass, "default");
	courtesytone[0] = '\0';
	strcpy(xfersound, "beep");
	strcpy(xferfailsound, "pbx-invalid");
	parking_start = 701;
	parking_stop = 750;
	parkfindnext = 0;
	adsipark = 0;
	comebacktoorigin = 1;
	parkaddhints = 0;
	parkedcalltransfers = 0;
	parkedcallreparking = 0;
	parkedcallrecording = 0;
	parkedcallhangup = 0;

	transferdigittimeout = DEFAULT_TRANSFER_DIGIT_TIMEOUT;
	featuredigittimeout = DEFAULT_FEATURE_DIGIT_TIMEOUT;
	atxfernoanswertimeout = DEFAULT_NOANSWER_TIMEOUT_ATTENDED_TRANSFER;
	atxferloopdelay = DEFAULT_ATXFER_LOOP_DELAY;
	atxferdropcall = DEFAULT_ATXFER_DROP_CALL;
	atxfercallbackretries = DEFAULT_ATXFER_CALLBACK_RETRIES;

	cfg = ast_config_load("features.conf", config_flags);
	if (!cfg) {
		ast_log(LOG_WARNING,"Could not load features.conf\n");
		return 0;
	}
	for (var = ast_variable_browse(cfg, "general"); var; var = var->next) {
		if (!strcasecmp(var->name, "parkext")) {
			ast_copy_string(parking_ext, var->value, sizeof(parking_ext));
		} else if (!strcasecmp(var->name, "context")) {
			ast_copy_string(parking_con, var->value, sizeof(parking_con));
		} else if (!strcasecmp(var->name, "parkingtime")) {
			if ((sscanf(var->value, "%30d", &parkingtime) != 1) || (parkingtime < 1)) {
				ast_log(LOG_WARNING, "%s is not a valid parkingtime\n", var->value);
				parkingtime = DEFAULT_PARK_TIME;
			} else
				parkingtime = parkingtime * 1000;
		} else if (!strcasecmp(var->name, "parkpos")) {
			if (sscanf(var->value, "%30d-%30d", &start, &end) != 2) {
				ast_log(LOG_WARNING, "Format for parking positions is a-b, where a and b are numbers at line %d of features.conf\n", var->lineno);
			} else {
				parking_start = start;
				parking_stop = end;
			}
		} else if (!strcasecmp(var->name, "findslot")) {
			parkfindnext = (!strcasecmp(var->value, "next"));
		} else if (!strcasecmp(var->name, "parkinghints")) {
			parkaddhints = ast_true(var->value);
		} else if (!strcasecmp(var->name, "parkedcalltransfers")) {
			if (!strcasecmp(var->value, "both"))
				parkedcalltransfers = AST_FEATURE_FLAG_BYBOTH;
			else if (!strcasecmp(var->value, "caller"))
				parkedcalltransfers = AST_FEATURE_FLAG_BYCALLER;
			else if (!strcasecmp(var->value, "callee"))
				parkedcalltransfers = AST_FEATURE_FLAG_BYCALLEE;
		} else if (!strcasecmp(var->name, "parkedcallreparking")) {
			if (!strcasecmp(var->value, "both"))
				parkedcalltransfers = AST_FEATURE_FLAG_BYBOTH;
			else if (!strcasecmp(var->value, "caller"))
				parkedcalltransfers = AST_FEATURE_FLAG_BYCALLER;
			else if (!strcasecmp(var->value, "callee"))
				parkedcalltransfers = AST_FEATURE_FLAG_BYCALLEE;
		} else if (!strcasecmp(var->name, "parkedcallhangup")) {
			if (!strcasecmp(var->value, "both"))
				parkedcallhangup = AST_FEATURE_FLAG_BYBOTH;
			else if (!strcasecmp(var->value, "caller"))
				parkedcallhangup = AST_FEATURE_FLAG_BYCALLER;
			else if (!strcasecmp(var->value, "callee"))
				parkedcallhangup = AST_FEATURE_FLAG_BYCALLEE;
		} else if (!strcasecmp(var->name, "parkedcallrecording")) {
			if (!strcasecmp(var->value, "both"))
				parkedcallrecording = AST_FEATURE_FLAG_BYBOTH;
			else if (!strcasecmp(var->value, "caller"))
				parkedcallrecording = AST_FEATURE_FLAG_BYCALLER;
			else if (!strcasecmp(var->value, "callee"))
				parkedcallrecording = AST_FEATURE_FLAG_BYCALLEE;
		} else if (!strcasecmp(var->name, "adsipark")) {
			adsipark = ast_true(var->value);
		} else if (!strcasecmp(var->name, "transferdigittimeout")) {
			if ((sscanf(var->value, "%30d", &transferdigittimeout) != 1) || (transferdigittimeout < 1)) {
				ast_log(LOG_WARNING, "%s is not a valid transferdigittimeout\n", var->value);
				transferdigittimeout = DEFAULT_TRANSFER_DIGIT_TIMEOUT;
			} else
				transferdigittimeout = transferdigittimeout * 1000;
		} else if (!strcasecmp(var->name, "featuredigittimeout")) {
			if ((sscanf(var->value, "%30d", &featuredigittimeout) != 1) || (featuredigittimeout < 1)) {
				ast_log(LOG_WARNING, "%s is not a valid featuredigittimeout\n", var->value);
				featuredigittimeout = DEFAULT_FEATURE_DIGIT_TIMEOUT;
			}
		} else if (!strcasecmp(var->name, "atxfernoanswertimeout")) {
			if ((sscanf(var->value, "%30d", &atxfernoanswertimeout) != 1) || (atxfernoanswertimeout < 1)) {
				ast_log(LOG_WARNING, "%s is not a valid atxfernoanswertimeout\n", var->value);
				atxfernoanswertimeout = DEFAULT_NOANSWER_TIMEOUT_ATTENDED_TRANSFER;
			} else
				atxfernoanswertimeout = atxfernoanswertimeout * 1000;
		} else if (!strcasecmp(var->name, "atxferloopdelay")) {
			if ((sscanf(var->value, "%30u", &atxferloopdelay) != 1)) {
				ast_log(LOG_WARNING, "%s is not a valid atxferloopdelay\n", var->value);
				atxferloopdelay = DEFAULT_ATXFER_LOOP_DELAY;
			} else 
				atxferloopdelay *= 1000;
		} else if (!strcasecmp(var->name, "atxferdropcall")) {
			atxferdropcall = ast_true(var->value);
		} else if (!strcasecmp(var->name, "atxfercallbackretries")) {
			if ((sscanf(var->value, "%30u", &atxferloopdelay) != 1)) {
				ast_log(LOG_WARNING, "%s is not a valid atxfercallbackretries\n", var->value);
				atxfercallbackretries = DEFAULT_ATXFER_CALLBACK_RETRIES;
			}
		} else if (!strcasecmp(var->name, "courtesytone")) {
			ast_copy_string(courtesytone, var->value, sizeof(courtesytone));
		}  else if (!strcasecmp(var->name, "parkedplay")) {
			if (!strcasecmp(var->value, "both"))
				parkedplay = 2;
			else if (!strcasecmp(var->value, "parked"))
				parkedplay = 1;
			else
				parkedplay = 0;
		} else if (!strcasecmp(var->name, "xfersound")) {
			ast_copy_string(xfersound, var->value, sizeof(xfersound));
		} else if (!strcasecmp(var->name, "xferfailsound")) {
			ast_copy_string(xferfailsound, var->value, sizeof(xferfailsound));
		} else if (!strcasecmp(var->name, "pickupexten")) {
			ast_copy_string(pickup_ext, var->value, sizeof(pickup_ext));
		} else if (!strcasecmp(var->name, "comebacktoorigin")) {
			comebacktoorigin = ast_true(var->value);
		} else if (!strcasecmp(var->name, "parkedmusicclass")) {
			ast_copy_string(parkmohclass, var->value, sizeof(parkmohclass));
		}
	}

	unmap_features();
	for (var = ast_variable_browse(cfg, "featuremap"); var; var = var->next) {
		if (remap_feature(var->name, var->value))
			ast_log(LOG_NOTICE, "Unknown feature '%s'\n", var->name);
	}

	/* Map a key combination to an application*/
	ast_unregister_features();
	for (var = ast_variable_browse(cfg, "applicationmap"); var; var = var->next) {
		char *tmp_val = ast_strdupa(var->value);
		char *activateon; 
		struct ast_call_feature *feature;
		AST_DECLARE_APP_ARGS(args,
			AST_APP_ARG(exten);
			AST_APP_ARG(activatedby);
			AST_APP_ARG(app);
			AST_APP_ARG(app_args);
			AST_APP_ARG(moh_class);
		);

		AST_STANDARD_APP_ARGS(args, tmp_val);
		if (strchr(args.app, '(')) {
			/* New syntax */
			args.moh_class = args.app_args;
			args.app_args = strchr(args.app, '(');
			*args.app_args++ = '\0';
			if (args.app_args[strlen(args.app_args) - 1] == ')') {
				args.app_args[strlen(args.app_args) - 1] = '\0';
			}
		}

		activateon = strsep(&args.activatedby, "/");	

		/*! \todo XXX var_name or app_args ? */
		if (ast_strlen_zero(args.app) || ast_strlen_zero(args.exten) || ast_strlen_zero(activateon) || ast_strlen_zero(var->name)) {
			ast_log(LOG_NOTICE, "Please check the feature Mapping Syntax, either extension, name, or app aren't provided %s %s %s %s\n",
				args.app, args.exten, activateon, var->name);
			continue;
		}

		AST_RWLIST_RDLOCK(&feature_list);
		if ((feature = find_dynamic_feature(var->name))) {
			AST_RWLIST_UNLOCK(&feature_list);
			ast_log(LOG_WARNING, "Dynamic Feature '%s' specified more than once!\n", var->name);
			continue;
		}
		AST_RWLIST_UNLOCK(&feature_list);
				
		if (!(feature = ast_calloc(1, sizeof(*feature)))) {
			continue;
		}

		ast_copy_string(feature->sname, var->name, FEATURE_SNAME_LEN);
		ast_copy_string(feature->app, args.app, FEATURE_APP_LEN);
		ast_copy_string(feature->exten, args.exten, FEATURE_EXTEN_LEN);
		
		if (args.app_args) {
			ast_copy_string(feature->app_args, args.app_args, FEATURE_APP_ARGS_LEN);
		}

		if (args.moh_class) {
			ast_copy_string(feature->moh_class, args.moh_class, FEATURE_MOH_LEN);
		}

		ast_copy_string(feature->exten, args.exten, sizeof(feature->exten));
		feature->operation = feature_exec_app;
		ast_set_flag(feature, AST_FEATURE_FLAG_NEEDSDTMF);

		/* Allow caller and calle to be specified for backwards compatability */
		if (!strcasecmp(activateon, "self") || !strcasecmp(activateon, "caller"))
			ast_set_flag(feature, AST_FEATURE_FLAG_ONSELF);
		else if (!strcasecmp(activateon, "peer") || !strcasecmp(activateon, "callee"))
			ast_set_flag(feature, AST_FEATURE_FLAG_ONPEER);
		else {
			ast_log(LOG_NOTICE, "Invalid 'ActivateOn' specification for feature '%s',"
				" must be 'self', or 'peer'\n", var->name);
			continue;
		}

		if (ast_strlen_zero(args.activatedby))
			ast_set_flag(feature, AST_FEATURE_FLAG_BYBOTH);
		else if (!strcasecmp(args.activatedby, "caller"))
			ast_set_flag(feature, AST_FEATURE_FLAG_BYCALLER);
		else if (!strcasecmp(args.activatedby, "callee"))
			ast_set_flag(feature, AST_FEATURE_FLAG_BYCALLEE);
		else if (!strcasecmp(args.activatedby, "both"))
			ast_set_flag(feature, AST_FEATURE_FLAG_BYBOTH);
		else {
			ast_log(LOG_NOTICE, "Invalid 'ActivatedBy' specification for feature '%s',"
				" must be 'caller', or 'callee', or 'both'\n", var->name);
			continue;
		}

		ast_register_feature(feature);

		ast_verb(2, "Mapping Feature '%s' to app '%s(%s)' with code '%s'\n", var->name, args.app, args.app_args, args.exten);
	}

	ast_unregister_groups();
	AST_RWLIST_WRLOCK(&feature_groups);

	ctg = NULL;
	while ((ctg = ast_category_browse(cfg, ctg))) {
		for (i = 0; i < ARRAY_LEN(categories); i++) {
			if (!strcasecmp(categories[i], ctg))
				break;
		}

		if (i < ARRAY_LEN(categories)) 
			continue;

		if (!(fg = register_group(ctg)))
			continue;

		for (var = ast_variable_browse(cfg, ctg); var; var = var->next) {
			struct ast_call_feature *feature;

			AST_RWLIST_RDLOCK(&feature_list);
			if (!(feature = find_dynamic_feature(var->name)) && 
			    !(feature = ast_find_call_feature(var->name))) {
				AST_RWLIST_UNLOCK(&feature_list);
				ast_log(LOG_WARNING, "Feature '%s' was not found.\n", var->name);
				continue;
			}
			AST_RWLIST_UNLOCK(&feature_list);

			register_group_feature(fg, var->value, feature);
		}
	}

	AST_RWLIST_UNLOCK(&feature_groups);

	ast_config_destroy(cfg);

	/* Remove the old parking extension */
	if (!ast_strlen_zero(old_parking_con) && (con = ast_context_find(old_parking_con)))	{
		if(ast_context_remove_extension2(con, old_parking_ext, 1, registrar, 0))
				notify_metermaids(old_parking_ext, old_parking_con, AST_DEVICE_NOT_INUSE);
		ast_debug(1, "Removed old parking extension %s@%s\n", old_parking_ext, old_parking_con);
	}
	
	if (!(con = ast_context_find_or_create(NULL, NULL, parking_con, registrar))) {
		ast_log(LOG_ERROR, "Parking context '%s' does not exist and unable to create\n", parking_con);
		return -1;
	}
	res = ast_add_extension2(con, 1, ast_parking_ext(), 1, NULL, NULL, parkcall, NULL, NULL, registrar);
	if (parkaddhints)
		park_add_hints(parking_con, parking_start, parking_stop);
	if (!res)
		notify_metermaids(ast_parking_ext(), parking_con, AST_DEVICE_INUSE);
	return res;

}

/*!
 * \brief CLI command to list configured features
 * \param e
 * \param cmd
 * \param a
 *
 * \retval CLI_SUCCESS on success.
 * \retval NULL when tab completion is used.
 */
static char *handle_feature_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int i;
	struct ast_call_feature *feature;
#define HFS_FORMAT "%-25s %-7s %-7s\n"

	switch (cmd) {
	
	case CLI_INIT:
		e->command = "features show";
		e->usage =
			"Usage: features show\n"
			"       Lists configured features\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, HFS_FORMAT, "Builtin Feature", "Default", "Current");
	ast_cli(a->fd, HFS_FORMAT, "---------------", "-------", "-------");

	ast_cli(a->fd, HFS_FORMAT, "Pickup", "*8", ast_pickup_ext());          /* default hardcoded above, so we'll hardcode it here */

	ast_rwlock_rdlock(&features_lock);
	for (i = 0; i < FEATURES_COUNT; i++)
		ast_cli(a->fd, HFS_FORMAT, builtin_features[i].fname, builtin_features[i].default_exten, builtin_features[i].exten);
	ast_rwlock_unlock(&features_lock);

	ast_cli(a->fd, "\n");
	ast_cli(a->fd, HFS_FORMAT, "Dynamic Feature", "Default", "Current");
	ast_cli(a->fd, HFS_FORMAT, "---------------", "-------", "-------");
	if (AST_RWLIST_EMPTY(&feature_list)) {
		ast_cli(a->fd, "(none)\n");
	} else {
		AST_RWLIST_RDLOCK(&feature_list);
		AST_RWLIST_TRAVERSE(&feature_list, feature, feature_entry) {
			ast_cli(a->fd, HFS_FORMAT, feature->sname, "no def", feature->exten);
		}
		AST_RWLIST_UNLOCK(&feature_list);
	}
	ast_cli(a->fd, "\nCall parking\n");
	ast_cli(a->fd, "------------\n");
	ast_cli(a->fd,"%-20s:      %s\n", "Parking extension", parking_ext);
	ast_cli(a->fd,"%-20s:      %s\n", "Parking context", parking_con);
	ast_cli(a->fd,"%-20s:      %d-%d\n", "Parked call extensions", parking_start, parking_stop);
	ast_cli(a->fd,"\n");

	return CLI_SUCCESS;
}

int ast_features_reload(void)
{
	load_config();

	return RESULT_SUCCESS;
}

static char *handle_features_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {	
	case CLI_INIT:
		e->command = "features reload";
		e->usage =
			"Usage: features reload\n"
			"       Reloads configured call features from features.conf\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	load_config();

	return CLI_SUCCESS;
}

static char mandescr_bridge[] =
"Description: Bridge together two channels already in the PBX\n"
"Variables: ( Headers marked with * are required )\n"
"   *Channel1: Channel to Bridge to Channel2\n"
"   *Channel2: Channel to Bridge to Channel1\n"
"        Tone: (Yes|No) Play courtesy tone to Channel 2\n"
"\n";

/*!
 * \brief Actual bridge
 * \param chan
 * \param tmpchan
 * 
 * Stop hold music, lock both channels, masq channels,
 * after bridge return channel to next priority.
*/
static void do_bridge_masquerade(struct ast_channel *chan, struct ast_channel *tmpchan)
{
	ast_moh_stop(chan);
	ast_channel_lock(chan);
	ast_setstate(tmpchan, chan->_state);
	tmpchan->readformat = chan->readformat;
	tmpchan->writeformat = chan->writeformat;
	ast_channel_masquerade(tmpchan, chan);
	ast_channel_lock(tmpchan);
	ast_do_masquerade(tmpchan);
	/* when returning from bridge, the channel will continue at the next priority */
	ast_explicit_goto(tmpchan, chan->context, chan->exten, chan->priority + 1);
	ast_channel_unlock(tmpchan);
	ast_channel_unlock(chan);
}

/*!
 * \brief Bridge channels together
 * \param s
 * \param m
 * 
 * Make sure valid channels were specified, 
 * send errors if any of the channels could not be found/locked, answer channels if needed,
 * create the placeholder channels and grab the other channels 
 * make the channels compatible, send error if we fail doing so 
 * setup the bridge thread object and start the bridge.
 * 
 * \retval 0 on success or on incorrect use.
 * \retval 1 on failure to bridge channels.
*/
static int action_bridge(struct mansession *s, const struct message *m)
{
	const char *channela = astman_get_header(m, "Channel1");
	const char *channelb = astman_get_header(m, "Channel2");
	const char *playtone = astman_get_header(m, "Tone");
	struct ast_channel *chana = NULL, *chanb = NULL;
	struct ast_channel *tmpchana = NULL, *tmpchanb = NULL;
	struct ast_bridge_thread_obj *tobj = NULL;

	/* make sure valid channels were specified */
	if (ast_strlen_zero(channela) || ast_strlen_zero(channelb)) {
		astman_send_error(s, m, "Missing channel parameter in request");
		return 0;
	}

	/* The same code must be executed for chana and chanb.  To avoid a
	 * theoretical deadlock, this code is separated so both chana and chanb will
	 * not hold locks at the same time. */

	/* Start with chana */
	chana = ast_get_channel_by_name_prefix_locked(channela, strlen(channela));

	/* send errors if any of the channels could not be found/locked */
	if (!chana) {
		char buf[256];
		snprintf(buf, sizeof(buf), "Channel1 does not exists: %s", channela);
		astman_send_error(s, m, buf);
		return 0;
	}

	/* Answer the channels if needed */
	if (chana->_state != AST_STATE_UP)
		ast_answer(chana);

	/* create the placeholder channels and grab the other channels */
	if (!(tmpchana = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, NULL, 
		NULL, NULL, 0, "Bridge/%s", chana->name))) {
		astman_send_error(s, m, "Unable to create temporary channel!");
		ast_channel_unlock(chana);
		return 1;
	}

	do_bridge_masquerade(chana, tmpchana);
	ast_channel_unlock(chana);
	chana = NULL;

	/* now do chanb */
	chanb = ast_get_channel_by_name_prefix_locked(channelb, strlen(channelb));
	/* send errors if any of the channels could not be found/locked */
	if (!chanb) {
		char buf[256];
		snprintf(buf, sizeof(buf), "Channel2 does not exists: %s", channelb);
		ast_hangup(tmpchana);
		astman_send_error(s, m, buf);
		return 0;
	}

	/* Answer the channels if needed */
	if (chanb->_state != AST_STATE_UP)
		ast_answer(chanb);

	/* create the placeholder channels and grab the other channels */
	if (!(tmpchanb = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, NULL, 
		NULL, NULL, 0, "Bridge/%s", chanb->name))) {
		astman_send_error(s, m, "Unable to create temporary channels!");
		ast_hangup(tmpchana);
		ast_channel_unlock(chanb);
		return 1;
	}
	do_bridge_masquerade(chanb, tmpchanb);
	ast_channel_unlock(chanb);
	chanb = NULL;

	/* make the channels compatible, send error if we fail doing so */
	if (ast_channel_make_compatible(tmpchana, tmpchanb)) {
		ast_log(LOG_WARNING, "Could not make channels %s and %s compatible for manager bridge\n", tmpchana->name, tmpchanb->name);
		astman_send_error(s, m, "Could not make channels compatible for manager bridge");
		ast_hangup(tmpchana);
		ast_hangup(tmpchanb);
		return 1;
	}

	/* setup the bridge thread object and start the bridge */
	if (!(tobj = ast_calloc(1, sizeof(*tobj)))) {
		ast_log(LOG_WARNING, "Unable to spawn a new bridge thread on %s and %s: %s\n", tmpchana->name, tmpchanb->name, strerror(errno));
		astman_send_error(s, m, "Unable to spawn a new bridge thread");
		ast_hangup(tmpchana);
		ast_hangup(tmpchanb);
		return 1;
	}

	tobj->chan = tmpchana;
	tobj->peer = tmpchanb;
	tobj->return_to_pbx = 1;

	if (ast_true(playtone)) {
		if (!ast_strlen_zero(xfersound) && !ast_streamfile(tmpchanb, xfersound, tmpchanb->language)) {
			if (ast_waitstream(tmpchanb, "") < 0)
				ast_log(LOG_WARNING, "Failed to play a courtesy tone on chan %s\n", tmpchanb->name);
		}
	}

	ast_bridge_call_thread_launch(tobj);

	astman_send_ack(s, m, "Launched bridge thread with success");

	return 0;
}

/*!
 * \brief CLI command to list parked calls
 * \param e 
 * \param cmd
 * \param a
 *  
 * Check right usage, lock parking lot, display parked calls, unlock parking lot list.
 * \retval CLI_SUCCESS on success.
 * \retval CLI_SHOWUSAGE on incorrect number of arguments.
 * \retval NULL when tab completion is used.
*/
static char *handle_parkedcalls(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct parkeduser *cur;
	int numparked = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "parkedcalls show";
		e->usage =
			"Usage: parkedcalls show\n"
			"       List currently parked calls\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc > e->args)
		return CLI_SHOWUSAGE;

	ast_cli(a->fd, "%4s %25s (%-15s %-12s %-4s) %-6s \n", "Num", "Channel"
		, "Context", "Extension", "Pri", "Timeout");

	AST_LIST_LOCK(&parkinglot);
	AST_LIST_TRAVERSE(&parkinglot, cur, list) {
		ast_cli(a->fd, "%-10.10s %25s (%-15s %-12s %-4d) %6lds\n"
			,cur->parkingexten, cur->chan->name, cur->context, cur->exten
			,cur->priority, (long) cur->start.tv_sec + (cur->parkingtime/1000) - time(NULL));

		numparked++;
	}
	AST_LIST_UNLOCK(&parkinglot);
	ast_cli(a->fd, "%d parked call%s.\n", numparked, ESS(numparked));


	return CLI_SUCCESS;
}

static char *handle_parkedcalls_deprecated(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char *res = handle_parkedcalls(e, cmd, a);
	if (cmd == CLI_INIT)
		e->command = "show parkedcalls";
	return res;
}

static struct ast_cli_entry cli_show_parkedcalls_deprecated = AST_CLI_DEFINE(handle_parkedcalls_deprecated, "List currently parked calls.");

static struct ast_cli_entry cli_features[] = {
	AST_CLI_DEFINE(handle_feature_show, "Lists configured features"),
	AST_CLI_DEFINE(handle_features_reload, "Reloads configured features"),
	AST_CLI_DEFINE(handle_parkedcalls, "List currently parked calls", .deprecate_cmd = &cli_show_parkedcalls_deprecated),
};

/*! 
 * \brief Dump parking lot status
 * \param s
 * \param m
 * 
 * Lock parking lot, iterate list and append parked calls status, unlock parking lot.
 * \return Always RESULT_SUCCESS 
*/
static int manager_parking_status(struct mansession *s, const struct message *m)
{
	struct parkeduser *cur;
	const char *id = astman_get_header(m, "ActionID");
	char idText[256] = "";

	if (!ast_strlen_zero(id))
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);

	astman_send_ack(s, m, "Parked calls will follow");

	AST_LIST_LOCK(&parkinglot);

	AST_LIST_TRAVERSE(&parkinglot, cur, list) {
		astman_append(s, "Event: ParkedCall\r\n"
			"Exten: %d\r\n"
			"Channel: %s\r\n"
			"From: %s\r\n"
			"Timeout: %ld\r\n"
			"CallerIDNum: %s\r\n"
			"CallerIDName: %s\r\n"
			"%s"
			"\r\n",
			cur->parkingnum, cur->chan->name, cur->peername,
			(long) cur->start.tv_sec + (long) (cur->parkingtime / 1000) - (long) time(NULL),
			S_OR(cur->chan->cid.cid_num, ""),	/* XXX in other places it is <unknown> */
			S_OR(cur->chan->cid.cid_name, ""),
			idText);
	}

	astman_append(s,
		"Event: ParkedCallsComplete\r\n"
		"%s"
		"\r\n",idText);

	AST_LIST_UNLOCK(&parkinglot);

	return RESULT_SUCCESS;
}

static char mandescr_park[] =
"Description: Park a channel.\n"
"Variables: (Names marked with * are required)\n"
"	*Channel: Channel name to park\n"
"	*Channel2: Channel to announce park info to (and return to if timeout)\n"
"	Timeout: Number of milliseconds to wait before callback.\n";  

/*!
 * \brief Create manager event for parked calls
 * \param s
 * \param m
 *
 * Get channels involved in park, create event.
 * \return Always 0
*/
static int manager_park(struct mansession *s, const struct message *m)
{
	const char *channel = astman_get_header(m, "Channel");
	const char *channel2 = astman_get_header(m, "Channel2");
	const char *timeout = astman_get_header(m, "Timeout");
	char buf[BUFSIZ];
	int to = 0;
	int res = 0;
	int parkExt = 0;
	struct ast_channel *ch1, *ch2;

	if (ast_strlen_zero(channel)) {
		astman_send_error(s, m, "Channel not specified");
		return 0;
	}

	if (ast_strlen_zero(channel2)) {
		astman_send_error(s, m, "Channel2 not specified");
		return 0;
	}

	ch1 = ast_get_channel_by_name_locked(channel);
	if (!ch1) {
		snprintf(buf, sizeof(buf), "Channel does not exist: %s", channel);
		astman_send_error(s, m, buf);
		return 0;
	}

	ch2 = ast_get_channel_by_name_locked(channel2);
	if (!ch2) {
		snprintf(buf, sizeof(buf), "Channel does not exist: %s", channel2);
		astman_send_error(s, m, buf);
		ast_channel_unlock(ch1);
		return 0;
	}

	if (!ast_strlen_zero(timeout)) {
		sscanf(timeout, "%30d", &to);
	}

	res = ast_masq_park_call(ch1, ch2, to, &parkExt);
	if (!res) {
		ast_softhangup(ch2, AST_SOFTHANGUP_EXPLICIT);
		astman_send_ack(s, m, "Park successful");
	} else {
		astman_send_error(s, m, "Park failure");
	}

	ast_channel_unlock(ch1);
	ast_channel_unlock(ch2);

	return 0;
}

/*!
 * \brief Pickup a call
 * \param chan channel that initiated pickup.
 *
 * Walk list of channels, checking it is not itself, channel is pbx one,
 * check that the callgroup for both channels are the same and the channel is ringing.
 * Answer calling channel, flag channel as answered on queue, masq channels together.
*/
int ast_pickup_call(struct ast_channel *chan)
{
	struct ast_channel *cur = NULL;
	int res = -1;

	while ((cur = ast_channel_walk_locked(cur)) != NULL) {
		if (!cur->pbx && 
			(cur != chan) &&
			(chan->pickupgroup & cur->callgroup) &&
			((cur->_state == AST_STATE_RINGING) ||
			 (cur->_state == AST_STATE_RING))) {
			 	break;
		}
		ast_channel_unlock(cur);
	}
	if (cur) {
		ast_debug(1, "Call pickup on chan '%s' by '%s'\n",cur->name, chan->name);
		res = ast_answer(chan);
		if (res)
			ast_log(LOG_WARNING, "Unable to answer '%s'\n", chan->name);
		res = ast_queue_control(chan, AST_CONTROL_ANSWER);
		if (res)
			ast_log(LOG_WARNING, "Unable to queue answer on '%s'\n", chan->name);
		res = ast_channel_masquerade(cur, chan);
		if (res)
			ast_log(LOG_WARNING, "Unable to masquerade '%s' into '%s'\n", chan->name, cur->name);		/* Done */
		ast_channel_unlock(cur);
	} else	{
		ast_debug(1, "No call pickup possible...\n");
	}
	return res;
}

static char *app_bridge = "Bridge";
static char *bridge_synopsis = "Bridge two channels";
static char *bridge_descrip =
"Usage: Bridge(channel[,options])\n"
"	Allows the ability to bridge two channels via the dialplan.\n"
"The current channel is bridged to the specified 'channel'.\n"
"  Options:\n"
"    p - Play a courtesy tone to 'channel'.\n"
"This application sets the following channel variable upon completion:\n"
" BRIDGERESULT    The result of the bridge attempt as a text string, one of\n"
"           SUCCESS | FAILURE | LOOP | NONEXISTENT | INCOMPATIBLE\n";

enum {
	BRIDGE_OPT_PLAYTONE = (1 << 0),
};

AST_APP_OPTIONS(bridge_exec_options, BEGIN_OPTIONS
	AST_APP_OPTION('p', BRIDGE_OPT_PLAYTONE)
END_OPTIONS );

/*!
 * \brief Bridge channels
 * \param chan
 * \param data channel to bridge with.
 * 
 * Split data, check we aren't bridging with ourself, check valid channel,
 * answer call if not already, check compatible channels, setup bridge config
 * now bridge call, if transfered party hangs up return to PBX extension.
*/
static int bridge_exec(struct ast_channel *chan, void *data)
{
	struct ast_channel *current_dest_chan, *final_dest_chan;
	char *tmp_data  = NULL;
	struct ast_flags opts = { 0, };
	struct ast_bridge_config bconfig = { { 0, }, };

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(dest_chan);
		AST_APP_ARG(options);
	);
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Bridge require at least 1 argument specifying the other end of the bridge\n");
		return -1;
	}

	tmp_data = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, tmp_data);
	if (!ast_strlen_zero(args.options))
		ast_app_parse_options(bridge_exec_options, &opts, NULL, args.options);

	/* avoid bridge with ourselves */
	if (!strcmp(chan->name, args.dest_chan)) {
		ast_log(LOG_WARNING, "Unable to bridge channel %s with itself\n", chan->name);
		manager_event(EVENT_FLAG_CALL, "BridgeExec",
					"Response: Failed\r\n"
					"Reason: Unable to bridge channel to itself\r\n"
					"Channel1: %s\r\n"
					"Channel2: %s\r\n",
					chan->name, args.dest_chan);
		pbx_builtin_setvar_helper(chan, "BRIDGERESULT", "LOOP");
		return 0;
	}

	/* make sure we have a valid end point */
	if (!(current_dest_chan = ast_get_channel_by_name_prefix_locked(args.dest_chan, 
		strlen(args.dest_chan)))) {
		ast_log(LOG_WARNING, "Bridge failed because channel %s does not exists or we "
			"cannot get its lock\n", args.dest_chan);
		manager_event(EVENT_FLAG_CALL, "BridgeExec",
					"Response: Failed\r\n"
					"Reason: Cannot grab end point\r\n"
					"Channel1: %s\r\n"
					"Channel2: %s\r\n", chan->name, args.dest_chan);
		pbx_builtin_setvar_helper(chan, "BRIDGERESULT", "NONEXISTENT");
		return 0;
	}

	/* answer the channel if needed */
	if (current_dest_chan->_state != AST_STATE_UP)
		ast_answer(current_dest_chan);

	/* try to allocate a place holder where current_dest_chan will be placed */
	if (!(final_dest_chan = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, NULL, 
		NULL, NULL, 0, "Bridge/%s", current_dest_chan->name))) {
		ast_log(LOG_WARNING, "Cannot create placeholder channel for chan %s\n", args.dest_chan);
		manager_event(EVENT_FLAG_CALL, "BridgeExec",
					"Response: Failed\r\n"
					"Reason: cannot create placeholder\r\n"
					"Channel1: %s\r\n"
					"Channel2: %s\r\n", chan->name, args.dest_chan);
	}
	do_bridge_masquerade(current_dest_chan, final_dest_chan);

	ast_channel_unlock(current_dest_chan);

	/* now current_dest_chan is a ZOMBIE and with softhangup set to 1 and final_dest_chan is our end point */
	/* try to make compatible, send error if we fail */
	if (ast_channel_make_compatible(chan, final_dest_chan) < 0) {
		ast_log(LOG_WARNING, "Could not make channels %s and %s compatible for bridge\n", chan->name, final_dest_chan->name);
		manager_event(EVENT_FLAG_CALL, "BridgeExec",
					"Response: Failed\r\n"
					"Reason: Could not make channels compatible for bridge\r\n"
					"Channel1: %s\r\n"
					"Channel2: %s\r\n", chan->name, final_dest_chan->name);
		ast_hangup(final_dest_chan); /* may be we should return this channel to the PBX? */
		pbx_builtin_setvar_helper(chan, "BRIDGERESULT", "INCOMPATIBLE");
		return 0;
	}

	/* Report that the bridge will be successfull */
	manager_event(EVENT_FLAG_CALL, "BridgeExec",
				"Response: Success\r\n"
				"Channel1: %s\r\n"
				"Channel2: %s\r\n", chan->name, final_dest_chan->name);

	/* we have 2 valid channels to bridge, now it is just a matter of setting up the bridge config and starting the bridge */	
	if (ast_test_flag(&opts, BRIDGE_OPT_PLAYTONE) && !ast_strlen_zero(xfersound)) {
		if (!ast_streamfile(final_dest_chan, xfersound, final_dest_chan->language)) {
			if (ast_waitstream(final_dest_chan, "") < 0)
				ast_log(LOG_WARNING, "Failed to play courtesy tone on %s\n", final_dest_chan->name);
		}
	}
	
	/* do the bridge */
	ast_bridge_call(chan, final_dest_chan, &bconfig);

	/* the bridge has ended, set BRIDGERESULT to SUCCESS. If the other channel has not been hung up, return it to the PBX */
	pbx_builtin_setvar_helper(chan, "BRIDGERESULT", "SUCCESS");
	if (!ast_check_hangup(final_dest_chan)) {
		ast_debug(1, "starting new PBX in %s,%s,%d for chan %s\n", 
			final_dest_chan->context, final_dest_chan->exten, 
			final_dest_chan->priority, final_dest_chan->name);

		if (ast_pbx_start(final_dest_chan) != AST_PBX_SUCCESS) {
			ast_log(LOG_WARNING, "FAILED continuing PBX on dest chan %s\n", final_dest_chan->name);
			ast_hangup(final_dest_chan);
		} else
			ast_debug(1, "SUCCESS continuing PBX on chan %s\n", final_dest_chan->name);
	} else {
		ast_debug(1, "hangup chan %s since the other endpoint has hung up\n", final_dest_chan->name);
		ast_hangup(final_dest_chan);
	}

	return 0;
}

int ast_features_init(void)
{
	int res;

	ast_register_application2(app_bridge, bridge_exec, bridge_synopsis, bridge_descrip, NULL);

	memset(parking_ext, 0, sizeof(parking_ext));
	memset(parking_con, 0, sizeof(parking_con));

	if ((res = load_config()))
		return res;
	ast_cli_register_multiple(cli_features, sizeof(cli_features) / sizeof(struct ast_cli_entry));
	ast_pthread_create(&parking_thread, NULL, do_parking_thread, NULL);
	res = ast_register_application2(parkedcall, park_exec, synopsis, descrip, NULL);
	if (!res)
		res = ast_register_application2(parkcall, park_call_exec, synopsis2, descrip2, NULL);
	if (!res) {
		ast_manager_register("ParkedCalls", 0, manager_parking_status, "List parked calls");
		ast_manager_register2("Park", EVENT_FLAG_CALL, manager_park,
			"Park a channel", mandescr_park); 
		ast_manager_register2("Bridge", EVENT_FLAG_CALL, action_bridge, "Bridge two channels already in the PBX", mandescr_bridge);
	}

	res |= ast_devstate_prov_add("Park", metermaidstate);

	return res;
}
