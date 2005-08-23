/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Routines implementing call parking
 * 
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <netinet/in.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

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

#ifdef __AST_DEBUG_MALLOC
static void FREE(void *ptr)
{
	free(ptr);
}
#else
#define FREE free
#endif

#define DEFAULT_PARK_TIME 45000
#define DEFAULT_TRANSFER_DIGIT_TIMEOUT 3000
#define DEFAULT_FEATURE_DIGIT_TIMEOUT 500

#define AST_MAX_WATCHERS 256

static char *parkedcall = "ParkedCall";

/* No more than 45 seconds parked before you do something with them */
static int parkingtime = DEFAULT_PARK_TIME;

/* Context for which parking is made accessible */
static char parking_con[AST_MAX_EXTENSION] = "parkedcalls";

/* Context for dialback for parking (KLUDGE) */
static char parking_con_dial[AST_MAX_EXTENSION] = "park-dial";

/* Extension you type to park the call */
static char parking_ext[AST_MAX_EXTENSION] = "700";

static char pickup_ext[AST_MAX_EXTENSION] = "*8";

/* Default sounds */
static char courtesytone[256] = "";
static char xfersound[256] = "beep";
static char xferfailsound[256] = "pbx-invalid";

/* First available extension for parking */
static int parking_start = 701;

/* Last available extension for parking */
static int parking_stop = 750;

static int parking_offset = 0;

static int parkfindnext = 0;

static int adsipark = 0;

static int transferdigittimeout = DEFAULT_TRANSFER_DIGIT_TIMEOUT;
static int featuredigittimeout = DEFAULT_FEATURE_DIGIT_TIMEOUT;

/* Default courtesy tone played when party joins conference */

/* Registrar for operations */
static char *registrar = "res_features";

static char *synopsis = "Answer a parked call";

static char *descrip = "ParkedCall(exten):"
"Used to connect to a parked call.  This application is always\n"
"registered internally and does not need to be explicitly added\n"
"into the dialplan, although you should include the 'parkedcalls'\n"
"context.\n";

static char *parkcall = "Park";

static char *synopsis2 = "Park yourself";

static char *descrip2 = "Park(exten):"
"Used to park yourself (typically in combination with a supervised\n"
"transfer to know the parking space). This application is always\n"
"registered internally and does not need to be explicitly added\n"
"into the dialplan, although you should include the 'parkedcalls'\n"
"context.\n";

static struct ast_app *monitor_app=NULL;
static int monitor_ok=1;

struct parkeduser {
	struct ast_channel *chan;
	struct timeval start;
	int parkingnum;
	/* Where to go if our parking time expires */
	char context[AST_MAX_CONTEXT];
	char exten[AST_MAX_EXTENSION];
	int priority;
	int parkingtime;
	int notquiteyet;
	char peername[1024];
	unsigned char moh_trys;
	struct parkeduser *next;
};

static struct parkeduser *parkinglot;

AST_MUTEX_DEFINE_STATIC(parking_lock);

static pthread_t parking_thread;

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

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

static void check_goto_on_transfer(struct ast_channel *chan) 
{
	struct ast_channel *xferchan;
	char *goto_on_transfer;

	goto_on_transfer = pbx_builtin_getvar_helper(chan, "GOTO_ON_BLINDXFR");

	if (goto_on_transfer && !ast_strlen_zero(goto_on_transfer) && (xferchan = ast_channel_alloc(0))) {
		char *x;
		struct ast_frame *f;
		
		for (x = goto_on_transfer; x && *x; x++)
			if (*x == '^')
				*x = '|';

		strcpy(xferchan->name, chan->name);
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
}

static struct ast_channel *ast_feature_request_and_dial(struct ast_channel *caller, const char *type, int format, void *data, int timeout, int *outstate, const char *cid_num, const char *cid_name);


static void *ast_bridge_call_thread(void *data) 
{
	struct ast_bridge_thread_obj *tobj = data;
	tobj->chan->appl = "Transferred Call";
	tobj->chan->data = tobj->peer->name;
	tobj->peer->appl = "Transferred Call";
	tobj->peer->data = tobj->chan->name;
	if (tobj->chan->cdr) {
		ast_cdr_reset(tobj->chan->cdr,0);
		ast_cdr_setdestchan(tobj->chan->cdr, tobj->peer->name);
	}
	if (tobj->peer->cdr) {
		ast_cdr_reset(tobj->peer->cdr,0);
		ast_cdr_setdestchan(tobj->peer->cdr, tobj->chan->name);
	}


	ast_bridge_call(tobj->peer, tobj->chan, &tobj->bconfig);
	ast_hangup(tobj->chan);
	ast_hangup(tobj->peer);
	tobj->chan = tobj->peer = NULL;
	free(tobj);
	tobj=NULL;
	return NULL;
}

static void ast_bridge_call_thread_launch(void *data) 
{
	pthread_t thread;
	pthread_attr_t attr;
	int result;

	result = pthread_attr_init(&attr);
	pthread_attr_setschedpolicy(&attr, SCHED_RR);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	result = ast_pthread_create(&thread, &attr,ast_bridge_call_thread, data);
	result = pthread_attr_destroy(&attr);
}



static int adsi_announce_park(struct ast_channel *chan, int parkingnum)
{
	int res;
	int justify[5] = {ADSI_JUST_CENT, ADSI_JUST_CENT, ADSI_JUST_CENT, ADSI_JUST_CENT};
	char tmp[256] = "";
	unsigned char *message[5] = {NULL, NULL, NULL, NULL, NULL};

	snprintf(tmp, sizeof(tmp), "Parked on %d", parkingnum);
	message[0] = (unsigned char *)tmp;
	res = adsi_load_session(chan, NULL, 0, 1);
	if (res == -1) {
		return res;
	}
	return adsi_print(chan, message, justify, 1);
}

/*--- ast_park_call: Park a call */
/* We put the user in the parking list, then wake up the parking thread to be sure it looks
	   after these channels too */
int ast_park_call(struct ast_channel *chan, struct ast_channel *peer, int timeout, int *extout)
{
	struct parkeduser *pu, *cur;
	int i,x,parking_range;
	char exten[AST_MAX_EXTENSION];
	struct ast_context *con;

	pu = malloc(sizeof(struct parkeduser));
	if (!pu) {
		ast_log(LOG_WARNING, "Out of memory\n");
		return -1;
	}
	memset(pu, 0, sizeof(struct parkeduser));
	ast_mutex_lock(&parking_lock);
	parking_range = parking_stop - parking_start+1;
	for (i = 0; i < parking_range; i++) {
		x = (i + parking_offset) % parking_range + parking_start;
		cur = parkinglot;
		while(cur) {
			if (cur->parkingnum == x) 
				break;
			cur = cur->next;
		}
		if (!cur)
			break;
	}

	if (!(i < parking_range)) {
		ast_log(LOG_WARNING, "No more parking spaces\n");
		free(pu);
		ast_mutex_unlock(&parking_lock);
		return -1;
	}
	if (parkfindnext) 
		parking_offset = x - parking_start + 1;
	chan->appl = "Parked Call";
	chan->data = NULL; 

	pu->chan = chan;
	/* Start music on hold */
	if (chan != peer) {
		ast_indicate(pu->chan, AST_CONTROL_HOLD);
		ast_moh_start(pu->chan, NULL);
	}
	pu->start = ast_tvnow();
	pu->parkingnum = x;
	if (timeout > 0)
		pu->parkingtime = timeout;
	else
		pu->parkingtime = parkingtime;
	if (extout)
		*extout = x;
	if (peer) 
		ast_copy_string(pu->peername, peer->name, sizeof(pu->peername));

	/* Remember what had been dialed, so that if the parking
	   expires, we try to come back to the same place */
	if (!ast_strlen_zero(chan->macrocontext))
		ast_copy_string(pu->context, chan->macrocontext, sizeof(pu->context));
	else
		ast_copy_string(pu->context, chan->context, sizeof(pu->context));
	if (!ast_strlen_zero(chan->macroexten))
		ast_copy_string(pu->exten, chan->macroexten, sizeof(pu->exten));
	else
		ast_copy_string(pu->exten, chan->exten, sizeof(pu->exten));
	if (chan->macropriority)
		pu->priority = chan->macropriority;
	else
		pu->priority = chan->priority;
	pu->next = parkinglot;
	parkinglot = pu;
	/* If parking a channel directly, don't quiet yet get parking running on it */
	if (peer == chan) 
		pu->notquiteyet = 1;
	ast_mutex_unlock(&parking_lock);
	/* Wake up the (presumably select()ing) thread */
	pthread_kill(parking_thread, SIGURG);
	if (option_verbose > 1) 
		ast_verbose(VERBOSE_PREFIX_2 "Parked %s on %d. Will timeout back to extension [%s] %s, %d in %d seconds\n", pu->chan->name, pu->parkingnum, pu->context, pu->exten, pu->priority, (pu->parkingtime/1000));

	manager_event(EVENT_FLAG_CALL, "ParkedCall",
		"Exten: %d\r\n"
		"Channel: %s\r\n"
		"From: %s\r\n"
		"Timeout: %ld\r\n"
		"CallerID: %s\r\n"
		"CallerIDName: %s\r\n\r\n"
		,pu->parkingnum, pu->chan->name, peer->name
		,(long)pu->start.tv_sec + (long)(pu->parkingtime/1000) - (long)time(NULL)
		,(pu->chan->cid.cid_num ? pu->chan->cid.cid_num : "<unknown>")
		,(pu->chan->cid.cid_name ? pu->chan->cid.cid_name : "<unknown>")
		);

	if (peer) {
		if (adsipark && adsi_available(peer)) {
			adsi_announce_park(peer, pu->parkingnum);
		}
		if (adsipark && adsi_available(peer)) {
			adsi_unload_session(peer);
		}
	}
	con = ast_context_find(parking_con);
	if (!con) {
		con = ast_context_create(NULL, parking_con, registrar);
		if (!con) {
			ast_log(LOG_ERROR, "Parking context '%s' does not exist and unable to create\n", parking_con);
		}
	}
	if (con) {
		snprintf(exten, sizeof(exten), "%d", x);
		ast_add_extension2(con, 1, exten, 1, NULL, NULL, parkedcall, strdup(exten), FREE, registrar);
	}
	if (peer) 
		ast_say_digits(peer, pu->parkingnum, "", peer->language);
	if (pu->notquiteyet) {
		/* Wake up parking thread if we're really done */
		ast_moh_start(pu->chan, NULL);
		pu->notquiteyet = 0;
		pthread_kill(parking_thread, SIGURG);
	}
	return 0;
}

int ast_masq_park_call(struct ast_channel *rchan, struct ast_channel *peer, int timeout, int *extout)
{
	struct ast_channel *chan;
	struct ast_frame *f;

	/* Make a new, fake channel that we'll use to masquerade in the real one */
	chan = ast_channel_alloc(0);
	if (chan) {
		/* Let us keep track of the channel name */
		snprintf(chan->name, sizeof (chan->name), "Parked/%s",rchan->name);

		/* Make formats okay */
		chan->readformat = rchan->readformat;
		chan->writeformat = rchan->writeformat;
		ast_channel_masquerade(chan, rchan);

		/* Setup the extensions and such */
		ast_copy_string(chan->context, rchan->context, sizeof(chan->context));
		ast_copy_string(chan->exten, rchan->exten, sizeof(chan->exten));
		chan->priority = rchan->priority;

		/* Make the masq execute */
		f = ast_read(chan);
		if (f)
			ast_frfree(f);
		ast_park_call(chan, peer, timeout, extout);
	} else {
		ast_log(LOG_WARNING, "Unable to create parked channel\n");
		return -1;
	}
	return 0;
}


#define FEATURE_RETURN_HANGUP		-1
#define FEATURE_RETURN_SUCCESSBREAK	 0
#define FEATURE_RETURN_PBX_KEEPALIVE	AST_PBX_KEEPALIVE
#define FEATURE_RETURN_NO_HANGUP_PEER	AST_PBX_NO_HANGUP_PEER
#define FEATURE_RETURN_PASSDIGITS	 21
#define FEATURE_RETURN_STOREDIGITS	 22
#define FEATURE_RETURN_SUCCESS	 	 23

#define FEATURE_SENSE_CHAN	(1 << 0)
#define FEATURE_SENSE_PEER	(1 << 1)


static int builtin_automonitor(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, char *code, int sense)
{
	char *touch_monitor = NULL, *caller_chan_id = NULL, *callee_chan_id = NULL, *args = NULL, *touch_format = NULL;
	int x = 0;
	size_t len;
	struct ast_channel *caller_chan = NULL, *callee_chan = NULL;


	if(sense == 2) {
		caller_chan = peer;
		callee_chan = chan;
	} else {
		callee_chan = peer;
		caller_chan = chan;
	}
	
	if (!monitor_ok) {
		ast_log(LOG_ERROR,"Cannot record the call. The monitor application is disabled.\n");
		return -1;
	}

	if (!monitor_app) { 
		if (!(monitor_app = pbx_findapp("Monitor"))) {
			monitor_ok=0;
			ast_log(LOG_ERROR,"Cannot record the call. The monitor application is disabled.\n");
			return -1;
		}
	}
	if (!ast_strlen_zero(courtesytone)) {
		if (ast_autoservice_start(callee_chan))
			return -1;
		if (!ast_streamfile(caller_chan, courtesytone, caller_chan->language)) {
			if (ast_waitstream(caller_chan, "") < 0) {
				ast_log(LOG_WARNING, "Failed to play courtesy tone!\n");
				ast_autoservice_stop(callee_chan);
				return -1;
			}
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
		touch_format = pbx_builtin_getvar_helper(caller_chan, "TOUCH_MONITOR_FORMAT");
		if (!touch_format)
			touch_format = pbx_builtin_getvar_helper(callee_chan, "TOUCH_MONITOR_FORMAT");

		touch_monitor = pbx_builtin_getvar_helper(caller_chan, "TOUCH_MONITOR");
		if (!touch_monitor)
			touch_monitor = pbx_builtin_getvar_helper(callee_chan, "TOUCH_MONITOR");
		
		if (touch_monitor) {
			len = strlen(touch_monitor) + 50;
			args = alloca(len);
			snprintf(args, len, "%s|auto-%ld-%s|m", (touch_format) ? touch_format : "wav", time(NULL), touch_monitor);
		} else {
			caller_chan_id = ast_strdupa(caller_chan->cid.cid_num ? caller_chan->cid.cid_num : caller_chan->name);
			callee_chan_id = ast_strdupa(callee_chan->cid.cid_num ? callee_chan->cid.cid_num : callee_chan->name);
			len = strlen(caller_chan_id) + strlen(callee_chan_id) + 50;
			args = alloca(len);
			snprintf(args, len, "%s|auto-%ld-%s-%s|m", (touch_format) ? touch_format : "wav", time(NULL), caller_chan_id, callee_chan_id);
		}

		for( x = 0; x < strlen(args); x++)
			if (args[x] == '/')
				args[x] = '-';
		
		if (option_verbose > 3)
			ast_verbose(VERBOSE_PREFIX_3 "User hit '%s' to record call. filename: %s\n", code, args);

		pbx_exec(callee_chan, monitor_app, args, 1);
		
		return FEATURE_RETURN_SUCCESS;
	}
	
	ast_log(LOG_NOTICE,"Cannot record the call. One or both channels have gone away.\n");	
	return -1;
}

static int builtin_disconnect(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, char *code, int sense)
{
	if (option_verbose > 3)
		ast_verbose(VERBOSE_PREFIX_3 "User hit '%s' to disconnect call.\n", code);
	return FEATURE_RETURN_HANGUP;
}

static int builtin_blindtransfer(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, char *code, int sense)
{
	struct ast_channel *transferer;
	struct ast_channel *transferee;
	char *transferer_real_context;
	char newext[256];
	int res;

	if (sense == FEATURE_SENSE_PEER) {
		transferer = peer;
		transferee = chan;
	} else {
		transferer = chan;
		transferee = peer;
	}
	if (!(transferer_real_context = pbx_builtin_getvar_helper(transferee, "TRANSFER_CONTEXT")) &&
	   !(transferer_real_context = pbx_builtin_getvar_helper(transferer, "TRANSFER_CONTEXT"))) {
		/* Use the non-macro context to transfer the call */
		if (!ast_strlen_zero(transferer->macrocontext))
			transferer_real_context = transferer->macrocontext;
		else
			transferer_real_context = transferer->context;
	}
	/* Start autoservice on chan while we talk
	   to the originator */
	ast_indicate(transferee, AST_CONTROL_HOLD);
	ast_autoservice_start(transferee);
	ast_moh_start(transferee, NULL);

	memset(newext, 0, sizeof(newext));
	
	/* Transfer */
	if ((res=ast_streamfile(transferer, "pbx-transfer", transferer->language))) {
		ast_moh_stop(transferee);
		ast_autoservice_stop(transferee);
		ast_indicate(transferee, AST_CONTROL_UNHOLD);
		return res;
	}
	if ((res=ast_waitstream(transferer, AST_DIGIT_ANY)) < 0) {
		ast_moh_stop(transferee);
		ast_autoservice_stop(transferee);
		ast_indicate(transferee, AST_CONTROL_UNHOLD);
		return res;
	} else if (res > 0) {
		/* If they've typed a digit already, handle it */
		newext[0] = (char) res;
	}

	ast_stopstream(transferer);
	res = ast_app_dtget(transferer, transferer_real_context, newext, sizeof(newext), 100, transferdigittimeout);
	if (res < 0) {
		ast_moh_stop(transferee);
		ast_autoservice_stop(transferee);
		ast_indicate(transferee, AST_CONTROL_UNHOLD);
		return res;
	}
	if (!strcmp(newext, ast_parking_ext())) {
		ast_moh_stop(transferee);

		res = ast_autoservice_stop(transferee);
		ast_indicate(transferee, AST_CONTROL_UNHOLD);
		if (res)
			res = -1;
		else if (!ast_park_call(transferee, transferer, 0, NULL)) {
			/* We return non-zero, but tell the PBX not to hang the channel when
			   the thread dies -- We have to be careful now though.  We are responsible for 
			   hanging up the channel, else it will never be hung up! */

			if (transferer == peer)
				res = AST_PBX_KEEPALIVE;
			else
				res = AST_PBX_NO_HANGUP_PEER;
			return res;
		} else {
			ast_log(LOG_WARNING, "Unable to park call %s\n", transferee->name);
		}
		/* XXX Maybe we should have another message here instead of invalid extension XXX */
	} else if (ast_exists_extension(transferee, transferer_real_context, newext, 1, transferer->cid.cid_num)) {
		pbx_builtin_setvar_helper(peer, "BLINDTRANSFER", chan->name);
		pbx_builtin_setvar_helper(chan, "BLINDTRANSFER", peer->name);
		ast_moh_stop(transferee);
		res=ast_autoservice_stop(transferee);
		ast_indicate(transferee, AST_CONTROL_UNHOLD);
		if (!transferee->pbx) {
			/* Doh!  Use our handy async_goto functions */
			if (option_verbose > 2) 
				ast_verbose(VERBOSE_PREFIX_3 "Transferring %s to '%s' (context %s) priority 1\n"
								,transferee->name, newext, transferer_real_context);
			if (ast_async_goto(transferee, transferer_real_context, newext, 1))
				ast_log(LOG_WARNING, "Async goto failed :-(\n");
			res = -1;
		} else {
			/* Set the channel's new extension, since it exists, using transferer context */
			ast_copy_string(transferee->exten, newext, sizeof(transferee->exten));
			ast_copy_string(transferee->context, transferer_real_context, sizeof(transferee->context));
			transferee->priority = 0;
		}
		check_goto_on_transfer(transferer);
		return res;
	} else {
		if (option_verbose > 2)	
			ast_verbose(VERBOSE_PREFIX_3 "Unable to find extension '%s' in context '%s'\n", newext, transferer_real_context);
	}
	if (!ast_strlen_zero(xferfailsound))
		res = ast_streamfile(transferer, xferfailsound, transferee->language);
	else
		res = 0;
	if (res) {
		ast_moh_stop(transferee);
		ast_autoservice_stop(transferee);
		ast_indicate(transferee, AST_CONTROL_UNHOLD);
		return res;
	}
	res = ast_waitstream(transferer, AST_DIGIT_ANY);
	ast_stopstream(transferer);
	ast_moh_stop(transferee);
	res = ast_autoservice_stop(transferee);
	ast_indicate(transferee, AST_CONTROL_UNHOLD);
	if (res) {
		if (option_verbose > 1)
			ast_verbose(VERBOSE_PREFIX_2 "Hungup during autoservice stop on '%s'\n", transferee->name);
		return res;
	}
	return FEATURE_RETURN_SUCCESS;
}

static int builtin_atxfer(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, char *code, int sense)
{
	struct ast_channel *transferer;
	struct ast_channel *transferee;
	struct ast_channel *newchan, *xferchan=NULL;
	int outstate=0;
	struct ast_bridge_config bconfig;
	char *transferer_real_context;
	char xferto[256],dialstr[265];
	char *cid_num;
	char *cid_name;
	int res;
	struct ast_frame *f = NULL;
	struct ast_bridge_thread_obj *tobj;

	ast_log(LOG_DEBUG, "Executing Attended Transfer %s, %s (sense=%d) XXX\n", chan->name, peer->name, sense);
	if (sense == FEATURE_SENSE_PEER) {
		transferer = peer;
		transferee = chan;
	} else {
		transferer = chan;
		transferee = peer;
	}
	if (!(transferer_real_context=pbx_builtin_getvar_helper(transferee, "TRANSFER_CONTEXT")) &&
	   !(transferer_real_context=pbx_builtin_getvar_helper(transferer, "TRANSFER_CONTEXT"))) {
		/* Use the non-macro context to transfer the call */
		if (!ast_strlen_zero(transferer->macrocontext))
			transferer_real_context = transferer->macrocontext;
		else
			transferer_real_context = transferer->context;
	}
	/* Start autoservice on chan while we talk
	   to the originator */
	ast_indicate(transferee, AST_CONTROL_HOLD);
	ast_autoservice_start(transferee);
	ast_moh_start(transferee, NULL);
	memset(xferto, 0, sizeof(xferto));
	/* Transfer */
	if ((res = ast_streamfile(transferer, "pbx-transfer", transferer->language))) {
		ast_moh_stop(transferee);
		ast_autoservice_stop(transferee);
		ast_indicate(transferee, AST_CONTROL_UNHOLD);
		return res;
	}
	if ((res=ast_waitstream(transferer, AST_DIGIT_ANY)) < 0) {
		ast_moh_stop(transferee);
		ast_autoservice_stop(transferee);
		ast_indicate(transferee, AST_CONTROL_UNHOLD);
		return res;
	} else if(res > 0) {
		/* If they've typed a digit already, handle it */
		xferto[0] = (char) res;
	}
	if ((ast_app_dtget(transferer, transferer_real_context, xferto, sizeof(xferto), 100, transferdigittimeout))) {
		cid_num = transferer->cid.cid_num;
		cid_name = transferer->cid.cid_name;
		if (ast_exists_extension(transferer, transferer_real_context,xferto, 1, cid_num)) {
			snprintf(dialstr, sizeof(dialstr), "%s@%s/n", xferto, transferer_real_context);
			newchan = ast_feature_request_and_dial(transferer, "Local", ast_best_codec(transferer->nativeformats), dialstr, 15000, &outstate, cid_num, cid_name);
			ast_indicate(transferer, -1);
			if (newchan) {
				res = ast_channel_make_compatible(transferer, newchan);
				if (res < 0) {
					ast_log(LOG_WARNING, "Had to drop call because I couldn't make %s compatible with %s\n", transferer->name, newchan->name);
					ast_hangup(newchan);
					return -1;
				}
				memset(&bconfig,0,sizeof(struct ast_bridge_config));
				ast_set_flag(&(bconfig.features_caller), AST_FEATURE_DISCONNECT);
				ast_set_flag(&(bconfig.features_callee), AST_FEATURE_DISCONNECT);
				res = ast_bridge_call(transferer,newchan,&bconfig);
				if (newchan->_softhangup || newchan->_state != AST_STATE_UP || !transferer->_softhangup) {
					ast_hangup(newchan);
					if (f) {
						ast_frfree(f);
						f = NULL;
					}
					if (!ast_strlen_zero(xfersound) && !ast_streamfile(transferer, xfersound, transferer->language)) {
						if (ast_waitstream(transferer, "") < 0) {
							ast_log(LOG_WARNING, "Failed to play courtesy tone!\n");
						}
					}
					ast_moh_stop(transferee);
					ast_autoservice_stop(transferee);
					ast_indicate(transferee, AST_CONTROL_UNHOLD);
					transferer->_softhangup = 0;
					return FEATURE_RETURN_SUCCESS;
				}
				
				res = ast_channel_make_compatible(transferee, newchan);
				if (res < 0) {
					ast_log(LOG_WARNING, "Had to drop call because I couldn't make %s compatible with %s\n", transferee->name, newchan->name);
					ast_hangup(newchan);
					return -1;
				}
				
				
				ast_moh_stop(transferee);
				
				if ((ast_autoservice_stop(transferee) < 0)
				   || (ast_waitfordigit(transferee, 100) < 0)
				   || (ast_waitfordigit(newchan, 100) < 0) 
				   || ast_check_hangup(transferee) 
				   || ast_check_hangup(newchan)) {
					ast_hangup(newchan);
					res = -1;
					return -1;
				}

				if ((xferchan = ast_channel_alloc(0))) {
					snprintf(xferchan->name, sizeof (xferchan->name), "Transfered/%s",transferee->name);
					/* Make formats okay */
					xferchan->readformat = transferee->readformat;
					xferchan->writeformat = transferee->writeformat;
					ast_channel_masquerade(xferchan, transferee);
					ast_explicit_goto(xferchan, transferee->context, transferee->exten, transferee->priority);
					xferchan->_state = AST_STATE_UP;
					ast_clear_flag(xferchan, AST_FLAGS_ALL);	
					xferchan->_softhangup = 0;

					if ((f = ast_read(xferchan))) {
						ast_frfree(f);
						f = NULL;
					}
					
				} else {
					ast_hangup(newchan);
					return -1;
				}

				newchan->_state = AST_STATE_UP;
				ast_clear_flag(newchan, AST_FLAGS_ALL);	
				newchan->_softhangup = 0;

				tobj = malloc(sizeof(struct ast_bridge_thread_obj));
				if (tobj) {
					memset(tobj,0,sizeof(struct ast_bridge_thread_obj));
					tobj->chan = xferchan;
					tobj->peer = newchan;
					tobj->bconfig = *config;
	
					if (!ast_strlen_zero(xfersound) && !ast_streamfile(newchan, xfersound, newchan->language)) {
						if (ast_waitstream(newchan, "") < 0) {
							ast_log(LOG_WARNING, "Failed to play courtesy tone!\n");
						}
					}
					ast_bridge_call_thread_launch(tobj);
				} else {
					ast_log(LOG_WARNING, "Out of memory!\n");
					ast_hangup(xferchan);
					ast_hangup(newchan);
				}
				return -1;
				
			} else {
				ast_moh_stop(transferee);
				ast_autoservice_stop(transferee);
				ast_indicate(transferee, AST_CONTROL_UNHOLD);
				/* any reason besides user requested cancel and busy triggers the failed sound */
				if (outstate != AST_CONTROL_UNHOLD && outstate != AST_CONTROL_BUSY && !ast_strlen_zero(xferfailsound)) {
					res = ast_streamfile(transferer, xferfailsound, transferer->language);
					if (!res && (ast_waitstream(transferer, "") < 0)) {
						return -1;
					}
				}
				return FEATURE_RETURN_SUCCESS;
			}
		} else {
			ast_log(LOG_WARNING, "Extension %s does not exist in context %s\n",xferto,transferer_real_context);
			ast_moh_stop(transferee);
			ast_autoservice_stop(transferee);
			ast_indicate(transferee, AST_CONTROL_UNHOLD);
			res = ast_streamfile(transferer, "beeperr", transferer->language);
			if (!res && (ast_waitstream(transferer, "") < 0)) {
				return -1;
			}
		}
	}  else {
		ast_log(LOG_WARNING, "Did not read data.\n");
		res = ast_streamfile(transferer, "beeperr", transferer->language);
		if (ast_waitstream(transferer, "") < 0) {
			return -1;
		}
	}
	ast_moh_stop(transferee);
	ast_autoservice_stop(transferee);
	ast_indicate(transferee, AST_CONTROL_UNHOLD);

	return FEATURE_RETURN_SUCCESS;
}


/* add atxfer and automon as undefined so you can only use em if you configure them */
#define FEATURES_COUNT (sizeof(builtin_features) / sizeof(builtin_features[0]))
struct ast_call_feature builtin_features[] = 
 {
	{ AST_FEATURE_REDIRECT, "Blind Transfer", "blindxfer", "#", "#", builtin_blindtransfer, AST_FEATURE_FLAG_NEEDSDTMF },
	{ AST_FEATURE_REDIRECT, "Attended Transfer", "atxfer", "", "", builtin_atxfer, AST_FEATURE_FLAG_NEEDSDTMF },
	{ AST_FEATURE_AUTOMON, "One Touch Monitor", "automon", "", "", builtin_automonitor, AST_FEATURE_FLAG_NEEDSDTMF },
	{ AST_FEATURE_DISCONNECT, "Disconnect Call", "disconnect", "*", "*", builtin_disconnect, AST_FEATURE_FLAG_NEEDSDTMF },
};


static AST_LIST_HEAD(feature_list,ast_call_feature) feature_list;

/* register new feature into feature_list*/
void ast_register_feature(struct ast_call_feature *feature)
{
	if (!feature) {
		ast_log(LOG_NOTICE,"You didn't pass a feature!\n");
    		return;
	}
  
	AST_LIST_LOCK(&feature_list);
	AST_LIST_INSERT_HEAD(&feature_list,feature,feature_entry);
	AST_LIST_UNLOCK(&feature_list);

	if (option_verbose >= 2) 
		ast_verbose(VERBOSE_PREFIX_2 "Registered Feature '%s'\n",feature->sname);
}

/* unregister feature from feature_list */
void ast_unregister_feature(struct ast_call_feature *feature)
{
	if (!feature) return;

	AST_LIST_LOCK(&feature_list);
	AST_LIST_REMOVE(&feature_list,feature,feature_entry);
	AST_LIST_UNLOCK(&feature_list);
	free(feature);
}

static void ast_unregister_features(void)
{
	struct ast_call_feature *feature;

	AST_LIST_LOCK(&feature_list);
	while ((feature = AST_LIST_REMOVE_HEAD(&feature_list,feature_entry)))
		free(feature);
	AST_LIST_UNLOCK(&feature_list);
}

/* find a feature by name */
static struct ast_call_feature *find_feature(char *name)
{
	struct ast_call_feature *tmp;

	AST_LIST_LOCK(&feature_list);
	AST_LIST_TRAVERSE(&feature_list,tmp,feature_entry) {
		if (!strcasecmp(tmp->sname,name)) break;
	}
	AST_LIST_UNLOCK(&feature_list);

	return tmp;
}

/* exec an app by feature */
static int feature_exec_app(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, char *code, int sense)
{
	struct ast_app *app;
	struct ast_call_feature *feature;
	int res;

	AST_LIST_LOCK(&feature_list);
	AST_LIST_TRAVERSE(&feature_list,feature,feature_entry) {
		if (!strcasecmp(feature->exten,code)) break;
	}
	AST_LIST_UNLOCK(&feature_list);

	if (!feature) { /* shouldn't ever happen! */
		ast_log(LOG_NOTICE, "Found feature before, but at execing we've lost it??\n");
		return -1; 
	}
	
	app = pbx_findapp(feature->app);
	if (app) {
		struct ast_channel *work=chan;
		if (ast_test_flag(feature,AST_FEATURE_FLAG_CALLEE)) work=peer;
		res = pbx_exec(work, app, feature->app_args, 1);
		if (res<0) return res; 
	} else {
		ast_log(LOG_WARNING, "Could not find application (%s)\n", feature->app);
		res = -2;
	}
	
	return FEATURE_RETURN_SUCCESS;
}

static void unmap_features(void)
{
	int x;
	for (x = 0; x < FEATURES_COUNT; x++)
		strcpy(builtin_features[x].exten, builtin_features[x].default_exten);
}

static int remap_feature(const char *name, const char *value)
{
	int x;
	int res = -1;
	for (x = 0; x < FEATURES_COUNT; x++) {
		if (!strcasecmp(name, builtin_features[x].sname)) {
			ast_copy_string(builtin_features[x].exten, value, sizeof(builtin_features[x].exten));
			if (option_verbose > 1)
				ast_verbose(VERBOSE_PREFIX_2 "Remapping feature %s (%s) to sequence '%s'\n", builtin_features[x].fname, builtin_features[x].sname, builtin_features[x].exten);
			res = 0;
		} else if (!strcmp(value, builtin_features[x].exten)) 
			ast_log(LOG_WARNING, "Sequence '%s' already mapped to function %s (%s) while assigning to %s\n", value, builtin_features[x].fname, builtin_features[x].sname, name);
	}
	return res;
}

static int ast_feature_interpret(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, char *code, int sense)
{
	int x;
	struct ast_flags features;
	int res = FEATURE_RETURN_PASSDIGITS;
	struct ast_call_feature *feature;
	char *dynamic_features=pbx_builtin_getvar_helper(chan,"DYNAMIC_FEATURES");

	if (sense == FEATURE_SENSE_CHAN)
		ast_copy_flags(&features, &(config->features_caller), AST_FLAGS_ALL);	
	else
		ast_copy_flags(&features, &(config->features_callee), AST_FLAGS_ALL);	
	ast_log(LOG_DEBUG, "Feature interpret: chan=%s, peer=%s, sense=%d, features=%d\n", chan->name, peer->name, sense, features.flags);
	for (x=0; x < FEATURES_COUNT; x++) {
		if ((ast_test_flag(&features, builtin_features[x].feature_mask)) &&
		    !ast_strlen_zero(builtin_features[x].exten)) {
			/* Feature is up for consideration */
			if (!strcmp(builtin_features[x].exten, code)) {
				res = builtin_features[x].operation(chan, peer, config, code, sense);
				break;
			} else if (!strncmp(builtin_features[x].exten, code, strlen(code))) {
				if (res == FEATURE_RETURN_PASSDIGITS)
				  res = FEATURE_RETURN_STOREDIGITS;
			}
		}
	}


	if (dynamic_features) {
		char *tmp=strdup(dynamic_features);
		char *tok;
		char *begin=tmp;
		
		if (!tmp) {
			ast_log(LOG_ERROR,"strdup failed");
			return res;
		}
		
		while ( (tok=strsep(&tmp,"#")) != NULL) {
			AST_LIST_LOCK(&feature_list);
			AST_LIST_TRAVERSE(&feature_list, feature, feature_entry) {
				if ( ! strcasecmp(tok,feature->sname))
					break;
			}
			AST_LIST_UNLOCK(&feature_list);			
			
			if ( feature ) {
				/* Feature is up for consideration */
				if (!strcmp(feature->exten, code)) {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 " Feature Found: %s exten: %s\n",feature->sname, tok);
					res = feature->operation(chan, peer, config, code, sense);
					break;
				} else if (!strncmp(feature->exten, code, strlen(code))) {
					res = FEATURE_RETURN_STOREDIGITS;
				}
			}
		}
		
		free(begin);
	}
	
	return res;
}

static void set_config_flags(struct ast_bridge_config *config)
{
	int x;

	ast_clear_flag(config, AST_FLAGS_ALL);	
	for (x = 0; x < FEATURES_COUNT; x++) {
		if (ast_test_flag(&(config->features_caller), builtin_features[x].feature_mask)) {
			if (ast_test_flag(builtin_features + x, AST_FEATURE_FLAG_NEEDSDTMF))
				ast_set_flag(config, AST_BRIDGE_DTMF_CHANNEL_0);
		}
		if (ast_test_flag(&(config->features_callee), builtin_features[x].feature_mask)) {
			if (ast_test_flag(builtin_features + x, AST_FEATURE_FLAG_NEEDSDTMF))
				ast_set_flag(config, AST_BRIDGE_DTMF_CHANNEL_1);
		}
	}
}


static struct ast_channel *ast_feature_request_and_dial(struct ast_channel *caller, const char *type, int format, void *data, int timeout, int *outstate, const char *cid_num, const char *cid_name)
{
	int state = 0;
	int cause = 0;
	int to;
	struct ast_channel *chan;
	struct ast_channel *monitor_chans[2];
	struct ast_channel *active_channel;
	struct ast_frame *f = NULL;
	int res = 0, ready = 0;
	
	if ((chan = ast_request(type, format, data, &cause))) {
		ast_set_callerid(chan, cid_num, cid_name, cid_num);
		
		if (!ast_call(chan, data, timeout)) {
			struct timeval started;
			int x, len = 0;
			char *disconnect_code = NULL, *dialed_code = NULL;

			ast_indicate(caller, AST_CONTROL_RINGING);
			/* support dialing of the featuremap disconnect code while performing an attended tranfer */
			for (x=0; x < FEATURES_COUNT; x++) {
				if (strcasecmp(builtin_features[x].sname, "disconnect"))
					continue;

				disconnect_code = builtin_features[x].exten;
				len = strlen(disconnect_code) + 1;
				dialed_code = alloca(len);
				memset(dialed_code, 0, len);
				break;
			}
			x = 0;
			started = ast_tvnow();
			to = timeout;
			while (!ast_check_hangup(caller) && timeout && (chan->_state != AST_STATE_UP)) {
				monitor_chans[0] = caller;
				monitor_chans[1] = chan;
				active_channel = ast_waitfor_n(monitor_chans, 2, &to);

				/* see if the timeout has been violated */
				if(ast_tvdiff_ms(ast_tvnow(), started) > timeout) {
					state = AST_CONTROL_UNHOLD;
					ast_log(LOG_NOTICE, "We exceeded our AT-timeout\n");
					break; /*doh! timeout*/
				}

				if (!active_channel) {
					continue;
				}

				if (chan && (chan == active_channel)){
					f = ast_read(chan);
					if (f == NULL) { /*doh! where'd he go?*/
						state = AST_CONTROL_HANGUP;
						res = 0;
						break;
					}
					
					if (f->frametype == AST_FRAME_CONTROL || f->frametype == AST_FRAME_DTMF || f->frametype == AST_FRAME_TEXT) {
						if (f->subclass == AST_CONTROL_RINGING) {
							state = f->subclass;
							if (option_verbose > 2)
								ast_verbose( VERBOSE_PREFIX_3 "%s is ringing\n", chan->name);
							ast_indicate(caller, AST_CONTROL_RINGING);
						} else if ((f->subclass == AST_CONTROL_BUSY) || (f->subclass == AST_CONTROL_CONGESTION)) {
							state = f->subclass;
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
						} else {
							ast_log(LOG_NOTICE, "Don't know what to do about control frame: %d\n", f->subclass);
						}
						/* else who cares */
					}

				} else if (caller && (active_channel == caller)) {
					f = ast_read(caller);
					if (f == NULL) { /*doh! where'd he go?*/
						if (caller->_softhangup && !chan->_softhangup) {
							/* make this a blind transfer */
							ready = 1;
							break;
						}
						state = AST_CONTROL_HANGUP;
						res = 0;
						break;
					}
					
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
				if (f) {
					ast_frfree(f);
				}
			}
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

	if (chan && res <= 0) {
		if (!chan->cdr) {
			chan->cdr = ast_cdr_alloc();
		}
		if (chan->cdr) {
			char tmp[256];
			ast_cdr_init(chan->cdr, chan);
			snprintf(tmp, 256, "%s/%s", type, (char *)data);
			ast_cdr_setapp(chan->cdr,"Dial",tmp);
			ast_cdr_update(chan);
			ast_cdr_start(chan->cdr);
			ast_cdr_end(chan->cdr);
			/* If the cause wasn't handled properly */
			if (ast_cdr_disposition(chan->cdr,chan->hangupcause))
				ast_cdr_failed(chan->cdr);
		} else {
			ast_log(LOG_WARNING, "Unable to create Call Detail Record\n");
		}
	}
	
	return chan;
}

int ast_bridge_call(struct ast_channel *chan,struct ast_channel *peer,struct ast_bridge_config *config)
{
	/* Copy voice back and forth between the two channels.  Give the peer
	   the ability to transfer calls with '#<extension' syntax. */
	struct ast_frame *f;
	struct ast_channel *who;
	char chan_featurecode[FEATURE_MAX_LEN + 1]="";
	char peer_featurecode[FEATURE_MAX_LEN + 1]="";
	int res;
	int diff;
	int hasfeatures=0;
	int hadfeatures=0;
	struct ast_option_header *aoh;
	struct timeval start = { 0 , 0 };
	struct ast_bridge_config backup_config;
	int allowdisconnect_in, allowdisconnect_out, allowredirect_in, allowredirect_out;
	char *monitor_exec;

	memset(&backup_config, 0, sizeof(backup_config));

	config->start_time = ast_tvnow();

	if (chan && peer) {
		pbx_builtin_setvar_helper(chan, "BRIDGEPEER", peer->name);
		pbx_builtin_setvar_helper(peer, "BRIDGEPEER", chan->name);
	} else if (chan)
		pbx_builtin_setvar_helper(chan, "BLINDTRANSFER", NULL);

	if (monitor_ok) {
		if (!monitor_app) { 
			if (!(monitor_app = pbx_findapp("Monitor")))
				monitor_ok=0;
		}
		if ((monitor_exec = pbx_builtin_getvar_helper(chan, "AUTO_MONITOR"))) 
			pbx_exec(chan, monitor_app, monitor_exec, 1);
		else if ((monitor_exec = pbx_builtin_getvar_helper(peer, "AUTO_MONITOR")))
			pbx_exec(peer, monitor_app, monitor_exec, 1);
	}
	
	allowdisconnect_in = ast_test_flag(&(config->features_callee), AST_FEATURE_DISCONNECT);
	allowdisconnect_out = ast_test_flag(&(config->features_caller), AST_FEATURE_DISCONNECT);
	allowredirect_in = ast_test_flag(&(config->features_callee), AST_FEATURE_REDIRECT);
	allowredirect_out = ast_test_flag(&(config->features_caller), AST_FEATURE_REDIRECT);
	set_config_flags(config);
	config->firstpass = 1;

	/* Answer if need be */
	if (ast_answer(chan))
		return -1;
	peer->appl = "Bridged Call";
	peer->data = chan->name;

	/* copy the userfield from the B-leg to A-leg if applicable */
	if (chan->cdr && peer->cdr && !ast_strlen_zero(peer->cdr->userfield)) {
		char tmp[256];
		if (!ast_strlen_zero(chan->cdr->userfield)) {
			snprintf(tmp, sizeof(tmp), "%s;%s", chan->cdr->userfield, peer->cdr->userfield);
			ast_cdr_appenduserfield(chan, tmp);
		} else
			ast_cdr_setuserfield(chan, peer->cdr->userfield);
		/* free the peer's cdr without ast_cdr_free complaining */
		free(peer->cdr);
		peer->cdr = NULL;
	}
	for (;;) {
		if (config->feature_timer)
			start = ast_tvnow();

		res = ast_channel_bridge(chan, peer, config, &f, &who);

		if (config->feature_timer) {
			/* Update time limit for next pass */
			diff = ast_tvdiff_ms(ast_tvnow(), start);
			config->feature_timer -= diff;
			if (hasfeatures) {
				/* Running on backup config, meaning a feature might be being
				   activated, but that's no excuse to keep things going 
				   indefinitely! */
				if (backup_config.feature_timer && ((backup_config.feature_timer -= diff) <= 0)) {
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
			ast_log(LOG_WARNING, "Bridge failed on channels %s and %s\n", chan->name, peer->name);
			return -1;
		}
		
		if (!f || ((f->frametype == AST_FRAME_CONTROL) && ((f->subclass == AST_CONTROL_HANGUP) || (f->subclass == AST_CONTROL_BUSY) || 
			(f->subclass == AST_CONTROL_CONGESTION)))) {
				res = -1;
				break;
		}
		if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_RINGING)) {
			if (who == chan)
				ast_indicate(peer, AST_CONTROL_RINGING);
			else
				ast_indicate(chan, AST_CONTROL_RINGING);
		}
		if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == -1)) {
			if (who == chan)
				ast_indicate(peer, -1);
			else
				ast_indicate(chan, -1);
		}
		if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_FLASH)) {
			if (who == chan)
				ast_indicate(peer, AST_CONTROL_FLASH);
			else
				ast_indicate(chan, AST_CONTROL_FLASH);
		}
		if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_OPTION)) {
			aoh = f->data;
			/* Forward option Requests */
			if (aoh && (aoh->flag == AST_OPTION_FLAG_REQUEST)) {
				if (who == chan)
					ast_channel_setoption(peer, ntohs(aoh->option), aoh->data, f->datalen - sizeof(struct ast_option_header), 0);
				else
					ast_channel_setoption(chan, ntohs(aoh->option), aoh->data, f->datalen - sizeof(struct ast_option_header), 0);
			}
		}
		/* check for '*', if we find it it's time to disconnect */
		if (f && (f->frametype == AST_FRAME_DTMF)) {
			char *featurecode;
			int sense;
			struct ast_channel *other;

			hadfeatures = hasfeatures;
			/* This cannot overrun because the longest feature is one shorter than our buffer */
			if (who == chan) {
				other = peer;
				sense = FEATURE_SENSE_CHAN;
				featurecode = chan_featurecode;
			} else  {
				other = chan;
				sense = FEATURE_SENSE_PEER;
				featurecode = peer_featurecode;
			}
			featurecode[strlen(featurecode)] = f->subclass;
			config->feature_timer = backup_config.feature_timer;
			res = ast_feature_interpret(chan, peer, config, featurecode, sense);
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
			} else {
				ast_frfree(f);
				break;
			}
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
				config->feature_timer = featuredigittimeout;
				ast_log(LOG_DEBUG, "Set time limit to %ld\n", config->feature_timer);
			}
		}
		if (f)
			ast_frfree(f);
	}
	return res;
}

static void *do_parking_thread(void *ignore)
{
	int ms, tms, max;
	struct parkeduser *pu, *pl, *pt = NULL;
	struct timeval tv;
	struct ast_frame *f;
	char exten[AST_MAX_EXTENSION];
	char *peername,*cp;
	char returnexten[AST_MAX_EXTENSION];
	struct ast_context *con;
	int x;
	fd_set rfds, efds;
	fd_set nrfds, nefds;
	FD_ZERO(&rfds);
	FD_ZERO(&efds);

	for (;;) {
		ms = -1;
		max = -1;
		ast_mutex_lock(&parking_lock);
		pl = NULL;
		pu = parkinglot;
		FD_ZERO(&nrfds);
		FD_ZERO(&nefds);
		while(pu) {
			if (pu->notquiteyet) {
				/* Pretend this one isn't here yet */
				pl = pu;
				pu = pu->next;
				continue;
			}
			tms = ast_tvdiff_ms(ast_tvnow(), pu->start);
			if (tms > pu->parkingtime) {
				/* Stop music on hold */
				ast_moh_stop(pu->chan);
				ast_indicate(pu->chan, AST_CONTROL_UNHOLD);
				/* Get chan, exten from derived kludge */
				if (pu->peername[0]) {
					peername = ast_strdupa(pu->peername);
					cp = strrchr(peername, '-');
					if (cp) 
						*cp = 0;
					con = ast_context_find(parking_con_dial);
					if (!con) {
						con = ast_context_create(NULL, parking_con_dial, registrar);
						if (!con) {
							ast_log(LOG_ERROR, "Parking dial context '%s' does not exist and unable to create\n", parking_con_dial);
						}
					}
					if (con) {
						snprintf(returnexten, sizeof(returnexten), "%s||t", peername);
						ast_add_extension2(con, 1, peername, 1, NULL, NULL, "Dial", strdup(returnexten), FREE, registrar);
					}
					ast_copy_string(pu->chan->exten, peername, sizeof(pu->chan->exten));
					ast_copy_string(pu->chan->context, parking_con_dial, sizeof(pu->chan->context));
					pu->chan->priority = 1;

				} else {
					/* They've been waiting too long, send them back to where they came.  Theoretically they
					   should have their original extensions and such, but we copy to be on the safe side */
					ast_copy_string(pu->chan->exten, pu->exten, sizeof(pu->chan->exten));
					ast_copy_string(pu->chan->context, pu->context, sizeof(pu->chan->context));
					pu->chan->priority = pu->priority;
				}

				manager_event(EVENT_FLAG_CALL, "ParkedCallTimeOut",
					"Exten: %d\r\n"
					"Channel: %s\r\n"
					"CallerID: %s\r\n"
					"CallerIDName: %s\r\n\r\n"
					,pu->parkingnum, pu->chan->name
					,(pu->chan->cid.cid_num ? pu->chan->cid.cid_num : "<unknown>")
					,(pu->chan->cid.cid_name ? pu->chan->cid.cid_name : "<unknown>")
					);

				if (option_verbose > 1) 
					ast_verbose(VERBOSE_PREFIX_2 "Timeout for %s parked on %d. Returning to %s,%s,%d\n", pu->chan->name, pu->parkingnum, pu->chan->context, pu->chan->exten, pu->chan->priority);
				/* Start up the PBX, or hang them up */
				if (ast_pbx_start(pu->chan))  {
					ast_log(LOG_WARNING, "Unable to restart the PBX for user on '%s', hanging them up...\n", pu->chan->name);
					ast_hangup(pu->chan);
				}
				/* And take them out of the parking lot */
				if (pl) 
					pl->next = pu->next;
				else
					parkinglot = pu->next;
				pt = pu;
				pu = pu->next;
				con = ast_context_find(parking_con);
				if (con) {
					snprintf(exten, sizeof(exten), "%d", pt->parkingnum);
					if (ast_context_remove_extension2(con, exten, 1, NULL))
						ast_log(LOG_WARNING, "Whoa, failed to remove the extension!\n");
				} else
					ast_log(LOG_WARNING, "Whoa, no parking context?\n");
				free(pt);
			} else {
				for (x = 0; x < AST_MAX_FDS; x++) {
					if ((pu->chan->fds[x] > -1) && (FD_ISSET(pu->chan->fds[x], &rfds) || FD_ISSET(pu->chan->fds[x], &efds))) {
						if (FD_ISSET(pu->chan->fds[x], &efds))
							ast_set_flag(pu->chan, AST_FLAG_EXCEPTION);
						else
							ast_clear_flag(pu->chan, AST_FLAG_EXCEPTION);
						pu->chan->fdno = x;
						/* See if they need servicing */
						f = ast_read(pu->chan);
						if (!f || ((f->frametype == AST_FRAME_CONTROL) && (f->subclass ==  AST_CONTROL_HANGUP))) {

							manager_event(EVENT_FLAG_CALL, "ParkedCallGiveUp",
								"Exten: %d\r\n"
								"Channel: %s\r\n"
								"CallerID: %s\r\n"
								"CallerIDName: %s\r\n\r\n"
								,pu->parkingnum, pu->chan->name
								,(pu->chan->cid.cid_num ? pu->chan->cid.cid_num : "<unknown>")
								,(pu->chan->cid.cid_name ? pu->chan->cid.cid_name : "<unknown>")
								);

							/* There's a problem, hang them up*/
							if (option_verbose > 1) 
								ast_verbose(VERBOSE_PREFIX_2 "%s got tired of being parked\n", pu->chan->name);
							ast_hangup(pu->chan);
							/* And take them out of the parking lot */
							if (pl) 
								pl->next = pu->next;
							else
								parkinglot = pu->next;
							pt = pu;
							pu = pu->next;
							con = ast_context_find(parking_con);
							if (con) {
								snprintf(exten, sizeof(exten), "%d", pt->parkingnum);
								if (ast_context_remove_extension2(con, exten, 1, NULL))
									ast_log(LOG_WARNING, "Whoa, failed to remove the extension!\n");
							} else
								ast_log(LOG_WARNING, "Whoa, no parking context?\n");
							free(pt);
							break;
						} else {
							/* XXX Maybe we could do something with packets, like dial "0" for operator or something XXX */
							ast_frfree(f);
							if (pu->moh_trys < 3 && !pu->chan->generatordata) {
								ast_log(LOG_DEBUG, "MOH on parked call stopped by outside source.  Restarting.\n");
								ast_moh_start(pu->chan, NULL);
								pu->moh_trys++;
							}
							goto std;	/* XXX Ick: jumping into an else statement??? XXX */
						}
					}
				}
				if (x >= AST_MAX_FDS) {
std:					for (x=0; x<AST_MAX_FDS; x++) {
						/* Keep this one for next one */
						if (pu->chan->fds[x] > -1) {
							FD_SET(pu->chan->fds[x], &nrfds);
							FD_SET(pu->chan->fds[x], &nefds);
							if (pu->chan->fds[x] > max)
								max = pu->chan->fds[x];
						}
					}
					/* Keep track of our longest wait */
					if ((tms < ms) || (ms < 0))
						ms = tms;
					pl = pu;
					pu = pu->next;
				}
			}
		}
		ast_mutex_unlock(&parking_lock);
		rfds = nrfds;
		efds = nefds;
		tv = ast_samp2tv(ms, 1000);
		/* Wait for something to happen */
		ast_select(max + 1, &rfds, NULL, &efds, (ms > -1) ? &tv : NULL);
		pthread_testcancel();
	}
	return NULL;	/* Never reached */
}

static int park_call_exec(struct ast_channel *chan, void *data)
{
	/* Data is unused at the moment but could contain a parking
	   lot context eventually */
	int res=0;
	struct localuser *u;
	LOCAL_USER_ADD(u);
	/* Setup the exten/priority to be s/1 since we don't know
	   where this call should return */
	strcpy(chan->exten, "s");
	chan->priority = 1;
	if (chan->_state != AST_STATE_UP)
		res = ast_answer(chan);
	if (!res)
		res = ast_safe_sleep(chan, 1000);
	if (!res)
		res = ast_park_call(chan, chan, 0, NULL);
	LOCAL_USER_REMOVE(u);
	if (!res)
		res = AST_PBX_KEEPALIVE;
	return res;
}

static int park_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	struct ast_channel *peer=NULL;
	struct parkeduser *pu, *pl=NULL;
	char exten[AST_MAX_EXTENSION];
	struct ast_context *con;
	int park;
	int dres;
	struct ast_bridge_config config;

	if (!data) {
		ast_log(LOG_WARNING, "Park requires an argument (extension number)\n");
		return -1;
	}
	LOCAL_USER_ADD(u);
	park = atoi((char *)data);
	ast_mutex_lock(&parking_lock);
	pu = parkinglot;
	while(pu) {
		if (pu->parkingnum == park) {
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
			snprintf(exten, sizeof(exten), "%d", pu->parkingnum);
			if (ast_context_remove_extension2(con, exten, 1, NULL))
				ast_log(LOG_WARNING, "Whoa, failed to remove the extension!\n");
		} else
			ast_log(LOG_WARNING, "Whoa, no parking context?\n");

		manager_event(EVENT_FLAG_CALL, "UnParkedCall",
			"Exten: %d\r\n"
			"Channel: %s\r\n"
			"From: %s\r\n"
			"CallerID: %s\r\n"
			"CallerIDName: %s\r\n\r\n"
			,pu->parkingnum, pu->chan->name, chan->name
			,(pu->chan->cid.cid_num ? pu->chan->cid.cid_num : "<unknown>")
			,(pu->chan->cid.cid_name ? pu->chan->cid.cid_name : "<unknown>")
			);

		free(pu);
	}
	/* JK02: it helps to answer the channel if not already up */
	if (chan->_state != AST_STATE_UP) {
		ast_answer(chan);
	}

	if (peer) {
		/* Play a courtesy beep in the calling channel to prefix the bridge connecting */	
		if (!ast_strlen_zero(courtesytone)) {
			if (!ast_streamfile(chan, courtesytone, chan->language)) {
				if (ast_waitstream(chan, "") < 0) {
					ast_log(LOG_WARNING, "Failed to play courtesy tone!\n");
					ast_hangup(peer);
					return -1;
				}
			}
		}
 
		ast_moh_stop(peer);
		ast_indicate(peer, AST_CONTROL_UNHOLD);
		res = ast_channel_make_compatible(chan, peer);
		if (res < 0) {
			ast_log(LOG_WARNING, "Could not make channels %s and %s compatible for bridge\n", chan->name, peer->name);
			ast_hangup(peer);
			return -1;
		}
		/* This runs sorta backwards, since we give the incoming channel control, as if it
		   were the person called. */
		if (option_verbose > 2) 
			ast_verbose(VERBOSE_PREFIX_3 "Channel %s connected to parked call %d\n", chan->name, park);

		memset(&config, 0, sizeof(struct ast_bridge_config));
		ast_set_flag(&(config.features_callee), AST_FEATURE_REDIRECT);
		ast_set_flag(&(config.features_caller), AST_FEATURE_REDIRECT);
		config.timelimit = 0;
		config.play_warning = 0;
		config.warning_freq = 0;
		config.warning_sound=NULL;
		res = ast_bridge_call(chan, peer, &config);

		/* Simulate the PBX hanging up */
		if (res != AST_PBX_NO_HANGUP_PEER)
			ast_hangup(peer);
		return res;
	} else {
		/* XXX Play a message XXX */
		dres = ast_streamfile(chan, "pbx-invalidpark", chan->language);
		if (!dres)
	    		dres = ast_waitstream(chan, "");
		else {
			ast_log(LOG_WARNING, "ast_streamfile of %s failed on %s\n", "pbx-invalidpark", chan->name);
			dres = 0;
		}
		if (option_verbose > 2) 
			ast_verbose(VERBOSE_PREFIX_3 "Channel %s tried to talk to nonexistent parked call %d\n", chan->name, park);
		res = -1;
	}
	LOCAL_USER_REMOVE(u);
	return res;
}

static int handle_showfeatures(int fd, int argc, char *argv[])
{
	int i;
	int fcount;
	struct ast_call_feature *feature;
	char format[] = "%-25s %-7s %-7s\n";

	ast_cli(fd, format, "Builtin Feature", "Default", "Current");
	ast_cli(fd, format, "---------------", "-------", "-------");

	ast_cli(fd, format, "Pickup", "*8", ast_pickup_ext());		/* default hardcoded above, so we'll hardcode it here */

	fcount = sizeof(builtin_features) / sizeof(builtin_features[0]);

	for (i = 0; i < fcount; i++)
	{
		ast_cli(fd, format, builtin_features[i].fname, builtin_features[i].default_exten, builtin_features[i].exten);
	}
	ast_cli(fd, "\n");
	ast_cli(fd, format, "Dynamic Feature", "Default", "Current");
	ast_cli(fd, format, "---------------", "-------", "-------");
	if (AST_LIST_EMPTY(&feature_list)) {
		ast_cli(fd, "(none)\n");
	}
	else {
		AST_LIST_LOCK(&feature_list);
		AST_LIST_TRAVERSE(&feature_list, feature, feature_entry) {
			ast_cli(fd, format, feature->sname, "no def", feature->exten);	
		}
		AST_LIST_UNLOCK(&feature_list);
	}
	
	return RESULT_SUCCESS;
}

static char showfeatures_help[] =
"Usage: show features\n"
"       Lists currently configured features.\n";

static struct ast_cli_entry showfeatures =
{ { "show", "features", NULL }, handle_showfeatures, "Lists configured features", showfeatures_help };

static int handle_parkedcalls(int fd, int argc, char *argv[])
{
	struct parkeduser *cur;
	int numparked = 0;

	ast_cli(fd, "%4s %25s (%-15s %-12s %-4s) %-6s \n", "Num", "Channel"
		, "Context", "Extension", "Pri", "Timeout");

	ast_mutex_lock(&parking_lock);

	cur = parkinglot;
	while(cur) {
		ast_cli(fd, "%4d %25s (%-15s %-12s %-4d) %6lds\n"
			,cur->parkingnum, cur->chan->name, cur->context, cur->exten
			,cur->priority, cur->start.tv_sec + (cur->parkingtime/1000) - time(NULL));

		cur = cur->next;
		numparked++;
	}
	ast_cli(fd, "%d parked call%s.\n", numparked, (numparked != 1) ? "s" : "");

	ast_mutex_unlock(&parking_lock);

	return RESULT_SUCCESS;
}

static char showparked_help[] =
"Usage: show parkedcalls\n"
"       Lists currently parked calls.\n";

static struct ast_cli_entry showparked =
{ { "show", "parkedcalls", NULL }, handle_parkedcalls, "Lists parked calls", showparked_help };

/* Dump lot status */
static int manager_parking_status( struct mansession *s, struct message *m )
{
	struct parkeduser *cur;
	char *id = astman_get_header(m,"ActionID");
	char idText[256] = "";

	if (id && !ast_strlen_zero(id))
		snprintf(idText,256,"ActionID: %s\r\n",id);

	astman_send_ack(s, m, "Parked calls will follow");

        ast_mutex_lock(&parking_lock);

        cur=parkinglot;
        while(cur) {
			ast_mutex_lock(&s->lock);
                ast_cli(s->fd, "Event: ParkedCall\r\n"
			"Exten: %d\r\n"
			"Channel: %s\r\n"
			"Timeout: %ld\r\n"
			"CallerID: %s\r\n"
			"CallerIDName: %s\r\n"
			"%s"
			"\r\n"
                        ,cur->parkingnum, cur->chan->name
                        ,(long)cur->start.tv_sec + (long)(cur->parkingtime/1000) - (long)time(NULL)
			,(cur->chan->cid.cid_num ? cur->chan->cid.cid_num : "")
			,(cur->chan->cid.cid_name ? cur->chan->cid.cid_name : "")
			,idText);
			ast_mutex_unlock(&s->lock);

            cur = cur->next;
        }

	ast_cli(s->fd,
	"Event: ParkedCallsComplete\r\n"
	"%s"
	"\r\n",idText);

        ast_mutex_unlock(&parking_lock);

        return RESULT_SUCCESS;
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
		ast_mutex_unlock(&cur->lock);
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
		ast_mutex_unlock(&cur->lock);
	} else	{
		if (option_debug)
			ast_log(LOG_DEBUG, "No call pickup possible...\n");
	}
	return res;
}

static int load_config(void) 
{
	int start = 0, end = 0;
	struct ast_context *con = NULL;
	struct ast_config *cfg = NULL;
	struct ast_variable *var = NULL;
	
	transferdigittimeout = DEFAULT_TRANSFER_DIGIT_TIMEOUT;
	featuredigittimeout = DEFAULT_FEATURE_DIGIT_TIMEOUT;

	cfg = ast_config_load("features.conf");
	if (!cfg) {
		cfg = ast_config_load("parking.conf");
		if (cfg)
			ast_log(LOG_NOTICE, "parking.conf is deprecated in favor of 'features.conf'.  Please rename it.\n");
	}
	if (cfg) {
		var = ast_variable_browse(cfg, "general");
		while(var) {
			if (!strcasecmp(var->name, "parkext")) {
				ast_copy_string(parking_ext, var->value, sizeof(parking_ext));
			} else if (!strcasecmp(var->name, "context")) {
				ast_copy_string(parking_con, var->value, sizeof(parking_con));
			} else if (!strcasecmp(var->name, "parkingtime")) {
				if ((sscanf(var->value, "%d", &parkingtime) != 1) || (parkingtime < 1)) {
					ast_log(LOG_WARNING, "%s is not a valid parkingtime\n", var->value);
					parkingtime = DEFAULT_PARK_TIME;
				} else
					parkingtime = parkingtime * 1000;
			} else if (!strcasecmp(var->name, "parkpos")) {
				if (sscanf(var->value, "%d-%d", &start, &end) != 2) {
					ast_log(LOG_WARNING, "Format for parking positions is a-b, where a and b are numbers at line %d of parking.conf\n", var->lineno);
				} else {
					parking_start = start;
					parking_stop = end;
				}
			} else if (!strcasecmp(var->name, "findslot")) {
				parkfindnext = (!strcasecmp(var->value, "next"));
			} else if (!strcasecmp(var->name, "adsipark")) {
				adsipark = ast_true(var->value);
			} else if (!strcasecmp(var->name, "transferdigittimeout")) {
				if ((sscanf(var->value, "%d", &transferdigittimeout) != 1) || (transferdigittimeout < 1)) {
					ast_log(LOG_WARNING, "%s is not a valid transferdigittimeout\n", var->value);
					transferdigittimeout = DEFAULT_TRANSFER_DIGIT_TIMEOUT;
				} else
					transferdigittimeout = transferdigittimeout * 1000;
			} else if (!strcasecmp(var->name, "featuredigittimeout")) {
				if ((sscanf(var->value, "%d", &featuredigittimeout) != 1) || (featuredigittimeout < 1)) {
					ast_log(LOG_WARNING, "%s is not a valid featuredigittimeout\n", var->value);
					featuredigittimeout = DEFAULT_FEATURE_DIGIT_TIMEOUT;
				}
			} else if (!strcasecmp(var->name, "courtesytone")) {
				ast_copy_string(courtesytone, var->value, sizeof(courtesytone));
			} else if (!strcasecmp(var->name, "xfersound")) {
				ast_copy_string(xfersound, var->value, sizeof(xfersound));
			} else if (!strcasecmp(var->name, "xferfailsound")) {
				ast_copy_string(xferfailsound, var->value, sizeof(xferfailsound));
			} else if (!strcasecmp(var->name, "pickupexten")) {
				ast_copy_string(pickup_ext, var->value, sizeof(pickup_ext));
			}
			var = var->next;
		}

		unmap_features();
		var = ast_variable_browse(cfg, "featuremap");
		while(var) {
			if (remap_feature(var->name, var->value))
				ast_log(LOG_NOTICE, "Unknown feature '%s'\n", var->name);
			var = var->next;
		}

		/* Map a key combination to an application*/
		ast_unregister_features();
		var = ast_variable_browse(cfg, "applicationmap");
		while(var) {
			char *tmp_val=strdup(var->value);
			char *exten, *party=NULL, *app=NULL, *app_args=NULL; 

			if (!tmp_val) { 
				ast_log(LOG_ERROR, "res_features: strdup failed");
				continue;
			}
			

			exten=strsep(&tmp_val,",");
			if (exten) party=strsep(&tmp_val,",");
			if (party) app=strsep(&tmp_val,",");

			if (app) app_args=strsep(&tmp_val,",");

			if (!(app && strlen(app)) || !(exten && strlen(exten)) || !(party && strlen(party)) || !(var->name && strlen(var->name))) {
				ast_log(LOG_NOTICE, "Please check the feature Mapping Syntax, either extension, name, or app aren't provided %s %s %s %s\n",app,exten,party,var->name);
				free(tmp_val);
				var = var->next;
				continue;
			}

			{
				struct ast_call_feature *feature=find_feature(var->name);
				int mallocd=0;
				
				if (!feature) {
					feature=malloc(sizeof(struct ast_call_feature));
					mallocd=1;
				}
				if (!feature) {
					ast_log(LOG_NOTICE, "Malloc failed at feature mapping\n");
					free(tmp_val);
					var = var->next;
					continue;
				}

				memset(feature,0,sizeof(struct ast_call_feature));
				ast_copy_string(feature->sname,var->name,FEATURE_SNAME_LEN);
				ast_copy_string(feature->app,app,FEATURE_APP_LEN);
				ast_copy_string(feature->exten, exten,FEATURE_EXTEN_LEN);
				free(tmp_val);
				
				if (app_args) 
					ast_copy_string(feature->app_args,app_args,FEATURE_APP_ARGS_LEN);
				
				ast_copy_string(feature->exten, exten,sizeof(feature->exten));
				feature->operation=feature_exec_app;
				ast_set_flag(feature,AST_FEATURE_FLAG_NEEDSDTMF);
				
				if (!strcasecmp(party,"caller"))
					ast_set_flag(feature,AST_FEATURE_FLAG_CALLER);
				else
					ast_set_flag(feature,AST_FEATURE_FLAG_CALLEE);

				ast_register_feature(feature);
				
				if (option_verbose >=1) ast_verbose(VERBOSE_PREFIX_2 "Mapping Feature '%s' to app '%s' with code '%s'\n", var->name, app, exten);  
			}
			var = var->next;
		}	 
	}
	ast_config_destroy(cfg);

	
	if (con)
		ast_context_remove_extension2(con, ast_parking_ext(), 1, registrar);
	
	if (!(con = ast_context_find(parking_con))) {
		if (!(con = ast_context_create(NULL, parking_con, registrar))) {
			ast_log(LOG_ERROR, "Parking context '%s' does not exist and unable to create\n", parking_con);
			return -1;
		}
	}
	return ast_add_extension2(con, 1, ast_parking_ext(), 1, NULL, NULL, parkcall, strdup(""), FREE, registrar);
}

int reload(void) {
	return load_config();
}

int load_module(void)
{
	int res;
	
	AST_LIST_HEAD_INIT(&feature_list);

	if ((res = load_config()))
		return res;
	ast_cli_register(&showparked);
	ast_cli_register(&showfeatures);
	ast_pthread_create(&parking_thread, NULL, do_parking_thread, NULL);
	res = ast_register_application(parkedcall, park_exec, synopsis, descrip);
	if (!res)
		res = ast_register_application(parkcall, park_call_exec, synopsis2, descrip2);
	if (!res) {
		ast_manager_register("ParkedCalls", 0, manager_parking_status, "List parked calls" );
	}
	return res;
}


int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;

	ast_manager_unregister("ParkedCalls");
	ast_cli_unregister(&showfeatures);
	ast_cli_unregister(&showparked);
	ast_unregister_application(parkcall);
	return ast_unregister_application(parkedcall);
}

char *description(void)
{
	return "Call Features Resource";
}

int usecount(void)
{
	/* Never allow parking to be unloaded because it will
	   unresolve needed symbols in the dialer */
#if 0
	int res;
	STANDARD_USECOUNT(res);
	return res;
#else
	return 1;
#endif
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
