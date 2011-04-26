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
 * \brief dial() & retrydial() - Trivial application to dial a channel and send an URL on answer
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */

/*** MODULEINFO
        <depend>chan_local</depend>
 ***/


#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <netinet/in.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/say.h"
#include "asterisk/config.h"
#include "asterisk/features.h"
#include "asterisk/musiconhold.h"
#include "asterisk/callerid.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/causes.h"
#include "asterisk/rtp.h"
#include "asterisk/cdr.h"
#include "asterisk/manager.h"
#include "asterisk/privacy.h"
#include "asterisk/stringfields.h"
#include "asterisk/global_datastores.h"

static char *app = "Dial";

static char *synopsis = "Place a call and connect to the current channel";

static char *descrip =
"  Dial(Technology/resource[&Tech2/resource2...][|timeout][|options][|URL]):\n"
"This application will place calls to one or more specified channels. As soon\n"
"as one of the requested channels answers, the originating channel will be\n"
"answered, if it has not already been answered. These two channels will then\n"
"be active in a bridged call. All other channels that were requested will then\n"
"be hung up.\n"
"  Unless there is a timeout specified, the Dial application will wait\n"
"indefinitely until one of the called channels answers, the user hangs up, or\n"
"if all of the called channels are busy or unavailable. Dialplan executing will\n"
"continue if no requested channels can be called, or if the timeout expires.\n\n"
"  This application sets the following channel variables upon completion:\n"
"    DIALEDTIME   - This is the time from dialing a channel until when it\n"
"                   is disconnected.\n" 
"    ANSWEREDTIME - This is the amount of time for actual call.\n"
"    DIALSTATUS   - This is the status of the call:\n"
"                   CHANUNAVAIL | CONGESTION | NOANSWER | BUSY | ANSWER | CANCEL\n" 
"                   DONTCALL | TORTURE | INVALIDARGS\n"
"  For the Privacy and Screening Modes, the DIALSTATUS variable will be set to\n"
"DONTCALL if the called party chooses to send the calling party to the 'Go Away'\n"
"script. The DIALSTATUS variable will be set to TORTURE if the called party\n"
"wants to send the caller to the 'torture' script.\n"
"  This application will report normal termination if the originating channel\n"
"hangs up, or if the call is bridged and either of the parties in the bridge\n"
"ends the call.\n"
"  The optional URL will be sent to the called party if the channel supports it.\n"
"  If the OUTBOUND_GROUP variable is set, all peer channels created by this\n"
"application will be put into that group (as in Set(GROUP()=...).\n"
"  If the OUTBOUND_GROUP_ONCE variable is set, all peer channels created by this\n"
"application will be put into that group (as in Set(GROUP()=...). Unlike OUTBOUND_GROUP,\n"
"however, the variable will be unset after use.\n\n"
"  Options:\n"
"    A(x) - Play an announcement to the called party, using 'x' as the file.\n"
"    C    - Reset the CDR for this call.\n"
"    d    - Allow the calling user to dial a 1 digit extension while waiting for\n"
"           a call to be answered. Exit to that extension if it exists in the\n"
"           current context, or the context defined in the EXITCONTEXT variable,\n"
"           if it exists.\n"
"    D([called][:calling]) - Send the specified DTMF strings *after* the called\n"
"           party has answered, but before the call gets bridged. The 'called'\n"
"           DTMF string is sent to the called party, and the 'calling' DTMF\n"
"           string is sent to the calling party. Both parameters can be used\n"
"           alone.\n"  	
"    f    - Force the callerid of the *calling* channel to be set as the\n"
"           extension associated with the channel using a dialplan 'hint'.\n"
"           For example, some PSTNs do not allow CallerID to be set to anything\n"
"           other than the number assigned to the caller.\n"
"    g    - Proceed with dialplan execution at the current extension if the\n"
"           destination channel hangs up.\n"
"    G(context^exten^pri) - If the call is answered, transfer the calling party to\n"
"           the specified priority and the called party to the specified priority+1.\n"
"           Optionally, an extension, or extension and context may be specified. \n"
"           Otherwise, the current extension is used. You cannot use any additional\n"
"           action post answer options in conjunction with this option.\n" 
"    h    - Allow the called party to hang up by sending the '*' DTMF digit, or\n"
"           whatever sequence was defined in the featuremap section for\n"
"           'disconnect' in features.conf\n"
"    H    - Allow the calling party to hang up by hitting the '*' DTMF digit, or\n"
"           whatever sequence was defined in the featuremap section for\n"
"           'disconnect' in features.conf\n"
"    i    - Asterisk will ignore any forwarding requests it may receive on this\n"
"           dial attempt.\n"
"    j    - Jump to priority n+101 if all of the requested channels were busy.\n"
"    k    - Allow the called party to enable parking of the call by sending\n"
"           the DTMF sequence defined for call parking in the featuremap section of features.conf.\n"
"    K    - Allow the calling party to enable parking of the call by sending\n"
"           the DTMF sequence defined for call parking in the featuremap section of features.conf.\n"
"    L(x[:y][:z]) - Limit the call to 'x' ms. Play a warning when 'y' ms are\n"
"           left. Repeat the warning every 'z' ms. The following special\n"
"           variables can be used with this option:\n"
"           * LIMIT_PLAYAUDIO_CALLER   yes|no (default yes)\n"
"                                      Play sounds to the caller.\n"
"           * LIMIT_PLAYAUDIO_CALLEE   yes|no\n"
"                                      Play sounds to the callee.\n"
"           * LIMIT_TIMEOUT_FILE       File to play when time is up.\n"
"           * LIMIT_CONNECT_FILE       File to play when call begins.\n"
"           * LIMIT_WARNING_FILE       File to play as warning if 'y' is defined.\n"
"                                      The default is to say the time remaining.\n"
"    m([class]) - Provide hold music to the calling party until a requested\n"
"           channel answers. A specific MusicOnHold class can be\n"
"           specified.\n"
"    M(x[^arg]) - Execute the Macro for the *called* channel before connecting\n"
"           to the calling channel. Arguments can be specified to the Macro\n"
"           using '^' as a delimeter. The Macro can set the variable\n"
"           MACRO_RESULT to specify the following actions after the Macro is\n" 
"           finished executing.\n"
"           * ABORT        Hangup both legs of the call.\n"
"           * CONGESTION   Behave as if line congestion was encountered.\n"
"           * BUSY         Behave as if a busy signal was encountered. This will also\n"
"                          have the application jump to priority n+101 if the\n"
"                          'j' option is set.\n"
"           * CONTINUE     Hangup the called party and allow the calling party\n"
"                          to continue dialplan execution at the next priority.\n"
"           * GOTO:<context>^<exten>^<priority> - Transfer the call to the\n"
"                          specified priority. Optionally, an extension, or\n"
"                          extension and priority can be specified.\n"
"           You cannot use any additional action post answer options in conjunction\n"
"           with this option. Also, pbx services are not run on the peer (called) channel,\n"
"           so you will not be able to set timeouts via the TIMEOUT() function in this macro.\n"
"    n([x]) - This option is a modifier for the screen/privacy mode. It specifies\n"
"             that no introductions are to be saved in the priv-callerintros\n"
"             directory.\n"
"             Specified without an arg, or with 0, the introduction is saved after\n"
"             an unanswered call originating from the same CallerID. With\n"
"             a 1 specified, the introduction is always deleted and rerequested.\n"
"    N    - This option is a modifier for the screen/privacy mode. It specifies\n"
"           that if callerID is present, do not screen the call.\n"
"    o    - Specify that the CallerID that was present on the *calling* channel\n"
"           be set as the CallerID on the *called* channel. This was the\n"
"           behavior of Asterisk 1.0 and earlier.\n"
"    O([x]) - \"Operator Services\" mode (Zaptel channel to Zaptel channel\n"
"             only, if specified on non-Zaptel interface, it will be ignored).\n"
"             When the destination answers (presumably an operator services\n"
"             station), the originator no longer has control of their line.\n"
"             They may hang up, but the switch will not release their line\n"
"             until the destination party hangs up (the operator). Specified\n"
"             without an arg, or with 1 as an arg, the originator hanging up\n"
"             will cause the phone to ring back immediately. With a 2 specified,\n"
"             when the \"operator\" flashes the trunk, it will ring their phone\n"
"             back.\n"
"    p    - This option enables screening mode. This is basically Privacy mode\n"
"           without memory.\n"
"    P([x]) - Enable privacy mode. Use 'x' as the family/key in the database if\n"
"           it is provided. The current extension is used if a database\n"
"           family/key is not specified.\n"
"    r    - Indicate ringing to the calling party. Pass no audio to the calling\n"
"           party until the called channel has answered.\n"
"    S(x) - Hang up the call after 'x' seconds *after* the called party has\n"
"           answered the call.\n"  	
"    t    - Allow the called party to transfer the calling party by sending the\n"
"           DTMF sequence defined in the blindxfer setting in the featuremap section\n"
"           of features.conf. This setting does not perform policy enforcement on\n"
"           transfers initiated by other methods.\n"
"    T    - Allow the calling party to transfer the called party by sending the\n"
"           DTMF sequence defined in the blindxfer setting in the featuremap section\n"
"           of features.conf. This setting does not perform policy enforcement on\n"
"           transfers initiated by other methods.\n"
"    w    - Allow the called party to enable recording of the call by sending\n"
"           the DTMF sequence defined in the automon setting in the featuremap section\n"
"           of features.conf.\n"
"    W    - Allow the calling party to enable recording of the call by sending\n"
"           the DTMF sequence defined in the automon setting in the featuremap section\n"
"           of features.conf.\n";

/* RetryDial App by Anthony Minessale II <anthmct@yahoo.com> Jan/2005 */
static char *rapp = "RetryDial";
static char *rsynopsis = "Place a call, retrying on failure allowing optional exit extension.";
static char *rdescrip =
"  RetryDial(announce|sleep|retries|dialargs): This application will attempt to\n"
"place a call using the normal Dial application. If no channel can be reached,\n"
"the 'announce' file will be played. Then, it will wait 'sleep' number of\n"
"seconds before retrying the call. After 'retries' number of attempts, the\n"
"calling channel will continue at the next priority in the dialplan. If the\n"
"'retries' setting is set to 0, this application will retry endlessly.\n"
"  While waiting to retry a call, a 1 digit extension may be dialed. If that\n"
"extension exists in either the context defined in ${EXITCONTEXT} or the current\n"
"one, The call will jump to that extension immediately.\n"
"  The 'dialargs' are specified in the same format that arguments are provided\n"
"to the Dial application.\n";

enum {
	OPT_ANNOUNCE =		(1 << 0),
	OPT_RESETCDR =		(1 << 1),
	OPT_DTMF_EXIT =		(1 << 2),
	OPT_SENDDTMF =		(1 << 3),
	OPT_FORCECLID =		(1 << 4),
	OPT_GO_ON =		(1 << 5),
	OPT_CALLEE_HANGUP =	(1 << 6),
	OPT_CALLER_HANGUP =	(1 << 7),
	OPT_PRIORITY_JUMP =	(1 << 8),
	OPT_DURATION_LIMIT =	(1 << 9),
	OPT_MUSICBACK =		(1 << 10),
	OPT_CALLEE_MACRO =	(1 << 11),
	OPT_SCREEN_NOINTRO =	(1 << 12),
	OPT_SCREEN_NOCLID =	(1 << 13),
	OPT_ORIGINAL_CLID =	(1 << 14),
	OPT_SCREENING =		(1 << 15),
	OPT_PRIVACY =		(1 << 16),
	OPT_RINGBACK =		(1 << 17),
	OPT_DURATION_STOP =	(1 << 18),
	OPT_CALLEE_TRANSFER =	(1 << 19),
	OPT_CALLER_TRANSFER =	(1 << 20),
	OPT_CALLEE_MONITOR =	(1 << 21),
	OPT_CALLER_MONITOR =	(1 << 22),
	OPT_GOTO =		(1 << 23),
	OPT_OPERMODE = 		(1 << 24),
	OPT_CALLEE_PARK =	(1 << 25),
	OPT_CALLER_PARK =	(1 << 26),
	OPT_IGNORE_FORWARDING = (1 << 27),
} dial_exec_option_flags;

#define DIAL_STILLGOING			(1 << 30)
#define DIAL_NOFORWARDHTML		(1 << 31)

enum {
	OPT_ARG_ANNOUNCE = 0,
	OPT_ARG_SENDDTMF,
	OPT_ARG_GOTO,
	OPT_ARG_DURATION_LIMIT,
	OPT_ARG_MUSICBACK,
	OPT_ARG_CALLEE_MACRO,
	OPT_ARG_PRIVACY,
	OPT_ARG_DURATION_STOP,
	OPT_ARG_OPERMODE,
	OPT_ARG_SCREEN_NOINTRO,
	/* note: this entry _MUST_ be the last one in the enum */
	OPT_ARG_ARRAY_SIZE,
} dial_exec_option_args;

AST_APP_OPTIONS(dial_exec_options, {
	AST_APP_OPTION_ARG('A', OPT_ANNOUNCE, OPT_ARG_ANNOUNCE),
	AST_APP_OPTION('C', OPT_RESETCDR),
	AST_APP_OPTION('d', OPT_DTMF_EXIT),
	AST_APP_OPTION_ARG('D', OPT_SENDDTMF, OPT_ARG_SENDDTMF),
	AST_APP_OPTION('f', OPT_FORCECLID),
	AST_APP_OPTION('g', OPT_GO_ON),
	AST_APP_OPTION_ARG('G', OPT_GOTO, OPT_ARG_GOTO),
	AST_APP_OPTION('h', OPT_CALLEE_HANGUP),
	AST_APP_OPTION('H', OPT_CALLER_HANGUP),
	AST_APP_OPTION('i', OPT_IGNORE_FORWARDING),
	AST_APP_OPTION('j', OPT_PRIORITY_JUMP),
	AST_APP_OPTION('k', OPT_CALLEE_PARK),
	AST_APP_OPTION('K', OPT_CALLER_PARK),
	AST_APP_OPTION_ARG('L', OPT_DURATION_LIMIT, OPT_ARG_DURATION_LIMIT),
	AST_APP_OPTION_ARG('m', OPT_MUSICBACK, OPT_ARG_MUSICBACK),
	AST_APP_OPTION_ARG('M', OPT_CALLEE_MACRO, OPT_ARG_CALLEE_MACRO),
	AST_APP_OPTION_ARG('n', OPT_SCREEN_NOINTRO, OPT_ARG_SCREEN_NOINTRO),
	AST_APP_OPTION('N', OPT_SCREEN_NOCLID),
	AST_APP_OPTION('o', OPT_ORIGINAL_CLID),
	AST_APP_OPTION_ARG('O', OPT_OPERMODE,OPT_ARG_OPERMODE),
	AST_APP_OPTION('p', OPT_SCREENING),
	AST_APP_OPTION_ARG('P', OPT_PRIVACY, OPT_ARG_PRIVACY),
	AST_APP_OPTION('r', OPT_RINGBACK),
	AST_APP_OPTION_ARG('S', OPT_DURATION_STOP, OPT_ARG_DURATION_STOP),
	AST_APP_OPTION('t', OPT_CALLEE_TRANSFER),
	AST_APP_OPTION('T', OPT_CALLER_TRANSFER),
	AST_APP_OPTION('w', OPT_CALLEE_MONITOR),
	AST_APP_OPTION('W', OPT_CALLER_MONITOR),
});

#define CAN_EARLY_BRIDGE(flags,chan,peer) (!ast_test_flag(flags, OPT_CALLEE_HANGUP | \
	OPT_CALLER_HANGUP | OPT_CALLEE_TRANSFER | OPT_CALLER_TRANSFER | \
	OPT_CALLEE_MONITOR | OPT_CALLER_MONITOR | OPT_CALLEE_PARK |  \
	OPT_CALLER_PARK | OPT_ANNOUNCE | OPT_CALLEE_MACRO) && \
	!chan->audiohooks && !peer->audiohooks)

/* We define a custom "local user" structure because we
   use it not only for keeping track of what is in use but
   also for keeping track of who we're dialing. */

struct dial_localuser {
	struct ast_channel *chan;
	unsigned int flags;
	struct dial_localuser *next;
};


static void hanguptree(struct dial_localuser *outgoing, struct ast_channel *exception)
{
	/* Hang up a tree of stuff */
	struct dial_localuser *oo;
	while (outgoing) {
		/* Hangup any existing lines we have open */
		if (outgoing->chan && (outgoing->chan != exception))
			ast_hangup(outgoing->chan);
		oo = outgoing;
		outgoing=outgoing->next;
		free(oo);
	}
}

#define AST_MAX_WATCHERS 256

#define HANDLE_CAUSE(cause, chan) do { \
	switch(cause) { \
	case AST_CAUSE_BUSY: \
		if (chan->cdr) \
			ast_cdr_busy(chan->cdr); \
		numbusy++; \
		break; \
	case AST_CAUSE_CONGESTION: \
		if (chan->cdr) \
			ast_cdr_failed(chan->cdr); \
		numcongestion++; \
		break; \
	case AST_CAUSE_NO_ROUTE_DESTINATION: \
	case AST_CAUSE_UNREGISTERED: \
		if (chan->cdr) \
			ast_cdr_failed(chan->cdr); \
		numnochan++; \
		break; \
	case AST_CAUSE_NO_ANSWER: \
		if (chan->cdr) \
			ast_cdr_noanswer(chan->cdr); \
		break; \
	case AST_CAUSE_NORMAL_CLEARING: \
		break; \
	default: \
		numnochan++; \
		break; \
	} \
} while (0)


static int onedigit_goto(struct ast_channel *chan, const char *context, char exten, int pri) 
{
	char rexten[2] = { exten, '\0' };

	if (context) {
		if (!ast_goto_if_exists(chan, context, rexten, pri))
			return 1;
	} else {
		if (!ast_goto_if_exists(chan, chan->context, rexten, pri))
			return 1;
		else if (!ast_strlen_zero(chan->macrocontext)) {
			if (!ast_goto_if_exists(chan, chan->macrocontext, rexten, pri))
				return 1;
		}
	}
	return 0;
}

static int detect_disconnect(struct ast_channel *chan, char code, char *featurecode, int len);

static const char *get_cid_name(char *name, int namelen, struct ast_channel *chan)
{
	const char *context = S_OR(chan->macrocontext, chan->context);
	const char *exten = S_OR(chan->macroexten, chan->exten);

	return ast_get_hint(NULL, 0, name, namelen, chan, context, exten) ? name : "";
}

static void senddialevent(struct ast_channel *src, struct ast_channel *dst)
{
	/* XXX do we need also CallerIDnum ? */
	manager_event(EVENT_FLAG_CALL, "Dial", 
			   "Source: %s\r\n"
			   "Destination: %s\r\n"
			   "CallerID: %s\r\n"
			   "CallerIDName: %s\r\n"
			   "SrcUniqueID: %s\r\n"
			   "DestUniqueID: %s\r\n",
			   src->name, dst->name, S_OR(src->cid.cid_num, "<unknown>"),
			   S_OR(src->cid.cid_name, "<unknown>"), src->uniqueid,
			   dst->uniqueid);
}

static struct ast_channel *wait_for_answer(struct ast_channel *in, struct dial_localuser *outgoing, int *to, struct ast_flags *peerflags, int *sentringing, char *status, size_t statussize, int busystart, int nochanstart, int congestionstart, int priority_jump, int *result)
{
	int numbusy = busystart;
	int numcongestion = congestionstart;
	int numnochan = nochanstart;
	int prestart = busystart + congestionstart + nochanstart;
	int orig = *to;
	struct ast_channel *peer = NULL;
	/* single is set if only one destination is enabled */
	int single = outgoing && !outgoing->next && !ast_test_flag(outgoing, OPT_MUSICBACK | OPT_RINGBACK);

	char featurecode[FEATURE_MAX_LEN + 1] = { 0, };

	if (single) {
		/* Turn off hold music, etc */
		ast_deactivate_generator(in);
		/* If we are calling a single channel, make them compatible for in-band tone purpose */
		if (ast_channel_make_compatible(outgoing->chan, in) < 0) {
			/* If these channels can not be made compatible, 
			 * there is no point in continuing.  The bridge
			 * will just fail if it gets that far.
			 */
			*to = -1;
			strcpy(status, "CONGESTION");
			ast_cdr_failed(in->cdr);
			return NULL;
		}
	}
	
	
	while (*to && !peer) {
		struct dial_localuser *o;
		int pos = 0;	/* how many channels do we handle */
		int numlines = prestart;
		struct ast_channel *winner;
		struct ast_channel *watchers[AST_MAX_WATCHERS];

		watchers[pos++] = in;
		for (o = outgoing; o; o = o->next) {
			/* Keep track of important channels */
			if (ast_test_flag(o, DIAL_STILLGOING) && o->chan)
				watchers[pos++] = o->chan;
			numlines++;
		}
		if (pos == 1) {	/* only the input channel is available */
			if (numlines == (numbusy + numcongestion + numnochan)) {
				if (option_verbose > 2)
					ast_verbose( VERBOSE_PREFIX_2 "Everyone is busy/congested at this time (%d:%d/%d/%d)\n", numlines, numbusy, numcongestion, numnochan);
				if (numbusy)
					strcpy(status, "BUSY");	
				else if (numcongestion)
					strcpy(status, "CONGESTION");
				else if (numnochan)
					strcpy(status, "CHANUNAVAIL");
				if (ast_opt_priority_jumping || priority_jump)
					ast_goto_if_exists(in, in->context, in->exten, in->priority + 101);
			} else {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "No one is available to answer at this time (%d:%d/%d/%d)\n", numlines, numbusy, numcongestion, numnochan);
			}
			*to = 0;
			return NULL;
		}
		winner = ast_waitfor_n(watchers, pos, to);
		for (o = outgoing; o; o = o->next) {
			struct ast_frame *f;
			struct ast_channel *c = o->chan;

			if (c == NULL)
				continue;
			if (ast_test_flag(o, DIAL_STILLGOING) && c->_state == AST_STATE_UP) {
				if (!peer) {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "%s answered %s\n", c->name, in->name);
					peer = c;
					ast_copy_flags(peerflags, o,
						       OPT_CALLEE_TRANSFER | OPT_CALLER_TRANSFER |
						       OPT_CALLEE_HANGUP | OPT_CALLER_HANGUP |
						       OPT_CALLEE_MONITOR | OPT_CALLER_MONITOR |
						       OPT_CALLEE_PARK | OPT_CALLER_PARK |
						       DIAL_NOFORWARDHTML);
					ast_copy_string(c->dialcontext, "", sizeof(c->dialcontext));
					ast_copy_string(c->exten, "", sizeof(c->exten));
				}
				continue;
			}
			if (c != winner)
				continue;
			if (!ast_strlen_zero(c->call_forward)) {
				char tmpchan[256];
				char *stuff;
				char *tech;
				int cause;

				ast_copy_string(tmpchan, c->call_forward, sizeof(tmpchan));
				if ((stuff = strchr(tmpchan, '/'))) {
					*stuff++ = '\0';
					tech = tmpchan;
				} else {
					const char *forward_context = pbx_builtin_getvar_helper(c, "FORWARD_CONTEXT");
					if (ast_strlen_zero(forward_context)) {
						forward_context = NULL;
					}
					snprintf(tmpchan, sizeof(tmpchan), "%s@%s", c->call_forward, forward_context ? forward_context : c->context);
					stuff = tmpchan;
					tech = "Local";
				}
				/* Before processing channel, go ahead and check for forwarding */
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Now forwarding %s to '%s/%s' (thanks to %s)\n", in->name, tech, stuff, c->name);
				/* If we have been told to ignore forwards, just set this channel to null and continue processing extensions normally */
				if (ast_test_flag(peerflags, OPT_IGNORE_FORWARDING)) {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "Forwarding %s to '%s/%s' prevented.\n", in->name, tech, stuff);
					c = o->chan = NULL;
					cause = AST_CAUSE_BUSY;
				} else {
					/* Setup parameters */
					if ((c = o->chan = ast_request(tech, in->nativeformats, stuff, &cause))) {
						if (single)
							ast_channel_make_compatible(o->chan, in);
						ast_channel_inherit_variables(in, o->chan);
						ast_channel_datastore_inherit(in, o->chan);
					} else
						ast_log(LOG_NOTICE,
							"Forwarding failed to create channel to dial '%s/%s' (cause = %d)\n",
							tech, stuff, cause);
				}
				if (!c) {
					ast_clear_flag(o, DIAL_STILLGOING);	
					HANDLE_CAUSE(cause, in);
				} else {
					if (CAN_EARLY_BRIDGE(peerflags, c, in)) {
						ast_rtp_make_compatible(c, in, single);
					}
					if (c->cid.cid_num)
						free(c->cid.cid_num);
					c->cid.cid_num = NULL;
					if (c->cid.cid_name)
						free(c->cid.cid_name);
					c->cid.cid_name = NULL;

					if (ast_test_flag(o, OPT_FORCECLID)) {
						c->cid.cid_num = ast_strdup(S_OR(in->macroexten, in->exten));
						ast_string_field_set(c, accountcode, winner->accountcode);
						c->cdrflags = winner->cdrflags;
					} else {
						c->cid.cid_num = ast_strdup(in->cid.cid_num);
						c->cid.cid_name = ast_strdup(in->cid.cid_name);
						ast_string_field_set(c, accountcode, in->accountcode);
						c->cdrflags = in->cdrflags;
					}

					if (in->cid.cid_ani) {
						if (c->cid.cid_ani)
							free(c->cid.cid_ani);
						c->cid.cid_ani = ast_strdup(in->cid.cid_ani);
					}
					if (c->cid.cid_rdnis) 
						free(c->cid.cid_rdnis);
					c->cid.cid_rdnis = ast_strdup(S_OR(in->macroexten, in->exten));
					if (ast_call(c, stuff, 0)) {
						ast_log(LOG_NOTICE, "Forwarding failed to dial '%s/%s'\n",
							tech, stuff);
						ast_clear_flag(o, DIAL_STILLGOING);	
						ast_hangup(c);
						c = o->chan = NULL;
						numnochan++;
					} else {
						senddialevent(in, c);
						/* After calling, set callerid to extension */
						if (!ast_test_flag(peerflags, OPT_ORIGINAL_CLID)) {
							char cidname[AST_MAX_EXTENSION] = "";
							ast_set_callerid(c, S_OR(in->macroexten, in->exten), get_cid_name(cidname, sizeof(cidname), in), NULL);
						}
					}
					if (single) {
						ast_indicate(in, -1);
					}
				}
				/* Hangup the original channel now, in case we needed it */
				ast_hangup(winner);
				continue;
			}
			f = ast_read(winner);
			if (!f) {
				in->hangupcause = c->hangupcause;
				ast_hangup(c);
				c = o->chan = NULL;
				ast_clear_flag(o, DIAL_STILLGOING);
				HANDLE_CAUSE(in->hangupcause, in);
				continue;
			}
			if (f->frametype == AST_FRAME_CONTROL) {
				switch(f->subclass) {
				case AST_CONTROL_ANSWER:
					/* This is our guy if someone answered. */
					if (!peer) {
						if (option_verbose > 2)
							ast_verbose( VERBOSE_PREFIX_3 "%s answered %s\n", c->name, in->name);
						peer = c;
						if (peer->cdr) {
							peer->cdr->answer = ast_tvnow();
							peer->cdr->disposition = AST_CDR_ANSWERED;
						}
						ast_copy_flags(peerflags, o,
							       OPT_CALLEE_TRANSFER | OPT_CALLER_TRANSFER |
							       OPT_CALLEE_HANGUP | OPT_CALLER_HANGUP |
							       OPT_CALLEE_MONITOR | OPT_CALLER_MONITOR |
							       OPT_CALLEE_PARK | OPT_CALLER_PARK |
							       DIAL_NOFORWARDHTML);
						ast_copy_string(c->dialcontext, "", sizeof(c->dialcontext));
						ast_copy_string(c->exten, "", sizeof(c->exten));
						/* Setup RTP early bridge if appropriate */
						if (CAN_EARLY_BRIDGE(peerflags, in, peer))
							ast_rtp_early_bridge(in, peer);
					}
					/* If call has been answered, then the eventual hangup is likely to be normal hangup */
					in->hangupcause = AST_CAUSE_NORMAL_CLEARING;
					c->hangupcause = AST_CAUSE_NORMAL_CLEARING;
					break;
				case AST_CONTROL_BUSY:
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "%s is busy\n", c->name);
					in->hangupcause = c->hangupcause;
					ast_hangup(c);
					c = o->chan = NULL;
					ast_clear_flag(o, DIAL_STILLGOING);	
					HANDLE_CAUSE(AST_CAUSE_BUSY, in);
					break;
				case AST_CONTROL_CONGESTION:
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "%s is circuit-busy\n", c->name);
					in->hangupcause = c->hangupcause;
					ast_hangup(c);
					c = o->chan = NULL;
					ast_clear_flag(o, DIAL_STILLGOING);
					HANDLE_CAUSE(AST_CAUSE_CONGESTION, in);
					break;
				case AST_CONTROL_RINGING:
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "%s is ringing\n", c->name);
					/* Setup early media if appropriate */
					if (single && CAN_EARLY_BRIDGE(peerflags, in, c))
						ast_rtp_early_bridge(in, c);
					if (!(*sentringing) && !ast_test_flag(outgoing, OPT_MUSICBACK)) {
						ast_indicate(in, AST_CONTROL_RINGING);
						(*sentringing)++;
					}
					break;
				case AST_CONTROL_PROGRESS:
					if (option_verbose > 2)
						ast_verbose (VERBOSE_PREFIX_3 "%s is making progress passing it to %s\n", c->name, in->name);
					/* Setup early media if appropriate */
					if (single && CAN_EARLY_BRIDGE(peerflags, in, c))
						ast_rtp_early_bridge(in, c);
					if (!ast_test_flag(outgoing, OPT_RINGBACK)) {
							if (single || (!single && !(*sentringing))) {
							/* want progress to go through if it's a single legged call or it's a
							 * branched call and ringing has not been sent */
								ast_indicate(in, AST_CONTROL_PROGRESS);
							}
					}
					break;
				case AST_CONTROL_VIDUPDATE:
					if (option_verbose > 2)
						ast_verbose (VERBOSE_PREFIX_3 "%s requested a video update, passing it to %s\n", c->name, in->name);
					ast_indicate(in, AST_CONTROL_VIDUPDATE);
					break;
				case AST_CONTROL_SRCUPDATE:
					if (option_verbose > 2)
						ast_verbose (VERBOSE_PREFIX_3 "%s requested a source update, passing it to %s\n", c->name, in->name);
					ast_indicate(in, AST_CONTROL_SRCUPDATE);
					break;
				case AST_CONTROL_PROCEEDING:
					if (option_verbose > 2)
						ast_verbose (VERBOSE_PREFIX_3 "%s is proceeding passing it to %s\n", c->name, in->name);
					if (single && CAN_EARLY_BRIDGE(peerflags, in, c))
						ast_rtp_early_bridge(in, c);
					if (!ast_test_flag(outgoing, OPT_RINGBACK))
						ast_indicate(in, AST_CONTROL_PROCEEDING);
					break;
				case AST_CONTROL_HOLD:
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "Call on %s placed on hold\n", c->name);
					ast_indicate(in, AST_CONTROL_HOLD);
					break;
				case AST_CONTROL_UNHOLD:
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "Call on %s left from hold\n", c->name);
					ast_indicate(in, AST_CONTROL_UNHOLD);
					break;
				case AST_CONTROL_OFFHOOK:
				case AST_CONTROL_FLASH:
					/* Ignore going off hook and flash */
					break;
				case -1:
					if (!ast_test_flag(outgoing, OPT_RINGBACK | OPT_MUSICBACK)) {
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "%s stopped sounds\n", c->name);
						ast_indicate(in, -1);
						(*sentringing) = 0;
					}
					break;
				default:
					if (option_debug)
						ast_log(LOG_DEBUG, "Dunno what to do with control type %d\n", f->subclass);
				}
			} else if (single) {
				/* XXX are we sure the logic is correct ? or we should just switch on f->frametype ? */
				if (f->frametype == AST_FRAME_VOICE && !ast_test_flag(outgoing, OPT_RINGBACK|OPT_MUSICBACK)) {
					if (ast_write(in, f)) 
						ast_log(LOG_WARNING, "Unable to forward voice frame\n");
				} else if (f->frametype == AST_FRAME_IMAGE && !ast_test_flag(outgoing, OPT_RINGBACK|OPT_MUSICBACK)) {
					if (ast_write(in, f))
						ast_log(LOG_WARNING, "Unable to forward image\n");
				} else if (f->frametype == AST_FRAME_TEXT && !ast_test_flag(outgoing, OPT_RINGBACK|OPT_MUSICBACK)) {
					if (ast_write(in, f))
						ast_log(LOG_WARNING, "Unable to send text\n");
				} else if (f->frametype == AST_FRAME_HTML && !ast_test_flag(outgoing, DIAL_NOFORWARDHTML)) {
					if (ast_channel_sendhtml(in, f->subclass, f->data, f->datalen) == -1)
						ast_log(LOG_WARNING, "Unable to send URL\n");
				}
			}
			ast_frfree(f);
		} /* end for */
		if (winner == in) {
			struct ast_frame *f = ast_read(in);
#if 0
			if (f && (f->frametype != AST_FRAME_VOICE))
				printf("Frame type: %d, %d\n", f->frametype, f->subclass);
			else if (!f || (f->frametype != AST_FRAME_VOICE))
				printf("Hangup received on %s\n", in->name);
#endif
			if (!f || ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_HANGUP))) {
				/* Got hung up */
				*to = -1;
				ast_cdr_noanswer(in->cdr);
				strcpy(status, "CANCEL");
				if (f)
					ast_frfree(f);
				return NULL;
			}

			if (f && (f->frametype == AST_FRAME_DTMF)) {
				if (ast_test_flag(peerflags, OPT_DTMF_EXIT)) {
					const char *context = pbx_builtin_getvar_helper(in, "EXITCONTEXT");
					if (onedigit_goto(in, context, (char) f->subclass, 1)) {
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "User hit %c to disconnect call.\n", f->subclass);
						*to=0;
						ast_cdr_noanswer(in->cdr);
						*result = f->subclass;
						strcpy(status, "CANCEL");
						ast_frfree(f);
						return NULL;
					}
				}

				if (ast_test_flag(peerflags, OPT_CALLER_HANGUP) &&
					detect_disconnect(in, f->subclass, featurecode, sizeof(featurecode))) {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "User requested call disconnect.\n");
					*to=0;
					ast_cdr_noanswer(in->cdr);
					strcpy(status, "CANCEL");
					ast_frfree(f);
					return NULL;
				}
			}

			/* Forward HTML stuff */
			if (single && f && (f->frametype == AST_FRAME_HTML) && !ast_test_flag(outgoing, DIAL_NOFORWARDHTML)) 
				if(ast_channel_sendhtml(outgoing->chan, f->subclass, f->data, f->datalen) == -1)
					ast_log(LOG_WARNING, "Unable to send URL\n");
			

			if (single && ((f->frametype == AST_FRAME_VOICE) || (f->frametype == AST_FRAME_DTMF_BEGIN) || (f->frametype == AST_FRAME_DTMF_END)))  {
				if (ast_write(outgoing->chan, f))
					ast_log(LOG_WARNING, "Unable to forward voice or dtmf\n");
			}
			if (single && (f->frametype == AST_FRAME_CONTROL) && 
				((f->subclass == AST_CONTROL_HOLD) || 
				 (f->subclass == AST_CONTROL_UNHOLD) || 
				 (f->subclass == AST_CONTROL_VIDUPDATE) ||
				 (f->subclass == AST_CONTROL_SRCUPDATE))) {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "%s requested special control %d, passing it to %s\n", in->name, f->subclass, outgoing->chan->name);
				ast_indicate_data(outgoing->chan, f->subclass, f->data, f->datalen);
			}
			ast_frfree(f);
		}
		if (!*to && (option_verbose > 2))
			ast_verbose(VERBOSE_PREFIX_3 "Nobody picked up in %d ms\n", orig);
		if (!*to || ast_check_hangup(in)) {
			ast_cdr_noanswer(in->cdr);
		}
		
	}
	
	return peer;
}

static int detect_disconnect(struct ast_channel *chan, char code, char *featurecode, int len)
{
	struct ast_flags features = { AST_FEATURE_DISCONNECT }; /* only concerned with disconnect feature */
	struct ast_call_feature feature = { 0, };
	char *tmp;
	int res;

	if ((strlen(featurecode)) < (len - 2)) { 
		tmp = &featurecode[strlen(featurecode)];
		tmp[0] = code;
		tmp[1] = '\0';
	} else {
		featurecode[0] = 0;
		return -1; /* no room in featurecode buffer */
	}

	res = ast_feature_detect(chan, &features, featurecode, &feature);

	if (res != FEATURE_RETURN_STOREDIGITS) {
		featurecode[0] = '\0';
	}
	if (feature.feature_mask & AST_FEATURE_DISCONNECT) {
		return 1;
	}

	return 0;
}

static void replace_macro_delimiter(char *s)
{
	for (; *s; s++)
		if (*s == '^')
			*s = '|';
}


/* returns true if there is a valid privacy reply */
static int valid_priv_reply(struct ast_flags *opts, int res)
{
	if (res < '1')
		return 0;
	if (ast_test_flag(opts, OPT_PRIVACY) && res <= '5')
		return 1;
	if (ast_test_flag(opts, OPT_SCREENING) && res <= '4')
		return 1;
	return 0;
}

static void end_bridge_callback (void *data)
{
	char buf[80];
	time_t end;
	struct ast_channel *chan = data;

	if (!chan->cdr) {
		return;
	}

	time(&end);

	ast_channel_lock(chan);
	if (chan->cdr->answer.tv_sec) {
		snprintf(buf, sizeof(buf), "%ld", (long) end - chan->cdr->answer.tv_sec);
		pbx_builtin_setvar_helper(chan, "ANSWEREDTIME", buf);
	}

	if (chan->cdr->start.tv_sec) {
		snprintf(buf, sizeof(buf), "%ld", (long) end - chan->cdr->start.tv_sec);
		pbx_builtin_setvar_helper(chan, "DIALEDTIME", buf);
	}
	ast_channel_unlock(chan);
}

static void end_bridge_callback_data_fixup(struct ast_bridge_config *bconfig, struct ast_channel *originator, struct ast_channel *terminator) {
	bconfig->end_bridge_callback_data = originator;
}

static int dial_exec_full(struct ast_channel *chan, void *data, struct ast_flags *peerflags, int *continue_exec)
{
	int res = -1;
	struct ast_module_user *u;
	char *rest, *cur;
	struct dial_localuser *outgoing = NULL;
	struct ast_channel *peer;
	int to;
	int numbusy = 0;
	int numcongestion = 0;
	int numnochan = 0;
	int cause;
	char numsubst[256];
	char cidname[AST_MAX_EXTENSION] = "";
	int privdb_val = 0;
	int calldurationlimit = -1;
	long timelimit = 0;
	long play_warning = 0;
	long warning_freq = 0;
	const char *warning_sound = NULL;
	const char *end_sound = NULL;
	const char *start_sound = NULL;
	char *dtmfcalled = NULL, *dtmfcalling = NULL;
	char status[256] = "INVALIDARGS";
	int play_to_caller = 0, play_to_callee = 0;
	int sentringing = 0, moh = 0;
	const char *outbound_group = NULL;
	int result = 0;
	time_t start_time;
	char privintro[1024];
	char privcid[256];
	char *parse;
	int opermode = 0;
	int delprivintro = 0;
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(peers);
			     AST_APP_ARG(timeout);
			     AST_APP_ARG(options);
			     AST_APP_ARG(url);
	);
	struct ast_flags opts = { 0, };
	char *opt_args[OPT_ARG_ARRAY_SIZE];
	struct ast_datastore *datastore = NULL;
	int fulldial = 0, num_dialed = 0;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Dial requires an argument (technology/number)\n");
		pbx_builtin_setvar_helper(chan, "DIALSTATUS", status);
		return -1;
	}

	/* Reset all DIAL variables back to blank, to prevent confusion (in case we don't reset all of them). */
	pbx_builtin_setvar_helper(chan, "DIALSTATUS", "");
	pbx_builtin_setvar_helper(chan, "DIALEDPEERNUMBER", "");
	pbx_builtin_setvar_helper(chan, "DIALEDPEERNAME", "");
	pbx_builtin_setvar_helper(chan, "ANSWEREDTIME", "");
	pbx_builtin_setvar_helper(chan, "DIALEDTIME", "");

	u = ast_module_user_add(chan);

	parse = ast_strdupa(data);
	
	AST_STANDARD_APP_ARGS(args, parse);

	if (!ast_strlen_zero(args.options) &&
			ast_app_parse_options(dial_exec_options, &opts, opt_args, args.options)) {
		pbx_builtin_setvar_helper(chan, "DIALSTATUS", status);
		goto done;
	}

	if (ast_strlen_zero(args.peers)) {
		ast_log(LOG_WARNING, "Dial requires an argument (technology/number)\n");
		pbx_builtin_setvar_helper(chan, "DIALSTATUS", status);
		goto done;
	}

	if (ast_test_flag(&opts, OPT_SCREEN_NOINTRO)) {
		if (!ast_strlen_zero(opt_args[OPT_ARG_SCREEN_NOINTRO])) {
			int mode = atoi(opt_args[OPT_ARG_SCREEN_NOINTRO]);
			if (mode < 0 || mode > 1) {
				ast_log(LOG_WARNING, "Unknown argument %d specified to n option, ignoring\n", mode);
			} else {
				delprivintro = mode;
			}
		}
	}

	if (ast_test_flag(&opts, OPT_OPERMODE)) {
		if (ast_strlen_zero(opt_args[OPT_ARG_OPERMODE]))
			opermode = 1;
		else opermode = atoi(opt_args[OPT_ARG_OPERMODE]);
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Setting operator services mode to %d.\n", opermode);
	}
	
	if (ast_test_flag(&opts, OPT_DURATION_STOP) && !ast_strlen_zero(opt_args[OPT_ARG_DURATION_STOP])) {
		calldurationlimit = atoi(opt_args[OPT_ARG_DURATION_STOP]);
		if (!calldurationlimit) {
			ast_log(LOG_WARNING, "Dial does not accept S(%s), hanging up.\n", opt_args[OPT_ARG_DURATION_STOP]);
			pbx_builtin_setvar_helper(chan, "DIALSTATUS", status);
			goto done;
		}
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "Setting call duration limit to %d milliseconds.\n", calldurationlimit);
	}

	if (ast_test_flag(&opts, OPT_SENDDTMF) && !ast_strlen_zero(opt_args[OPT_ARG_SENDDTMF])) {
		dtmfcalling = opt_args[OPT_ARG_SENDDTMF];
		dtmfcalled = strsep(&dtmfcalling, ":");
	}

	if (ast_test_flag(&opts, OPT_DURATION_LIMIT) && !ast_strlen_zero(opt_args[OPT_ARG_DURATION_LIMIT])) {
		char *limit_str, *warning_str, *warnfreq_str;
		const char *var;

		warnfreq_str = opt_args[OPT_ARG_DURATION_LIMIT];
		limit_str = strsep(&warnfreq_str, ":");
		warning_str = strsep(&warnfreq_str, ":");

		timelimit = atol(limit_str);
		if (warning_str)
			play_warning = atol(warning_str);
		if (warnfreq_str)
			warning_freq = atol(warnfreq_str);

		if (!timelimit) {
			ast_log(LOG_WARNING, "Dial does not accept L(%s), hanging up.\n", limit_str);
			goto done;
		} else if (play_warning > timelimit) {
			/* If the first warning is requested _after_ the entire call would end,
			   and no warning frequency is requested, then turn off the warning. If
			   a warning frequency is requested, reduce the 'first warning' time by
			   that frequency until it falls within the call's total time limit.
			*/

			if (!warning_freq) {
				play_warning = 0;
			} else {
				/* XXX fix this!! */
				while (play_warning > timelimit)
					play_warning -= warning_freq;
				if (play_warning < 1)
					play_warning = warning_freq = 0;
			}
		}

		var = pbx_builtin_getvar_helper(chan,"LIMIT_PLAYAUDIO_CALLER");
		play_to_caller = var ? ast_true(var) : 1;
		
		var = pbx_builtin_getvar_helper(chan,"LIMIT_PLAYAUDIO_CALLEE");
		play_to_callee = var ? ast_true(var) : 0;
		
		if (!play_to_caller && !play_to_callee)
			play_to_caller = 1;
		
		var = pbx_builtin_getvar_helper(chan,"LIMIT_WARNING_FILE");
		warning_sound = S_OR(var, "timeleft");
		
		var = pbx_builtin_getvar_helper(chan,"LIMIT_TIMEOUT_FILE");
		end_sound = S_OR(var, "");
		
		var = pbx_builtin_getvar_helper(chan,"LIMIT_CONNECT_FILE");
		start_sound = S_OR(var, "");

		/* undo effect of S(x) in case they are both used */
		calldurationlimit = -1;
		/* more efficient to do it like S(x) does since no advanced opts */
		if (!play_warning && !start_sound && !end_sound && timelimit) {
			calldurationlimit = timelimit / 1000;
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Setting call duration limit to %d milliseconds.\n", calldurationlimit);
			timelimit = play_to_caller = play_to_callee = play_warning = warning_freq = 0;
		} else if (option_verbose > 2) {
			ast_verbose(VERBOSE_PREFIX_3 "Limit Data for this call:\n");
			ast_verbose(VERBOSE_PREFIX_4 "timelimit      = %ld\n", timelimit);
			ast_verbose(VERBOSE_PREFIX_4 "play_warning   = %ld\n", play_warning);
			ast_verbose(VERBOSE_PREFIX_4 "play_to_caller = %s\n", play_to_caller ? "yes" : "no");
			ast_verbose(VERBOSE_PREFIX_4 "play_to_callee = %s\n", play_to_callee ? "yes" : "no");
			ast_verbose(VERBOSE_PREFIX_4 "warning_freq   = %ld\n", warning_freq);
			ast_verbose(VERBOSE_PREFIX_4 "start_sound    = %s\n", start_sound);
			ast_verbose(VERBOSE_PREFIX_4 "warning_sound  = %s\n", warning_sound);
			ast_verbose(VERBOSE_PREFIX_4 "end_sound      = %s\n", end_sound);
		}
	}

	if (ast_test_flag(&opts, OPT_RESETCDR) && chan->cdr)
		ast_cdr_reset(chan->cdr, NULL);
	if (ast_test_flag(&opts, OPT_PRIVACY) && ast_strlen_zero(opt_args[OPT_ARG_PRIVACY]))
		opt_args[OPT_ARG_PRIVACY] = ast_strdupa(chan->exten);
	if (ast_test_flag(&opts, OPT_PRIVACY) || ast_test_flag(&opts, OPT_SCREENING)) {
		char callerid[60];
		char *l = chan->cid.cid_num;	/* XXX watch out, we are overwriting it */
		if (!ast_strlen_zero(l)) {
			ast_shrink_phone_number(l);
			if( ast_test_flag(&opts, OPT_PRIVACY) ) {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3  "Privacy DB is '%s', clid is '%s'\n",
						     opt_args[OPT_ARG_PRIVACY], l);
				privdb_val = ast_privacy_check(opt_args[OPT_ARG_PRIVACY], l);
			}
			else {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3  "Privacy Screening, clid is '%s'\n", l);
				privdb_val = AST_PRIVACY_UNKNOWN;
			}
		} else {
			char *tnam, *tn2;

			tnam = ast_strdupa(chan->name);
			/* clean the channel name so slashes don't try to end up in disk file name */
			for(tn2 = tnam; *tn2; tn2++) {
				if( *tn2=='/')
					*tn2 = '=';  /* any other chars to be afraid of? */
			}
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3  "Privacy-- callerid is empty\n");

			snprintf(callerid, sizeof(callerid), "NOCALLERID_%s%s", chan->exten, tnam);
			l = callerid;
			privdb_val = AST_PRIVACY_UNKNOWN;
		}
		
		ast_copy_string(privcid,l,sizeof(privcid));

		if( strncmp(privcid,"NOCALLERID",10) != 0 && ast_test_flag(&opts, OPT_SCREEN_NOCLID) ) { /* if callerid is set, and ast_test_flag(&opts, OPT_SCREEN_NOCLID) is set also */  
			if (option_verbose > 2)
				ast_verbose( VERBOSE_PREFIX_3  "CallerID set (%s); N option set; Screening should be off\n", privcid);
			privdb_val = AST_PRIVACY_ALLOW;
		}
		else if(ast_test_flag(&opts, OPT_SCREEN_NOCLID) && strncmp(privcid,"NOCALLERID",10) == 0 ) {
			if (option_verbose > 2)
				ast_verbose( VERBOSE_PREFIX_3  "CallerID blank; N option set; Screening should happen; dbval is %d\n", privdb_val);
		}
		
		if(privdb_val == AST_PRIVACY_DENY ) {
			ast_copy_string(status, "NOANSWER", sizeof(status));
			if (option_verbose > 2)
				ast_verbose( VERBOSE_PREFIX_3  "Privacy DB reports PRIVACY_DENY for this callerid. Dial reports unavailable\n");
			res=0;
			goto out;
		}
		else if(privdb_val == AST_PRIVACY_KILL ) {
			ast_copy_string(status, "DONTCALL", sizeof(status));
			if (ast_opt_priority_jumping || ast_test_flag(&opts, OPT_PRIORITY_JUMP)) {
				ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 201);
			}
			res = 0;
			goto out; /* Is this right? */
		}
		else if(privdb_val == AST_PRIVACY_TORTURE ) {
			ast_copy_string(status, "TORTURE", sizeof(status));
			if (ast_opt_priority_jumping || ast_test_flag(&opts, OPT_PRIORITY_JUMP)) {
				ast_goto_if_exists(chan, chan->context, chan->exten, chan->priority + 301);
			}
			res = 0;
			goto out; /* is this right??? */
		}
		else if(privdb_val == AST_PRIVACY_UNKNOWN ) {
			/* Get the user's intro, store it in priv-callerintros/$CID, 
			   unless it is already there-- this should be done before the 
			   call is actually dialed  */

			/* make sure the priv-callerintros dir actually exists */
			snprintf(privintro, sizeof(privintro), "%s/sounds/priv-callerintros", ast_config_AST_DATA_DIR);
			if (mkdir(privintro, 0755) && errno != EEXIST) {
				ast_log(LOG_WARNING, "privacy: can't create directory priv-callerintros: %s\n", strerror(errno));
				res = -1;
				goto out;
			}

			snprintf(privintro,sizeof(privintro), "priv-callerintros/%s", privcid);
			if( ast_fileexists(privintro,NULL,NULL ) > 0 && strncmp(privcid,"NOCALLERID",10) != 0) {
				/* the DELUX version of this code would allow this caller the
				   option to hear and retape their previously recorded intro.
				*/
			}
			else {
				int duration; /* for feedback from play_and_wait */
				/* the file doesn't exist yet. Let the caller submit his
				   vocal intro for posterity */
				/* priv-recordintro script:

				   "At the tone, please say your name:"

				*/
				ast_answer(chan);
				res = ast_play_and_record(chan, "priv-recordintro", privintro, 4, "sln", &duration, 128, 2000, 0);  /* NOTE: I've reduced the total time to 4 sec */
										/* don't think we'll need a lock removed, we took care of
										   conflicts by naming the privintro file */
				if (res == -1) {
					/* Delete the file regardless since they hung up during recording */
					delprivintro = 1;
					goto out;
				}
                                if( !ast_streamfile(chan, "vm-dialout", chan->language) )
                                        ast_waitstream(chan, "");
			}
		}
	}

	if (continue_exec)
		*continue_exec = 0;
	
	/* If a channel group has been specified, get it for use when we create peer channels */
	if ((outbound_group = pbx_builtin_getvar_helper(chan, "OUTBOUND_GROUP_ONCE"))) {
		outbound_group = ast_strdupa(outbound_group);
		pbx_builtin_setvar_helper(chan, "OUTBOUND_GROUP_ONCE", NULL);
	} else {
		outbound_group = pbx_builtin_getvar_helper(chan, "OUTBOUND_GROUP");
	}
	    
	ast_copy_flags(peerflags, &opts, OPT_DTMF_EXIT | OPT_GO_ON | OPT_ORIGINAL_CLID | OPT_CALLER_HANGUP | OPT_IGNORE_FORWARDING | OPT_ANNOUNCE | OPT_CALLEE_MACRO);

	/* loop through the list of dial destinations */
	rest = args.peers;
	while ((cur = strsep(&rest, "&")) ) {
		struct dial_localuser *tmp;
		/* Get a technology/[device:]number pair */
		char *number = cur;
		char *interface = ast_strdupa(number);
		char *tech = strsep(&number, "/");
		/* find if we already dialed this interface */
		struct ast_dialed_interface *di;
		AST_LIST_HEAD(, ast_dialed_interface) *dialed_interfaces;
		num_dialed++;
		if (ast_strlen_zero(number)) {
			ast_log(LOG_WARNING, "Dial argument takes format (technology/[device:]number1)\n");
			goto out;
		}
		if (!(tmp = ast_calloc(1, sizeof(*tmp))))
			goto out;
		if (opts.flags) {
			ast_copy_flags(tmp, &opts,
				       OPT_CALLEE_TRANSFER | OPT_CALLER_TRANSFER |
				       OPT_CALLEE_HANGUP | OPT_CALLER_HANGUP |
				       OPT_CALLEE_MONITOR | OPT_CALLER_MONITOR |
				       OPT_CALLEE_PARK | OPT_CALLER_PARK |
				       OPT_RINGBACK | OPT_MUSICBACK | OPT_FORCECLID);
			ast_set2_flag(tmp, args.url, DIAL_NOFORWARDHTML);	
		}
		ast_copy_string(numsubst, number, sizeof(numsubst));
		/* Request the peer */

		ast_channel_lock(chan);
		datastore = ast_channel_datastore_find(chan, &dialed_interface_info, NULL);
		ast_channel_unlock(chan);

		if (datastore)
			dialed_interfaces = datastore->data;
		else {
			if (!(datastore = ast_channel_datastore_alloc(&dialed_interface_info, NULL))) {
				ast_log(LOG_WARNING, "Unable to create channel datastore for dialed interfaces. Aborting!\n"); 
				free(tmp);
				goto out;
			}

			datastore->inheritance = DATASTORE_INHERIT_FOREVER;

			if (!(dialed_interfaces = ast_calloc(1, sizeof(*dialed_interfaces)))) {
				ast_channel_datastore_free(datastore);
				free(tmp);
				goto out;
			}

			datastore->data = dialed_interfaces;
			AST_LIST_HEAD_INIT(dialed_interfaces);

			ast_channel_lock(chan);
			ast_channel_datastore_add(chan, datastore);
			ast_channel_unlock(chan);
		}

		AST_LIST_LOCK(dialed_interfaces);
		AST_LIST_TRAVERSE(dialed_interfaces, di, list) {
			if (!strcasecmp(di->interface, interface)) {
				ast_log(LOG_WARNING, "Skipping dialing interface '%s' again since it has already been dialed\n", 
					di->interface);
				break;
			}
		}
		AST_LIST_UNLOCK(dialed_interfaces);

		if (di) {
			fulldial++;
			free(tmp);
			continue;
		}

		/* It is always ok to dial a Local interface.  We only keep track of
		 * which "real" interfaces have been dialed.  The Local channel will
		 * inherit this list so that if it ends up dialing a real interface,
		 * it won't call one that has already been called. */
		if (strcasecmp(tech, "Local")) {
			if (!(di = ast_calloc(1, sizeof(*di) + strlen(interface)))) {
				AST_LIST_UNLOCK(dialed_interfaces);
				free(tmp);
				goto out;
			}
			strcpy(di->interface, interface);

			AST_LIST_LOCK(dialed_interfaces);
			AST_LIST_INSERT_TAIL(dialed_interfaces, di, list);
			AST_LIST_UNLOCK(dialed_interfaces);
		}

		tmp->chan = ast_request(tech, chan->nativeformats, numsubst, &cause);
		if (!tmp->chan) {
			/* If we can't, just go on to the next call */
			ast_log(LOG_WARNING, "Unable to create channel of type '%s' (cause %d - %s)\n", tech, cause, ast_cause2str(cause));
			HANDLE_CAUSE(cause, chan);
			if (!rest)	/* we are on the last destination */
				chan->hangupcause = cause;
			free(tmp);
			continue;
		}

		pbx_builtin_setvar_helper(tmp->chan, "DIALEDPEERNUMBER", numsubst);

		/* Setup outgoing SDP to match incoming one */
		if (CAN_EARLY_BRIDGE(peerflags, chan, tmp->chan)) {
			ast_rtp_make_compatible(tmp->chan, chan, !outgoing && !rest);
		}

		/* Inherit specially named variables from parent channel */
		ast_channel_inherit_variables(chan, tmp->chan);
		ast_channel_datastore_inherit(chan, tmp->chan);

		tmp->chan->appl = "AppDial";
		tmp->chan->data = "(Outgoing Line)";
		tmp->chan->whentohangup = 0;

		if (tmp->chan->cid.cid_num)
			free(tmp->chan->cid.cid_num);
		tmp->chan->cid.cid_num = ast_strdup(chan->cid.cid_num);

		if (tmp->chan->cid.cid_name)
			free(tmp->chan->cid.cid_name);
		tmp->chan->cid.cid_name = ast_strdup(chan->cid.cid_name);

		if (tmp->chan->cid.cid_ani)
			free(tmp->chan->cid.cid_ani);
		tmp->chan->cid.cid_ani = ast_strdup(chan->cid.cid_ani);
		
		/* Copy language from incoming to outgoing */
		ast_string_field_set(tmp->chan, language, chan->language);
		ast_string_field_set(tmp->chan, accountcode, chan->accountcode);
		tmp->chan->cdrflags = chan->cdrflags;
		if (ast_strlen_zero(tmp->chan->musicclass))
			ast_string_field_set(tmp->chan, musicclass, chan->musicclass);
		/* XXX don't we free previous values ? */
		tmp->chan->cid.cid_rdnis = ast_strdup(chan->cid.cid_rdnis);
		/* Pass callingpres setting */
		tmp->chan->cid.cid_pres = chan->cid.cid_pres;
		/* Pass type of number */
		tmp->chan->cid.cid_ton = chan->cid.cid_ton;
		/* Pass type of tns */
		tmp->chan->cid.cid_tns = chan->cid.cid_tns;
		/* Presense of ADSI CPE on outgoing channel follows ours */
		tmp->chan->adsicpe = chan->adsicpe;
		/* Pass the transfer capability */
		tmp->chan->transfercapability = chan->transfercapability;

		/* If we have an outbound group, set this peer channel to it */
		if (outbound_group)
			ast_app_group_set_channel(tmp->chan, outbound_group);

		/* Inherit context and extension */
		if (!ast_strlen_zero(chan->macrocontext))
			ast_copy_string(tmp->chan->dialcontext, chan->macrocontext, sizeof(tmp->chan->dialcontext));
		else
			ast_copy_string(tmp->chan->dialcontext, chan->context, sizeof(tmp->chan->dialcontext));
		if (!ast_strlen_zero(chan->macroexten))
			ast_copy_string(tmp->chan->exten, chan->macroexten, sizeof(tmp->chan->exten));
		else
			ast_copy_string(tmp->chan->exten, chan->exten, sizeof(tmp->chan->exten));

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
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Couldn't call %s\n", numsubst);
			if (tmp->chan->hangupcause) {
				chan->hangupcause = tmp->chan->hangupcause;
			}
			ast_hangup(tmp->chan);
			tmp->chan = NULL;
			free(tmp);
			continue;
		} else {
			senddialevent(chan, tmp->chan);
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Called %s\n", numsubst);
			if (!ast_test_flag(peerflags, OPT_ORIGINAL_CLID))
				ast_set_callerid(tmp->chan, S_OR(chan->macroexten, chan->exten), get_cid_name(cidname, sizeof(cidname), chan), NULL);
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
	}
	
	if (ast_strlen_zero(args.timeout)) {
		to = -1;
	} else {
		to = atoi(args.timeout);
		if (to > 0)
			to *= 1000;
		else
			ast_log(LOG_WARNING, "Invalid timeout specified: '%s'\n", args.timeout);
	}

	if (!outgoing) {
		strcpy(status, "CHANUNAVAIL");
		if(fulldial == num_dialed) {
			res = -1;
			goto out;
		}
	} else {
		/* Our status will at least be NOANSWER */
		strcpy(status, "NOANSWER");
		if (ast_test_flag(outgoing, OPT_MUSICBACK)) {
			moh = 1;
			if (!ast_strlen_zero(opt_args[OPT_ARG_MUSICBACK])) {
				char *original_moh = ast_strdupa(chan->musicclass);
				ast_string_field_set(chan, musicclass, opt_args[OPT_ARG_MUSICBACK]);
				ast_moh_start(chan, opt_args[OPT_ARG_MUSICBACK], NULL);
				ast_string_field_set(chan, musicclass, original_moh);
			} else {
				ast_moh_start(chan, NULL, NULL);
			}
			ast_indicate(chan, AST_CONTROL_PROGRESS);
		} else if (ast_test_flag(outgoing, OPT_RINGBACK)) {
			ast_indicate(chan, AST_CONTROL_RINGING);
			sentringing++;
		}
	}

	time(&start_time);
	peer = wait_for_answer(chan, outgoing, &to, peerflags, &sentringing, status, sizeof(status), numbusy, numnochan, numcongestion, ast_test_flag(&opts, OPT_PRIORITY_JUMP), &result);

	if (!peer) {
		if (result) {
			res = result;
		} else if (to) { /* Musta gotten hung up */
			res = -1;
		} else { /* Nobody answered, next please? */
			res = 0;
		}
		/* almost done, although the 'else' block is 400 lines */
	} else {
		const char *number;

		strcpy(status, "ANSWER");
		pbx_builtin_setvar_helper(chan, "DIALSTATUS", status);
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

		number = pbx_builtin_getvar_helper(peer, "DIALEDPEERNUMBER");
		if (!number)
			number = numsubst;
		pbx_builtin_setvar_helper(chan, "DIALEDPEERNUMBER", number);
 		if (!ast_strlen_zero(args.url) && ast_channel_supports_html(peer) ) {
			if (option_debug)
 				ast_log(LOG_DEBUG, "app_dial: sendurl=%s.\n", args.url);
 			ast_channel_sendurl( peer, args.url );
 		}
		if ( (ast_test_flag(&opts, OPT_PRIVACY) || ast_test_flag(&opts, OPT_SCREENING)) && privdb_val == AST_PRIVACY_UNKNOWN) {
			int res2;
			int loopcount = 0;

			/* Get the user's intro, store it in priv-callerintros/$CID, 
			   unless it is already there-- this should be done before the 
			   call is actually dialed  */

			/* all ring indications and moh for the caller has been halted as soon as the 
			   target extension was picked up. We are going to have to kill some
			   time and make the caller believe the peer hasn't picked up yet */

			if (ast_test_flag(&opts, OPT_MUSICBACK) && !ast_strlen_zero(opt_args[OPT_ARG_MUSICBACK])) {
				char *original_moh = ast_strdupa(chan->musicclass);
				ast_indicate(chan, -1);
				ast_string_field_set(chan, musicclass, opt_args[OPT_ARG_MUSICBACK]);
				ast_moh_start(chan, opt_args[OPT_ARG_MUSICBACK], NULL);
				ast_string_field_set(chan, musicclass, original_moh);
			} else if (ast_test_flag(&opts, OPT_RINGBACK)) {
				ast_indicate(chan, AST_CONTROL_RINGING);
				sentringing++;
			}

			/* Start autoservice on the other chan ?? */
			res2 = ast_autoservice_start(chan);
			/* Now Stream the File */
			for (loopcount = 0; loopcount < 3; loopcount++) {
				if (res2 && loopcount == 0)	/* error in ast_autoservice_start() */
					break;
				if (!res2)	/* on timeout, play the message again */
					res2 = ast_play_and_wait(peer,"priv-callpending");
				if (!valid_priv_reply(&opts, res2))
					res2 = 0;
				/* priv-callpending script: 
				   "I have a caller waiting, who introduces themselves as:"
				*/
				if (!res2)
					res2 = ast_play_and_wait(peer,privintro);
				if (!valid_priv_reply(&opts, res2))
					res2 = 0;
				/* now get input from the called party, as to their choice */
				if( !res2 ) {
					/* XXX can we have both, or they are mutually exclusive ? */
					if( ast_test_flag(&opts, OPT_PRIVACY) )
						res2 = ast_play_and_wait(peer,"priv-callee-options");
					if( ast_test_flag(&opts, OPT_SCREENING) )
						res2 = ast_play_and_wait(peer,"screen-callee-options");
				}
				/*! \page DialPrivacy Dial Privacy scripts
				\par priv-callee-options script:
					"Dial 1 if you wish this caller to reach you directly in the future,
						and immediately connect to their incoming call
					 Dial 2 if you wish to send this caller to voicemail now and 
						forevermore.
					 Dial 3 to send this caller to the torture menus, now and forevermore.
					 Dial 4 to send this caller to a simple "go away" menu, now and forevermore.
					 Dial 5 to allow this caller to come straight thru to you in the future,
						but right now, just this once, send them to voicemail."
				\par screen-callee-options script:
					"Dial 1 if you wish to immediately connect to the incoming call
					 Dial 2 if you wish to send this caller to voicemail.
					 Dial 3 to send this caller to the torture menus.
					 Dial 4 to send this caller to a simple "go away" menu.
				*/
				if (valid_priv_reply(&opts, res2))
					break;
				/* invalid option */
				res2 = ast_play_and_wait(peer, "vm-sorry");
			}

			if (ast_test_flag(&opts, OPT_MUSICBACK)) {
				ast_moh_stop(chan);
			} else if (ast_test_flag(&opts, OPT_RINGBACK)) {
				ast_indicate(chan, -1);
				sentringing=0;
			}
			ast_autoservice_stop(chan);

			switch (res2) {
			case '1':
				if( ast_test_flag(&opts, OPT_PRIVACY) ) {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "--Set privacy database entry %s/%s to ALLOW\n",
							     opt_args[OPT_ARG_PRIVACY], privcid);
					ast_privacy_set(opt_args[OPT_ARG_PRIVACY], privcid, AST_PRIVACY_ALLOW);
				}
				break;
			case '2':
				if( ast_test_flag(&opts, OPT_PRIVACY) ) {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "--Set privacy database entry %s/%s to DENY\n",
							     opt_args[OPT_ARG_PRIVACY], privcid);
					ast_privacy_set(opt_args[OPT_ARG_PRIVACY], privcid, AST_PRIVACY_DENY);
				}
				ast_copy_string(status, "NOANSWER", sizeof(status));
				ast_hangup(peer); /* hang up on the callee -- he didn't want to talk anyway! */
				res=0;
				goto out;
			case '3':
				if( ast_test_flag(&opts, OPT_PRIVACY) ) {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "--Set privacy database entry %s/%s to TORTURE\n",
							     opt_args[OPT_ARG_PRIVACY], privcid);
					ast_privacy_set(opt_args[OPT_ARG_PRIVACY], privcid, AST_PRIVACY_TORTURE);
				}
				ast_copy_string(status, "TORTURE", sizeof(status));
				
				res = 0;
				ast_hangup(peer); /* hang up on the caller -- he didn't want to talk anyway! */
				goto out; /* Is this right? */
			case '4':
				if( ast_test_flag(&opts, OPT_PRIVACY) ) {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "--Set privacy database entry %s/%s to KILL\n",
							     opt_args[OPT_ARG_PRIVACY], privcid);
					ast_privacy_set(opt_args[OPT_ARG_PRIVACY], privcid, AST_PRIVACY_KILL);
				}

				ast_copy_string(status, "DONTCALL", sizeof(status));
				res = 0;
				ast_hangup(peer); /* hang up on the caller -- he didn't want to talk anyway! */
				goto out; /* Is this right? */
			case '5':
				if( ast_test_flag(&opts, OPT_PRIVACY) ) {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "--Set privacy database entry %s/%s to ALLOW\n",
							     opt_args[OPT_ARG_PRIVACY], privcid);
					ast_privacy_set(opt_args[OPT_ARG_PRIVACY], privcid, AST_PRIVACY_ALLOW);
					ast_hangup(peer); /* hang up on the caller -- he didn't want to talk anyway! */
					res=0;
					goto out;
				} /* if not privacy, then 5 is the same as "default" case */
			default:	/* bad input or -1 if failure to start autoservice */
				/* well, if the user messes up, ... he had his chance... What Is The Best Thing To Do?  */
				/* well, there seems basically two choices. Just patch the caller thru immediately,
					  or,... put 'em thru to voicemail. */
				/* since the callee may have hung up, let's do the voicemail thing, no database decision */
				ast_log(LOG_NOTICE, "privacy: no valid response from the callee. Sending the caller to voicemail, the callee isn't responding\n");
				ast_hangup(peer); /* hang up on the callee -- he didn't want to talk anyway! */
				res=0;
				goto out;
			}

			/* XXX once again, this path is only taken in the case '1', so it could be
			 * moved there, although i am not really sure that this is correct - maybe
			 * the check applies to other cases as well.
			 */
			/* if the intro is NOCALLERID, then there's no reason to leave it on disk, it'll 
			   just clog things up, and it's not useful information, not being tied to a CID */
			if( strncmp(privcid,"NOCALLERID",10) == 0 || ast_test_flag(&opts, OPT_SCREEN_NOINTRO) ) {
				delprivintro = 1;
			}
		}
		if (!ast_test_flag(&opts, OPT_ANNOUNCE) || ast_strlen_zero(opt_args[OPT_ARG_ANNOUNCE])) {
			res = 0;
		} else {
			int digit = 0;
			struct ast_channel *chans[2];
			struct ast_channel *active_chan;

			chans[0] = chan;
			chans[1] = peer;

			/* we need to stream the announcment while monitoring the caller for a hangup */

			/* stream the file */
			res = ast_streamfile(peer, opt_args[OPT_ARG_ANNOUNCE], peer->language);
			if (res) {
				res = 0;
				ast_log(LOG_ERROR, "error streaming file '%s' to callee\n", opt_args[OPT_ARG_ANNOUNCE]);
			}

			ast_set_flag(peer, AST_FLAG_END_DTMF_ONLY);
			while (peer->stream) {
				int ms;

				ms = ast_sched_wait(peer->sched);

				if (ms < 0 && !peer->timingfunc) {
					ast_stopstream(peer);
					break;
				}
				if (ms < 0)
					ms = 1000;

				active_chan = ast_waitfor_n(chans, 2, &ms);
				if (active_chan) {
					struct ast_frame *fr = ast_read(active_chan);
					if (!fr) {
						ast_hangup(peer);
						res = -1;
						goto done;
					}
					switch(fr->frametype) {
						case AST_FRAME_DTMF_END:
							digit = fr->subclass;
							if (active_chan == peer && strchr(AST_DIGIT_ANY, res)) {
								ast_stopstream(peer);
								res = ast_senddigit(chan, digit);
							}
							break;
						case AST_FRAME_CONTROL:
							switch (fr->subclass) {
								case AST_CONTROL_HANGUP:
									ast_frfree(fr);
									ast_hangup(peer);
									res = -1;
									goto done;
								default:
									break;
							}
							break;
						default:
							/* Ignore all others */
							break;
					}
					ast_frfree(fr);
				}
				ast_sched_runq(peer->sched);
			}
			ast_clear_flag(peer, AST_FLAG_END_DTMF_ONLY);
		}

		if (chan && peer && ast_test_flag(&opts, OPT_GOTO) && !ast_strlen_zero(opt_args[OPT_ARG_GOTO])) {
			/* chan and peer are going into the PBX, they both
			 * should probably get CDR records. */
			ast_clear_flag(chan->cdr, AST_CDR_FLAG_DIALED);
			ast_clear_flag(peer->cdr, AST_CDR_FLAG_DIALED);

			replace_macro_delimiter(opt_args[OPT_ARG_GOTO]);
			ast_parseable_goto(chan, opt_args[OPT_ARG_GOTO]);
			/* peer goes to the same context and extension as chan, so just copy info from chan*/
			ast_copy_string(peer->context, chan->context, sizeof(peer->context));
			ast_copy_string(peer->exten, chan->exten, sizeof(peer->exten));
			peer->priority = chan->priority + 2;
			ast_pbx_start(peer);
			hanguptree(outgoing, NULL);
			if (continue_exec)
				*continue_exec = 1;
			res = 0;
			goto done;
		}

		if (ast_test_flag(&opts, OPT_CALLEE_MACRO) && !ast_strlen_zero(opt_args[OPT_ARG_CALLEE_MACRO])) {
			struct ast_app *theapp;
			const char *macro_result;

			res = ast_autoservice_start(chan);
			if (res) {
				ast_log(LOG_ERROR, "Unable to start autoservice on calling channel\n");
				res = -1;
			}

			theapp = pbx_findapp("Macro");

			if (theapp && !res) {	/* XXX why check res here ? */
				replace_macro_delimiter(opt_args[OPT_ARG_CALLEE_MACRO]);
				res = pbx_exec(peer, theapp, opt_args[OPT_ARG_CALLEE_MACRO]);
				ast_log(LOG_DEBUG, "Macro exited with status %d\n", res);
				res = 0;
			} else {
				ast_log(LOG_ERROR, "Could not find application Macro\n");
				res = -1;
			}

			if (ast_autoservice_stop(chan) < 0) {
				res = -1;
			}

			if (!res && (macro_result = pbx_builtin_getvar_helper(peer, "MACRO_RESULT"))) {
					char *macro_transfer_dest;

					if (!strcasecmp(macro_result, "BUSY")) {
						ast_copy_string(status, macro_result, sizeof(status));
						if (ast_opt_priority_jumping || ast_test_flag(&opts, OPT_PRIORITY_JUMP)) {
							if (!ast_goto_if_exists(chan, NULL, NULL, chan->priority + 101)) {
								ast_set_flag(peerflags, OPT_GO_ON);
							}
						} else
							ast_set_flag(peerflags, OPT_GO_ON);
						res = -1;
					} else if (!strcasecmp(macro_result, "CONGESTION") || !strcasecmp(macro_result, "CHANUNAVAIL")) {
						ast_copy_string(status, macro_result, sizeof(status));
						ast_set_flag(peerflags, OPT_GO_ON);	
						res = -1;
					} else if (!strcasecmp(macro_result, "CONTINUE")) {
						/* hangup peer and keep chan alive assuming the macro has changed 
						   the context / exten / priority or perhaps 
						   the next priority in the current exten is desired.
						*/
						ast_set_flag(peerflags, OPT_GO_ON);	
						res = -1;
					} else if (!strcasecmp(macro_result, "ABORT")) {
						/* Hangup both ends unless the caller has the g flag */
						res = -1;
					} else if (!strncasecmp(macro_result, "GOTO:", 5) && (macro_transfer_dest = ast_strdupa(macro_result + 5))) {
						res = -1;
						/* perform a transfer to a new extension */
						if (strchr(macro_transfer_dest, '^')) { /* context^exten^priority*/
							replace_macro_delimiter(macro_transfer_dest);
							if (!ast_parseable_goto(chan, macro_transfer_dest))
								ast_set_flag(peerflags, OPT_GO_ON);

						}
					}
			}
		}

		if (!res) {
			if (calldurationlimit > 0) {
				peer->whentohangup = time(NULL) + calldurationlimit;
			} else if (calldurationlimit != -1 && timelimit > 0) {
				/* Not enough granularity to make it less, but we can't use the special value 0 */
				peer->whentohangup = time(NULL) + 1;
			}
			if (!ast_strlen_zero(dtmfcalled)) { 
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Sending DTMF '%s' to the called party.\n", dtmfcalled);
				res = ast_dtmf_stream(peer,chan,dtmfcalled,250);
			}
			if (!ast_strlen_zero(dtmfcalling)) {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Sending DTMF '%s' to the calling party.\n", dtmfcalling);
				res = ast_dtmf_stream(chan,peer,dtmfcalling,250);
			}
		}

		if (!res) {
			struct ast_bridge_config config;

			memset(&config,0,sizeof(struct ast_bridge_config));
			if (play_to_caller)
				ast_set_flag(&(config.features_caller), AST_FEATURE_PLAY_WARNING);
			if (play_to_callee)
				ast_set_flag(&(config.features_callee), AST_FEATURE_PLAY_WARNING);
			if (ast_test_flag(peerflags, OPT_CALLEE_TRANSFER))
				ast_set_flag(&(config.features_callee), AST_FEATURE_REDIRECT);
			if (ast_test_flag(peerflags, OPT_CALLER_TRANSFER))
				ast_set_flag(&(config.features_caller), AST_FEATURE_REDIRECT);
			if (ast_test_flag(peerflags, OPT_CALLEE_HANGUP))
				ast_set_flag(&(config.features_callee), AST_FEATURE_DISCONNECT);
			if (ast_test_flag(peerflags, OPT_CALLER_HANGUP))
				ast_set_flag(&(config.features_caller), AST_FEATURE_DISCONNECT);
			if (ast_test_flag(peerflags, OPT_CALLEE_MONITOR))
				ast_set_flag(&(config.features_callee), AST_FEATURE_AUTOMON);
			if (ast_test_flag(peerflags, OPT_CALLER_MONITOR)) 
				ast_set_flag(&(config.features_caller), AST_FEATURE_AUTOMON);
			if (ast_test_flag(peerflags, OPT_CALLEE_PARK))
				ast_set_flag(&(config.features_callee), AST_FEATURE_PARKCALL);
			if (ast_test_flag(peerflags, OPT_CALLER_PARK))
				ast_set_flag(&(config.features_caller), AST_FEATURE_PARKCALL);
			if (ast_test_flag(peerflags, OPT_GO_ON))
				ast_set_flag(&(config.features_caller), AST_FEATURE_NO_H_EXTEN);

			config.timelimit = timelimit;
			config.play_warning = play_warning;
			config.warning_freq = warning_freq;
			config.warning_sound = warning_sound;
			config.end_sound = end_sound;
			config.start_sound = start_sound;
			config.end_bridge_callback = end_bridge_callback;
			config.end_bridge_callback_data = chan;
			config.end_bridge_callback_data_fixup = end_bridge_callback_data_fixup;
			if (moh) {
				moh = 0;
				ast_moh_stop(chan);
			} else if (sentringing) {
				sentringing = 0;
				ast_indicate(chan, -1);
			}
			/* Be sure no generators are left on it and reset the visible indication */
			ast_deactivate_generator(chan);
			chan->visible_indication = 0;
			/* Make sure channels are compatible */
			res = ast_channel_make_compatible(chan, peer);
			if (res < 0) {
				ast_log(LOG_WARNING, "Had to drop call because I couldn't make %s compatible with %s\n", chan->name, peer->name);
				ast_hangup(peer);
				res = -1;
				goto done;
			}
			if (opermode && 
				(((!strncasecmp(chan->name,"Zap",3)) && (!strncasecmp(peer->name,"Zap",3))) ||
				 ((!strncasecmp(chan->name,"Dahdi",5)) && (!strncasecmp(peer->name,"Dahdi",5)))))
			{
				struct oprmode oprmode;

				oprmode.peer = peer;
				oprmode.mode = opermode;

				ast_channel_setoption(chan,
					AST_OPTION_OPRMODE,&oprmode,sizeof(struct oprmode),0);
			}
			res = ast_bridge_call(chan,peer,&config);
		} else {
			res = -1;
		}

		if (!chan->_softhangup)
			chan->hangupcause = peer->hangupcause;
		ast_hangup(peer);
	}	
out:
	if (moh) {
		moh = 0;
		ast_moh_stop(chan);
	} else if (sentringing) {
		sentringing = 0;
		ast_indicate(chan, -1);
	}
	if (delprivintro) {
		ast_filedelete(privintro, NULL);
		if(ast_fileexists(privintro, NULL, NULL) > 0) {
			ast_log(LOG_NOTICE,"privacy: ast_filedelete didn't do its job on %s\n", privintro);
		} else if (option_verbose > 2) {
			ast_verbose(VERBOSE_PREFIX_3 "Successfully deleted %s intro file\n", privintro);
		}
	}

	ast_rtp_early_bridge(chan, NULL);
	hanguptree(outgoing, NULL);
	pbx_builtin_setvar_helper(chan, "DIALSTATUS", status);
	if (option_debug)
		ast_log(LOG_DEBUG, "Exiting with DIALSTATUS=%s.\n", status);
	
	if (ast_test_flag(peerflags, OPT_GO_ON) && !chan->_softhangup) {
		if (calldurationlimit)
			chan->whentohangup = 0;
		res = 0;
	}
done:
	ast_module_user_remove(u);    
	return res;
}

static int dial_exec(struct ast_channel *chan, void *data)
{
	struct ast_flags peerflags;

	memset(&peerflags, 0, sizeof(peerflags));

	return dial_exec_full(chan, data, &peerflags, NULL);
}

static int retrydial_exec(struct ast_channel *chan, void *data)
{
	char *announce = NULL, *dialdata = NULL;
	const char *context = NULL;
	int sleep = 0, loops = 0, res = -1;
	struct ast_module_user *u;
	struct ast_flags peerflags;
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "RetryDial requires an argument!\n");
		return -1;
	}	

	u = ast_module_user_add(chan);

	announce = ast_strdupa(data);

	memset(&peerflags, 0, sizeof(peerflags));

	if ((dialdata = strchr(announce, '|'))) {
		*dialdata++ = '\0';
		if (sscanf(dialdata, "%30d", &sleep) == 1) {
			sleep *= 1000;
		} else {
			ast_log(LOG_ERROR, "%s requires the numerical argument <sleep>\n",rapp);
			goto done;
		}
		if ((dialdata = strchr(dialdata, '|'))) {
			*dialdata++ = '\0';
			if (sscanf(dialdata, "%30d", &loops) != 1) {
				ast_log(LOG_ERROR, "%s requires the numerical argument <loops>\n",rapp);
				goto done;
			}
		}
	}
	
	if (dialdata && (dialdata = strchr(dialdata, '|'))) {
		*dialdata++ = '\0';
	} else {
		ast_log(LOG_ERROR, "%s requires more arguments\n",rapp);
		goto done;
	}
		
	if (sleep < 1000)
		sleep = 10000;

	if (!loops)
		loops = -1;	/* run forever */
	
	context = pbx_builtin_getvar_helper(chan, "EXITCONTEXT");

	res = 0;
	while (loops) {
		int continue_exec;

		chan->data = "Retrying";
		if (ast_test_flag(chan, AST_FLAG_MOH))
			ast_moh_stop(chan);

		res = dial_exec_full(chan, dialdata, &peerflags, &continue_exec);
		if (continue_exec)
			break;

		if (res == 0) {
			if (ast_test_flag(&peerflags, OPT_DTMF_EXIT)) {
				if (!ast_strlen_zero(announce)) {
					if (ast_fileexists(announce, NULL, chan->language) > 0) {
						if(!(res = ast_streamfile(chan, announce, chan->language)))								
							ast_waitstream(chan, AST_DIGIT_ANY);
					} else
						ast_log(LOG_WARNING, "Announce file \"%s\" specified in Retrydial does not exist\n", announce);
				}
				if (!res && sleep) {
					if (!ast_test_flag(chan, AST_FLAG_MOH))
						ast_moh_start(chan, NULL, NULL);
					res = ast_waitfordigit(chan, sleep);
				}
			} else {
				if (!ast_strlen_zero(announce)) {
					if (ast_fileexists(announce, NULL, chan->language) > 0) {
						if (!(res = ast_streamfile(chan, announce, chan->language)))
							res = ast_waitstream(chan, "");
					} else
						ast_log(LOG_WARNING, "Announce file \"%s\" specified in Retrydial does not exist\n", announce);
				}
				if (sleep) {
					if (!ast_test_flag(chan, AST_FLAG_MOH))
						ast_moh_start(chan, NULL, NULL);
					if (!res)
						res = ast_waitfordigit(chan, sleep);
				}
			}
		}

		if (res < 0)
			break;
		else if (res > 0) { /* Trying to send the call elsewhere (1 digit ext) */
			if (onedigit_goto(chan, context, (char) res, 1)) {
				res = 0;
				break;
			}
		}
		loops--;
	}
	if (loops == 0)
		res = 0;
	else if (res == 1)
		res = 0;

	if (ast_test_flag(chan, AST_FLAG_MOH))
		ast_moh_stop(chan);
 done:
	ast_module_user_remove(u);
	return res;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);
	res |= ast_unregister_application(rapp);

	ast_module_user_hangup_all();
	
	return res;
}

static int load_module(void)
{
	int res;

	res = ast_register_application(app, dial_exec, synopsis, descrip);
	res |= ast_register_application(rapp, retrydial_exec, rsynopsis, rdescrip);
	
	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Dialing Application");
