/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Trivial application to dial a channel and send an URL on answer
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
#include <asterisk/config.h>
#include <asterisk/features.h>
#include <asterisk/musiconhold.h>
#include <asterisk/callerid.h>
#include <asterisk/utils.h>
#include <asterisk/app.h>
#include <asterisk/causes.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <netinet/in.h>

static char *tdesc = "Dialing Application";

static char *app = "Dial";

static char *synopsis = "Place a call and connect to the current channel";

static char *descrip =
"  Dial(Technology/resource[&Technology2/resource2...][|timeout][|options][|URL]):\n"
"Requests one or more channels and places specified outgoing calls on them.\n"
"As soon as a channel answers, the Dial app will answer the originating\n"
"channel (if it needs to be answered) and will bridge a call with the channel\n"
"which first answered. All other calls placed by the Dial app will be hung up.\n"
"If a timeout is not specified, the Dial application will wait indefinitely\n"
"until either one of the called channels answers, the user hangs up, or all\n"
"channels return busy or error. In general, the dialer will return 0 if it\n"
"was unable to place the call, or the timeout expired. However, if all\n"
"channels were busy, and there exists an extension with priority n+101 (where\n"
"n is the priority of the dialer instance), then it will be the next\n"
"executed extension (this allows you to setup different behavior on busy from\n"
"no-answer).\n"
"  This application returns -1 if the originating channel hangs up, or if the\n"
"call is bridged and either of the parties in the bridge terminate the call.\n"
"The option string may contain zero or more of the following characters:\n"
"      'd' -- allow the calling user to dial a 1 digit extension while waiting for a call to\n"
"             be answered exiting to that extension if it exists in the context defined by\n"
"             ${EXITCONTEXT} or the current context.\n"
"      't' -- allow the called user to transfer the calling user by hitting #.\n"
"      'T' -- allow the calling user to transfer the call by hitting #.\n"
"      'w' -- allow the called user to write the conversation to disk via app_monitor\n"
"      'W' -- allow the calling user to write the conversation to disk via app_monitor\n"
"      'f' -- Forces callerid to be set as the extension of the line \n"
"             making/redirecting the outgoing call. For example, some PSTNs\n"
"             don't allow callerids from other extensions then the ones\n"
"             that are assigned to you.\n"
"      'r' -- indicate ringing to the calling party, pass no audio until answered.\n"
"      'm[(class)]' -- provide hold music to the calling party until answered (optionally\n"
"                      with the specified class.\n"
"      'M(x[^arg]) -- Executes the macro (x with ^ delim arg list) upon connect of the call.\n"
"                     Also, the macro can set the MACRO_RESULT variable to do the following:\n"
"                     -- ABORT - Hangup both legs of the call.\n"
"                     -- CONGESTION - Behave as if line congestion was encountered.\n"
"                     -- BUSY - Behave as if a busy signal was encountered. (n+101)\n"
"                     -- CONTINUE - Hangup the called party and continue on in the dialplan.\n"
"                     -- GOTO:<context>^<exten>^<priority> - Transfer the call.\n"
"      'h' -- allow callee to hang up by hitting *.\n"
"      'H' -- allow caller to hang up by hitting *.\n"
"      'C' -- reset call detail record for this call.\n"
"      'P[(x)]' -- privacy mode, using 'x' as database if provided.\n"
"      'g' -- goes on in context if the destination channel hangs up\n"
"      'A(x)' -- play an announcement to the called party, using x as file\n"
"      'S(x)' -- hangup the call after x seconds AFTER called party picked up\n"  	
"      'D([digits])'  -- Send DTMF digit string *after* called party has answered\n"
"             but before the bridge. (w=500ms sec pause)\n"
"      'L(x[:y][:z])' -- Limit the call to 'x' ms warning when 'y' ms are left\n"
"             repeated every 'z' ms) Only 'x' is required, 'y' and 'z' are optional.\n"
"             The following special variables are optional:\n"
"             * LIMIT_PLAYAUDIO_CALLER    yes|no (default yes)\n"
"                                         Play sounds to the caller.\n"
"             * LIMIT_PLAYAUDIO_CALLEE    yes|no\n"
"                                         Play sounds to the callee.\n"
"             * LIMIT_TIMEOUT_FILE        File to play when time is up.\n"
"             * LIMIT_CONNECT_FILE        File to play when call begins.\n"
"             * LIMIT_WARNING_FILE        File to play as warning if 'y' is defined.\n"
"                        'timeleft' is a special sound macro to auto-say the time \n"
"                        left and is the default.\n\n"
"  In addition to transferring the call, a call may be parked and then picked\n"
"up by another user.\n"
"  The optional URL will be sent to the called party if the channel supports it.\n"
"  If the OUTBOUND_GROUP variable is set, all peer channels created by this\n"
"  application will be put into that group (as in SetGroup).\n"
"  This application sets the following channel variables upon completion:\n"
"      DIALEDTIME    Time from dial to answer\n" 
"      ANSWEREDTIME  Time for actual call\n"
"      DIALSTATUS    The status of the call as a text string, one of\n"
"             CHANUNAVAIL | CONGESTION | NOANSWER | BUSY | ANSWER | CANCEL\n"
"";

/* RetryDial App by Anthony Minessale II <anthmct@yahoo.com> Jan/2005 */
static char *rapp = "RetryDial";
static char *rsynopsis = "Place a call, retrying on failure allowing optional exit extension.\n";
static char *rdescrip =
"  RetryDial(announce|sleep|loops|Technology/resource[&Technology2/resource2...][|timeout][|options][|URL]):\n"
"Attempt to place a call.  If no channel can be reached, play the file defined by 'announce'\n"
"waiting 'sleep' seconds to retry the call.  If the specified number of attempts matches \n"
"'loops' the call will continue in the dialplan.  If 'loops' is set to 0, the call will retry endlessly.\n\n"
"While waiting, a 1 digit extension may be dialed.  If that extension exists in either\n"
"the context defined in ${EXITCONTEXT} or the current one, The call will transfer\n"
"to that extension immmediately.\n\n"
"All arguements after 'loops' are passed directly to the Dial() application.\n"
"";


/* We define a customer "local user" structure because we
   use it not only for keeping track of what is in use but
   also for keeping track of who we're dialing. */

#define DIAL_STILLGOING				(1 << 0)
#define DIAL_ALLOWREDIRECT_IN		(1 << 1)
#define DIAL_ALLOWREDIRECT_OUT		(1 << 2)
#define DIAL_ALLOWDISCONNECT_IN		(1 << 3)
#define DIAL_ALLOWDISCONNECT_OUT	(1 << 4)
#define DIAL_RINGBACKONLY			(1 << 5)
#define DIAL_MUSICONHOLD			(1 << 6)
#define DIAL_FORCECALLERID			(1 << 7)
#define DIAL_MONITOR_IN				(1 << 8)
#define DIAL_MONITOR_OUT			(1 << 9)
#define DIAL_GO_ON					(1 << 10)
#define DIAL_HALT_ON_DTMF 			(1 << 11)

struct localuser {
	struct ast_channel *chan;
	unsigned int flags;
	int forwards;
	struct localuser *next;
};

LOCAL_USER_DECL;

static void hanguptree(struct localuser *outgoing, struct ast_channel *exception)
{
	/* Hang up a tree of stuff */
	struct localuser *oo;
	while (outgoing) {
		/* Hangup any existing lines we have open */
		if (outgoing->chan && (outgoing->chan != exception))
			ast_hangup(outgoing->chan);
		oo = outgoing;
		outgoing=outgoing->next;
		free(oo);
	}
}

#define AST_MAX_FORWARDS   8

#define AST_MAX_WATCHERS 256

#define HANDLE_CAUSE(cause, chan) do { \
	switch(cause) { \
	case AST_CAUSE_BUSY: \
		if (chan->cdr) \
			ast_cdr_busy(chan->cdr); \
		numbusy++; \
		break; \
	case AST_CAUSE_CONGESTION: \
	case AST_CAUSE_UNREGISTERED: \
		if (chan->cdr) \
			ast_cdr_busy(chan->cdr); \
		numcongestion++; \
		break; \
	default: \
		numnochan++; \
		break; \
	} \
} while (0)


static int ast_onedigit_goto(struct ast_channel *chan, char *context, char exten, int pri, char *cid) 
{
	char rexten[2];
	snprintf(rexten, 2, "%c", exten);
	if (context) {
		if (ast_exists_extension(chan, context, rexten, pri, cid)) {
			ast_explicit_goto(chan, context, rexten, pri-1);
			return 1;
		}
	} else {
		if (ast_exists_extension(chan, chan->context, rexten, pri, cid)) {
			ast_explicit_goto(chan, chan->context, rexten, pri-1);
			return 1;
		} else if (!ast_strlen_zero(chan->macrocontext)) {
			if (ast_exists_extension(chan, chan->macrocontext, rexten, pri, cid)) {
				ast_explicit_goto(chan, chan->macrocontext, rexten, pri-1);
				return 1;
			}
		}
	}
	return 0;
}


static struct ast_channel *wait_for_answer(struct ast_channel *in, struct localuser *outgoing, int *to, struct ast_flags *peerflags, int *sentringing, char *status, size_t statussize, int busystart, int nochanstart, int congestionstart, int *result)
{
	struct localuser *o;
	int found;
	int numlines;
	int numbusy = busystart;
	int numcongestion = congestionstart;
	int numnochan = nochanstart;
	int prestart = busystart + congestionstart + nochanstart;
	int cause;
	int orig = *to;
	struct ast_frame *f;
	struct ast_channel *peer = NULL;
	struct ast_channel *watchers[AST_MAX_WATCHERS];
	int pos;
	int single;
	struct ast_channel *winner;
	char *context = NULL;

	single = (outgoing && !outgoing->next && !ast_test_flag(outgoing, DIAL_MUSICONHOLD | DIAL_RINGBACKONLY));
	
	if (single) {
		/* Turn off hold music, etc */
		ast_deactivate_generator(in);
		/* If we are calling a single channel, make them compatible for in-band tone purpose */
		ast_channel_make_compatible(outgoing->chan, in);
	}
	
	
	while (*to && !peer) {
		o = outgoing;
		found = -1;
		pos = 1;
		numlines = prestart;
		watchers[0] = in;
		while (o) {
			/* Keep track of important channels */
			if (ast_test_flag(o, DIAL_STILLGOING) && o->chan) {
				watchers[pos++] = o->chan;
				found = 1;
			}
			o = o->next;
			numlines++;
		}
		if (found < 0) {
			if (numlines == (numbusy + numcongestion + numnochan)) {
				if (option_verbose > 2)
					ast_verbose( VERBOSE_PREFIX_2 "Everyone is busy/congested at this time (%d:%d/%d/%d)\n", numlines, numbusy, numcongestion, numnochan);
				if (numbusy)
					strncpy(status, "BUSY", statussize - 1);
				else if (numcongestion)
					strncpy(status, "CONGESTION", statussize - 1);
				else if (numnochan)
					strncpy(status, "CHANUNAVAIL", statussize - 1);
				/* See if there is a special busy message */
				if (ast_exists_extension(in, in->context, in->exten, in->priority + 101, in->cid.cid_num)) 
					in->priority+=100;
			} else {
				if (option_verbose > 2)
					ast_verbose( VERBOSE_PREFIX_2 "No one is available to answer at this time (%d:%d/%d/%d)\n", numlines, numbusy, numcongestion, numnochan);
			}
			*to = 0;
			return NULL;
		}
		winner = ast_waitfor_n(watchers, pos, to);
		o = outgoing;
		while (o) {
			if (ast_test_flag(o, DIAL_STILLGOING) && o->chan && (o->chan->_state == AST_STATE_UP)) {
				if (!peer) {
					if (option_verbose > 2)
						ast_verbose( VERBOSE_PREFIX_3 "%s answered %s\n", o->chan->name, in->name);
					peer = o->chan;
					ast_copy_flags(peerflags, o, DIAL_ALLOWREDIRECT_IN|DIAL_ALLOWREDIRECT_OUT|DIAL_ALLOWDISCONNECT_IN|DIAL_ALLOWDISCONNECT_OUT);
				}
			} else if (o->chan && (o->chan == winner)) {
				if (!ast_strlen_zero(o->chan->call_forward)) {
					char tmpchan[256]="";
					char *stuff;
					char *tech;
					strncpy(tmpchan, o->chan->call_forward, sizeof(tmpchan) - 1);
					if ((stuff = strchr(tmpchan, '/'))) {
						*stuff = '\0';
						stuff++;
						tech = tmpchan;
					} else {
						snprintf(tmpchan, sizeof(tmpchan), "%s@%s", o->chan->call_forward, o->chan->context);
						stuff = tmpchan;
						tech = "Local";
					}
					/* Before processing channel, go ahead and check for forwarding */
					o->forwards++;
					if (o->forwards < AST_MAX_FORWARDS) {
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "Now forwarding %s to '%s/%s' (thanks to %s)\n", in->name, tech, stuff, o->chan->name);
						/* Setup parameters */
						o->chan = ast_request(tech, in->nativeformats, stuff, &cause);
						if (!o->chan)
							ast_log(LOG_NOTICE, "Unable to create local channel for call forward to '%s/%s' (cause = %d)\n", tech, stuff, cause);
					} else {
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "Too many forwards from %s\n", o->chan->name);
						cause = AST_CAUSE_CONGESTION;
						o->chan = NULL;
					}
					if (!o->chan) {
						ast_clear_flag(o, DIAL_STILLGOING);	
						HANDLE_CAUSE(cause, in);
					} else {
						if (o->chan->cid.cid_num)
							free(o->chan->cid.cid_num);
						o->chan->cid.cid_num = NULL;
						if (o->chan->cid.cid_name)
							free(o->chan->cid.cid_name);
						o->chan->cid.cid_name = NULL;

						if (ast_test_flag(o, DIAL_FORCECALLERID)) {
							char *newcid = NULL;

							if (strlen(in->macroexten))
								newcid = in->macroexten;
							else
								newcid = in->exten;
							o->chan->cid.cid_num = strdup(newcid);
							strncpy(o->chan->accountcode, winner->accountcode, sizeof(o->chan->accountcode) - 1);
							o->chan->cdrflags = winner->cdrflags;
							if (!o->chan->cid.cid_num)
								ast_log(LOG_WARNING, "Out of memory\n");
						} else {
							if (in->cid.cid_num) {
								o->chan->cid.cid_num = strdup(in->cid.cid_num);
								if (!o->chan->cid.cid_num)
									ast_log(LOG_WARNING, "Out of memory\n");	
							}
							if (in->cid.cid_name) {
								o->chan->cid.cid_name = strdup(in->cid.cid_name);
								if (!o->chan->cid.cid_name)
									ast_log(LOG_WARNING, "Out of memory\n");	
							}
							strncpy(o->chan->accountcode, in->accountcode, sizeof(o->chan->accountcode) - 1);
							o->chan->cdrflags = in->cdrflags;
						}

						if (in->cid.cid_ani) {
							if (o->chan->cid.cid_ani)
								free(o->chan->cid.cid_ani);
							o->chan->cid.cid_ani = malloc(strlen(in->cid.cid_ani) + 1);
							if (o->chan->cid.cid_ani)
								strncpy(o->chan->cid.cid_ani, in->cid.cid_ani, strlen(in->cid.cid_ani) + 1);
							else
								ast_log(LOG_WARNING, "Out of memory\n");
						}
						if (o->chan->cid.cid_rdnis) 
							free(o->chan->cid.cid_rdnis);
						if (!ast_strlen_zero(in->macroexten))
							o->chan->cid.cid_rdnis = strdup(in->macroexten);
						else
							o->chan->cid.cid_rdnis = strdup(in->exten);
						if (ast_call(o->chan, tmpchan, 0)) {
							ast_log(LOG_NOTICE, "Failed to dial on local channel for call forward to '%s'\n", tmpchan);
							ast_clear_flag(o, DIAL_STILLGOING);	
							ast_hangup(o->chan);
							o->chan = NULL;
							numnochan++;
						} else {
							/* After calling, set callerid to extension */
							ast_set_callerid(o->chan, ast_strlen_zero(in->macroexten) ? in->exten : in->macroexten, NULL, NULL);
						}
					}
					/* Hangup the original channel now, in case we needed it */
					ast_hangup(winner);
					continue;
				}
				f = ast_read(winner);
				if (f) {
					if (f->frametype == AST_FRAME_CONTROL) {
						switch(f->subclass) {
					    case AST_CONTROL_ANSWER:
							/* This is our guy if someone answered. */
							if (!peer) {
								if (option_verbose > 2)
									ast_verbose( VERBOSE_PREFIX_3 "%s answered %s\n", o->chan->name, in->name);
								peer = o->chan;
								ast_copy_flags(peerflags, o, DIAL_ALLOWREDIRECT_IN|DIAL_ALLOWREDIRECT_OUT|DIAL_ALLOWDISCONNECT_IN|DIAL_ALLOWDISCONNECT_OUT);
							}
							/* If call has been answered, then the eventual hangup is likely to be normal hangup */
							in->hangupcause = AST_CAUSE_NORMAL_CLEARING;
							o->chan->hangupcause = AST_CAUSE_NORMAL_CLEARING;
							break;
						case AST_CONTROL_BUSY:
							if (option_verbose > 2)
								ast_verbose( VERBOSE_PREFIX_3 "%s is busy\n", o->chan->name);
							in->hangupcause = o->chan->hangupcause;
							ast_hangup(o->chan);
							o->chan = NULL;
							ast_clear_flag(o, DIAL_STILLGOING);	
							HANDLE_CAUSE(AST_CAUSE_BUSY, in);
							break;
						case AST_CONTROL_CONGESTION:
							if (option_verbose > 2)
								ast_verbose( VERBOSE_PREFIX_3 "%s is circuit-busy\n", o->chan->name);
							in->hangupcause = o->chan->hangupcause;
							ast_hangup(o->chan);
							o->chan = NULL;
							ast_clear_flag(o, DIAL_STILLGOING);
							HANDLE_CAUSE(AST_CAUSE_CONGESTION, in);
							break;
						case AST_CONTROL_RINGING:
							if (option_verbose > 2)
								ast_verbose( VERBOSE_PREFIX_3 "%s is ringing\n", o->chan->name);
							if (!(*sentringing) && !ast_test_flag(outgoing, DIAL_MUSICONHOLD)) {
								ast_indicate(in, AST_CONTROL_RINGING);
								(*sentringing)++;
							}
							break;
						case AST_CONTROL_PROGRESS:
							if (option_verbose > 2)
								ast_verbose ( VERBOSE_PREFIX_3 "%s is making progress passing it to %s\n", o->chan->name,in->name);
							if (!ast_test_flag(outgoing, DIAL_RINGBACKONLY))
								ast_indicate(in, AST_CONTROL_PROGRESS);
							break;
						case AST_CONTROL_HOLD:
							if (option_verbose > 2)
								ast_verbose(VERBOSE_PREFIX_3 "Call on %s placed on hold\n", o->chan->name);
							ast_indicate(in, AST_CONTROL_HOLD);
							break;
						case AST_CONTROL_UNHOLD:
							if (option_verbose > 2)
								ast_verbose(VERBOSE_PREFIX_3 "Call on %s left from hold\n", o->chan->name);
							ast_indicate(in, AST_CONTROL_UNHOLD);
							break;
						case AST_CONTROL_OFFHOOK:
						case AST_CONTROL_FLASH:
							/* Ignore going off hook and flash */
							break;
						case -1:
							if (!ast_test_flag(outgoing, DIAL_RINGBACKONLY | DIAL_MUSICONHOLD)) {
								if (option_verbose > 2)
									ast_verbose( VERBOSE_PREFIX_3 "%s stopped sounds\n", o->chan->name);
								ast_indicate(in, -1);
								(*sentringing) = 0;
							}
							break;
						default:
							ast_log(LOG_DEBUG, "Dunno what to do with control type %d\n", f->subclass);
						}
					} else if (single && (f->frametype == AST_FRAME_VOICE) && 
								!(ast_test_flag(outgoing, DIAL_RINGBACKONLY) || ast_test_flag(outgoing, DIAL_MUSICONHOLD))) {
						if (ast_write(in, f)) 
							ast_log(LOG_WARNING, "Unable to forward frame\n");
					} else if (single && (f->frametype == AST_FRAME_IMAGE) && 
								!(ast_test_flag(outgoing, DIAL_RINGBACKONLY) || ast_test_flag(outgoing, DIAL_MUSICONHOLD))) {
						if (ast_write(in, f))
							ast_log(LOG_WARNING, "Unable to forward image\n");
					}
					ast_frfree(f);
				} else {
					in->hangupcause = o->chan->hangupcause;
					ast_hangup(o->chan);
					o->chan = NULL;
					ast_clear_flag(o, DIAL_STILLGOING);
				}
			}
			o = o->next;
		}
		if (winner == in) {
			f = ast_read(in);
#if 0
			if (f && (f->frametype != AST_FRAME_VOICE))
					printf("Frame type: %d, %d\n", f->frametype, f->subclass);
			else if (!f || (f->frametype != AST_FRAME_VOICE))
				printf("Hangup received on %s\n", in->name);
#endif
			if (!f || ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_HANGUP))) {
				/* Got hung up */
				*to=-1;
				strncpy(status, "CANCEL", statussize - 1);
				if (f)
					ast_frfree(f);
				return NULL;
			}

			if (f && (f->frametype == AST_FRAME_DTMF)) {
				if (ast_test_flag(peerflags, DIAL_HALT_ON_DTMF)) {
					context = pbx_builtin_getvar_helper(in, "EXITCONTEXT");
					if (ast_onedigit_goto(in, context, (char) f->subclass, 1, in->cid.cid_num)) {
						if (option_verbose > 3)
							ast_verbose(VERBOSE_PREFIX_3 "User hit %c to disconnect call.\n", f->subclass);
						*to=0;
						*result = f->subclass;
						strcpy(status, "CANCEL");
						ast_frfree(f);
						return NULL;
					}
				}

				if (ast_test_flag(peerflags, DIAL_ALLOWDISCONNECT_OUT) && 
						  (f->subclass == '*')) { /* hmm it it not guarenteed to be '*' anymore. */
					if (option_verbose > 3)
						ast_verbose(VERBOSE_PREFIX_3 "User hit %c to disconnect call.\n", f->subclass);
					*to=0;
					strcpy(status, "CANCEL");
					ast_frfree(f);
					return NULL;
				}
			}

			if (single && ((f->frametype == AST_FRAME_VOICE) || (f->frametype == AST_FRAME_DTMF)))  {
				if (ast_write(outgoing->chan, f))
					ast_log(LOG_WARNING, "Unable to forward voice\n");
				ast_frfree(f);
			}
		}
		if (!*to && (option_verbose > 2))
			ast_verbose( VERBOSE_PREFIX_3 "Nobody picked up in %d ms\n", orig);
	}

	return peer;
	
}


static int dial_exec_full(struct ast_channel *chan, void *data, struct ast_flags *peerflags)
{
	int res=-1;
	struct localuser *u;
	char *info, *peers, *timeout, *tech, *number, *rest, *cur;
	char  privdb[256] = "", *s;
	char  announcemsg[256] = "", *ann;
	struct localuser *outgoing=NULL, *tmp;
	struct ast_channel *peer;
	int to;
	int hasmacro = 0;
	int privacy=0;
	int announce=0;
	int resetcdr=0;
	int numbusy = 0;
	int numcongestion = 0;
	int numnochan = 0;
	int cause;
	char numsubst[AST_MAX_EXTENSION];
	char restofit[AST_MAX_EXTENSION];
	char *transfer = NULL;
	char *newnum;
	char *l;
	char *url=NULL; /* JDG */
	unsigned int calldurationlimit=0;
	char *cdl;
	time_t now;
	struct ast_bridge_config config;
	long timelimit = 0;
	long play_warning = 0;
	long warning_freq=0;
	char *warning_sound=NULL;
	char *end_sound=NULL;
	char *start_sound=NULL;
	char *limitptr;
	char limitdata[256];
	char *sdtmfptr;
	char sdtmfdata[256] = "";
	char *stack,*var;
	char *mac = NULL, *macroname = NULL;
	char status[256]="";
	char toast[80];
	int play_to_caller=0,play_to_callee=0;
	int playargs=0, sentringing=0, moh=0;
	char *mohclass = NULL;
	char *outbound_group = NULL;
	char *macro_result = NULL, *macro_transfer_dest = NULL;
	int digit = 0, result = 0;
	time_t start_time, answer_time, end_time;
	struct ast_app *app = NULL;
	
	if (!data) {
		ast_log(LOG_WARNING, "Dial requires an argument (technology1/number1&technology2/number2...|optional timeout|options)\n");
		return -1;
	}

	if (!(info = ast_strdupa(data))) {
		ast_log(LOG_WARNING, "Unable to dupe data :(\n");
		return -1;
	}
	LOCAL_USER_ADD(u);
	
	peers = info;
	if (peers) {
		
		timeout = strchr(info, '|');
		if (timeout) {
			*timeout = '\0';
			timeout++;
			transfer = strchr(timeout, '|');
			if (transfer) {
				*transfer = '\0';
				transfer++;
				/* JDG */
				url = strchr(transfer, '|');
				if (url) {
					*url = '\0';
					url++;
					if (option_debug)
						ast_log(LOG_DEBUG, "DIAL WITH URL=%s_\n", url);
				} else 
					if (option_debug) {
						ast_log(LOG_DEBUG, "SIMPLE DIAL (NO URL)\n");
					}
				/* /JDG */
			}
		}
	} else
		timeout = NULL;
	if (!peers || ast_strlen_zero(peers)) {
		ast_log(LOG_WARNING, "Dial argument takes format (technology1/number1&technology2/number2...|optional timeout)\n");
		goto out;
	}
	

	if (transfer) {

		/* Extract call duration limit */
		if ((cdl = strstr(transfer, "S("))) {
			calldurationlimit=atoi(cdl+2);
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Setting call duration limit to %i seconds.\n",calldurationlimit);			
		} 

		/* DTMF SCRIPT*/
		if ((sdtmfptr = strstr(transfer, "D("))) {
			strncpy(sdtmfdata, sdtmfptr + 2, sizeof(sdtmfdata) - 1);
			/* Overwrite with X's what was the sdtmf info */
			while (*sdtmfptr && (*sdtmfptr != ')')) 
				*(sdtmfptr++) = 'X';
			if (*sdtmfptr)
				*sdtmfptr = 'X';
			/* Now find the end  */
			sdtmfptr = strchr(sdtmfdata, ')');
			if (sdtmfptr)
				*sdtmfptr = '\0';
			else 
				ast_log(LOG_WARNING, "D( Data lacking trailing ')'\n");
		}
		
		/* XXX LIMIT SUPPORT */
		if ((limitptr = strstr(transfer, "L("))) {
			strncpy(limitdata, limitptr + 2, sizeof(limitdata) - 1);
			/* Overwrite with X's what was the limit info */
			while (*limitptr && (*limitptr != ')')) 
				*(limitptr++) = 'X';
			if (*limitptr)
				*limitptr = 'X';
			/* Now find the end */
			limitptr = strchr(limitdata, ')');
			if (limitptr)
				*limitptr = '\0';
			else
				ast_log(LOG_WARNING, "Limit Data lacking trailing ')'\n");

			var = pbx_builtin_getvar_helper(chan,"LIMIT_PLAYAUDIO_CALLER");
			play_to_caller = var ? ast_true(var) : 1;
		  
			var = pbx_builtin_getvar_helper(chan,"LIMIT_PLAYAUDIO_CALLEE");
			play_to_callee = var ? ast_true(var) : 0;
		  
			if (!play_to_caller && !play_to_callee)
				play_to_caller=1;
		  
			var = pbx_builtin_getvar_helper(chan,"LIMIT_WARNING_FILE");
			warning_sound = var ? var : "timeleft";

			var = pbx_builtin_getvar_helper(chan,"LIMIT_TIMEOUT_FILE");
			end_sound = var ? var : NULL;

			var = pbx_builtin_getvar_helper(chan,"LIMIT_CONNECT_FILE");
			start_sound = var ? var : NULL;

			var=stack=limitdata;

			var = strsep(&stack, ":");
			if (var) {
				timelimit = atol(var);
				playargs++;
				var = strsep(&stack, ":");
				if (var) {
					play_warning = atol(var);
					playargs++;
					var = strsep(&stack, ":");
					if (var) {
						warning_freq = atol(var);
						playargs++;
					}
				}
			}
		  
			if (!timelimit) {
				timelimit=play_to_caller=play_to_callee=play_warning=warning_freq=0;
				warning_sound=NULL;
			}
			/* undo effect of S(x) in case they are both used */
			calldurationlimit=0; 
			/* more efficient do it like S(x) does since no advanced opts*/
			if (!play_warning && !start_sound && !end_sound && timelimit) { 
				calldurationlimit=timelimit/1000;
				timelimit=play_to_caller=play_to_callee=play_warning=warning_freq=0;
			} else if (option_verbose > 2) {
				ast_verbose(VERBOSE_PREFIX_3"Limit Data:\n");
				ast_verbose(VERBOSE_PREFIX_3"timelimit=%ld\n",timelimit);
				ast_verbose(VERBOSE_PREFIX_3"play_warning=%ld\n",play_warning);
				ast_verbose(VERBOSE_PREFIX_3"play_to_caller=%s\n",play_to_caller ? "yes" : "no");
				ast_verbose(VERBOSE_PREFIX_3"play_to_callee=%s\n",play_to_callee ? "yes" : "no");
				ast_verbose(VERBOSE_PREFIX_3"warning_freq=%ld\n",warning_freq);
				ast_verbose(VERBOSE_PREFIX_3"start_sound=%s\n",start_sound ? start_sound : "UNDEF");
				ast_verbose(VERBOSE_PREFIX_3"warning_sound=%s\n",warning_sound ? warning_sound : "UNDEF");
				ast_verbose(VERBOSE_PREFIX_3"end_sound=%s\n",end_sound ? end_sound : "UNDEF");
			}
		}
		
		/* XXX ANNOUNCE SUPPORT */
		if ((ann = strstr(transfer, "A("))) {
			announce = 1;
			strncpy(announcemsg, ann + 2, sizeof(announcemsg) - 1);
			/* Overwrite with X's what was the announce info */
			while (*ann && (*ann != ')')) 
				*(ann++) = 'X';
			if (*ann)
				*ann = 'X';
			/* Now find the end of the privdb */
			ann = strchr(announcemsg, ')');
			if (ann)
				*ann = '\0';
			else {
				ast_log(LOG_WARNING, "Transfer with Announce spec lacking trailing ')'\n");
				announce = 0;
			}
		}
		
		/* Get the macroname from the dial option string */
		if ((mac = strstr(transfer, "M("))) {
			hasmacro = 1;
			macroname = ast_strdupa(mac + 2);
			while (*mac && (*mac != ')'))
				*(mac++) = 'X';
			if (*mac)
				*mac = 'X';
			else {
				ast_log(LOG_WARNING, "Could not find macro to which we should jump.\n");
				hasmacro = 0;
			}
			mac = strchr(macroname, ')');
			if (mac)
				*mac = '\0';
			else {
				ast_log(LOG_WARNING, "Macro flag set without trailing ')'\n");
				hasmacro = 0;
			}
		}
		/* Get music on hold class */
		if ((mac = strstr(transfer, "m("))) {
			mohclass = ast_strdupa(mac + 2);
			mac++; /* Leave the "m" in the string */
			while (*mac && (*mac != ')'))
				*(mac++) = 'X';
			if (*mac)
				*mac = 'X';
			else {
				ast_log(LOG_WARNING, "Could not find music on hold class to use, assuming default.\n");
				mohclass=NULL;
			}
			mac = strchr(macroname, ')');
			if (mac)
				*mac = '\0';
			else {
				ast_log(LOG_WARNING, "Music on hold class specified without trailing ')'\n");
				mohclass = NULL;
			}
		}
		/* Extract privacy info from transfer */
		if ((s = strstr(transfer, "P("))) {
			privacy = 1;
			strncpy(privdb, s + 2, sizeof(privdb) - 1);
			/* Overwrite with X's what was the privacy info */
			while (*s && (*s != ')')) 
				*(s++) = 'X';
			if (*s)
				*s = 'X';
			/* Now find the end of the privdb */
			s = strchr(privdb, ')');
			if (s)
				*s = '\0';
			else {
				ast_log(LOG_WARNING, "Transfer with privacy lacking trailing ')'\n");
				privacy = 0;
			}
		} else if (strchr(transfer, 'P')) {
			/* No specified privdb */
			privacy = 1;
		} else if (strchr(transfer, 'C')) {
			resetcdr = 1;
		}
	}
	if (resetcdr && chan->cdr)
		ast_cdr_reset(chan->cdr, 0);
	if (ast_strlen_zero(privdb) && privacy) {
		/* If privdb is not specified and we are using privacy, copy from extension */
		strncpy(privdb, chan->exten, sizeof(privdb) - 1);
	}
	if (privacy) {
		l = chan->cid.cid_num;
		if (!l)
			l = "";
		ast_log(LOG_NOTICE, "Privacy DB is '%s', privacy is %d, clid is '%s'\n", privdb, privacy, l);
	}

	/* If a channel group has been specified, get it for use when we create peer channels */
	outbound_group = pbx_builtin_getvar_helper(chan, "OUTBOUND_GROUP");

	cur = peers;
	do {
		/* Remember where to start next time */
		rest = strchr(cur, '&');
		if (rest) {
			*rest = 0;
			rest++;
		}
		/* Get a technology/[device:]number pair */
		tech = cur;
		number = strchr(tech, '/');
		if (!number) {
			ast_log(LOG_WARNING, "Dial argument takes format (technology1/[device:]number1&technology2/[device:]number2...|optional timeout)\n");
			goto out;
		}
		*number = '\0';
		number++;
		tmp = malloc(sizeof(struct localuser));
		if (!tmp) {
			ast_log(LOG_WARNING, "Out of memory\n");
			goto out;
		}
		memset(tmp, 0, sizeof(struct localuser));
		if (transfer) {
			ast_set2_flag(tmp, strchr(transfer, 't'), DIAL_ALLOWREDIRECT_IN);
			ast_set2_flag(tmp, strchr(transfer, 'T'), DIAL_ALLOWREDIRECT_OUT);
			ast_set2_flag(tmp, strchr(transfer, 'r'), DIAL_RINGBACKONLY);	
			ast_set2_flag(tmp, strchr(transfer, 'm'), DIAL_MUSICONHOLD);	
			ast_set2_flag(tmp, strchr(transfer, 'H'), DIAL_ALLOWDISCONNECT_OUT);	
			ast_set2_flag(peerflags, strchr(transfer, 'H'), DIAL_ALLOWDISCONNECT_OUT);	
			ast_set2_flag(tmp, strchr(transfer, 'h'), DIAL_ALLOWDISCONNECT_IN);
			ast_set2_flag(peerflags, strchr(transfer, 'h'), DIAL_ALLOWDISCONNECT_IN);
			ast_set2_flag(tmp, strchr(transfer, 'f'), DIAL_FORCECALLERID);	
			ast_set2_flag(peerflags, strchr(transfer, 'w'), DIAL_MONITOR_IN);	
			ast_set2_flag(peerflags, strchr(transfer, 'W'), DIAL_MONITOR_OUT);	
			ast_set2_flag(peerflags, strchr(transfer, 'd'), DIAL_HALT_ON_DTMF);	
			ast_set2_flag(tmp, strchr(transfer, 'g'), DIAL_GO_ON);	
		}
		strncpy(numsubst, number, sizeof(numsubst)-1);
		/* If we're dialing by extension, look at the extension to know what to dial */
		if ((newnum = strstr(numsubst, "BYEXTENSION"))) {
			strncpy(restofit, newnum + strlen("BYEXTENSION"), sizeof(restofit)-1);
			snprintf(newnum, sizeof(numsubst) - (newnum - numsubst), "%s%s", chan->exten,restofit);
			if (option_debug)
				ast_log(LOG_DEBUG, "Dialing by extension %s\n", numsubst);
		}
		/* Request the peer */
		tmp->chan = ast_request(tech, chan->nativeformats, numsubst, &cause);
		if (!tmp->chan) {
			/* If we can't, just go on to the next call */
			ast_log(LOG_NOTICE, "Unable to create channel of type '%s' (cause %d)\n", tech, cause);
			HANDLE_CAUSE(cause, chan);
			cur = rest;
			continue;
		}
		if (!ast_strlen_zero(tmp->chan->call_forward)) {
			char tmpchan[256]="";
			char *stuff;
			char *tech;
			strncpy(tmpchan, tmp->chan->call_forward, sizeof(tmpchan) - 1);
			if ((stuff = strchr(tmpchan, '/'))) {
				*stuff = '\0';
				stuff++;
				tech = tmpchan;
			} else {
				snprintf(tmpchan, sizeof(tmpchan), "%s@%s", tmp->chan->call_forward, tmp->chan->context);
				stuff = tmpchan;
				tech = "Local";
			}
			tmp->forwards++;
			if (tmp->forwards < AST_MAX_FORWARDS) {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Now forwarding %s to '%s/%s' (thanks to %s)\n", chan->name, tech, stuff, tmp->chan->name);
				ast_hangup(tmp->chan);
				/* Setup parameters */
				tmp->chan = ast_request(tech, chan->nativeformats, stuff, &cause);
				if (!tmp->chan)
					ast_log(LOG_NOTICE, "Unable to create local channel for call forward to '%s/%s' (cause = %d)\n", tech, stuff, cause);
			} else {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Too many forwards from %s\n", tmp->chan->name);
				ast_hangup(tmp->chan);
				tmp->chan = NULL;
				cause = AST_CAUSE_CONGESTION;
			}
			if (!tmp->chan) {
				HANDLE_CAUSE(cause, chan);
				cur = rest;
				continue;
			}
		}

		/* Inherit specially named variables from parent channel */
		ast_channel_inherit_variables(chan, tmp->chan);

		tmp->chan->appl = "AppDial";
		tmp->chan->data = "(Outgoing Line)";
		tmp->chan->whentohangup = 0;
		if (tmp->chan->cid.cid_num)
			free(tmp->chan->cid.cid_num);
		tmp->chan->cid.cid_num = NULL;
		if (tmp->chan->cid.cid_name)
			free(tmp->chan->cid.cid_name);
		tmp->chan->cid.cid_name = NULL;
		if (tmp->chan->cid.cid_ani)
			free(tmp->chan->cid.cid_ani);
		tmp->chan->cid.cid_ani = NULL;

		if (chan->cid.cid_num) 
			tmp->chan->cid.cid_num = strdup(chan->cid.cid_num);
		if (chan->cid.cid_name) 
			tmp->chan->cid.cid_name = strdup(chan->cid.cid_name);
		if (chan->cid.cid_ani) 
			tmp->chan->cid.cid_ani = strdup(chan->cid.cid_ani);
		
		/* Copy language from incoming to outgoing */
		strncpy(tmp->chan->language, chan->language, sizeof(tmp->chan->language) - 1);
		strncpy(tmp->chan->accountcode, chan->accountcode, sizeof(tmp->chan->accountcode) - 1);
		tmp->chan->cdrflags = chan->cdrflags;
		if (ast_strlen_zero(tmp->chan->musicclass))
			strncpy(tmp->chan->musicclass, chan->musicclass, sizeof(tmp->chan->musicclass) - 1);
		if (chan->cid.cid_rdnis)
			tmp->chan->cid.cid_rdnis = strdup(chan->cid.cid_rdnis);
		/* Pass callingpres setting */
		tmp->chan->cid.cid_pres = chan->cid.cid_pres;
		/* Pass type of number */
		tmp->chan->cid.cid_ton = chan->cid.cid_ton;
		/* Pass type of tns */
		tmp->chan->cid.cid_tns = chan->cid.cid_tns;
		/* Presense of ADSI CPE on outgoing channel follows ours */
		tmp->chan->adsicpe = chan->adsicpe;
		/* pass the digital flag */
		ast_copy_flags(tmp->chan, chan, AST_FLAG_DIGITAL);

		/* If we have an outbound group, set this peer channel to it */
		if (outbound_group)
			ast_app_group_set_channel(tmp->chan, outbound_group);

		/* Place the call, but don't wait on the answer */
		res = ast_call(tmp->chan, numsubst, 0);

		/* Save the info in cdr's that we called them */
		if (chan->cdr)
			ast_cdr_setdestchan(chan->cdr, tmp->chan->name);

		/* check the results of ast_call */
		if (res) {
			/* Again, keep going even if there's an error */
			if (option_debug)
				ast_log(LOG_DEBUG, "ast call on peer returned %d\n", res);
			else if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Couldn't call %s\n", numsubst);
			ast_hangup(tmp->chan);
			tmp->chan = NULL;
			cur = rest;
			continue;
		} else {
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Called %s\n", numsubst);
			ast_set_callerid(tmp->chan, ast_strlen_zero(chan->macroexten) ? chan->exten : chan->macroexten, NULL, NULL);
		}
		/* Put them in the list of outgoing thingies...  We're ready now. 
		   XXX If we're forcibly removed, these outgoing calls won't get
		   hung up XXX */
		ast_set_flag(tmp, DIAL_STILLGOING);	
		tmp->next = outgoing;
		outgoing = tmp;
		/* If this line is up, don't try anybody else */
		if (outgoing->chan->_state == AST_STATE_UP)
			break;
		cur = rest;
	} while (cur);
	
	if (timeout && !ast_strlen_zero(timeout)) {
		to = atoi(timeout);
		if (to > 0)
			to *= 1000;
		else
			ast_log(LOG_WARNING, "Invalid timeout specified: '%s'\n", timeout);
	} else
		to = -1;

	if (outgoing) {
		/* Our status will at least be NOANSWER */
		strncpy(status, "NOANSWER", sizeof(status) - 1);
		if (ast_test_flag(outgoing, DIAL_MUSICONHOLD)) {
			moh=1;
			ast_moh_start(chan, mohclass);
		} else if (ast_test_flag(outgoing, DIAL_RINGBACKONLY)) {
			ast_indicate(chan, AST_CONTROL_RINGING);
			sentringing++;
		}
	} else
		strncpy(status, "CHANUNAVAIL", sizeof(status) - 1);

	time(&start_time);
	peer = wait_for_answer(chan, outgoing, &to, peerflags, &sentringing, status, sizeof(status), numbusy, numnochan, numcongestion, &result);
	
	if (!peer) {
		if (result) {
			res = result;
		}
		else if (to) 
			/* Musta gotten hung up */
			res = -1;
		else 
		 	/* Nobody answered, next please? */
			res=0;
		
		goto out;
	}
	if (peer) {
		time(&answer_time);
#ifdef OSP_SUPPORT
		/* Once call is answered, ditch the OSP Handle */
		pbx_builtin_setvar_helper(chan, "_OSPHANDLE", "");
#endif		
		strncpy(status, "ANSWER", sizeof(status) - 1);
		/* Ah ha!  Someone answered within the desired timeframe.  Of course after this
		   we will always return with -1 so that it is hung up properly after the 
		   conversation.  */
		hanguptree(outgoing, peer);
		outgoing = NULL;
		/* If appropriate, log that we have a destination channel */
		if (chan->cdr)
			ast_cdr_setdestchan(chan->cdr, peer->name);
		if (peer->name)
			pbx_builtin_setvar_helper(chan, "DIALEDPEERNAME", peer->name);
		if (numsubst)
			pbx_builtin_setvar_helper(chan, "DIALEDPEERNUMBER", numsubst);
 		/* JDG: sendurl */
 		if ( url && !ast_strlen_zero(url) && ast_channel_supports_html(peer) ) {
 			ast_log(LOG_DEBUG, "app_dial: sendurl=%s.\n", url);
 			ast_channel_sendurl( peer, url );
 		} /* /JDG */
		if (announce && announcemsg) {
			/* Start autoservice on the other chan */
			res = ast_autoservice_start(chan);
			/* Now Stream the File */
			if (!res)
				res = ast_streamfile(peer,announcemsg,peer->language);
			if (!res) {
				digit = ast_waitstream(peer, AST_DIGIT_ANY); 
			}
			/* Ok, done. stop autoservice */
			res = ast_autoservice_stop(chan);
			if (digit > 0 && !res)
				res = ast_senddigit(chan, digit); 
			else
				res = digit;

		} else
			res = 0;

		if (hasmacro && macroname) {
			res = ast_autoservice_start(chan);
			if (res) {
				ast_log(LOG_ERROR, "Unable to start autoservice on calling channel\n");
				res = -1;
			}

			app = pbx_findapp("Macro");

			if (app && !res) {
				for (res=0;res<strlen(macroname);res++)
					if (macroname[res] == '^')
						macroname[res] = '|';
				res = pbx_exec(peer, app, macroname, 1);
				ast_log(LOG_DEBUG, "Macro exited with status %d\n", res);
				res = 0;
			} else {
				ast_log(LOG_ERROR, "Could not find application Macro\n");
				res = -1;
			}

			if (ast_autoservice_stop(chan) < 0) {
				ast_log(LOG_ERROR, "Could not stop autoservice on calling channel\n");
				res = -1;
			}

			if (!res) {
				if ((macro_result = pbx_builtin_getvar_helper(peer, "MACRO_RESULT"))) {
					if (!strcasecmp(macro_result, "BUSY")) {
						strncpy(status, macro_result, sizeof(status) - 1);
						if (!ast_goto_if_exists(chan, NULL, NULL, chan->priority + 101)) {
							ast_set_flag(peerflags, DIAL_GO_ON);
						}
						res = -1;
					}
					else if (!strcasecmp(macro_result, "CONGESTION") || !strcasecmp(macro_result, "CHANUNAVAIL")) {
						strncpy(status, macro_result, sizeof(status) - 1);
						ast_set_flag(peerflags, DIAL_GO_ON);	
						res = -1;
					}
					else if (!strcasecmp(macro_result, "CONTINUE")) {
						/* hangup peer and keep chan alive assuming the macro has changed 
						   the context / exten / priority or perhaps 
						   the next priority in the current exten is desired.
						*/
						ast_set_flag(peerflags, DIAL_GO_ON);	
						res = -1;
					} else if (!strcasecmp(macro_result, "ABORT")) {
						/* Hangup both ends unless the caller has the g flag */
						res = -1;
					} else if (!strncasecmp(macro_result, "GOTO:",5) && (macro_transfer_dest = ast_strdupa(macro_result + 5))) {
						res = -1;
						/* perform a transfer to a new extension */
						if (strchr(macro_transfer_dest,'^')) { /* context^exten^priority*/
							/* no brainer mode... substitute ^ with | and feed it to builtin goto */
							for (res=0;res<strlen(macro_transfer_dest);res++)
								if (macro_transfer_dest[res] == '^')
									macro_transfer_dest[res] = '|';

							if (!ast_parseable_goto(chan, macro_transfer_dest))
								ast_set_flag(peerflags, DIAL_GO_ON);

						}
					}
				}
			}
		}

		if (!res) {
			if (calldurationlimit > 0) {
				time(&now);
				chan->whentohangup = now + calldurationlimit;
			}
			if (!ast_strlen_zero(sdtmfdata)) 
				res = ast_dtmf_stream(peer,chan,sdtmfdata,250);
		}
		
		if (!res) {
			memset(&config,0,sizeof(struct ast_bridge_config));
			if (play_to_caller)
				ast_set_flag(&(config.features_caller), AST_FEATURE_PLAY_WARNING);
			if (play_to_callee)
				ast_set_flag(&(config.features_callee), AST_FEATURE_PLAY_WARNING);
			if (ast_test_flag(peerflags, DIAL_ALLOWREDIRECT_IN))
				ast_set_flag(&(config.features_callee), AST_FEATURE_REDIRECT);
			if (ast_test_flag(peerflags, DIAL_ALLOWREDIRECT_OUT))
				ast_set_flag(&(config.features_caller), AST_FEATURE_REDIRECT);
			if (ast_test_flag(peerflags, DIAL_ALLOWDISCONNECT_IN))
				ast_set_flag(&(config.features_callee), AST_FEATURE_DISCONNECT);
			if (ast_test_flag(peerflags, DIAL_ALLOWDISCONNECT_OUT))
				ast_set_flag(&(config.features_caller), AST_FEATURE_DISCONNECT);
			if (ast_test_flag(peerflags, DIAL_MONITOR_IN))
				ast_set_flag(&(config.features_callee), AST_FEATURE_AUTOMON);
			if (ast_test_flag(peerflags, DIAL_MONITOR_OUT)) 
				ast_set_flag(&(config.features_caller), AST_FEATURE_AUTOMON);

			config.timelimit = timelimit;
			config.play_warning = play_warning;
			config.warning_freq = warning_freq;
			config.warning_sound = warning_sound;
			config.end_sound = end_sound;
			config.start_sound = start_sound;
			if (moh) {
				moh = 0;
				ast_moh_stop(chan);
			} else if (sentringing) {
				sentringing = 0;
				ast_indicate(chan, -1);
			}
			/* Be sure no generators are left on it */
			ast_deactivate_generator(chan);
			/* Make sure channels are compatible */
			res = ast_channel_make_compatible(chan, peer);
			if (res < 0) {
				ast_log(LOG_WARNING, "Had to drop call because I couldn't make %s compatible with %s\n", chan->name, peer->name);
				ast_hangup(peer);
				return -1;
			}
			res = ast_bridge_call(chan,peer,&config);
			time(&end_time);
			snprintf(toast, sizeof(toast), "%ld", (long)(end_time - start_time));
			pbx_builtin_setvar_helper(chan, "DIALEDTIME", toast);
			snprintf(toast, sizeof(toast), "%ld", (long)(end_time - answer_time));
			pbx_builtin_setvar_helper(chan, "ANSWEREDTIME", toast);
			
		} else 
			res = -1;
		
		if (res != AST_PBX_NO_HANGUP_PEER) {
			if (!chan->_softhangup)
				chan->hangupcause = peer->hangupcause;
			ast_hangup(peer);
		}
	}	
out:
	if (moh) {
		moh = 0;
		ast_moh_stop(chan);
	} else if (sentringing) {
		sentringing = 0;
		ast_indicate(chan, -1);
	}
	hanguptree(outgoing, NULL);
	pbx_builtin_setvar_helper(chan, "DIALSTATUS", status);
	ast_log(LOG_DEBUG, "Exiting with DIALSTATUS=%s.\n", status);
	
	LOCAL_USER_REMOVE(u);
	
	if ((ast_test_flag(peerflags, DIAL_GO_ON)) && (!chan->_softhangup))
	    res=0;
	    
	return res;
}

static int dial_exec(struct ast_channel *chan, void *data)
{
	struct ast_flags peerflags;
	memset(&peerflags, 0, sizeof(peerflags));
	return dial_exec_full(chan, data, &peerflags);
}

static int retrydial_exec(struct ast_channel *chan, void *data)
{
	char *announce = NULL, *context = NULL, *dialdata = NULL;
	int sleep = 0, loops = 0, res = 0;
	struct localuser *u = NULL;
	struct ast_flags peerflags;
	
	memset(&peerflags, 0, sizeof(peerflags));

	LOCAL_USER_ADD(u);
	
	if (!data || !(announce = ast_strdupa(data))) {
		ast_log(LOG_ERROR, "Out of memory!\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}
	
	if ((dialdata = strchr(announce, '|'))) {
		*dialdata = '\0';
		dialdata++;
		if ((sleep = atoi(dialdata))) {
			sleep *= 1000;
		} else {
			ast_log(LOG_ERROR, "%s requires the numrical arguement <sleep>\n",rapp);
			LOCAL_USER_REMOVE(u);
			return -1;
		}
		if ((dialdata = strchr(dialdata, '|'))) {
			*dialdata = '\0';
			dialdata++;
			if (!(loops = atoi(dialdata))) {
				ast_log(LOG_ERROR, "%s requires the numrical arguement <loops>\n",rapp);
				LOCAL_USER_REMOVE(u);
				return -1;
			}
		}
	}
	
	if ((dialdata = strchr(dialdata, '|'))) {
		*dialdata = '\0';
		dialdata++;
	} else {
		ast_log(LOG_ERROR, "%s requires more arguements\n",rapp);
		LOCAL_USER_REMOVE(u);
		return -1;
	}
		
	if (sleep < 1000)
		sleep = 10000;
	
	if (!loops)
		loops = -1;
	
	context = pbx_builtin_getvar_helper(chan, "EXITCONTEXT");
	
	while (loops) {
		chan->data = "Retrying";
		if (ast_test_flag(chan, AST_FLAG_MOH))
			ast_moh_stop(chan);

		if ((res = dial_exec_full(chan, dialdata, &peerflags)) == 0) {
			if (ast_test_flag(&peerflags, DIAL_HALT_ON_DTMF)) {
				if (!(res = ast_streamfile(chan, announce, chan->language)))
					res = ast_waitstream(chan, AST_DIGIT_ANY);
				if (!res && sleep) {
					if (!ast_test_flag(chan, AST_FLAG_MOH))
						ast_moh_start(chan, NULL);
					res = ast_waitfordigit(chan, sleep);
				}
			} else {
				if (!(res = ast_streamfile(chan, announce, chan->language)))
					res = ast_waitstream(chan, "");
				if (sleep) {
					if (!ast_test_flag(chan, AST_FLAG_MOH))
						ast_moh_start(chan, NULL);
					if (!res) 
						res = ast_safe_sleep(chan, sleep);
				}
			}
		}

		if (res < 0)
			break;
		else if (res > 0) { /* Trying to send the call elsewhere (1 digit ext) */
			if (ast_onedigit_goto(chan, context, (char) res, 1, chan->cid.cid_num)) {
				res = 0;
				break;
			}
		}
		loops--;
	}
	
	if (ast_test_flag(chan, AST_FLAG_MOH))
		ast_moh_stop(chan);

	LOCAL_USER_REMOVE(u);
	return loops ? res : 0;

}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	ast_unregister_application(app);
	return ast_unregister_application(rapp);
}

int load_module(void)
{
	int res;
	if (!(res = ast_register_application(app, dial_exec, synopsis, descrip)))
		res = ast_register_application(rapp, retrydial_exec, rsynopsis, rdescrip);
	return res;
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
