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

#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/module.h>
#include <asterisk/translate.h>
#include <asterisk/app.h>
#include <asterisk/say.h>
#include <asterisk/features.h>
#include <asterisk/musiconhold.h>
#include <asterisk/config.h>
#include <asterisk/cli.h>
#include <asterisk/manager.h>
#include <asterisk/utils.h>
#include <asterisk/adsi.h>
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

#define DEFAULT_PARK_TIME 45000
#define DEFAULT_TRANSFER_DIGIT_TIMEOUT 3000
#define DEFAULT_FEATURE_DIGIT_TIMEOUT 500

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
	char context[AST_MAX_EXTENSION];
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
	char *message[5] = {NULL, NULL, NULL, NULL, NULL};

	snprintf(tmp, sizeof(tmp), "Parked on %d", parkingnum);
	message[0] = tmp;
	res = adsi_load_session(chan, NULL, 0, 1);
	if (res == -1) {
		return res;
	}
	return adsi_print(chan, message, justify, 1);
}

int ast_park_call(struct ast_channel *chan, struct ast_channel *peer, int timeout, int *extout)
{
	/* We put the user in the parking list, then wake up the parking thread to be sure it looks
	   after these channels too */
	struct parkeduser *pu, *cur;
	int x;
	char exten[AST_MAX_EXTENSION];
	struct ast_context *con;
	pu = malloc(sizeof(struct parkeduser));
	if (pu) {
		memset(pu,0,sizeof(struct parkeduser));
		ast_mutex_lock(&parking_lock);
		for (x=parking_start;x<=parking_stop;x++) {
			cur = parkinglot;
			while(cur) {
				if (cur->parkingnum == x) 
					break;
				cur = cur->next;
			}
			if (!cur)
				break;
		}
		if (x <= parking_stop) {
			chan->appl = "Parked Call";
			chan->data = NULL; 

			pu->chan = chan;
			/* Start music on hold */
			if (chan != peer) {
				ast_indicate(pu->chan, AST_CONTROL_HOLD);
				ast_moh_start(pu->chan, NULL);
			}
			gettimeofday(&pu->start, NULL);
			pu->parkingnum = x;
			if (timeout > 0)
				pu->parkingtime = timeout;
			else
				pu->parkingtime = parkingtime;
			if (extout)
				*extout = x;
			if (peer) {
				strncpy(pu->peername,peer->name,sizeof(pu->peername) - 1);
			}
			/* Remember what had been dialed, so that if the parking
			   expires, we try to come back to the same place */
			if (!ast_strlen_zero(chan->macrocontext))
				strncpy(pu->context, chan->macrocontext, sizeof(pu->context)-1);
			else
				strncpy(pu->context, chan->context, sizeof(pu->context)-1);
			if (!ast_strlen_zero(chan->macroexten))
				strncpy(pu->exten, chan->macroexten, sizeof(pu->exten)-1);
			else
				strncpy(pu->exten, chan->exten, sizeof(pu->exten)-1);
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
				ast_verbose(VERBOSE_PREFIX_2 "Parked %s on %d. Will timeout back to %s,%s,%d in %d seconds\n", pu->chan->name, pu->parkingnum, pu->context, pu->exten, pu->priority, (pu->parkingtime/1000));

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
				con = ast_context_create(NULL,parking_con, registrar);
				if (!con) {
					ast_log(LOG_ERROR, "Parking context '%s' does not exist and unable to create\n", parking_con);
				}
			}
			if (con) {
				snprintf(exten, sizeof(exten), "%d", x);
				ast_add_extension2(con, 1, exten, 1, NULL, NULL, parkedcall, strdup(exten), free, registrar);
			}
			if (peer) ast_say_digits(peer, pu->parkingnum, "", peer->language);
			if (pu->notquiteyet) {
				/* Wake up parking thread if we're really done */
				ast_moh_start(pu->chan, NULL);
				pu->notquiteyet = 0;
				pthread_kill(parking_thread, SIGURG);
			}
			return 0;
		} else {
			ast_log(LOG_WARNING, "No more parking spaces\n");
			free(pu);
			ast_mutex_unlock(&parking_lock);
			return -1;
		}
	} else {
		ast_log(LOG_WARNING, "Out of memory\n");
		return -1;
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
		strncpy(chan->context, rchan->context, sizeof(chan->context) - 1);
		strncpy(chan->exten, rchan->exten, sizeof(chan->exten) - 1);
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
#define FEATURE_MAX_LEN		11

static int builtin_automonitor(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, char *code, int sense)
{
	char *touch_monitor = NULL, *caller_chan_id = NULL, *callee_chan_id = NULL, *args = NULL;
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
		touch_monitor = pbx_builtin_getvar_helper(caller_chan, "TOUCH_MONITOR");
		if (!touch_monitor)
			touch_monitor = pbx_builtin_getvar_helper(callee_chan, "TOUCH_MONITOR");
		
		if (touch_monitor) {
			len = strlen(touch_monitor) + 50;
			args = alloca(len);
			snprintf(args, len, "WAV|auto-%ld-%s|m", time(NULL), touch_monitor);
		} else {
			caller_chan_id = ast_strdupa(caller_chan->cid.cid_num ? caller_chan->cid.cid_num : caller_chan->name);
			callee_chan_id = ast_strdupa(callee_chan->cid.cid_num ? callee_chan->cid.cid_num : callee_chan->name);
			len = strlen(caller_chan_id) + strlen(callee_chan_id) + 50;
			args = alloca(len);
			snprintf(args, len, "WAV|auto-%ld-%s-%s|m", time(NULL), caller_chan_id, callee_chan_id);
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

			if (transferer==peer)
				res=AST_PBX_KEEPALIVE;
			else
				res=AST_PBX_NO_HANGUP_PEER;
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
			strncpy(transferee->exten, newext, sizeof(transferee->exten)-1);
			strncpy(transferee->context, transferer_real_context, sizeof(transferee->context)-1);
			transferee->priority = 0;
		}
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
	} else if(res > 0) {
		/* If they've typed a digit already, handle it */
		xferto[0] = (char) res;
	}
	if ((ast_app_dtget(transferer, transferer_real_context, xferto, sizeof(xferto), 100, transferdigittimeout))) {
		cid_num = transferer->cid.cid_num;
		cid_name = transferer->cid.cid_name;
		if (ast_exists_extension(transferer, transferer_real_context,xferto, 1, cid_num)) {
			snprintf(dialstr, sizeof(dialstr), "%s@%s/n", xferto, transferer_real_context);
			if ((newchan = ast_request_and_dial("Local", ast_best_codec(transferer->nativeformats), dialstr,30000, &outstate, cid_num, cid_name))) {
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
				if (newchan->_softhangup || newchan->_state != AST_STATE_UP) {
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
				   ||(ast_waitfordigit(transferee,100) < 0)
				   || (ast_waitfordigit(newchan,100) < 0) 
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
				ast_log(LOG_WARNING, "Unable to create channel Local/%s do you have chan_local?\n",dialstr);
				ast_moh_stop(transferee);
				ast_autoservice_stop(transferee);
				ast_indicate(transferee, AST_CONTROL_UNHOLD);
				if (!ast_strlen_zero(xferfailsound)) {
					res = ast_streamfile(transferer, xferfailsound, transferer->language);
					if (!res && (ast_waitstream(transferer, "") < 0)) {
						return -1;
					}
				}
				return -1;
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

struct ast_call_feature {
	int feature_mask;
	char *fname;
	char *sname;
	char exten[FEATURE_MAX_LEN];
	char default_exten[FEATURE_MAX_LEN];
	int (*operation)(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, char *code, int sense);
	unsigned int flags;
};

/* add atxfer and automon as undefined so you can only use em if you configure them */
#define FEATURES_COUNT (sizeof(builtin_features) / sizeof(builtin_features[0]))
struct ast_call_feature builtin_features[] = 
{
	{ AST_FEATURE_REDIRECT, "Blind Transfer", "blindxfer", "#", "#", builtin_blindtransfer, AST_FEATURE_FLAG_NEEDSDTMF },
	{ AST_FEATURE_REDIRECT, "Attended Transfer", "atxfer", "", "", builtin_atxfer, AST_FEATURE_FLAG_NEEDSDTMF },
	{ AST_FEATURE_AUTOMON, "One Touch Monitor", "automon", "", "", builtin_automonitor, AST_FEATURE_FLAG_NEEDSDTMF },
	{ AST_FEATURE_DISCONNECT, "Disconnect Call", "disconnect", "*", "*", builtin_disconnect, AST_FEATURE_FLAG_NEEDSDTMF },
};

static void unmap_features(void)
{
	int x;
	for (x=0;x<FEATURES_COUNT;x++)
		strcpy(builtin_features[x].exten, builtin_features[x].default_exten);
}

static int remap_feature(const char *name, const char *value)
{
	int x;
	int res = -1;
	for (x=0;x<FEATURES_COUNT;x++) {
		if (!strcasecmp(name, builtin_features[x].sname)) {
			strncpy(builtin_features[x].exten, value, sizeof(builtin_features[x].exten) - 1);
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

	if (sense == FEATURE_SENSE_CHAN)
		ast_copy_flags(&features, &(config->features_caller), AST_FLAGS_ALL);	
	else
		ast_copy_flags(&features, &(config->features_callee), AST_FLAGS_ALL);	
	ast_log(LOG_DEBUG, "Feature interpret: chan=%s, peer=%s, sense=%d, features=%d\n", chan->name, peer->name, sense, features.flags);
	for (x=0;x<FEATURES_COUNT;x++) {
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
	return res;
}

static void set_config_flags(struct ast_bridge_config *config)
{
	int x;
	ast_clear_flag(config, AST_FLAGS_ALL);	
	for (x=0;x<FEATURES_COUNT;x++) {
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
	struct timeval start, end;
	struct ast_bridge_config backup_config;
	int allowdisconnect_in,allowdisconnect_out,allowredirect_in,allowredirect_out;
	char *monitor_exec;

	memset(&backup_config, 0, sizeof(backup_config));

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
			snprintf(tmp, sizeof(tmp), "%s;%s",chan->cdr->userfield, peer->cdr->userfield);
			ast_cdr_appenduserfield(chan, tmp);
		} else
			ast_cdr_setuserfield(chan, peer->cdr->userfield);
		/* free the peer's cdr without ast_cdr_free complaining */
		free(peer->cdr);
		peer->cdr = NULL;
	}
	for (;;) {
		if (config->timelimit)
			gettimeofday(&start, NULL);
		res = ast_channel_bridge(chan,peer,config,&f, &who);
		if (config->timelimit) {
			/* Update time limit for next pass */
			gettimeofday(&end, NULL);
			diff = (end.tv_sec - start.tv_sec) * 1000;
			diff += (end.tv_usec - start.tv_usec) / 1000;
			config->timelimit -= diff;
			if (hasfeatures) {
				/* Running on backup config, meaning a feature might be being
				   activated, but that's no excuse to keep things going 
				   indefinitely! */
				if (backup_config.timelimit && ((backup_config.timelimit -= diff) <= 0)) {
					ast_log(LOG_DEBUG, "Timed out, realtime this time!\n");
					config->timelimit = 0;
					who = chan;
					if (f)
						ast_frfree(f);
					f = NULL;
					res = 0;
				} else if (config->timelimit <= 0) {
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
				if (config->timelimit <=0) {
					/* We ran out of time */
					config->timelimit = 0;
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
			config->timelimit = backup_config.timelimit;
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
					ast_clear_flag(&(config->features_callee),AST_FEATURE_PLAY_WARNING);
					config->warning_freq = 0;
					config->warning_sound = NULL;
					config->end_sound = NULL;
					config->start_sound = NULL;
					config->firstpass = 0;
				}
				config->timelimit = featuredigittimeout;
				ast_log(LOG_DEBUG, "Set time limit to %ld\n", config->timelimit);
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
			gettimeofday(&tv, NULL);
			tms = (tv.tv_sec - pu->start.tv_sec) * 1000 + (tv.tv_usec - pu->start.tv_usec) / 1000;
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
						ast_add_extension2(con, 1, peername, 1, NULL, NULL, "Dial", strdup(returnexten), free, registrar);
					}
					strncpy(pu->chan->exten, peername, sizeof(pu->chan->exten) - 1);
					strncpy(pu->chan->context, parking_con_dial, sizeof(pu->chan->context) - 1);
					pu->chan->priority = 1;

				} else {
					/* They've been waiting too long, send them back to where they came.  Theoretically they
					   should have their original extensions and such, but we copy to be on the safe side */
					strncpy(pu->chan->exten, pu->exten, sizeof(pu->chan->exten)-1);
					strncpy(pu->chan->context, pu->context, sizeof(pu->chan->context)-1);
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
				for (x=0; x<AST_MAX_FDS; x++) {
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
		tv.tv_sec = ms / 1000;
		tv.tv_usec = (ms % 1000) * 1000;
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

		memset(&config,0,sizeof(struct ast_bridge_config));
		ast_set_flag(&(config.features_callee), AST_FEATURE_REDIRECT);
		ast_set_flag(&(config.features_caller), AST_FEATURE_REDIRECT);
		config.timelimit = 0;
		config.play_warning = 0;
		config.warning_freq = 0;
		config.warning_sound=NULL;
		res = ast_bridge_call(chan,peer,&config);

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
	char format[] = "%-25s %-7s %-7s\n";

	ast_cli(fd, format, "Feature", "Default", "Current");
	ast_cli(fd, format, "-------", "-------", "-------");

	ast_cli(fd, format, "Pickup", "*8", ast_pickup_ext());		/* default hardcoded above, so we'll hardcode it here */

	fcount = sizeof(builtin_features) / sizeof(builtin_features[0]);

	for (i = 0; i < fcount; i++)
	{
		ast_cli(fd, format, builtin_features[i].fname, builtin_features[i].default_exten, builtin_features[i].exten);
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
	ast_cli(fd, "%d parked call(s).\n",numparked);

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
				strncpy(parking_ext, var->value, sizeof(parking_ext) - 1);
			} else if (!strcasecmp(var->name, "context")) {
				strncpy(parking_con, var->value, sizeof(parking_con) - 1);
			} else if (!strcasecmp(var->name, "parkingtime")) {
				if ((sscanf(var->value, "%d", &parkingtime) != 1) || (parkingtime < 1)) {
					ast_log(LOG_WARNING, "%s is not a valid parkingtime\n", var->value);
					parkingtime = DEFAULT_PARK_TIME;
				} else
					parkingtime = parkingtime * 1000;
			} else if (!strcasecmp(var->name, "parkpos")) {
				if (sscanf(var->value, "%i-%i", &start, &end) != 2) {
					ast_log(LOG_WARNING, "Format for parking positions is a-b, where a and b are numbers at line %d of parking.conf\n", var->lineno);
				} else {
					parking_start = start;
					parking_stop = end;
				}
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
				strncpy(courtesytone, var->value, sizeof(courtesytone) - 1);
			} else if (!strcasecmp(var->name, "xfersound")) {
				strncpy(xfersound, var->value, sizeof(xfersound) - 1);
			} else if (!strcasecmp(var->name, "xferfailsound")) {
				strncpy(xferfailsound, var->value, sizeof(xferfailsound) - 1);
			} else if (!strcasecmp(var->name, "pickupexten")) {
				strncpy(pickup_ext, var->value, sizeof(pickup_ext) - 1);
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
		ast_config_destroy(cfg);
	}
	
	if (con)
		ast_context_remove_extension2(con, ast_parking_ext(), 1, registrar);
	
	if (!(con = ast_context_find(parking_con))) {
		if (!(con = ast_context_create(NULL, parking_con, registrar))) {
			ast_log(LOG_ERROR, "Parking context '%s' does not exist and unable to create\n", parking_con);
			return -1;
		}
	}
	return ast_add_extension2(con, 1, ast_parking_ext(), 1, NULL, NULL, parkcall, strdup(""),free, registrar);
}

int reload(void) {
	return load_config();
}

int load_module(void)
{
	int res;
	if ((res = load_config()))
		return res;
	ast_cli_register(&showparked);
	ast_cli_register(&showfeatures);
	ast_pthread_create(&parking_thread, NULL, do_parking_thread, NULL);
	res = ast_register_application(parkedcall, park_exec, synopsis, descrip);
	if (!res)
		res = ast_register_application(parkcall, park_call_exec, synopsis2, descrip2);
	if (!res) {
		ast_manager_register( "ParkedCalls", 0, manager_parking_status, "List parked calls" );
	}
	return res;
}

int ast_pickup_call(struct ast_channel *chan)
{
	struct ast_channel *cur;
	int res = -1;
	cur = ast_channel_walk_locked(NULL);
	while(cur) {
		if (!cur->pbx && 
			(cur != chan) &&
			(chan->pickupgroup & cur->callgroup) &&
			((cur->_state == AST_STATE_RINGING) ||
			 (cur->_state == AST_STATE_RING))) {
			 	break;
		}
		ast_mutex_unlock(&cur->lock);
		cur = ast_channel_walk_locked(cur);
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

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;

	ast_manager_unregister( "ParkedCalls" );
	ast_cli_unregister(&showfeatures);
	ast_cli_unregister(&showparked);
	ast_unregister_application(parkcall);
	return ast_unregister_application(parkedcall);
}

char *description(void)
{
	return "Call Parking Resource";
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
