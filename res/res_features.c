/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Routines implementing call parking
 * 
 * Copyright (C) 1999-2004, Digium, Inc.
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
#include <asterisk/say.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/features.h>
#include <asterisk/musiconhold.h>
#include <asterisk/config.h>
#include <asterisk/cli.h>
#include <asterisk/manager.h>
#include <asterisk/utils.h>
#include <asterisk/adsi.h>
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

static char *parkedcall = "ParkedCall";

/* No more than 45 seconds parked before you do something with them */
static int parkingtime = DEFAULT_PARK_TIME;

/* Context for which parking is made accessible */
static char parking_con[AST_MAX_EXTENSION] = "parkedcalls";

/* Extension you type to park the call */
static char parking_ext[AST_MAX_EXTENSION] = "700";

static char pickup_ext[AST_MAX_EXTENSION] = "*8";

/* First available extension for parking */
static int parking_start = 701;

/* Last available extension for parking */
static int parking_stop = 750;

static int adsipark = 0;

static int transferdigittimeout = DEFAULT_TRANSFER_DIGIT_TIMEOUT;

/* Default courtesy tone played when party joins conference */
static char courtesytone[256] = "";

/* Registrar for operations */
static char *registrar = "res_features";

static char *synopsis = "Answer a parked call";

static char *descrip = "ParkedCall(exten):"
"Used to connect to a parked call.  This Application is always\n"
"registered internally and does not need to be explicitly added\n"
"into the dialplan, although you should include the 'parkedcalls'\n"
"context.\n";


static char *parkcall = "Park";

static char *synopsis2 = "Park yourself";

static char *descrip2 = "Park(exten):"
"Used to park yourself (typically in combination with a supervised\n"
"transfer to know the parking space.  This Application is always\n"
"registered internally and does not need to be explicitly added\n"
"into the dialplan, although you should include the 'parkedcalls'\n"
"context.\n";

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
			if (chan != peer)
				ast_moh_start(pu->chan, NULL);
			gettimeofday(&pu->start, NULL);
			pu->parkingnum = x;
			if (timeout > 0)
				pu->parkingtime = timeout;
			else
				pu->parkingtime = parkingtime;
			if (extout)
				*extout = x;
			/* Remember what had been dialed, so that if the parking
			   expires, we try to come back to the same place */
			if (strlen(chan->macrocontext))
				strncpy(pu->context, chan->macrocontext, sizeof(pu->context)-1);
			else
				strncpy(pu->context, chan->context, sizeof(pu->context)-1);
			if (strlen(chan->macroexten))
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
                                ,pu->parkingnum, pu->chan->name, peer->name
                                ,(long)pu->start.tv_sec + (long)(pu->parkingtime/1000) - (long)time(NULL)
                                ,(pu->chan->callerid ? pu->chan->callerid : "")
                                );

			if (peer) {
				if (adsipark && adsi_available(peer)) {
					adsi_announce_park(peer, pu->parkingnum);
				}
				ast_say_digits(peer, pu->parkingnum, "", peer->language);
				if (adsipark && adsi_available(peer)) {
					adsi_unload_session(peer);
				}
				if (pu->notquiteyet) {
					/* Wake up parking thread if we're really done */
					ast_moh_start(pu->chan, NULL);
					pu->notquiteyet = 0;
					pthread_kill(parking_thread, SIGURG);
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
				ast_add_extension2(con, 1, exten, 1, NULL, parkedcall, strdup(exten), free, registrar);
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

int ast_bridge_call(struct ast_channel *chan,struct ast_channel *peer,struct ast_bridge_config *config)
{
	/* Copy voice back and forth between the two channels.  Give the peer
	   the ability to transfer calls with '#<extension' syntax. */
	int len;
	struct ast_frame *f;
	struct ast_channel *who;
	char newext[256], *ptr;
	int res;
	int diff;
	struct ast_option_header *aoh;
	struct ast_channel *transferer;
	struct ast_channel *transferee;
	struct timeval start, end;
	char *transferer_real_context;
	int allowdisconnect_in,allowdisconnect_out,allowredirect_in,allowredirect_out;

	allowdisconnect_in = config->allowdisconnect_in;
	allowdisconnect_out = config->allowdisconnect_out;
	allowredirect_in = config->allowredirect_in;
	allowredirect_out = config->allowredirect_out;
	config->firstpass = 1;

	/* Answer if need be */
	if (ast_answer(chan))
		return -1;
	peer->appl = "Bridged Call";
	peer->data = chan->name;
	/* copy the userfield from the B-leg to A-leg if applicable */
	if (chan->cdr && peer->cdr && strlen(peer->cdr->userfield)) {
		char tmp[256];
		if (strlen(chan->cdr->userfield)) {
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
			if (config->timelimit <=0) {
				/* We ran out of time */
				config->timelimit = 0;
				who = chan;
				f = NULL;
				res = 0;
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
		if (f && (f->frametype == AST_FRAME_DTMF) &&
			(((who == chan) && allowdisconnect_out) || ((who == peer) && allowdisconnect_in)) &&
			(f->subclass == '*')) {
			
			if (option_verbose > 3)
				ast_verbose(VERBOSE_PREFIX_3 "User hit %c to disconnect call.\n", f->subclass);
			res = -1;
			break;
		}

		if ((f->frametype == AST_FRAME_DTMF) &&
			((allowredirect_in && who == peer) || (allowredirect_out && who == chan)) &&
			(f->subclass == '#')) {
				if(allowredirect_in &&  who == peer) {
					transferer = peer;
					transferee = chan;
				}
				else {
					transferer = chan;
					transferee = peer;
				}
				if(!(transferer_real_context=pbx_builtin_getvar_helper(transferee, "TRANSFER_CONTEXT")) &&
				   !(transferer_real_context=pbx_builtin_getvar_helper(transferer, "TRANSFER_CONTEXT"))) {
					/* Use the non-macro context to transfer the call */
					if(strlen(transferer->macrocontext))
						transferer_real_context=transferer->macrocontext;
					else
						transferer_real_context=transferer->context;
				}
				/* Start autoservice on chan while we talk
				   to the originator */
				ast_autoservice_start(transferee);
				ast_moh_start(transferee, NULL);

				memset(newext, 0, sizeof(newext));
				ptr = newext;

					/* Transfer */
				if ((res=ast_streamfile(transferer, "pbx-transfer", transferer->language))) {
					ast_moh_stop(transferee);
					ast_autoservice_stop(transferee);
					break;
				}
				if ((res=ast_waitstream(transferer, AST_DIGIT_ANY)) < 0) {
					ast_moh_stop(transferee);
					ast_autoservice_stop(transferee);
					break;
				}
				ast_stopstream(transferer);
				if (res > 0) {
					/* If they've typed a digit already, handle it */
					newext[0] = res;
					ptr++;
					len --;
				}
				res = 0;
				while(strlen(newext) < sizeof(newext) - 1) {
					res = ast_waitfordigit(transferer, transferdigittimeout);
					if (res < 1) 
						break;
					if (res == '#')
						break;
					*(ptr++) = res;
					if (!ast_matchmore_extension(transferer, transferer_real_context
								, newext, 1, transferer->callerid)) {
						break;
					}
				}

				if (res < 0) {
					ast_moh_stop(transferee);
					ast_autoservice_stop(transferee);
					break;
				}
				if (!strcmp(newext, ast_parking_ext())) {
					ast_moh_stop(transferee);

					if (ast_autoservice_stop(transferee))
						res = -1;
					else if (!ast_park_call(transferee, transferer, 0, NULL)) {
						/* We return non-zero, but tell the PBX not to hang the channel when
						   the thread dies -- We have to be careful now though.  We are responsible for 
						   hanging up the channel, else it will never be hung up! */

						if(transferer==peer)
							res=AST_PBX_KEEPALIVE;
						else
							res=AST_PBX_NO_HANGUP_PEER;
						break;
					} else {
						ast_log(LOG_WARNING, "Unable to park call %s\n", transferee->name);
					}
					/* XXX Maybe we should have another message here instead of invalid extension XXX */
				} else if (ast_exists_extension(transferee, transferer_real_context, newext, 1, transferer->callerid)) {
					ast_moh_stop(transferee);
					res=ast_autoservice_stop(transferee);
					if (!transferee->pbx) {
						/* Doh!  Use our handy async_goto funcitons */
						if (option_verbose > 2) 
							ast_verbose(VERBOSE_PREFIX_3 "Transferring %s to '%s' (context %s) priority 1\n"
								,transferee->name, newext, transferer_real_context);
						if (ast_async_goto(transferee, transferer_real_context, newext, 1))
							ast_log(LOG_WARNING, "Async goto fialed :(\n");
						res = -1;
					} else {
						/* Set the channel's new extension, since it exists, using transferer context */
						strncpy(transferee->exten, newext, sizeof(transferee->exten)-1);
						strncpy(transferee->context, transferer_real_context, sizeof(transferee->context)-1);
						transferee->priority = 0;
						ast_frfree(f);
					}
					break;
				} else {
					if (option_verbose > 2)	
						ast_verbose(VERBOSE_PREFIX_3 "Unable to find extension '%s' in context '%s'\n", newext, transferer_real_context);
				}
				res = ast_streamfile(transferer, "pbx-invalid", transferee->language);
				if (res) {
					ast_moh_stop(transferee);
					ast_autoservice_stop(transferee);
					break;
				}
				res = ast_waitstream(transferer, AST_DIGIT_ANY);
				ast_stopstream(transferer);
				ast_moh_stop(transferee);
				res = ast_autoservice_stop(transferee);
				if (res) {
					if (option_verbose > 1)
						ast_verbose(VERBOSE_PREFIX_2 "Hungup during autoservice stop on '%s'\n", transferee->name);
				}
			} else {
            if (f && (f->frametype == AST_FRAME_DTMF)) {
                  if (who == peer)
                        ast_write(chan, f);
                  else
                        ast_write(peer, f);
            }            
#if 1
				ast_log(LOG_DEBUG, "Read from %s (%d,%d)\n", who->name, f->frametype, f->subclass);
#endif
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
	struct ast_context *con;
	int x;
	int gc=0;
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
		gettimeofday(&tv, NULL);
		FD_ZERO(&nrfds);
		FD_ZERO(&nefds);
		while(pu) {
			if (pu->notquiteyet) {
				/* Pretend this one isn't here yet */
				pl = pu;
				pu = pu->next;
				continue;
			}
			if (gc < 5 && !pu->chan->generator) {
				gc++;
				ast_moh_start(pu->chan,NULL);
			}
			tms = (tv.tv_sec - pu->start.tv_sec) * 1000 + (tv.tv_usec - pu->start.tv_usec) / 1000;
			if (tms > pu->parkingtime) {
				/* They've been waiting too long, send them back to where they came.  Theoretically they
				   should have their original extensions and such, but we copy to be on the safe side */
				strncpy(pu->chan->exten, pu->exten, sizeof(pu->chan->exten)-1);
				strncpy(pu->chan->context, pu->context, sizeof(pu->chan->context)-1);
				pu->chan->priority = pu->priority;
				if (option_verbose > 1) 
					ast_verbose(VERBOSE_PREFIX_2 "Timeout for %s parked on %d. Returning to %s,%s,%d\n", pu->chan->name, pu->parkingnum, pu->chan->context, pu->chan->exten, pu->chan->priority);
				/* Stop music on hold */
				ast_moh_stop(pu->chan);
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
				for (x=0;x<AST_MAX_FDS;x++) {
					if ((pu->chan->fds[x] > -1) && (FD_ISSET(pu->chan->fds[x], &rfds) || FD_ISSET(pu->chan->fds[x], &efds))) {
						if (FD_ISSET(pu->chan->fds[x], &efds))
							pu->chan->exception = 1;
						pu->chan->fdno = x;
						/* See if they need servicing */
						f = ast_read(pu->chan);
						if (!f || ((f->frametype == AST_FRAME_CONTROL) && (f->subclass ==  AST_CONTROL_HANGUP))) {
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
							goto std;	/* XXX Ick: jumping into an else statement??? XXX */
						}
					}
				}
				if (x >= AST_MAX_FDS) {
std:					for (x=0;x<AST_MAX_FDS;x++) {
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
		config.allowredirect_in = 1;
		config.allowredirect_out = 1;
		config.allowdisconnect_out = 0;
		config.allowdisconnect_in = 0;
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
			ast_verbose(VERBOSE_PREFIX_3 "Channel %s tried to talk to non-existant parked call %d\n", chan->name, park);
		res = -1;
	}
	LOCAL_USER_REMOVE(u);
	return res;
}

static int handle_parkedcalls(int fd, int argc, char *argv[])
{
	struct parkeduser *cur;

	ast_cli(fd, "%4s %25s (%-15s %-12s %-4s) %-6s \n", "Num", "Channel"
		, "Context", "Extension", "Pri", "Timeout");

	ast_mutex_lock(&parking_lock);

	cur=parkinglot;
	while(cur) {
		ast_cli(fd, "%4d %25s (%-15s %-12s %-4d) %6lds\n"
			,cur->parkingnum, cur->chan->name, cur->context, cur->exten
			,cur->priority, cur->start.tv_sec + (cur->parkingtime/1000) - time(NULL));

		cur = cur->next;
	}

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
			"%s"
			"\r\n"
                        ,cur->parkingnum, cur->chan->name
                        ,(long)cur->start.tv_sec + (long)(cur->parkingtime/1000) - (long)time(NULL)
			,(cur->chan->callerid ? cur->chan->callerid : "")
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



int load_module(void)
{
	int res;
	int start, end;
	struct ast_context *con;
	struct ast_config *cfg;
	struct ast_variable *var;

	ast_cli_register(&showparked);

	cfg = ast_load("features.conf");
	if (!cfg) {
		cfg = ast_load("parking.conf");
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
			} else if(!strcasecmp(var->name, "transferdigittimeout")) {
				if ((sscanf(var->value, "%d", &transferdigittimeout) != 1) || (transferdigittimeout < 1)) {
					ast_log(LOG_WARNING, "%s is not a valid transferdigittimeout\n", var->value);
					transferdigittimeout = DEFAULT_TRANSFER_DIGIT_TIMEOUT;
				} else
					transferdigittimeout = transferdigittimeout * 1000;
			} else if  (!strcasecmp(var->name, "courtesytone")) {
				strncpy(courtesytone, var->value, sizeof(courtesytone) - 1);
			}
			var = var->next;
		}
		ast_destroy(cfg);
	}
	con = ast_context_find(parking_con);
	if (!con) {
		con = ast_context_create(NULL,parking_con, registrar);
		if (!con) {
			ast_log(LOG_ERROR, "Parking context '%s' does not exist and unable to create\n", parking_con);
			return -1;
		}
	}
	ast_add_extension2(con, 1, ast_parking_ext(), 1, NULL, parkcall, strdup(""),free, registrar);
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
		ast_log(LOG_DEBUG, "No call pickup possible...\n");
	}
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;

	ast_manager_unregister( "ParkedCalls" );
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
