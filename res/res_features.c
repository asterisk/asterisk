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
 * \brief Routines implementing call features as call pickup, parking and transfer
 *
 * \author Mark Spencer <markster@digium.com> 
 */

/*** MODULEINFO
        <depend>chan_local</depend>
        <depend>res_adsi</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <netinet/in.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
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
#include "asterisk/global_datastores.h"

/*
 * Party A - transferee
 * Party B - transferer
 * Party C - target of transfer
 *
 * DTMF attended transfer works within the channel bridge.
 * Unfortunately, when either party A or B in the channel bridge
 * hangs up, that channel is not completely hung up until the
 * transfer completes.  This is a real problem depending upon
 * the channel technology involved.
 *
 * For chan_dahdi, the channel is crippled until the hangup is
 * complete.  Either the channel is not useable (analog) or the
 * protocol disconnect messages are held up (PRI) and
 * the media is not released.
 *
 * For chan_sip, a call limit of one is going to block that
 * endpoint from any further calls until the hangup is complete.
 *
 * For party A this is a minor problem.  The party A channel
 * will only be in this condition while party B is dialing and
 * when party B and C are conferring.  The conversation between
 * party B and C is expected to be a short one.  Party B is
 * either asking a question of party C or announcing party A.
 * Also party A does not have much incentive to hangup at this
 * point.
 *
 * For party B this can be a major problem during a blonde
 * transfer.  (A blonde transfer is our term for an attended
 * transfer that is converted into a blind transfer. :))  Party
 * B could be the operator.  When party B hangs up, he assumes
 * that he is out of the original call entirely.  The party B
 * channel will be in this condition while party C is ringing.
 *
 * WARNING:
 * The ATXFER_NULL_TECH conditional is a hack to fix the
 * problem.  It will replace the party B channel technology with
 * a NULL channel driver.  The consequences of this code is that
 * the 'h' extension will not be able to access any channel
 * technology specific information like SIP statistics for the
 * call.
 *
 * Uncomment the ATXFER_NULL_TECH define below to replace the
 * party B channel technology in the channel bridge to complete
 * hanging up the channel technology.
 */
//#define ATXFER_NULL_TECH	1

#define DEFAULT_PARK_TIME 45000
#define DEFAULT_TRANSFER_DIGIT_TIMEOUT 3000
#define DEFAULT_FEATURE_DIGIT_TIMEOUT 1000
#define DEFAULT_NOANSWER_TIMEOUT_ATTENDED_TRANSFER 15000

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

static char *parkedcall = "ParkedCall";

static int parkaddhints = 0;                               /*!< Add parking hints automatically */
static int parkingtime = DEFAULT_PARK_TIME;                /*!< No more than 45 seconds parked before you do something with them */
static char parking_con[AST_MAX_EXTENSION];                /*!< Context for which parking is made accessible */
static char parking_con_dial[AST_MAX_EXTENSION];           /*!< Context for dialback for parking (KLUDGE) */
static char parking_ext[AST_MAX_EXTENSION];                /*!< Extension you type to park the call */
static char pickup_ext[AST_MAX_EXTENSION];                 /*!< Call pickup extension */
static char parkmohclass[MAX_MUSICCLASS];                  /*!< Music class used for parking */
static int parking_start;                                  /*!< First available extension for parking */
static int parking_stop;                                   /*!< Last available extension for parking */

static int parkedcalltransfers;                            /*!< Who can REDIRECT after picking up a parked a call */
static int parkedcallreparking;                            /*!< Who can PARKCALL after picking up a parked call */
static int parkedcallhangup;                               /*!< Who can DISCONNECT after picking up a parked call */
static int parkedcallrecording;                            /*!< Who can AUTOMON after picking up a parked call */

static char courtesytone[256];                             /*!< Courtesy tone */
static int parkedplay = 0;                                 /*!< Who to play the courtesy tone to */
static char xfersound[256];                                /*!< Call transfer sound */
static char xferfailsound[256];                            /*!< Call transfer failure sound */

static int parking_offset;
static int parkfindnext;

static int adsipark;

static int transferdigittimeout;
static int featuredigittimeout;

static int atxfernoanswertimeout;

static char *registrar = "res_features";		   /*!< Registrar for operations */

/* module and CLI command definitions */
static char *synopsis = "Answer a parked call";

static char *descrip = "ParkedCall(exten):"
"Used to connect to a parked call.  This application is always\n"
"registered internally and does not need to be explicitly added\n"
"into the dialplan, although you should include the 'parkedcalls'\n"
"context.\n";

static char *parkcall = PARK_APP_NAME;

static char *synopsis2 = "Park yourself";

static char *descrip2 = "Park():"
"Used to park yourself (typically in combination with a supervised\n"
"transfer to know the parking space). This application is always\n"
"registered internally and does not need to be explicitly added\n"
"into the dialplan, although you should include the 'parkedcalls'\n"
"context (or the context specified in features.conf).\n\n"
"If you set the PARKINGEXTEN variable to an extension in your\n"
"parking context, park() will park the call on that extension, unless\n"
"it already exists. In that case, execution will continue at next\n"
"priority.\n" ;

static struct ast_app *monitor_app = NULL;
static int monitor_ok = 1;

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
	struct parkeduser *next;
};

static struct parkeduser *parkinglot;

AST_MUTEX_DEFINE_STATIC(parking_lock);	/*!< protects all static variables above */

static pthread_t parking_thread;

struct ast_dial_features {
	struct ast_flags features_caller;
	struct ast_flags features_callee;
	int is_caller;
};

#if defined(ATXFER_NULL_TECH)
static struct ast_frame *null_read(struct ast_channel *chan)
{
	/* Hangup channel. */
	return NULL;
}

static struct ast_frame *null_exception(struct ast_channel *chan)
{
	/* Hangup channel. */
	return NULL;
}

static int null_write(struct ast_channel *chan, struct ast_frame *frame)
{
	/* Hangup channel. */
	return -1;
}

static int null_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	/* No problem fixing up the channel. */
	return 0;
}

static int null_hangup(struct ast_channel *chan)
{
	chan->tech_pvt = NULL;
	return 0;
}

static const struct ast_channel_tech null_tech = {
	.type = "NULL",
	.description = "NULL channel driver for atxfer",
	.capabilities = -1,
	.read = null_read,
	.exception = null_exception,
	.write = null_write,
	.fixup = null_fixup,
	.hangup = null_hangup,
};
#endif	/* defined(ATXFER_NULL_TECH) */

#if defined(ATXFER_NULL_TECH)
/*!
 * \internal
 * \brief Set the channel technology to the NULL technology.
 *
 * \param chan Channel to change technology.
 *
 * \return Nothing
 */
static void set_null_chan_tech(struct ast_channel *chan)
{
	int idx;

	ast_channel_lock(chan);

	/* Hangup the channel's physical side */
	if (chan->tech->hangup) {
		chan->tech->hangup(chan);
	}
	if (chan->tech_pvt) {
		ast_log(LOG_WARNING, "Channel '%s' may not have been hung up properly\n",
			chan->name);
		ast_free(chan->tech_pvt);
		chan->tech_pvt = NULL;
	}

	/* Install the NULL technology and wake up anyone waiting on it. */
	chan->tech = &null_tech;
	for (idx = 0; idx < AST_MAX_FDS; ++idx) {
		switch (idx) {
		case AST_ALERT_FD:
		case AST_TIMING_FD:
		case AST_GENERATOR_FD:
			/* Don't clear these fd's. */
			break;
		default:
			chan->fds[idx] = -1;
			break;
		}
	}
	ast_queue_frame(chan, &ast_null_frame);

	ast_channel_unlock(chan);
}
#endif	/* defined(ATXFER_NULL_TECH) */

#if defined(ATXFER_NULL_TECH)
/*!
 * \internal
 * \brief Set the channel name to something unique.
 *
 * \param chan Channel to change name.
 *
 * \return Nothing
 */
static void set_new_chan_name(struct ast_channel *chan)
{
	char *orig_name;
	static int seq_num;

	ast_channel_lock(chan);

	orig_name = ast_strdupa(chan->name);
	ast_string_field_build(chan, name, "%s<XFER_%x>", orig_name,
		ast_atomic_fetchadd_int(&seq_num, +1));

	ast_channel_unlock(chan);
}
#endif	/* defined(ATXFER_NULL_TECH) */

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

char *ast_parking_ext(void)
{
	return parking_ext;
}

char *ast_pickup_ext(void)
{
	return pickup_ext;
}

struct ast_bridge_thread_obj 
{
	struct ast_bridge_config bconfig;
	struct ast_channel *chan;
	struct ast_channel *peer;
};

/*! \brief store context, priority and extension */
static void set_c_e_p(struct ast_channel *chan, const char *context, const char *ext, int pri)
{
	ast_copy_string(chan->context, context, sizeof(chan->context));
	ast_copy_string(chan->exten, ext, sizeof(chan->exten));
	chan->priority = pri;
}

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
	ast_channel_clear_softhangup(xferchan, AST_SOFTHANGUP_ALL);
	if ((f = ast_read(xferchan))) {
		ast_frfree(f);
		f = NULL;
		ast_pbx_start(xferchan);
	} else {
		ast_hangup(xferchan);
	}
}

static struct ast_channel *feature_request_and_dial(struct ast_channel *caller,
	struct ast_channel *transferee, const char *type,
	int format, void *data, int timeout, int *outstate, const char *cid_num,
	const char *cid_name, const char *language);

static void *ast_bridge_call_thread(void *data)
{
	struct ast_bridge_thread_obj *tobj = data;

	tobj->chan->appl = "Transferred Call";
	tobj->chan->data = tobj->peer->name;
	tobj->peer->appl = "Transferred Call";
	tobj->peer->data = tobj->chan->name;

	ast_bridge_call(tobj->peer, tobj->chan, &tobj->bconfig);
	ast_hangup(tobj->chan);
	ast_hangup(tobj->peer);
	bzero(tobj, sizeof(*tobj)); /*! \todo XXX for safety */
	free(tobj);
	return NULL;
}

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
static void notify_metermaids(char *exten, char *context)
{
	if (option_debug > 3)
		ast_log(LOG_DEBUG, "Notification of state change to metermaids %s@%s\n", exten, context);

	/* Send notification to devicestate subsystem */
	ast_device_state_changed("park:%s@%s", exten, context);
	return;
}

/*! \brief metermaids callback from devicestate.c */
static int metermaidstate(const char *data)
{
	int res = AST_DEVICE_INVALID;
	char *context = ast_strdupa(data);
	char *exten;

	exten = strsep(&context, "@");
	if (!context)
		return res;
	
	if (option_debug > 3)
		ast_log(LOG_DEBUG, "Checking state of exten %s in context %s\n", exten, context);

	res = ast_exists_extension(NULL, context, exten, 1, NULL);

	if (!res)
		return AST_DEVICE_NOT_INUSE;
	else
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
	ast_mutex_lock(&parking_lock);
	/* Check for channel variable PARKINGEXTEN */
	parkingexten = pbx_builtin_getvar_helper(chan, "PARKINGEXTEN");
	if (!ast_strlen_zero(parkingexten)) {
		/*!\note The API forces us to specify a numeric parking slot, even
		 * though the architecture would tend to support non-numeric extensions
		 * (as are possible with SIP, for example).  Hence, we enforce that
		 * limitation here.  If extout was not numeric, we could permit
		 * arbitrary non-numeric extensions.
		 */
		if (sscanf(parkingexten, "%30d", &parking_space) != 1 || parking_space < 0) {
			ast_log(LOG_WARNING, "PARKINGEXTEN does not indicate a valid parking slot: '%s'.\n", parkingexten);
			ast_mutex_unlock(&parking_lock);
			free(pu);
			return NULL;
		}
		snprintf(pu->parkingexten, sizeof(pu->parkingexten), "%d", parking_space);

		if (ast_exists_extension(NULL, parking_con, pu->parkingexten, 1, NULL)) {
			ast_mutex_unlock(&parking_lock);
			ast_log(LOG_WARNING, "Requested parking extension already exists: %s@%s\n", parkingexten, parking_con);
			free(pu);
			return NULL;
		}
	} else {
		/* Select parking space within range */
		parking_range = parking_stop - parking_start+1;
		for (i = 0; i < parking_range; i++) {
			parking_space = (i + parking_offset) % parking_range + parking_start;
			cur = parkinglot;
			while(cur) {
				if (cur->parkingnum == parking_space) 
					break;
				cur = cur->next;
			}
			if (!cur)
				break;
		}

		if (!(i < parking_range)) {
			ast_log(LOG_WARNING, "No more parking spaces\n");
			ast_mutex_unlock(&parking_lock);
			free(pu);
			return NULL;
		}
		/* Set pointer for next parking */
		if (parkfindnext) 
			parking_offset = parking_space - parking_start + 1;
		snprintf(pu->parkingexten, sizeof(pu->parkingexten), "%d", parking_space);
	}
	
	pu->notquiteyet = 1;
	pu->parkingnum = parking_space;
	pu->next = parkinglot;
	parkinglot = pu;
	ast_mutex_unlock(&parking_lock);

	return pu;
}

static int park_call_full(struct ast_channel *chan, struct ast_channel *peer, int timeout, int *extout, const char *orig_chan_name, struct parkeduser *pu)
{
	struct ast_context *con;
	int parkingnum_copy;
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
			if ((c = strrchr(other_side, ','))) {
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
	parkingnum_copy = pu->parkingnum;

	/* If parking a channel directly (peer == chan), don't quite yet get parking running on it.
     * All parking lot entires are put into the parking lot with notquiteyet on. */
	if (peer != chan) 
		pu->notquiteyet = 0;

	if (option_verbose > 1) 
		ast_verbose(VERBOSE_PREFIX_2 "Parked %s on %d@%s. Will timeout back to extension [%s] %s, %d in %d seconds\n", pu->chan->name, pu->parkingnum, parking_con, pu->context, pu->exten, pu->priority, (pu->parkingtime/1000));

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
		"CallerID: %s\r\n"
		"CallerIDName: %s\r\n",
		pu->parkingexten, pu->chan->name, event_from ? event_from : "",
		(long)pu->start.tv_sec + (long)(pu->parkingtime/1000) - (long)time(NULL),
		S_OR(pu->chan->cid.cid_num, "<unknown>"),
		S_OR(pu->chan->cid.cid_name, "<unknown>")
		);

	if (peer && adsipark && ast_adsi_available(peer)) {
		adsi_announce_park(peer, pu->parkingexten);	/* Only supports parking numbers */
		ast_adsi_unload_session(peer);
	}

	con = ast_context_find(parking_con);
	if (!con) 
		con = ast_context_create(NULL, parking_con, registrar);
	if (!con)	/* Still no context? Bad */
		ast_log(LOG_ERROR, "Parking context '%s' does not exist and unable to create\n", parking_con);
	if (con) {
		if (!ast_add_extension2(con, 1, pu->parkingexten, 1, NULL, NULL, parkedcall, strdup(pu->parkingexten), ast_free_ptr, registrar)) {
			notify_metermaids(pu->parkingexten, parking_con);
		}
	}

	/* Wake up the (presumably poll()ing) thread */
	pthread_kill(parking_thread, SIGURG);

	/* Only say number if it's a number and the channel hasn't been masqueraded away */
	if (peer && (ast_strlen_zero(orig_chan_name) || !strcasecmp(peer->name, orig_chan_name))) {
		/* Make sure we don't start saying digits to the channel being parked */
		ast_set_flag(peer, AST_FLAG_MASQ_NOSTREAM);
		/* Tell the peer channel the number of the parking space */
		ast_say_digits(peer, parkingnum_copy, "", peer->language);
		ast_clear_flag(peer, AST_FLAG_MASQ_NOSTREAM);
	}

	if (peer == chan) { /* pu->notquiteyet = 1 */
		/* Wake up parking thread if we're really done */
		ast_indicate_data(pu->chan, AST_CONTROL_HOLD, 
			S_OR(parkmohclass, NULL),
			!ast_strlen_zero(parkmohclass) ? strlen(parkmohclass) + 1 : 0);
		pu->notquiteyet = 0;
		pthread_kill(parking_thread, SIGURG);
	}
	return 0;
}

/*! \brief Park a call 
 	\note We put the user in the parking list, then wake up the parking thread to be sure it looks
	after these channels too */
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
			ast_stream_and_wait(peer, "beeperr", peer->language, "");
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
	if ((f = ast_read(chan))) {
		ast_frfree(f);
	}

	if (peer == rchan) {
		peer = chan;
	}

	if (peer && (!play_announcement || !orig_chan_name)) {
		/* chan is the channel being parked, peer is the effective park-er */
		orig_chan_name = ast_strdupa(peer->name);
	}

	park_status = park_call_full(chan, peer, timeout, extout, orig_chan_name, pu);
	if (park_status == 1) {
		/* would be nice to play: "invalid parking extension" */
		ast_hangup(chan);
		return -1;
	}

	return 0;
}

int ast_masq_park_call(struct ast_channel *rchan, struct ast_channel *peer, int timeout, int *extout)
{
	return masq_park_call(rchan, peer, timeout, extout, 0, NULL);
}

static int masq_park_call_announce(struct ast_channel *rchan, struct ast_channel *peer, int timeout, int *extout, const char *orig_chan_name)
{
	return masq_park_call(rchan, peer, timeout, extout, 1, orig_chan_name);
}

/*! \brief
 * set caller and callee according to the direction
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

/*! \brief support routing for one touch call parking */
static int builtin_parkcall(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, char *code, int sense, void *data)
{
	struct ast_channel *parker;
	struct ast_channel *parkee;
	int res = 0;
	struct ast_module_user *u;
	const char *orig_chan_name;

	u = ast_module_user_add(chan);

	set_peers(&parker, &parkee, peer, chan, sense);
	orig_chan_name = ast_strdupa(parker->name);
	/* we used to set chan's exten and priority to "s" and 1
	   here, but this generates (in some cases) an invalid
	   extension, and if "s" exists, could errantly
	   cause execution of extensions you don't expect It
	   makes more sense to let nature take its course
	   when chan finishes, and let the pbx do its thing
	   and hang up when the park is over.
	*/
	if (chan->_state != AST_STATE_UP)
		res = ast_answer(chan);
	if (!res)
		res = ast_safe_sleep(chan, 1000);

	if (!res) { /* one direction used to call park_call.... */
		res = masq_park_call_announce(parkee, parker, 0, NULL, orig_chan_name);
		/* PBX should hangup zombie channel if a masquerade actually occurred (res=0) */
	}

	ast_module_user_remove(u);
	return res;
}

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
		ast_autoservice_ignore(callee_chan, AST_FRAME_DTMF_END);
		if (ast_stream_and_wait(caller_chan, courtesytone, caller_chan->language, "")) {
			ast_log(LOG_WARNING, "Failed to play courtesy tone!\n");
			ast_autoservice_stop(callee_chan);
			return -1;
		}
		if (ast_autoservice_stop(callee_chan))
			return -1;
	}
	
	if (callee_chan->monitor) {
		if (option_verbose > 3)
			ast_verbose(VERBOSE_PREFIX_3 "User hit '%s' to stop recording call.\n", code);
		ast_monitor_stop(callee_chan, 1);
		return FEATURE_RETURN_SUCCESS;
	}

	if (caller_chan && callee_chan) {
		const char *touch_format = pbx_builtin_getvar_helper(caller_chan, "TOUCH_MONITOR_FORMAT");
		const char *touch_monitor = pbx_builtin_getvar_helper(caller_chan, "TOUCH_MONITOR");

		if (!touch_format)
			touch_format = pbx_builtin_getvar_helper(callee_chan, "TOUCH_MONITOR_FORMAT");

		if (!touch_monitor)
			touch_monitor = pbx_builtin_getvar_helper(callee_chan, "TOUCH_MONITOR");
	
		if (touch_monitor) {
			len = strlen(touch_monitor) + 50;
			args = alloca(len);
			touch_filename = alloca(len);
			snprintf(touch_filename, len, "auto-%ld-%s", (long)time(NULL), touch_monitor);
			snprintf(args, len, "%s|%s|m", (touch_format) ? touch_format : "wav", touch_filename);
		} else {
			caller_chan_id = ast_strdupa(S_OR(caller_chan->cid.cid_num, caller_chan->name));
			callee_chan_id = ast_strdupa(S_OR(callee_chan->cid.cid_num, callee_chan->name));
			len = strlen(caller_chan_id) + strlen(callee_chan_id) + 50;
			args = alloca(len);
			touch_filename = alloca(len);
			snprintf(touch_filename, len, "auto-%ld-%s-%s", (long)time(NULL), caller_chan_id, callee_chan_id);
			snprintf(args, len, "%s|%s|m", S_OR(touch_format, "wav"), touch_filename);
		}

		for( x = 0; x < strlen(args); x++) {
			if (args[x] == '/')
				args[x] = '-';
		}
		
		if (option_verbose > 3)
			ast_verbose(VERBOSE_PREFIX_3 "User hit '%s' to record call. filename: %s\n", code, args);

		pbx_exec(callee_chan, monitor_app, args);
		pbx_builtin_setvar_helper(callee_chan, "TOUCH_MONITOR_OUTPUT", touch_filename);
		pbx_builtin_setvar_helper(caller_chan, "TOUCH_MONITOR_OUTPUT", touch_filename);
	
		return FEATURE_RETURN_SUCCESS;
	}
	
	ast_log(LOG_NOTICE,"Cannot record the call. One or both channels have gone away.\n");	
	return -1;
}

static int builtin_disconnect(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, char *code, int sense, void *data)
{
	if (option_verbose > 3)
		ast_verbose(VERBOSE_PREFIX_3 "User hit '%s' to disconnect call.\n", code);
	return FEATURE_RETURN_HANGUP;
}

static int finishup(struct ast_channel *chan)
{
        ast_indicate(chan, AST_CONTROL_UNHOLD);
  
        return ast_autoservice_stop(chan);
}

/*! \brief Find the context for the transfer */
static const char *real_ctx(struct ast_channel *transferer, struct ast_channel *transferee)
{
        const char *s = pbx_builtin_getvar_helper(transferer, "TRANSFER_CONTEXT");
        if (ast_strlen_zero(s))
                s = pbx_builtin_getvar_helper(transferee, "TRANSFER_CONTEXT");
        if (ast_strlen_zero(s)) /* Use the non-macro context to transfer the call XXX ? */
                s = transferer->macrocontext;
        if (ast_strlen_zero(s))
                s = transferer->context;
        return s;  
}

static int builtin_blindtransfer(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, char *code, int sense, void *data)
{
	struct ast_channel *transferer;
	struct ast_channel *transferee;
	const char *transferer_real_context;
	char xferto[256];
	int res;
	const char *orig_chan_name;
	int parkstatus = 0;

	set_peers(&transferer, &transferee, peer, chan, sense);
	orig_chan_name = ast_strdupa(transferer->name);
	transferer_real_context = real_ctx(transferer, transferee);
	/* Start autoservice on chan while we talk to the originator */
	ast_autoservice_start(transferee);
	ast_autoservice_ignore(transferee, AST_FRAME_DTMF_END);
	ast_indicate(transferee, AST_CONTROL_HOLD);

	memset(xferto, 0, sizeof(xferto));

	/* Transfer */
	res = ast_stream_and_wait(transferer, "pbx-transfer", transferer->language, AST_DIGIT_ANY);
	if (res < 0) {
		finishup(transferee);
		return -1; /* error ? */
	}
	if (res > 0)	/* If they've typed a digit already, handle it */
		xferto[0] = (char) res;

	ast_stopstream(transferer);
	res = ast_app_dtget(transferer, transferer_real_context, xferto, sizeof(xferto), 100, transferdigittimeout);
	if (res < 0) {  /* hangup or error, (would be 0 for invalid and 1 for valid) */
		finishup(transferee);
		return -1;
	}
	if (res == 0) {
		if (xferto[0]) {
			ast_log(LOG_WARNING, "Extension '%s' does not exist in context '%s'\n",
				xferto, transferer_real_context);
		} else {
			/* Does anyone care about this case? */
			ast_log(LOG_WARNING, "No digits dialed.\n");
		}
		ast_stream_and_wait(transferer, "pbx-invalid", transferer->language, "");
		finishup(transferee);
		return FEATURE_RETURN_SUCCESS;
	}

	if (!strcmp(xferto, ast_parking_ext())) {
		res = finishup(transferee);
		if (res) {
		} else if (!(parkstatus = masq_park_call_announce(transferee, transferer, 0, NULL, orig_chan_name))) {	/* success */
			/* We return non-zero, but tell the PBX not to hang the channel when
			   the thread dies -- We have to be careful now though.  We are responsible for 
			   hanging up the channel, else it will never be hung up! */
			return 0;
		} else {
			ast_log(LOG_WARNING, "Unable to park call %s, parkstatus=%d\n", transferee->name, parkstatus);
		}
		ast_autoservice_start(transferee);
	} else {
		pbx_builtin_setvar_helper(transferer, "BLINDTRANSFER", transferee->name);
		pbx_builtin_setvar_helper(transferee, "BLINDTRANSFER", transferer->name);
		res=finishup(transferee);
		if (!transferer->cdr) { /* this code should never get called (in a perfect world) */
			transferer->cdr=ast_cdr_alloc();
			if (transferer->cdr) {
				ast_cdr_init(transferer->cdr, transferer); /* initilize our channel's cdr */
				ast_cdr_start(transferer->cdr);
			}
		}
		if (transferer->cdr) {
			struct ast_cdr *swap = transferer->cdr;
			/* swap cdrs-- it will save us some time & work */
			transferer->cdr = transferee->cdr;
			transferee->cdr = swap;
		}
		if (!transferee->pbx) {
			/* Doh!  Use our handy async_goto functions */
			if (option_verbose > 2) 
				ast_verbose(VERBOSE_PREFIX_3 "Transferring %s to '%s' (context %s) priority 1\n"
								,transferee->name, xferto, transferer_real_context);
			if (ast_async_goto(transferee, transferer_real_context, xferto, 1))
				ast_log(LOG_WARNING, "Async goto failed :-(\n");
			res = -1;
		} else {
			/* Set the channel's new extension, since it exists, using transferer context */
			ast_set_flag(transferee, AST_FLAG_BRIDGE_HANGUP_DONT); /* don't let the after-bridge code run the h-exten */
			set_c_e_p(transferee, transferer_real_context, xferto, 0);
		}
		check_goto_on_transfer(transferer);
		return res;
	}
	if (parkstatus != FEATURE_RETURN_PARKFAILED
		&& ast_stream_and_wait(transferer, xferfailsound, transferer->language, "")) {
		finishup(transferee);
		return -1;
	}
	ast_stopstream(transferer);
	res = finishup(transferee);
	if (res) {
		if (option_verbose > 1)
			ast_verbose(VERBOSE_PREFIX_2 "Hungup during autoservice stop on '%s'\n", transferee->name);
		return res;
	}
	return FEATURE_RETURN_SUCCESS;
}

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
	struct ast_channel *transferer;/* Party B */
	struct ast_channel *transferee;/* Party A */
	const char *transferer_real_context;
	char xferto[256] = "";
	int res;
	int outstate=0;
	struct ast_channel *newchan;
	struct ast_channel *xferchan;
	struct ast_bridge_thread_obj *tobj;
	struct ast_bridge_config bconfig;
	int l;
	struct ast_datastore *features_datastore;
	struct ast_dial_features *dialfeatures = NULL;

	if (option_debug)
		ast_log(LOG_DEBUG, "Executing Attended Transfer %s, %s (sense=%d) \n", chan->name, peer->name, sense);
	set_peers(&transferer, &transferee, peer, chan, sense);
	transferer_real_context = real_ctx(transferer, transferee);

	/* Start autoservice on transferee while we talk to the transferer */
	ast_autoservice_start(transferee);
	ast_indicate(transferee, AST_CONTROL_HOLD);

	/* Transfer */
	res = ast_stream_and_wait(transferer, "pbx-transfer", transferer->language, AST_DIGIT_ANY);
	if (res < 0) {
		finishup(transferee);
		return -1;
	}
	if (res > 0) /* If they've typed a digit already, handle it */
		xferto[0] = (char) res;

	/* this is specific of atxfer */
	res = ast_app_dtget(transferer, transferer_real_context, xferto, sizeof(xferto), 100, transferdigittimeout);
	if (res < 0) {  /* hangup or error, (would be 0 for invalid and 1 for valid) */
		finishup(transferee);
		return -1;
	}
	l = strlen(xferto);
	if (res == 0) {
		if (l) {
			ast_log(LOG_WARNING, "Extension '%s' does not exist in context '%s'\n",
				xferto, transferer_real_context);
		} else {
			/* Does anyone care about this case? */
			ast_log(LOG_WARNING, "No digits dialed for atxfer.\n");
		}
		ast_stream_and_wait(transferer, "pbx-invalid", transferer->language, "");
		finishup(transferee);
		return FEATURE_RETURN_SUCCESS;
	}

	/* If we are attended transfering to parking, just use builtin_parkcall instead of trying to track all of
	 * the different variables for handling this properly with a builtin_atxfer */
	if (!strcmp(xferto, ast_parking_ext())) {
		finishup(transferee);
		return builtin_parkcall(chan, peer, config, code, sense, data);
	}

	/* Append context to dialed transfer number. */
	snprintf(xferto + l, sizeof(xferto) - l, "@%s/n", transferer_real_context);

	/* Stop autoservice so we can monitor all parties involved in the transfer. */
	if (ast_autoservice_stop(transferee) < 0) {
		ast_indicate(transferee, AST_CONTROL_UNHOLD);
		return -1;
	}

	/* Dial party C */
	newchan = feature_request_and_dial(transferer, transferee, "Local",
		ast_best_codec(transferer->nativeformats), xferto, atxfernoanswertimeout,
		&outstate, transferer->cid.cid_num, transferer->cid.cid_name,
		transferer->language);
	if (option_debug) {
		ast_log(LOG_DEBUG, "Dial party C result: newchan:%d, outstate:%d\n", !!newchan, outstate);
	}

	if (!ast_check_hangup(transferer)) {
		int hangup_dont = 0;

		/* Transferer (party B) is up */
		if (option_debug) {
			ast_log(LOG_DEBUG, "Actually doing an attended transfer.\n");
		}

		/* Start autoservice on transferee while the transferer deals with party C. */
		ast_autoservice_start(transferee);

		ast_indicate(transferer, -1);
		if (!newchan) {
			/* any reason besides user requested cancel and busy triggers the failed sound */
			switch (outstate) {
			case AST_CONTROL_UNHOLD:/* Caller requested cancel or party C answer timeout. */
			case AST_CONTROL_BUSY:
			case AST_CONTROL_CONGESTION:
				if (ast_stream_and_wait(transferer, xfersound, transferer->language, "")) {
					ast_log(LOG_WARNING, "Failed to play transfer sound!\n");
				}
				break;
			default:
				if (ast_stream_and_wait(transferer, xferfailsound, transferer->language, "")) {
					ast_log(LOG_WARNING, "Failed to play transfer failed sound!\n");
				}
				break;
			}
			finishup(transferee);
			return FEATURE_RETURN_SUCCESS;
		}

		if (check_compat(transferer, newchan)) {
			if (ast_stream_and_wait(transferer, xferfailsound, transferer->language, "")) {
				ast_log(LOG_WARNING, "Failed to play transfer failed sound!\n");
			}
			/* we do mean transferee here, NOT transferer */
			finishup(transferee);
			return FEATURE_RETURN_SUCCESS;
		}
		memset(&bconfig,0,sizeof(struct ast_bridge_config));
		ast_set_flag(&(bconfig.features_caller), AST_FEATURE_DISCONNECT);
		ast_set_flag(&(bconfig.features_callee), AST_FEATURE_DISCONNECT);

		/* ast_bridge_call clears AST_FLAG_BRIDGE_HANGUP_DONT, but we don't
		   want that to happen here because we're also in another bridge already
		 */
		if (ast_test_flag(chan, AST_FLAG_BRIDGE_HANGUP_DONT)) {
			hangup_dont = 1;
		}
		/* Let party B and party C talk as long as they want. */
		ast_bridge_call(transferer, newchan, &bconfig);
		if (hangup_dont) {
			ast_set_flag(chan, AST_FLAG_BRIDGE_HANGUP_DONT);
		}

		if (ast_check_hangup(newchan) || !ast_check_hangup(transferer)) {
			ast_hangup(newchan);
			if (ast_stream_and_wait(transferer, xfersound, transferer->language, "")) {
				ast_log(LOG_WARNING, "Failed to play transfer sound!\n");
			}
			finishup(transferee);
			return FEATURE_RETURN_SUCCESS;
		}

		/* Transferer (party B) is confirmed hung up at this point. */
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
	} else if (!ast_check_hangup(transferee)) {
		/* Transferer (party B) has hung up at this point.  Doing blonde transfer. */
		if (option_debug) {
			ast_log(LOG_DEBUG, "Actually doing a blonde transfer.\n");
		}

		if (!newchan) {
			/* No party C. */
			return -1;
		}

		/* newchan is up, we should prepare transferee and bridge them */
		if (ast_check_hangup(newchan)) {
			ast_hangup(newchan);
			return -1;
		}
		if (check_compat(transferee, newchan)) {
			return -1;
		}
	} else {
		/*
		 * Both the transferer and transferee have hungup.  If newchan
		 * is up, hang it up as it has no one to talk to.
		 */
		if (option_debug) {
			ast_log(LOG_DEBUG, "Everyone is hungup.\n");
		}
		if (newchan) {
			ast_hangup(newchan);
		}
		return -1;
	}

	/* Initiate the channel transfer of party A to party C. */

	xferchan = ast_channel_alloc(0, AST_STATE_DOWN, 0, 0, "", "", "", 0, "Transfered/%s", transferee->name);
	if (!xferchan) {
		ast_hangup(newchan);
		return -1;
	}

	/* Give party A a momentary ringback tone during transfer. */
	xferchan->visible_indication = AST_CONTROL_RINGING;

	/* Make formats okay */
	xferchan->readformat = transferee->readformat;
	xferchan->writeformat = transferee->writeformat;

	ast_channel_masquerade(xferchan, transferee);
	ast_explicit_goto(xferchan, transferee->context, transferee->exten, transferee->priority);
	xferchan->_state = AST_STATE_UP;
	ast_clear_flag(xferchan, AST_FLAGS_ALL);

	/* Do the masquerade manually to make sure that is is completed. */
	ast_channel_lock(xferchan);
	if (xferchan->masq) {
		ast_do_masquerade(xferchan);
	}
	ast_channel_unlock(xferchan);

	newchan->_state = AST_STATE_UP;
	ast_clear_flag(newchan, AST_FLAGS_ALL);
	tobj = ast_calloc(1, sizeof(*tobj));
	if (!tobj) {
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

	if (ast_stream_and_wait(newchan, xfersound, newchan->language, ""))
		ast_log(LOG_WARNING, "Failed to play transfer sound!\n");
	ast_bridge_call_thread_launch(tobj);
	return -1;/* The transferee is masqueraded and the original bridged channels can be hungup. */
}


/* add atxfer and automon as undefined so you can only use em if you configure them */
#define FEATURES_COUNT (sizeof(builtin_features) / sizeof(builtin_features[0]))

AST_RWLOCK_DEFINE_STATIC(features_lock);

static struct ast_call_feature builtin_features[] = 
 {
	{ AST_FEATURE_REDIRECT, "Blind Transfer", "blindxfer", "#", "#", builtin_blindtransfer, AST_FEATURE_FLAG_NEEDSDTMF, "" },
	{ AST_FEATURE_REDIRECT, "Attended Transfer", "atxfer", "", "", builtin_atxfer, AST_FEATURE_FLAG_NEEDSDTMF, "" },
	{ AST_FEATURE_AUTOMON, "One Touch Monitor", "automon", "", "", builtin_automonitor, AST_FEATURE_FLAG_NEEDSDTMF, "" },
	{ AST_FEATURE_DISCONNECT, "Disconnect Call", "disconnect", "*", "*", builtin_disconnect, AST_FEATURE_FLAG_NEEDSDTMF, "" },
	{ AST_FEATURE_PARKCALL, "Park Call", "parkcall", "", "", builtin_parkcall, AST_FEATURE_FLAG_NEEDSDTMF, "" },
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
	AST_RWLIST_INSERT_HEAD(&feature_list, feature, feature_entry);
	AST_RWLIST_UNLOCK(&feature_list);

	if (option_verbose >= 2) {
		ast_verbose(VERBOSE_PREFIX_2 "Registered Feature '%s'\n",feature->sname);
	}
}

/*! \brief unregister feature from feature_list */
void ast_unregister_feature(struct ast_call_feature *feature)
{
	if (!feature)
		return;

	AST_RWLIST_WRLOCK(&feature_list);
	AST_RWLIST_REMOVE(&feature_list, feature, feature_entry);
	AST_RWLIST_UNLOCK(&feature_list);
	
	free(feature);
}

/*! \brief Remove all features in the list */
static void ast_unregister_features(void)
{
	struct ast_call_feature *feature;

	AST_RWLIST_WRLOCK(&feature_list);
	while ((feature = AST_LIST_REMOVE_HEAD(&feature_list, feature_entry))) {
		free(feature);
	}
	AST_RWLIST_UNLOCK(&feature_list);
}

/*! \brief find a feature by name */
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

/*! \brief exec an app by feature */
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
	ast_autoservice_ignore(idle, AST_FRAME_DTMF_END);
	
	if (!ast_strlen_zero(feature->moh_class))
		ast_moh_start(idle, feature->moh_class, NULL);

	res = pbx_exec(work, app, feature->app_args);

	if (!ast_strlen_zero(feature->moh_class))
		ast_moh_stop(idle);

	ast_autoservice_stop(idle);

	if (res)
		return FEATURE_RETURN_SUCCESSBREAK;
	
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
	struct ast_call_feature *tmpfeature;
	char *tmp, *tok;
	int res = FEATURE_RETURN_PASSDIGITS;
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
				if (option_debug > 2) {
					ast_log(LOG_DEBUG, "Feature detected: fname=%s sname=%s exten=%s\n", builtin_features[x].fname, builtin_features[x].sname, builtin_features[x].exten);
				}
				if (operation) {
					res = builtin_features[x].operation(chan, peer, config, code, sense, NULL);
				}
				memcpy(feature, &builtin_features[x], sizeof(feature));
				feature_detected = 1;
				break;
			} else if (!strncmp(builtin_features[x].exten, code, strlen(code))) {
				if (res == FEATURE_RETURN_PASSDIGITS)
					res = FEATURE_RETURN_STOREDIGITS;
			}
		}
	}
	ast_rwlock_unlock(&features_lock);

	if (ast_strlen_zero(dynamic_features_buf) || feature_detected) {
		return res;
	}

	tmp = dynamic_features_buf;

	while ((tok = strsep(&tmp, "#"))) {
		AST_RWLIST_RDLOCK(&feature_list);
		if (!(tmpfeature = find_dynamic_feature(tok))) {
			AST_RWLIST_UNLOCK(&feature_list);
			continue;
		}

		/* Feature is up for consideration */
		if (!strcmp(tmpfeature->exten, code)) {
			if (option_debug > 2) {
				ast_log(LOG_NOTICE, " Feature Found: %s exten: %s\n",tmpfeature->sname, tok);
			}
			if (operation) {
				res = tmpfeature->operation(chan, peer, config, code, sense, tmpfeature);
			}
			memcpy(feature, tmpfeature, sizeof(feature));
			if (res != FEATURE_RETURN_KEEPTRYING) {
				AST_RWLIST_UNLOCK(&feature_list);
				break;
			}
			res = FEATURE_RETURN_PASSDIGITS;
		} else if (!strncmp(tmpfeature->exten, code, strlen(code)))
			res = FEATURE_RETURN_STOREDIGITS;

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

static int feature_interpret(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, char *code, int sense) {

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

	if (option_debug > 2) {
		ast_log(LOG_DEBUG, "Feature interpret: chan=%s, peer=%s, code=%s, sense=%d, features=%d, dynamic=%s\n", chan->name, peer->name, code, sense, features.flags, dynamic_features_buf);
	}

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
 * \internal
 * \brief Get feature and dial.
 *
 * \param caller Channel to represent as the calling channel for the dialed channel.
 * \param transferee Channel that the dialed channel will be transferred to.
 * \param type Channel technology type to dial.
 * \param format Codec formats for dialed channel.
 * \param data Dialed channel extra parameters for ast_request() and ast_call().
 * \param timeout Time limit for dialed channel to answer in ms. Must be greater than zero.
 * \param outstate Status of dialed channel if unsuccessful.
 * \param cid_num CallerID number to give dialed channel.
 * \param cid_name CallerID name to give dialed channel.
 * \param language Language of the caller.
 *
 * \note
 * outstate can be:
 * 0, AST_CONTROL_BUSY, AST_CONTROL_CONGESTION,
 * AST_CONTROL_ANSWER, or AST_CONTROL_UNHOLD.  If
 * AST_CONTROL_UNHOLD then the caller channel cancelled the
 * transfer or the dialed channel did not answer before the
 * timeout.
 *
 * \details
 * Request channel, set channel variables, initiate call,
 * check if they want to disconnect, go into loop, check if timeout has elapsed,
 * check if person to be transfered hung up, check for answer break loop,
 * set cdr return channel.
 *
 * \retval Channel Connected channel for transfer.
 * \retval NULL on failure to get third party connected.
 *
 * \note This is similar to __ast_request_and_dial() in channel.c
 */
static struct ast_channel *feature_request_and_dial(struct ast_channel *caller,
	struct ast_channel *transferee, const char *type,
	int format, void *data, int timeout, int *outstate, const char *cid_num,
	const char *cid_name, const char *language)
{
	int state = 0;
	int cause = 0;
	int to;
	int caller_hungup;
	int transferee_hungup;
	struct ast_channel *chan;
	struct ast_channel *monitor_chans[3];
	struct ast_channel *active_channel;
	int ready = 0;
	struct timeval started;
	int x, len = 0;
	char *disconnect_code = NULL, *dialed_code = NULL;
	struct ast_frame *f;
	AST_LIST_HEAD_NOLOCK(, ast_frame) deferred_frames;

	caller_hungup = ast_check_hangup(caller);

	if (!(chan = ast_request(type, format, data, &cause))) {
		ast_log(LOG_NOTICE, "Unable to request channel %s/%s\n", type, (char *)data);
		switch (cause) {
		case AST_CAUSE_BUSY:
			state = AST_CONTROL_BUSY;
			break;
		case AST_CAUSE_CONGESTION:
			state = AST_CONTROL_CONGESTION;
			break;
		default:
			state = 0;
			break;
		}
		goto done;
	}

	ast_set_callerid(chan, cid_num, cid_name, cid_num);
	ast_string_field_set(chan, language, language);
	ast_channel_inherit_variables(caller, chan);
	pbx_builtin_setvar_helper(chan, "TRANSFERERNAME", caller->name);

	if (ast_call(chan, data, timeout)) {
		ast_log(LOG_NOTICE, "Unable to call channel %s/%s\n", type, (char *)data);
		switch (chan->hangupcause) {
		case AST_CAUSE_BUSY:
			state = AST_CONTROL_BUSY;
			break;
		case AST_CAUSE_CONGESTION:
			state = AST_CONTROL_CONGESTION;
			break;
		default:
			state = 0;
			break;
		}
		goto done;
	}

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
	AST_LIST_HEAD_INIT_NOLOCK(&deferred_frames);

	if (caller_hungup) {
		/* Convert to a blonde transfer */
		ast_indicate(transferee, AST_CONTROL_UNHOLD);
		ast_indicate(transferee, AST_CONTROL_RINGING);
	}

	transferee_hungup = 0;
	while (!ast_check_hangup(transferee) && (chan->_state != AST_STATE_UP)) {
		int num_chans = 0;

		monitor_chans[num_chans++] = transferee;
		monitor_chans[num_chans++] = chan;
		if (!caller_hungup) {
			if (ast_check_hangup(caller)) {
				caller_hungup = 1;

#if defined(ATXFER_NULL_TECH)
				/* Change caller's name to ensure that it will remain unique. */
				set_new_chan_name(caller);

				/*
				 * Get rid of caller's physical technology so it is free for
				 * other calls.
				 */
				set_null_chan_tech(caller);
#endif	/* defined(ATXFER_NULL_TECH) */

				/* Convert to a blonde transfer */
				ast_indicate(transferee, AST_CONTROL_UNHOLD);
				ast_indicate(transferee, AST_CONTROL_RINGING);
				started = ast_tvnow();
				to = timeout;
			} else {
				/* caller is not hungup so monitor it. */
				monitor_chans[num_chans++] = caller;
			}
		}

		/* see if the timeout has been violated */
		if (ast_tvdiff_ms(ast_tvnow(), started) > timeout) {
			state = AST_CONTROL_UNHOLD;
			ast_log(LOG_NOTICE, "We exceeded our AT-timeout for %s\n", chan->name);
			break; /*doh! timeout*/
		}

		active_channel = ast_waitfor_n(monitor_chans, num_chans, &to);
		if (!active_channel)
			continue;

		f = NULL;
		if (transferee == active_channel) {
			struct ast_frame *dup_f;

			f = ast_read(transferee);
			if (f == NULL) { /*doh! where'd he go?*/
				transferee_hungup = 1;
				state = 0;
				break;
			}
			if (ast_is_deferrable_frame(f)) {
				dup_f = ast_frisolate(f);
				if (dup_f) {
					if (dup_f == f) {
						f = NULL;
					}
					AST_LIST_INSERT_HEAD(&deferred_frames, dup_f, frame_list);
				}
			}
		} else if (chan == active_channel) {
			if (!ast_strlen_zero(chan->call_forward)) {
				state = 0;
				chan = ast_call_forward(caller, chan, NULL, format, NULL, &state);
				if (!chan) {
					break;
				}
				continue;
			}
			f = ast_read(chan);
			if (f == NULL) { /*doh! where'd he go?*/
				switch (chan->hangupcause) {
				case AST_CAUSE_BUSY:
					state = AST_CONTROL_BUSY;
					break;
				case AST_CAUSE_CONGESTION:
					state = AST_CONTROL_CONGESTION;
					break;
				default:
					state = 0;
					break;
				}
				break;
			}

			if (f->frametype == AST_FRAME_CONTROL) {
				if (f->subclass == AST_CONTROL_RINGING) {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "%s is ringing\n", chan->name);
					ast_indicate(caller, AST_CONTROL_RINGING);
				} else if (f->subclass == AST_CONTROL_BUSY) {
					state = f->subclass;
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "%s is busy\n", chan->name);
					ast_indicate(caller, AST_CONTROL_BUSY);
					ast_frfree(f);
					break;
				} else if (f->subclass == AST_CONTROL_CONGESTION) {
					state = f->subclass;
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "%s is congested\n", chan->name);
					ast_indicate(caller, AST_CONTROL_CONGESTION);
					ast_frfree(f);
					break;
				} else if (f->subclass == AST_CONTROL_ANSWER) {
					/* This is what we are hoping for */
					state = f->subclass;
					ast_frfree(f);
					ready=1;
					break;
				} else if (f->subclass != -1 && f->subclass != AST_CONTROL_PROGRESS) {
					ast_log(LOG_NOTICE, "Don't know what to do about control frame: %d\n", f->subclass);
				}
				/* else who cares */
			} else if (f->frametype == AST_FRAME_VOICE || f->frametype == AST_FRAME_VIDEO) {
				ast_write(caller, f);
			}
		} else if (caller == active_channel) {
			f = ast_read(caller);
			if (f) {
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
						break;
					}
				} else if (f->frametype == AST_FRAME_VOICE || f->frametype == AST_FRAME_VIDEO) {
					ast_write(chan, f);
				}
			}
		}
		if (f)
			ast_frfree(f);
	} /* end while */

	/*
	 * We need to free all the deferred frames, but we only need to
	 * queue the deferred frames if no hangup was received.
	 */
	ast_channel_lock(transferee);
	transferee_hungup = (transferee_hungup || ast_check_hangup(transferee));
	while ((f = AST_LIST_REMOVE_HEAD(&deferred_frames, frame_list))) {
		if (!transferee_hungup) {
			ast_queue_frame_head(transferee, f);
		}
		ast_frfree(f);
	}
	ast_channel_unlock(transferee);

done:
	ast_indicate(caller, -1);
	if (chan && (ready || chan->_state == AST_STATE_UP)) {
		state = AST_CONTROL_ANSWER;
	} else if (chan) {
		ast_hangup(chan);
		chan = NULL;
	}

	if (outstate)
		*outstate = state;

	return chan;
}

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
	if (ast_answer(chan))
		return -1;

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
			bridge_cdr->disposition = (chan->_state == AST_STATE_UP) ?  AST_CDR_ANSWERED : AST_CDR_NOANSWER;
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
		/* the DIALED flag may be set if a dialed channel is transfered
		 * and then bridged to another channel.  In order for the
		 * bridge CDR to be written, the DIALED flag must not be
		 * present. */
		ast_clear_flag(bridge_cdr, AST_CDR_FLAG_DIALED);
	}

	for (;;) {
		struct ast_channel *other;	/* used later */

		res = ast_channel_bridge(chan, peer, config, &f, &who);
		
		/* When frame is not set, we are probably involved in a situation
		   where we've timed out.
		   When frame is set, we'll come thru this code twice; once for DTMF_BEGIN
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
					if (option_debug)
						ast_log(LOG_DEBUG, "Timed out, realtime this time!\n");
					config->feature_timer = 0;
					who = chan;
					if (f)
						ast_frfree(f);
					f = NULL;
					res = 0;
				} else if (config->feature_timer <= 0) {
					/* Not *really* out of time, just out of time for
					   digits to come in for features. */
					if (option_debug)
						ast_log(LOG_DEBUG, "Timed out for feature!\n");
					if (!ast_strlen_zero(peer_featurecode)) {
						ast_dtmf_stream(chan, peer, peer_featurecode, 0);
						memset(peer_featurecode, 0, sizeof(peer_featurecode));
					}
					if (!ast_strlen_zero(chan_featurecode)) {
						ast_dtmf_stream(peer, chan, chan_featurecode, 0);
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
					f->subclass == AST_CONTROL_CONGESTION ) ) ) {
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
			res = feature_interpret(chan, peer, config, featurecode, sense);
			switch(res) {
			case FEATURE_RETURN_PASSDIGITS:
				ast_dtmf_stream(other, who, featurecode, 0);
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
				if (option_debug)
					ast_log(LOG_DEBUG, "Set time limit to %ld\n", config->feature_timer);
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
		int  save_prio, spawn_error = 0;
		
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
		ast_copy_string(chan->exten, "h", sizeof(chan->exten));
		save_prio = chan->priority;
		chan->priority = 1;
		ast_channel_unlock(chan);
		while(ast_exists_extension(chan, chan->context, chan->exten, chan->priority, chan->cid.cid_num)) {
			if ((spawn_error = ast_spawn_extension(chan, chan->context, chan->exten, chan->priority, chan->cid.cid_num))) {
				/* Something bad happened, or a hangup has been requested. */
				if (option_debug)
					ast_log(LOG_DEBUG, "Spawn h extension (%s,%s,%d) exited non-zero on '%s'\n", chan->context, chan->exten, chan->priority, chan->name);
				if (option_verbose > 1)
					ast_verbose( VERBOSE_PREFIX_2 "Spawn h extension (%s, %s, %d) exited non-zero on '%s'\n", chan->context, chan->exten, chan->priority, chan->name);
				break;
			}
			chan->priority++;
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
		if (chan->priority != 1 || !spawn_error) {
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
	if (bridge_cdr && new_chan_cdr && ast_test_flag(new_chan_cdr, AST_CDR_FLAG_POST_DISABLED)) {
		ast_set_flag(bridge_cdr, AST_CDR_FLAG_POST_DISABLED);
	}

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
			ast_cdr_specialized_reset(chan->cdr, 0); /* nothing changed, reset the chan cdr  */
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
			if (new_peer_cdr) {
				ast_cdr_specialized_reset(new_peer_cdr, 0);
			}
		} else {
			ast_cdr_specialized_reset(peer->cdr, 0); /* nothing changed, reset the peer cdr  */
		}
	}
	
	return res;
}

static void post_manager_event(const char *s, char *parkingexten, struct ast_channel *chan)
{
	manager_event(EVENT_FLAG_CALL, s,
		"Exten: %s\r\n"
		"Channel: %s\r\n"
		"CallerID: %s\r\n"
		"CallerIDName: %s\r\n\r\n",
		parkingexten, 
		chan->name,
		S_OR(chan->cid.cid_num, "<unknown>"),
		S_OR(chan->cid.cid_name, "<unknown>")
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

/*! \brief Take care of parked calls and unpark them if needed */
static void *do_parking_thread(void *ignore)
{
	/* results from previous poll, to be preserved across loops. */
	struct pollfd *fds = NULL;
	int nfds = 0;
	struct timeval tv;

	for (;;) {
		struct parkeduser *pu, *pl, *pt = NULL;
		int ms = -1;	/* poll2 timeout, uninitialized */
		struct pollfd *new_fds = NULL;
		int new_nfds = 0;

		ast_mutex_lock(&parking_lock);
		pl = NULL;
		pu = parkinglot;
		/* navigate the list with prev-cur pointers to support removals */
		while (pu) {
			struct ast_channel *chan = pu->chan;	/* shorthand */
			int tms;        /* timeout for this item */
			int x;          /* fd index in channel */
			struct ast_context *con;

			if (pu->notquiteyet) { /* Pretend this one isn't here yet */
				pl = pu;
				pu = pu->next;
				continue;
			}
			tms = ast_tvdiff_ms(ast_tvnow(), pu->start);
			if (tms > pu->parkingtime) {
				ast_indicate(chan, AST_CONTROL_UNHOLD);
				/* Get chan, exten from derived kludge */
				if (pu->peername[0]) {
					/* Don't use ast_strdupa() inside an infinite loop */
					char *dash, *peername = ast_strdup(pu->peername);
					if (!peername) {
						/* Skip for the time being. */
						pl = pu;
						pu = pu->next;
						continue;
					}
					if ((dash = strrchr(peername, '-'))) {
						*dash = '\0';
					}
					if (!(con = ast_context_find_or_create(NULL, parking_con_dial, registrar))) {
						ast_log(LOG_ERROR, "Parking dial context '%s' does not exist and unable to create\n", parking_con_dial);
					} else {
						char returnexten[AST_MAX_EXTENSION];
						struct ast_datastore *features_datastore;
						struct ast_dial_features *dialfeatures = NULL;

						ast_channel_lock(chan);

						if ((features_datastore = ast_channel_datastore_find(chan, &dial_features_info, NULL))) {
							dialfeatures = features_datastore->data;
						}

						ast_channel_unlock(chan);

						if (!strncmp(peername, "Parked/", 7)) {
							peername += 7;
						}

						if (dialfeatures) {
							char buf[MAX_DIAL_FEATURE_OPTIONS] = "";
							snprintf(returnexten, sizeof(returnexten), "%s|30|%s", peername, callback_dialoptions(&(dialfeatures->features_callee), &(dialfeatures->features_caller), buf, sizeof(buf)));
						} else { /* Existing default */
							ast_log(LOG_WARNING, "Dialfeatures not found on %s, using default!\n", chan->name);
							snprintf(returnexten, sizeof(returnexten), "%s|30|t", peername);
						}

						ast_add_extension2(con, 1, peername, 1, NULL, NULL, "Dial", strdup(returnexten), ast_free_ptr, registrar);
					}
					set_c_e_p(chan, parking_con_dial, peername, 1);
					ast_free(peername);
				} else {
					/* They've been waiting too long, send them back to where they came.  Theoretically they
					   should have their original extensions and such, but we copy to be on the safe side */
					set_c_e_p(chan, pu->context, pu->exten, pu->priority);
				}

				post_manager_event("ParkedCallTimeOut", pu->parkingexten, chan);

				if (option_verbose > 1) {
					ast_verbose(VERBOSE_PREFIX_2 "Timeout for %s parked on %d. Returning to %s,%s,%d\n", chan->name, pu->parkingnum, chan->context, chan->exten, chan->priority);
				}
				/* Start up the PBX, or hang them up */
				if (ast_pbx_start(chan))  {
					ast_log(LOG_WARNING, "Unable to restart the PBX for user on '%s', hanging them up...\n", chan->name);
					ast_hangup(chan);
				}
				/* And take them out of the parking lot */
				if (pl) {
					pl->next = pu->next;
				} else {
					parkinglot = pu->next;
				}
				pt = pu;
				pu = pu->next;
				con = ast_context_find(parking_con);
				if (con) {
					if (ast_context_remove_extension2(con, pt->parkingexten, 1, NULL)) {
						ast_log(LOG_WARNING, "Whoa, failed to remove the extension!\n");
					} else {
						notify_metermaids(pt->parkingexten, parking_con);
					}
				} else {
					ast_log(LOG_WARNING, "Whoa, no parking context?\n");
				}
				free(pt);
			} else {	/* still within parking time, process descriptors */
				for (x = 0; x < AST_MAX_FDS; x++) {
					struct ast_frame *f;
					int y;

					if (chan->fds[x] == -1) {
						continue;	/* nothing on this descriptor */
					}

					for (y = 0; y < nfds; y++) {
						if (fds[y].fd == chan->fds[x]) {
							/* Found poll record! */
							break;
						}
					}
					if (y == nfds) {
						/* Not found */
						continue;
					}

					if (!(fds[y].revents & (POLLIN | POLLERR | POLLPRI))) {
						/* Next x */
						continue;
					}

					if (fds[y].revents & POLLPRI) {
						ast_set_flag(chan, AST_FLAG_EXCEPTION);
					} else {
						ast_clear_flag(chan, AST_FLAG_EXCEPTION);
					}
					chan->fdno = x;

					/* See if they need servicing */
					f = ast_read(chan);
					if (!f || (f->frametype == AST_FRAME_CONTROL && f->subclass ==  AST_CONTROL_HANGUP)) {
						if (f) {
							ast_frfree(f);
						}
						post_manager_event("ParkedCallGiveUp", pu->parkingexten, chan);

						/* There's a problem, hang them up*/
						if (option_verbose > 1) {
							ast_verbose(VERBOSE_PREFIX_2 "%s got tired of being parked\n", chan->name);
						}
						/* And take them out of the parking lot */
						if (pl) {
							pl->next = pu->next;
						} else {
							parkinglot = pu->next;
						}
						pt = pu;
						pu = pu->next;

						ast_hangup(chan);
						con = ast_context_find(parking_con);
						if (con) {
							if (ast_context_remove_extension2(con, pt->parkingexten, 1, NULL)) {
								ast_log(LOG_WARNING, "Whoa, failed to remove the extension!\n");
							} else {
								notify_metermaids(pt->parkingexten, parking_con);
							}
						} else {
							ast_log(LOG_WARNING, "Whoa, no parking context?\n");
						}
						free(pt);
						break;
					} else {
						/*! \todo XXX Maybe we could do something with packets, like dial "0" for operator or something XXX */
						ast_frfree(f);
						if (pu->moh_trys < 3 && !chan->generatordata) {
							if (option_debug) {
								ast_log(LOG_DEBUG, "MOH on parked call stopped by outside source.  Restarting.\n");
							}
							ast_indicate_data(pu->chan, AST_CONTROL_HOLD,
								S_OR(parkmohclass, NULL),
								!ast_strlen_zero(parkmohclass) ? strlen(parkmohclass) + 1 : 0);
							pu->moh_trys++;
						}
						goto std;	/*! \todo XXX Ick: jumping into an else statement??? XXX */
					}

				} /* end for */
				if (x >= AST_MAX_FDS) {
std:				for (x = 0; x < AST_MAX_FDS; x++) {	/* mark fds for next round */
						if (chan->fds[x] > -1) {
							void *tmp = ast_realloc(new_fds, (new_nfds + 1) * sizeof(*new_fds));
							if (!tmp) {
								continue;
							}
							new_fds = tmp;
							new_fds[new_nfds].fd = chan->fds[x];
							new_fds[new_nfds].events = POLLIN | POLLERR | POLLPRI;
							new_fds[new_nfds].revents = 0;
							new_nfds++;
						}
					}
					/* Keep track of our shortest wait */
					if (tms < ms || ms < 0) {
						ms = tms;
					}
					pl = pu;
					pu = pu->next;
				}
			}
		} /* end while */
		ast_mutex_unlock(&parking_lock);
		ast_free(fds);
		fds = new_fds;
		nfds = new_nfds;
		new_fds = NULL;
		new_nfds = 0;

		tv = ast_samp2tv(ms, 1000);
		/* Wait for something to happen */
		ast_poll2(fds, nfds, (ms > -1) ? &tv : NULL);

		pthread_testcancel();
	}
	return NULL;	/* Never reached */
}

/*! \brief Park a call */
static int park_call_exec(struct ast_channel *chan, void *data)
{
	/* Cache the original channel name in case we get masqueraded in the middle
	 * of a park--it is still theoretically possible for a transfer to happen before
	 * we get here, but it is _really_ unlikely */
	char *orig_chan_name = ast_strdupa(chan->name);
	char orig_exten[AST_MAX_EXTENSION];
	int orig_priority = chan->priority;

	/* Data is unused at the moment but could contain a parking
	   lot context eventually */
	int res = 0;
	struct ast_module_user *u;

	u = ast_module_user_add(chan);

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

	ast_module_user_remove(u);

	return res;
}

/*! \brief Pickup parked call */
static int park_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct ast_module_user *u;
	struct ast_channel *peer=NULL;
	struct parkeduser *pu, *pl=NULL;
	struct ast_context *con;

	int park;
	struct ast_bridge_config config;

	if (!data) {
		ast_log(LOG_WARNING, "Parkedcall requires an argument (extension number)\n");
		return -1;
	}

	u = ast_module_user_add(chan);

	park = atoi((char *)data);
	ast_mutex_lock(&parking_lock);
	pu = parkinglot;
	while(pu) {
		if (pu->parkingnum == park && !pu->notquiteyet) {
			if (pu->chan->pbx) { /* do not allow call to be picked up until the PBX thread is finished */
				ast_mutex_unlock(&parking_lock);
				ast_module_user_remove(u);
				return -1;
			}
			if (pl)
				pl->next = pu->next;
			else
				parkinglot = pu->next;
			break;
		}
		pl = pu;
		pu = pu->next;
	}
	ast_mutex_unlock(&parking_lock);
	if (pu) {
		peer = pu->chan;
		con = ast_context_find(parking_con);
		if (con) {
			if (ast_context_remove_extension2(con, pu->parkingexten, 1, NULL))
				ast_log(LOG_WARNING, "Whoa, failed to remove the extension!\n");
			else
				notify_metermaids(pu->parkingexten, parking_con);
		} else
			ast_log(LOG_WARNING, "Whoa, no parking context?\n");

		manager_event(EVENT_FLAG_CALL, "UnParkedCall",
			"Exten: %s\r\n"
			"Channel: %s\r\n"
			"From: %s\r\n"
			"CallerID: %s\r\n"
			"CallerIDName: %s\r\n",
			pu->parkingexten, pu->chan->name, chan->name,
			S_OR(pu->chan->cid.cid_num, "<unknown>"),
			S_OR(pu->chan->cid.cid_name, "<unknown>")
			);

		free(pu);
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
				error = ast_stream_and_wait(chan, courtesytone, chan->language, "");
			} else if (parkedplay == 1) {
				error = ast_stream_and_wait(peer, courtesytone, chan->language, "");
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
				ast_module_user_remove(u);
				return -1;
			}
		} else
			ast_indicate(peer, AST_CONTROL_UNHOLD);

		res = ast_channel_make_compatible(chan, peer);
		if (res < 0) {
			ast_log(LOG_WARNING, "Could not make channels %s and %s compatible for bridge\n", chan->name, peer->name);
			ast_hangup(peer);
			ast_module_user_remove(u);
			return -1;
		}
		/* This runs sorta backwards, since we give the incoming channel control, as if it
		   were the person called. */
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Channel %s connected to parked call %d\n", chan->name, park);

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
		ast_module_user_remove(u);
		return -1;
	} else {
		/*! \todo XXX Play a message XXX */
		if (ast_stream_and_wait(chan, "pbx-invalidpark", chan->language, ""))
			ast_log(LOG_WARNING, "ast_streamfile of %s failed on %s\n", "pbx-invalidpark", chan->name);
		if (option_verbose > 2) 
			ast_verbose(VERBOSE_PREFIX_3 "Channel %s tried to talk to nonexistent parked call %d\n", chan->name, park);
		res = -1;
	}

	ast_module_user_remove(u);

	return -1;
}

static int handle_showfeatures(int fd, int argc, char *argv[])
{
	int i;
	struct ast_call_feature *feature;
	char format[] = "%-25s %-7s %-7s\n";

	ast_cli(fd, format, "Builtin Feature", "Default", "Current");
	ast_cli(fd, format, "---------------", "-------", "-------");

	ast_cli(fd, format, "Pickup", "*8", ast_pickup_ext());		/* default hardcoded above, so we'll hardcode it here */

	ast_rwlock_rdlock(&features_lock);
	for (i = 0; i < FEATURES_COUNT; i++)
		ast_cli(fd, format, builtin_features[i].fname, builtin_features[i].default_exten, builtin_features[i].exten);
	ast_rwlock_unlock(&features_lock);

	ast_cli(fd, "\n");
	ast_cli(fd, format, "Dynamic Feature", "Default", "Current");
	ast_cli(fd, format, "---------------", "-------", "-------");
	if (AST_RWLIST_EMPTY(&feature_list)) {
		ast_cli(fd, "(none)\n");
	} else {
		AST_RWLIST_RDLOCK(&feature_list);
		AST_RWLIST_TRAVERSE(&feature_list, feature, feature_entry) {
			ast_cli(fd, format, feature->sname, "no def", feature->exten);
		}
		AST_RWLIST_UNLOCK(&feature_list);
	}
	ast_cli(fd, "\nCall parking\n");
	ast_cli(fd, "------------\n");
	ast_cli(fd,"%-20s:	%s\n", "Parking extension", parking_ext);
	ast_cli(fd,"%-20s:	%s\n", "Parking context", parking_con);
	ast_cli(fd,"%-20s:	%d-%d\n", "Parked call extensions", parking_start, parking_stop);
	ast_cli(fd,"\n");
	
	return RESULT_SUCCESS;
}

static char showfeatures_help[] =
"Usage: feature list\n"
"       Lists currently configured features.\n";

static int handle_parkedcalls(int fd, int argc, char *argv[])
{
	struct parkeduser *cur;
	int numparked = 0;

	ast_cli(fd, "%4s %25s (%-15s %-12s %-4s) %-6s \n", "Num", "Channel"
		, "Context", "Extension", "Pri", "Timeout");

	ast_mutex_lock(&parking_lock);

	for (cur = parkinglot; cur; cur = cur->next) {
		ast_cli(fd, "%-10.10s %25s (%-15s %-12s %-4d) %6lds\n"
			,cur->parkingexten, cur->chan->name, cur->context, cur->exten
			,cur->priority, (long) cur->start.tv_sec + (cur->parkingtime/1000) - time(NULL));

		numparked++;
	}
	ast_mutex_unlock(&parking_lock);
	ast_cli(fd, "%d parked call%s.\n", numparked, (numparked != 1) ? "s" : "");


	return RESULT_SUCCESS;
}

static char showparked_help[] =
"Usage: show parkedcalls\n"
"       Lists currently parked calls.\n";

static struct ast_cli_entry cli_show_features_deprecated = {
	{ "show", "features", NULL },
	handle_showfeatures, NULL,
	NULL };

static struct ast_cli_entry cli_features[] = {
	{ { "feature", "show", NULL },
	handle_showfeatures, "Lists configured features",
	showfeatures_help, NULL, &cli_show_features_deprecated },

	{ { "show", "parkedcalls", NULL },
	handle_parkedcalls, "Lists parked calls",
	showparked_help },
};

/*! \brief Dump lot status */
static int manager_parking_status( struct mansession *s, const struct message *m)
{
	struct parkeduser *cur;
	const char *id = astman_get_header(m, "ActionID");
	char idText[256] = "";

	if (!ast_strlen_zero(id))
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);

	astman_send_ack(s, m, "Parked calls will follow");

	ast_mutex_lock(&parking_lock);

	for (cur = parkinglot; cur; cur = cur->next) {
		astman_append(s, "Event: ParkedCall\r\n"
			"Exten: %d\r\n"
			"Channel: %s\r\n"
			"From: %s\r\n"
			"Timeout: %ld\r\n"
			"CallerID: %s\r\n"
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

	ast_mutex_unlock(&parking_lock);

	return RESULT_SUCCESS;
}

static char mandescr_park[] =
"Description: Park a channel.\n"
"Variables: (Names marked with * are required)\n"
"	*Channel: Channel name to park\n"
"	*Channel2: Channel to announce park info to (and return to if timeout)\n"
"	Timeout: Number of milliseconds to wait before callback.\n";  

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


int ast_pickup_call(struct ast_channel *chan)
{
	struct ast_channel *cur = NULL;
	int res = -1;

	while ( (cur = ast_channel_walk_locked(cur)) != NULL) {
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
		if (option_debug)
			ast_log(LOG_DEBUG, "Call pickup on chan '%s' by '%s'\n",cur->name, chan->name);
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
		if (option_debug)
			ast_log(LOG_DEBUG, "No call pickup possible...\n");
	}
	return res;
}

/*! \brief Add parking hints for all defined parking lots */
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
	struct ast_context *con = NULL;
	struct ast_config *cfg = NULL;
	struct ast_variable *var = NULL;
	char old_parking_ext[AST_MAX_EXTENSION];
	char old_parking_con[AST_MAX_EXTENSION] = "";

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
	strcpy(xferfailsound, "beeperr");
	parking_start = 701;
	parking_stop = 750;
	parkfindnext = 0;
	adsipark = 0;
	parkaddhints = 0;
	parkedcalltransfers = AST_FEATURE_FLAG_BYBOTH;
	parkedcallreparking = 0;
	parkedcallhangup = 0;
	parkedcallrecording = 0;

	transferdigittimeout = DEFAULT_TRANSFER_DIGIT_TIMEOUT;
	featuredigittimeout = DEFAULT_FEATURE_DIGIT_TIMEOUT;
	atxfernoanswertimeout = DEFAULT_NOANSWER_TIMEOUT_ATTENDED_TRANSFER;

	cfg = ast_config_load("features.conf");
	if (!cfg) {
		ast_log(LOG_WARNING,"Could not load features.conf\n");
		return AST_MODULE_LOAD_DECLINE;
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
			if (!strcasecmp(var->value, "no"))
				parkedcalltransfers = 0;
			else if (!strcasecmp(var->value, "caller"))
				parkedcalltransfers = AST_FEATURE_FLAG_BYCALLER;
			else if (!strcasecmp(var->value, "callee"))
				parkedcalltransfers = AST_FEATURE_FLAG_BYCALLEE;
			else if (!strcasecmp(var->value, "both"))
				parkedcalltransfers = AST_FEATURE_FLAG_BYBOTH;
		} else if (!strcasecmp(var->name, "parkedcallreparking")) {
			if (!strcasecmp(var->value, "no"))
				parkedcallreparking = 0;
			else if (!strcasecmp(var->value, "caller"))
				parkedcallreparking = AST_FEATURE_FLAG_BYCALLER;
			else if (!strcasecmp(var->value, "callee"))
				parkedcallreparking = AST_FEATURE_FLAG_BYCALLEE;
			else if (!strcasecmp(var->value, "both"))
				parkedcallreparking = AST_FEATURE_FLAG_BYBOTH;
		} else if (!strcasecmp(var->name, "parkedcallhangup")) {
			if (!strcasecmp(var->value, "no"))
				parkedcallhangup = 0;
			else if (!strcasecmp(var->value, "caller"))
				parkedcallhangup = AST_FEATURE_FLAG_BYCALLER;
			else if (!strcasecmp(var->value, "callee"))
				parkedcallhangup = AST_FEATURE_FLAG_BYCALLEE;
			else if (!strcasecmp(var->value, "both"))
				parkedcallhangup = AST_FEATURE_FLAG_BYBOTH;
		} else if (!strcasecmp(var->name, "parkedcallrecording")) {
			if (!strcasecmp(var->value, "no"))
				parkedcallrecording = 0;
			else if (!strcasecmp(var->value, "caller"))
				parkedcallrecording = AST_FEATURE_FLAG_BYCALLER;
			else if (!strcasecmp(var->value, "callee"))
				parkedcallrecording = AST_FEATURE_FLAG_BYCALLEE;
			else if (!strcasecmp(var->value, "both"))
				parkedcallrecording = AST_FEATURE_FLAG_BYBOTH;
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
		char *exten, *activateon, *activatedby, *app, *app_args, *moh_class; 
		struct ast_call_feature *feature;

		/* strsep() sets the argument to NULL if match not found, and it
		 * is safe to use it with a NULL argument, so we don't check
		 * between calls.
		 */
		exten = strsep(&tmp_val,",");
		activatedby = strsep(&tmp_val,",");
		app = strsep(&tmp_val,",");
		app_args = strsep(&tmp_val,",");
		moh_class = strsep(&tmp_val,",");

		activateon = strsep(&activatedby, "/");	

		/*! \todo XXX var_name or app_args ? */
		if (ast_strlen_zero(app) || ast_strlen_zero(exten) || ast_strlen_zero(activateon) || ast_strlen_zero(var->name)) {
			ast_log(LOG_NOTICE, "Please check the feature Mapping Syntax, either extension, name, or app aren't provided %s %s %s %s\n",
				app, exten, activateon, var->name);
			continue;
		}

		AST_RWLIST_RDLOCK(&feature_list);
		if ((feature = find_dynamic_feature(var->name))) {
			AST_RWLIST_UNLOCK(&feature_list);
			ast_log(LOG_WARNING, "Dynamic Feature '%s' specified more than once!\n", var->name);
			continue;
		}
		AST_RWLIST_UNLOCK(&feature_list);
				
		if (!(feature = ast_calloc(1, sizeof(*feature))))
			continue;					

		ast_copy_string(feature->sname, var->name, FEATURE_SNAME_LEN);
		ast_copy_string(feature->app, app, FEATURE_APP_LEN);
		ast_copy_string(feature->exten, exten, FEATURE_EXTEN_LEN);
		
		if (app_args) 
			ast_copy_string(feature->app_args, app_args, FEATURE_APP_ARGS_LEN);

		if (moh_class)
			ast_copy_string(feature->moh_class, moh_class, FEATURE_MOH_LEN);
			
		ast_copy_string(feature->exten, exten, sizeof(feature->exten));
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

		if (ast_strlen_zero(activatedby))
			ast_set_flag(feature, AST_FEATURE_FLAG_BYBOTH);
		else if (!strcasecmp(activatedby, "caller"))
			ast_set_flag(feature, AST_FEATURE_FLAG_BYCALLER);
		else if (!strcasecmp(activatedby, "callee"))
			ast_set_flag(feature, AST_FEATURE_FLAG_BYCALLEE);
		else if (!strcasecmp(activatedby, "both"))
			ast_set_flag(feature, AST_FEATURE_FLAG_BYBOTH);
		else {
			ast_log(LOG_NOTICE, "Invalid 'ActivatedBy' specification for feature '%s',"
				" must be 'caller', or 'callee', or 'both'\n", var->name);
			continue;
		}

		ast_register_feature(feature);
			
		if (option_verbose >= 1)
			ast_verbose(VERBOSE_PREFIX_2 "Mapping Feature '%s' to app '%s(%s)' with code '%s'\n", var->name, app, app_args, exten);  
	}	 
	ast_config_destroy(cfg);

	/* Remove the old parking extension */
	if (!ast_strlen_zero(old_parking_con) && (con = ast_context_find(old_parking_con)))	{
		if(ast_context_remove_extension2(con, old_parking_ext, 1, registrar))
				notify_metermaids(old_parking_ext, old_parking_con);
		if (option_debug)
			ast_log(LOG_DEBUG, "Removed old parking extension %s@%s\n", old_parking_ext, old_parking_con);
	}
	
	if (!(con = ast_context_find(parking_con)) && !(con = ast_context_create(NULL, parking_con, registrar))) {
		ast_log(LOG_ERROR, "Parking context '%s' does not exist and unable to create\n", parking_con);
		return -1;
	}
	res = ast_add_extension2(con, 1, ast_parking_ext(), 1, NULL, NULL, parkcall, NULL, NULL, registrar);
	if (parkaddhints)
		park_add_hints(parking_con, parking_start, parking_stop);
	if (!res)
		notify_metermaids(ast_parking_ext(), parking_con);
	return res;

}

static int reload(void)
{
	return load_config();
}

static int load_module(void)
{
	int res;
	
	memset(parking_ext, 0, sizeof(parking_ext));
	memset(parking_con, 0, sizeof(parking_con));

	if ((res = load_config()))
		return res;
	ast_cli_register_multiple(cli_features, sizeof(cli_features) / sizeof(struct ast_cli_entry));
	ast_pthread_create(&parking_thread, NULL, do_parking_thread, NULL);
	res = ast_register_application(parkedcall, park_exec, synopsis, descrip);
	if (!res)
		res = ast_register_application(parkcall, park_call_exec, synopsis2, descrip2);
	if (!res) {
		ast_manager_register("ParkedCalls", 0, manager_parking_status, "List parked calls" );
		ast_manager_register2("Park", EVENT_FLAG_CALL, manager_park,
			"Park a channel", mandescr_park); 
	}

	res |= ast_devstate_prov_add("Park", metermaidstate);

	return res;
}


static int unload_module(void)
{
	ast_module_user_hangup_all();

	ast_manager_unregister("ParkedCalls");
	ast_manager_unregister("Park");
	ast_cli_unregister_multiple(cli_features, sizeof(cli_features) / sizeof(struct ast_cli_entry));
	ast_unregister_application(parkcall);
	ast_devstate_prov_del("Park");
	return ast_unregister_application(parkedcall);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS, "Call Features Resource",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
