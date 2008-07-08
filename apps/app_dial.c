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

#include <sys/time.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <netinet/in.h>

#include "asterisk/paths.h" /* use ast_config_AST_DATA_DIR */
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
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
"  Dial(Technology/resource[&Tech2/resource2...][,timeout][,options][,URL]):\n"
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
"    c    - If DIAL cancels this call, always set the flag to tell the channel\n"
"           driver that the call is answered elsewhere.\n"
"    d    - Allow the calling user to dial a 1 digit extension while waiting for\n"
"           a call to be answered. Exit to that extension if it exists in the\n"
"           current context, or the context defined in the EXITCONTEXT variable,\n"
"           if it exists.\n"
"    D([called][:calling]) - Send the specified DTMF strings *after* the called\n"
"           party has answered, but before the call gets bridged. The 'called'\n"
"           DTMF string is sent to the called party, and the 'calling' DTMF\n"
"           string is sent to the calling party. Both parameters can be used\n"
"           alone.\n"
"    e    - execute the 'h' extension for peer after the call ends\n"
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
"    h    - Allow the called party to hang up by sending the '*' DTMF digit.\n"
"    H    - Allow the calling party to hang up by hitting the '*' DTMF digit.\n"
"    i    - Asterisk will ignore any forwarding requests it may receive on this\n"
"           dial attempt.\n"
"    k    - Allow the called party to enable parking of the call by sending\n"
"           the DTMF sequence defined for call parking in features.conf.\n"
"    K    - Allow the calling party to enable parking of the call by sending\n"
"           the DTMF sequence defined for call parking in features.conf.\n"
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
"           * BUSY         Behave as if a busy signal was encountered.\n"
"           * CONTINUE     Hangup the called party and allow the calling party\n"
"                          to continue dialplan execution at the next priority.\n"
"           * GOTO:<context>^<exten>^<priority> - Transfer the call to the\n"
"                          specified priority. Optionally, an extension, or\n"
"                          extension and priority can be specified.\n"
"           You cannot use any additional action post answer options in conjunction\n"
"           with this option. Also, pbx services are not run on the peer (called) channel,\n"
"           so you will not be able to set timeouts via the TIMEOUT() function in this macro.\n"
"    n    - This option is a modifier for the screen/privacy mode. It specifies\n"
"           that no introductions are to be saved in the priv-callerintros\n"
"           directory.\n"
"    N    - This option is a modifier for the screen/privacy mode. It specifies\n"
"           that if callerID is present, do not screen the call.\n"
"    o    - Specify that the CallerID that was present on the *calling* channel\n"
"           be set as the CallerID on the *called* channel. This was the\n"
"           behavior of Asterisk 1.0 and earlier.\n"
"    O([x]) - \"Operator Services\" mode (DAHDI channel to DAHDI channel\n"
"             only, if specified on non-DAHDI interface, it will be ignored).\n"
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
"           DTMF sequence defined in features.conf.\n"
"    T    - Allow the calling party to transfer the called party by sending the\n"
"           DTMF sequence defined in features.conf.\n"
"    U(x[^arg]) - Execute via Gosub the routine 'x' for the *called* channel before connecting\n"
"           to the calling channel. Arguments can be specified to the Gosub\n"
"           using '^' as a delimeter. The Gosub routine can set the variable\n"
"           GOSUB_RESULT to specify the following actions after the Gosub returns.\n"
"           * ABORT        Hangup both legs of the call.\n"
"           * CONGESTION   Behave as if line congestion was encountered.\n"
"           * BUSY         Behave as if a busy signal was encountered.\n"
"           * CONTINUE     Hangup the called party and allow the calling party\n"
"                          to continue dialplan execution at the next priority.\n"
"           * GOTO:<context>^<exten>^<priority> - Transfer the call to the\n"
"                          specified priority. Optionally, an extension, or\n"
"                          extension and priority can be specified.\n"
"           You cannot use any additional action post answer options in conjunction\n"
"           with this option. Also, pbx services are not run on the peer (called) channel,\n"
"           so you will not be able to set timeouts via the TIMEOUT() function in this routine.\n"
"    w    - Allow the called party to enable recording of the call by sending\n"
"           the DTMF sequence defined for one-touch recording in features.conf.\n"
"    W    - Allow the calling party to enable recording of the call by sending\n"
"           the DTMF sequence defined for one-touch recording in features.conf.\n"
"    x    - Allow the called party to enable recording of the call by sending\n"
"           the DTMF sequence defined for one-touch automixmonitor in features.conf\n"
"    X    - Allow the calling party to enable recording of the call by sending\n"
"           the DTMF sequence defined for one-touch automixmonitor in features.conf\n";

/* RetryDial App by Anthony Minessale II <anthmct@yahoo.com> Jan/2005 */
static char *rapp = "RetryDial";
static char *rsynopsis = "Place a call, retrying on failure allowing optional exit extension.";
static char *rdescrip =
"  RetryDial(announce,sleep,retries,dialargs): This application will attempt to\n"
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
	OPT_ANNOUNCE =          (1 << 0),
	OPT_RESETCDR =          (1 << 1),
	OPT_DTMF_EXIT =         (1 << 2),
	OPT_SENDDTMF =          (1 << 3),
	OPT_FORCECLID =         (1 << 4),
	OPT_GO_ON =             (1 << 5),
	OPT_CALLEE_HANGUP =     (1 << 6),
	OPT_CALLER_HANGUP =     (1 << 7),
	OPT_DURATION_LIMIT =    (1 << 9),
	OPT_MUSICBACK =         (1 << 10),
	OPT_CALLEE_MACRO =      (1 << 11),
	OPT_SCREEN_NOINTRO =    (1 << 12),
	OPT_SCREEN_NOCLID =     (1 << 13),
	OPT_ORIGINAL_CLID =     (1 << 14),
	OPT_SCREENING =         (1 << 15),
	OPT_PRIVACY =           (1 << 16),
	OPT_RINGBACK =          (1 << 17),
	OPT_DURATION_STOP =     (1 << 18),
	OPT_CALLEE_TRANSFER =   (1 << 19),
	OPT_CALLER_TRANSFER =   (1 << 20),
	OPT_CALLEE_MONITOR =    (1 << 21),
	OPT_CALLER_MONITOR =    (1 << 22),
	OPT_GOTO =              (1 << 23),
	OPT_OPERMODE =          (1 << 24),
	OPT_CALLEE_PARK =       (1 << 25),
	OPT_CALLER_PARK =       (1 << 26),
	OPT_IGNORE_FORWARDING = (1 << 27),
	OPT_CALLEE_GOSUB =      (1 << 28),
	OPT_CALLEE_MIXMONITOR = (1 << 29),
	OPT_CALLER_MIXMONITOR = (1 << 30),
};

#define DIAL_STILLGOING      (1 << 31)
#define DIAL_NOFORWARDHTML   ((uint64_t)1 << 32) /* flags are now 64 bits, so keep it up! */
#define OPT_CANCEL_ELSEWHERE ((uint64_t)1 << 33)
#define OPT_PEER_H           ((uint64_t)1 << 34)

enum {
	OPT_ARG_ANNOUNCE = 0,
	OPT_ARG_SENDDTMF,
	OPT_ARG_GOTO,
	OPT_ARG_DURATION_LIMIT,
	OPT_ARG_MUSICBACK,
	OPT_ARG_CALLEE_MACRO,
	OPT_ARG_CALLEE_GOSUB,
	OPT_ARG_PRIVACY,
	OPT_ARG_DURATION_STOP,
	OPT_ARG_OPERMODE,
	/* note: this entry _MUST_ be the last one in the enum */
	OPT_ARG_ARRAY_SIZE,
};

AST_APP_OPTIONS(dial_exec_options, BEGIN_OPTIONS
	AST_APP_OPTION_ARG('A', OPT_ANNOUNCE, OPT_ARG_ANNOUNCE),
	AST_APP_OPTION('C', OPT_RESETCDR),
	AST_APP_OPTION('c', OPT_CANCEL_ELSEWHERE),
	AST_APP_OPTION('d', OPT_DTMF_EXIT),
	AST_APP_OPTION_ARG('D', OPT_SENDDTMF, OPT_ARG_SENDDTMF),
	AST_APP_OPTION('e', OPT_PEER_H),
	AST_APP_OPTION('f', OPT_FORCECLID),
	AST_APP_OPTION('g', OPT_GO_ON),
	AST_APP_OPTION_ARG('G', OPT_GOTO, OPT_ARG_GOTO),
	AST_APP_OPTION('h', OPT_CALLEE_HANGUP),
	AST_APP_OPTION('H', OPT_CALLER_HANGUP),
	AST_APP_OPTION('i', OPT_IGNORE_FORWARDING),
	AST_APP_OPTION('k', OPT_CALLEE_PARK),
	AST_APP_OPTION('K', OPT_CALLER_PARK),
	AST_APP_OPTION('k', OPT_CALLEE_PARK),
	AST_APP_OPTION('K', OPT_CALLER_PARK),
	AST_APP_OPTION_ARG('L', OPT_DURATION_LIMIT, OPT_ARG_DURATION_LIMIT),
	AST_APP_OPTION_ARG('m', OPT_MUSICBACK, OPT_ARG_MUSICBACK),
	AST_APP_OPTION_ARG('M', OPT_CALLEE_MACRO, OPT_ARG_CALLEE_MACRO),
	AST_APP_OPTION('n', OPT_SCREEN_NOINTRO),
	AST_APP_OPTION('N', OPT_SCREEN_NOCLID),
	AST_APP_OPTION('o', OPT_ORIGINAL_CLID),
	AST_APP_OPTION_ARG('O', OPT_OPERMODE, OPT_ARG_OPERMODE),
	AST_APP_OPTION('p', OPT_SCREENING),
	AST_APP_OPTION_ARG('P', OPT_PRIVACY, OPT_ARG_PRIVACY),
	AST_APP_OPTION('r', OPT_RINGBACK),
	AST_APP_OPTION_ARG('S', OPT_DURATION_STOP, OPT_ARG_DURATION_STOP),
	AST_APP_OPTION('t', OPT_CALLEE_TRANSFER),
	AST_APP_OPTION('T', OPT_CALLER_TRANSFER),
	AST_APP_OPTION_ARG('U', OPT_CALLEE_GOSUB, OPT_ARG_CALLEE_GOSUB),
	AST_APP_OPTION('w', OPT_CALLEE_MONITOR),
	AST_APP_OPTION('W', OPT_CALLER_MONITOR),
	AST_APP_OPTION('x', OPT_CALLEE_MIXMONITOR),
	AST_APP_OPTION('X', OPT_CALLER_MIXMONITOR),
END_OPTIONS );

#define CAN_EARLY_BRIDGE(flags) (!ast_test_flag64(flags, OPT_CALLEE_HANGUP | \
	OPT_CALLER_HANGUP | OPT_CALLEE_TRANSFER | OPT_CALLER_TRANSFER | \
	OPT_CALLEE_MONITOR | OPT_CALLER_MONITOR | OPT_CALLEE_PARK | OPT_CALLER_PARK))

/*
 * The list of active channels
 */
struct chanlist {
	struct chanlist *next;
	struct ast_channel *chan;
	uint64_t flags;
};


static void hanguptree(struct chanlist *outgoing, struct ast_channel *exception, int answered_elsewhere)
{
	/* Hang up a tree of stuff */
	struct chanlist *oo;
	while (outgoing) {
		/* Hangup any existing lines we have open */
		if (outgoing->chan && (outgoing->chan != exception)) {
			if (answered_elsewhere)
				ast_set_flag(outgoing->chan, AST_FLAG_ANSWERED_ELSEWHERE);
			ast_hangup(outgoing->chan);
		}
		oo = outgoing;
		outgoing = outgoing->next;
		ast_free(oo);
	}
}

#define AST_MAX_WATCHERS 256

/*
 * argument to handle_cause() and other functions.
 */
struct cause_args {
	struct ast_channel *chan;
	int busy;
	int congestion;
	int nochan;
};

static void handle_cause(int cause, struct cause_args *num)
{
	struct ast_cdr *cdr = num->chan->cdr;

	switch(cause) {
	case AST_CAUSE_BUSY:
		if (cdr)
			ast_cdr_busy(cdr);
		num->busy++;
		break;

	case AST_CAUSE_CONGESTION:
		if (cdr)
			ast_cdr_failed(cdr);
		num->congestion++;
		break;

	case AST_CAUSE_NO_ROUTE_DESTINATION:
	case AST_CAUSE_UNREGISTERED:
		if (cdr)
			ast_cdr_failed(cdr);
		num->nochan++;
		break;

	case AST_CAUSE_NORMAL_CLEARING:
		break;

	default:
		num->nochan++;
		break;
	}
}

/* free the buffer if allocated, and set the pointer to the second arg */
#define S_REPLACE(s, new_val)		\
	do {				\
		if (s)			\
			ast_free(s);	\
		s = (new_val);		\
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


static const char *get_cid_name(char *name, int namelen, struct ast_channel *chan)
{
	const char *context = S_OR(chan->macrocontext, chan->context);
	const char *exten = S_OR(chan->macroexten, chan->exten);

	return ast_get_hint(NULL, 0, name, namelen, chan, context, exten) ? name : "";
}

static void senddialevent(struct ast_channel *src, struct ast_channel *dst, const char *dialstring)
{
	manager_event(EVENT_FLAG_CALL, "Dial",
		"SubEvent: Begin\r\n"
		"Channel: %s\r\n"
		"Destination: %s\r\n"
		"CallerIDNum: %s\r\n"
		"CallerIDName: %s\r\n"
		"UniqueID: %s\r\n"
		"DestUniqueID: %s\r\n"
		"Dialstring: %s\r\n",
		src->name, dst->name, S_OR(src->cid.cid_num, "<unknown>"),
		S_OR(src->cid.cid_name, "<unknown>"), src->uniqueid,
		dst->uniqueid, dialstring ? dialstring : "");
}

static void senddialendevent(const struct ast_channel *src, const char *dialstatus)
{
	manager_event(EVENT_FLAG_CALL, "Dial",
		"SubEvent: End\r\n"
		"Channel: %s\r\n"
		"UniqueID: %s\r\n"
		"DialStatus: %s\r\n",
		src->name, src->uniqueid, dialstatus);
}

/*!
 * helper function for wait_for_answer()
 *
 * XXX this code is highly suspicious, as it essentially overwrites
 * the outgoing channel without properly deleting it.
 */
static void do_forward(struct chanlist *o,
	struct cause_args *num, struct ast_flags64 *peerflags, int single)
{
	char tmpchan[256];
	struct ast_channel *original = o->chan;
	struct ast_channel *c = o->chan; /* the winner */
	struct ast_channel *in = num->chan; /* the input channel */
	char *stuff;
	char *tech;
	int cause;

	ast_copy_string(tmpchan, c->call_forward, sizeof(tmpchan));
	if ((stuff = strchr(tmpchan, '/'))) {
		*stuff++ = '\0';
		tech = tmpchan;
	} else {
		const char *forward_context = pbx_builtin_getvar_helper(c, "FORWARD_CONTEXT");
		snprintf(tmpchan, sizeof(tmpchan), "%s@%s", c->call_forward, forward_context ? forward_context : c->context);
		stuff = tmpchan;
		tech = "Local";
	}
	/* Before processing channel, go ahead and check for forwarding */
	ast_verb(3, "Now forwarding %s to '%s/%s' (thanks to %s)\n", in->name, tech, stuff, c->name);
	/* If we have been told to ignore forwards, just set this channel to null and continue processing extensions normally */
	if (ast_test_flag64(peerflags, OPT_IGNORE_FORWARDING)) {
		ast_verb(3, "Forwarding %s to '%s/%s' prevented.\n", in->name, tech, stuff);
		c = o->chan = NULL;
		cause = AST_CAUSE_BUSY;
	} else {
		/* Setup parameters */
		c = o->chan = ast_request(tech, in->nativeformats, stuff, &cause);
		if (c) {
			if (single)
				ast_channel_make_compatible(o->chan, in);
			ast_channel_inherit_variables(in, o->chan);
			ast_channel_datastore_inherit(in, o->chan);
		} else
			ast_log(LOG_NOTICE, "Unable to create local channel for call forward to '%s/%s' (cause = %d)\n", tech, stuff, cause);
	}
	if (!c) {
		ast_clear_flag64(o, DIAL_STILLGOING);
		handle_cause(cause, num);
	} else {
		char *new_cid_num, *new_cid_name;
		struct ast_channel *src;

		ast_rtp_make_compatible(c, in, single);
		if (ast_test_flag64(o, OPT_FORCECLID)) {
			new_cid_num = ast_strdup(S_OR(in->macroexten, in->exten));
			new_cid_name = NULL; /* XXX no name ? */
			src = c; /* XXX possible bug in previous code, which used 'winner' ? it may have changed */
		} else {
			new_cid_num = ast_strdup(in->cid.cid_num);
			new_cid_name = ast_strdup(in->cid.cid_name);
			src = in;
		}
		ast_string_field_set(c, accountcode, src->accountcode);
		c->cdrflags = src->cdrflags;
		S_REPLACE(c->cid.cid_num, new_cid_num);
		S_REPLACE(c->cid.cid_name, new_cid_name);

		if (in->cid.cid_ani) { /* XXX or maybe unconditional ? */
			S_REPLACE(c->cid.cid_ani, ast_strdup(in->cid.cid_ani));
		}
		S_REPLACE(c->cid.cid_rdnis, ast_strdup(S_OR(in->macroexten, in->exten)));
		if (ast_call(c, tmpchan, 0)) {
			ast_log(LOG_NOTICE, "Failed to dial on local channel for call forward to '%s'\n", tmpchan);
			ast_clear_flag64(o, DIAL_STILLGOING);
			ast_hangup(original);
			c = o->chan = NULL;
			num->nochan++;
		} else {
			senddialevent(in, c, stuff);
			/* After calling, set callerid to extension */
			if (!ast_test_flag64(peerflags, OPT_ORIGINAL_CLID)) {
				char cidname[AST_MAX_EXTENSION] = "";
				ast_set_callerid(c, S_OR(in->macroexten, in->exten), get_cid_name(cidname, sizeof(cidname), in), NULL);
			}
			/* Hangup the original channel now, in case we needed it */
			ast_hangup(original);
		}
	}
}

/* argument used for some functions. */
struct privacy_args {
	int sentringing;
	int privdb_val;
	char privcid[256];
	char privintro[1024];
	char status[256];
};

static struct ast_channel *wait_for_answer(struct ast_channel *in,
	struct chanlist *outgoing, int *to, struct ast_flags64 *peerflags,
	struct privacy_args *pa,
	const struct cause_args *num_in, int *result)
{
	struct cause_args num = *num_in;
	int prestart = num.busy + num.congestion + num.nochan;
	int orig = *to;
	struct ast_channel *peer = NULL;
	/* single is set if only one destination is enabled */
	int single = outgoing && !outgoing->next && !ast_test_flag64(outgoing, OPT_MUSICBACK | OPT_RINGBACK);
#ifdef HAVE_EPOLL
	struct chanlist *epollo;
#endif

	if (single) {
		/* Turn off hold music, etc */
		ast_deactivate_generator(in);
		/* If we are calling a single channel, make them compatible for in-band tone purpose */
		ast_channel_make_compatible(outgoing->chan, in);
	}

#ifdef HAVE_EPOLL
	for (epollo = outgoing; epollo; epollo = epollo->next)
		ast_poll_channel_add(in, epollo->chan);
#endif

	while (*to && !peer) {
		struct chanlist *o;
		int pos = 0; /* how many channels do we handle */
		int numlines = prestart;
		struct ast_channel *winner;
		struct ast_channel *watchers[AST_MAX_WATCHERS];

		watchers[pos++] = in;
		for (o = outgoing; o; o = o->next) {
			/* Keep track of important channels */
			if (ast_test_flag64(o, DIAL_STILLGOING) && o->chan)
				watchers[pos++] = o->chan;
			numlines++;
		}
		if (pos == 1) { /* only the input channel is available */
			if (numlines == (num.busy + num.congestion + num.nochan)) {
				ast_verb(2, "Everyone is busy/congested at this time (%d:%d/%d/%d)\n", numlines, num.busy, num.congestion, num.nochan);
				if (num.busy)
					strcpy(pa->status, "BUSY");
				else if (num.congestion)
					strcpy(pa->status, "CONGESTION");
				else if (num.nochan)
					strcpy(pa->status, "CHANUNAVAIL");
			} else {
				ast_verb(3, "No one is available to answer at this time (%d:%d/%d/%d)\n", numlines, num.busy, num.congestion, num.nochan);
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
			if (ast_test_flag64(o, DIAL_STILLGOING) && c->_state == AST_STATE_UP) {
				if (!peer) {
					ast_verb(3, "%s answered %s\n", c->name, in->name);
					peer = c;
					ast_copy_flags64(peerflags, o,
						OPT_CALLEE_TRANSFER | OPT_CALLER_TRANSFER |
						OPT_CALLEE_HANGUP | OPT_CALLER_HANGUP |
						OPT_CALLEE_MONITOR | OPT_CALLER_MONITOR |
						OPT_CALLEE_PARK | OPT_CALLER_PARK |
						OPT_CALLEE_MIXMONITOR | OPT_CALLER_MIXMONITOR |
						DIAL_NOFORWARDHTML);
					ast_copy_string(c->dialcontext, "", sizeof(c->dialcontext));
					ast_copy_string(c->exten, "", sizeof(c->exten));
				}
				continue;
			}
			if (c != winner)
				continue;
			/* here, o->chan == c == winner */
			if (!ast_strlen_zero(c->call_forward)) {
				do_forward(o, &num, peerflags, single);
				continue;
			}
			f = ast_read(winner);
			if (!f) {
				in->hangupcause = c->hangupcause;
#ifdef HAVE_EPOLL
				ast_poll_channel_del(in, c);
#endif
				ast_hangup(c);
				c = o->chan = NULL;
				ast_clear_flag64(o, DIAL_STILLGOING);
				handle_cause(in->hangupcause, &num);
				continue;
			}
			if (f->frametype == AST_FRAME_CONTROL) {
				switch(f->subclass) {
				case AST_CONTROL_ANSWER:
					/* This is our guy if someone answered. */
					if (!peer) {
						ast_verb(3, "%s answered %s\n", c->name, in->name);
						peer = c;
						ast_copy_flags64(peerflags, o,
							OPT_CALLEE_TRANSFER | OPT_CALLER_TRANSFER |
							OPT_CALLEE_HANGUP | OPT_CALLER_HANGUP |
							OPT_CALLEE_MONITOR | OPT_CALLER_MONITOR |
							OPT_CALLEE_PARK | OPT_CALLER_PARK |
							OPT_CALLEE_MIXMONITOR | OPT_CALLER_MIXMONITOR |
							DIAL_NOFORWARDHTML);
						ast_copy_string(c->dialcontext, "", sizeof(c->dialcontext));
						ast_copy_string(c->exten, "", sizeof(c->exten));
						if (CAN_EARLY_BRIDGE(peerflags))
							/* Setup early bridge if appropriate */
							ast_channel_early_bridge(in, peer);
					}
					/* If call has been answered, then the eventual hangup is likely to be normal hangup */
					in->hangupcause = AST_CAUSE_NORMAL_CLEARING;
					c->hangupcause = AST_CAUSE_NORMAL_CLEARING;
					break;
				case AST_CONTROL_BUSY:
					ast_verb(3, "%s is busy\n", c->name);
					in->hangupcause = c->hangupcause;
					ast_hangup(c);
					c = o->chan = NULL;
					ast_clear_flag64(o, DIAL_STILLGOING);
					handle_cause(AST_CAUSE_BUSY, &num);
					break;
				case AST_CONTROL_CONGESTION:
					ast_verb(3, "%s is circuit-busy\n", c->name);
					in->hangupcause = c->hangupcause;
					ast_hangup(c);
					c = o->chan = NULL;
					ast_clear_flag64(o, DIAL_STILLGOING);
					handle_cause(AST_CAUSE_CONGESTION, &num);
					break;
				case AST_CONTROL_RINGING:
					ast_verb(3, "%s is ringing\n", c->name);
					/* Setup early media if appropriate */
					if (single && CAN_EARLY_BRIDGE(peerflags))
						ast_channel_early_bridge(in, c);
					if (!(pa->sentringing) && !ast_test_flag64(outgoing, OPT_MUSICBACK)) {
						ast_indicate(in, AST_CONTROL_RINGING);
						pa->sentringing++;
					}
					break;
				case AST_CONTROL_PROGRESS:
					ast_verb(3, "%s is making progress passing it to %s\n", c->name, in->name);
					/* Setup early media if appropriate */
					if (single && CAN_EARLY_BRIDGE(peerflags))
						ast_channel_early_bridge(in, c);
					if (!ast_test_flag64(outgoing, OPT_RINGBACK))
						ast_indicate(in, AST_CONTROL_PROGRESS);
					break;
				case AST_CONTROL_VIDUPDATE:
					ast_verb(3, "%s requested a video update, passing it to %s\n", c->name, in->name);
					ast_indicate(in, AST_CONTROL_VIDUPDATE);
					break;
				case AST_CONTROL_SRCUPDATE:
					ast_verb(3, "%s requested a source update, passing it to %s\n", c->name, in->name);
					ast_indicate(in, AST_CONTROL_SRCUPDATE);
					break;
				case AST_CONTROL_PROCEEDING:
					ast_verb(3, "%s is proceeding passing it to %s\n", c->name, in->name);
					if (single && CAN_EARLY_BRIDGE(peerflags))
						ast_channel_early_bridge(in, c);
					if (!ast_test_flag64(outgoing, OPT_RINGBACK))
						ast_indicate(in, AST_CONTROL_PROCEEDING);
					break;
				case AST_CONTROL_HOLD:
					ast_verb(3, "Call on %s placed on hold\n", c->name);
					ast_indicate(in, AST_CONTROL_HOLD);
					break;
				case AST_CONTROL_UNHOLD:
					ast_verb(3, "Call on %s left from hold\n", c->name);
					ast_indicate(in, AST_CONTROL_UNHOLD);
					break;
				case AST_CONTROL_OFFHOOK:
				case AST_CONTROL_FLASH:
					/* Ignore going off hook and flash */
					break;
				case -1:
					if (!ast_test_flag64(outgoing, OPT_RINGBACK | OPT_MUSICBACK)) {
						ast_verb(3, "%s stopped sounds\n", c->name);
						ast_indicate(in, -1);
						pa->sentringing = 0;
					}
					break;
				default:
					ast_debug(1, "Dunno what to do with control type %d\n", f->subclass);
				}
			} else if (single) {
				/* XXX are we sure the logic is correct ? or we should just switch on f->frametype ? */
				if (f->frametype == AST_FRAME_VOICE && !ast_test_flag64(outgoing, OPT_RINGBACK|OPT_MUSICBACK)) {
					if (ast_write(in, f))
						ast_log(LOG_WARNING, "Unable to forward voice frame\n");
				} else if (f->frametype == AST_FRAME_IMAGE && !ast_test_flag64(outgoing, OPT_RINGBACK|OPT_MUSICBACK)) {
					if (ast_write(in, f))
						ast_log(LOG_WARNING, "Unable to forward image\n");
				} else if (f->frametype == AST_FRAME_TEXT && !ast_test_flag64(outgoing, OPT_RINGBACK|OPT_MUSICBACK)) {
					if (ast_write(in, f))
						ast_log(LOG_WARNING, "Unable to send text\n");
				} else if (f->frametype == AST_FRAME_HTML && !ast_test_flag64(outgoing, DIAL_NOFORWARDHTML)) {
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
				strcpy(pa->status, "CANCEL");
				ast_cdr_noanswer(in->cdr);
				if (f)
					ast_frfree(f);
				return NULL;
			}

			/* now f is guaranteed non-NULL */
			if (f->frametype == AST_FRAME_DTMF) {
				if (ast_test_flag64(peerflags, OPT_DTMF_EXIT)) {
					const char *context = pbx_builtin_getvar_helper(in, "EXITCONTEXT");
					if (onedigit_goto(in, context, (char) f->subclass, 1)) {
						ast_verb(3, "User hit %c to disconnect call.\n", f->subclass);
						*to = 0;
						ast_cdr_noanswer(in->cdr);
						*result = f->subclass;
						strcpy(pa->status, "CANCEL");
						ast_frfree(f);
						return NULL;
					}
				}

				if (ast_test_flag64(peerflags, OPT_CALLER_HANGUP) &&
						(f->subclass == '*')) { /* hmm it it not guaranteed to be '*' anymore. */
					ast_verb(3, "User hit %c to disconnect call.\n", f->subclass);
					*to = 0;
					strcpy(pa->status, "CANCEL");
					ast_cdr_noanswer(in->cdr);
					ast_frfree(f);
					return NULL;
				}
			}

			/* Forward HTML stuff */
			if (single && (f->frametype == AST_FRAME_HTML) && !ast_test_flag64(outgoing, DIAL_NOFORWARDHTML))
				if (ast_channel_sendhtml(outgoing->chan, f->subclass, f->data, f->datalen) == -1)
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
				ast_verb(3, "%s requested special control %d, passing it to %s\n", in->name, f->subclass, outgoing->chan->name);
				ast_indicate_data(outgoing->chan, f->subclass, f->data, f->datalen);
			}
			ast_frfree(f);
		}
		if (!*to)
			ast_verb(3, "Nobody picked up in %d ms\n", orig);
		if (!*to || ast_check_hangup(in))
			ast_cdr_noanswer(in->cdr);
	}

#ifdef HAVE_EPOLL
	for (epollo = outgoing; epollo; epollo = epollo->next) {
		if (epollo->chan)
			ast_poll_channel_del(in, epollo->chan);
	}
#endif

	return peer;
}

static void replace_macro_delimiter(char *s)
{
	for (; *s; s++)
		if (*s == '^')
			*s = ',';
}

/* returns true if there is a valid privacy reply */
static int valid_priv_reply(struct ast_flags64 *opts, int res)
{
	if (res < '1')
		return 0;
	if (ast_test_flag64(opts, OPT_PRIVACY) && res <= '5')
		return 1;
	if (ast_test_flag64(opts, OPT_SCREENING) && res <= '4')
		return 1;
	return 0;
}

static int do_timelimit(struct ast_channel *chan, struct ast_bridge_config *config,
	char *parse, unsigned int *calldurationlimit)
{
	char *stringp = ast_strdupa(parse);
	char *limit_str, *warning_str, *warnfreq_str;
	const char *var;
	int play_to_caller = 0, play_to_callee = 0;
	int delta;

	limit_str = strsep(&stringp, ":");
	warning_str = strsep(&stringp, ":");
	warnfreq_str = strsep(&stringp, ":");

	config->timelimit = atol(limit_str);
	if (warning_str)
		config->play_warning = atol(warning_str);
	if (warnfreq_str)
		config->warning_freq = atol(warnfreq_str);

	if (!config->timelimit) {
		ast_log(LOG_WARNING, "Dial does not accept L(%s), hanging up.\n", limit_str);
		config->timelimit = config->play_warning = config->warning_freq = 0;
		config->warning_sound = NULL;
		return -1; /* error */
	} else if ( (delta = config->play_warning - config->timelimit) > 0) {
		int w = config->warning_freq;

		/* If the first warning is requested _after_ the entire call would end,
		   and no warning frequency is requested, then turn off the warning. If
		   a warning frequency is requested, reduce the 'first warning' time by
		   that frequency until it falls within the call's total time limit.
		   Graphically:
				  timelim->|    delta        |<-playwarning
			0__________________|_________________|
					 | w  |    |    |    |

		   so the number of intervals to cut is 1+(delta-1)/w
		*/

		if (w == 0) {
			config->play_warning = 0;
		} else {
			config->play_warning -= w * ( 1 + (delta-1)/w );
			if (config->play_warning < 1)
				config->play_warning = config->warning_freq = 0;
		}
	}

	var = pbx_builtin_getvar_helper(chan, "LIMIT_PLAYAUDIO_CALLER");
	play_to_caller = var ? ast_true(var) : 1;

	var = pbx_builtin_getvar_helper(chan, "LIMIT_PLAYAUDIO_CALLEE");
	play_to_callee = var ? ast_true(var) : 0;

	if (!play_to_caller && !play_to_callee)
		play_to_caller = 1;

	var = pbx_builtin_getvar_helper(chan, "LIMIT_WARNING_FILE");
	config->warning_sound = S_OR(var, "timeleft");

	/* The code looking at config wants a NULL, not just "", to decide
	 * that the message should not be played, so we replace "" with NULL.
	 * Note, pbx_builtin_getvar_helper _can_ return NULL if the variable is
	 * not found.
	 */
	var = pbx_builtin_getvar_helper(chan, "LIMIT_TIMEOUT_FILE");
	config->end_sound = S_OR(var, NULL);
	var = pbx_builtin_getvar_helper(chan, "LIMIT_CONNECT_FILE");
	config->start_sound = S_OR(var, NULL);

	/* undo effect of S(x) in case they are both used */
	*calldurationlimit = 0;
	/* more efficient to do it like S(x) does since no advanced opts */
	if (!config->play_warning && !config->start_sound && !config->end_sound && config->timelimit) {
		*calldurationlimit = config->timelimit / 1000;
		ast_verb(3, "Setting call duration limit to %d seconds.\n",
			*calldurationlimit);
		config->timelimit = play_to_caller = play_to_callee =
		config->play_warning = config->warning_freq = 0;
	} else {
		ast_verb(3, "Limit Data for this call:\n");
		ast_verb(4, "timelimit      = %ld\n", config->timelimit);
		ast_verb(4, "play_warning   = %ld\n", config->play_warning);
		ast_verb(4, "play_to_caller = %s\n", play_to_caller ? "yes" : "no");
		ast_verb(4, "play_to_callee = %s\n", play_to_callee ? "yes" : "no");
		ast_verb(4, "warning_freq   = %ld\n", config->warning_freq);
		ast_verb(4, "start_sound    = %s\n", S_OR(config->start_sound, ""));
		ast_verb(4, "warning_sound  = %s\n", config->warning_sound);
		ast_verb(4, "end_sound      = %s\n", S_OR(config->end_sound, ""));
	}
	if (play_to_caller)
		ast_set_flag(&(config->features_caller), AST_FEATURE_PLAY_WARNING);
	if (play_to_callee)
		ast_set_flag(&(config->features_callee), AST_FEATURE_PLAY_WARNING);
	return 0;
}

static int do_privacy(struct ast_channel *chan, struct ast_channel *peer,
	struct ast_flags64 *opts, char **opt_args, struct privacy_args *pa)
{

	int res2;
	int loopcount = 0;

	/* Get the user's intro, store it in priv-callerintros/$CID,
	   unless it is already there-- this should be done before the
	   call is actually dialed  */

	/* all ring indications and moh for the caller has been halted as soon as the
	   target extension was picked up. We are going to have to kill some
	   time and make the caller believe the peer hasn't picked up yet */

	if (ast_test_flag64(opts, OPT_MUSICBACK) && !ast_strlen_zero(opt_args[OPT_ARG_MUSICBACK])) {
		char *original_moh = ast_strdupa(chan->musicclass);
		ast_indicate(chan, -1);
		ast_string_field_set(chan, musicclass, opt_args[OPT_ARG_MUSICBACK]);
		ast_moh_start(chan, opt_args[OPT_ARG_MUSICBACK], NULL);
		ast_string_field_set(chan, musicclass, original_moh);
	} else if (ast_test_flag64(opts, OPT_RINGBACK)) {
		ast_indicate(chan, AST_CONTROL_RINGING);
		pa->sentringing++;
	}

	/* Start autoservice on the other chan ?? */
	res2 = ast_autoservice_start(chan);
	/* Now Stream the File */
	for (loopcount = 0; loopcount < 3; loopcount++) {
		if (res2 && loopcount == 0) /* error in ast_autoservice_start() */
			break;
		if (!res2) /* on timeout, play the message again */
			res2 = ast_play_and_wait(peer, "priv-callpending");
		if (!valid_priv_reply(opts, res2))
			res2 = 0;
		/* priv-callpending script:
		   "I have a caller waiting, who introduces themselves as:"
		*/
		if (!res2)
			res2 = ast_play_and_wait(peer, pa->privintro);
		if (!valid_priv_reply(opts, res2))
			res2 = 0;
		/* now get input from the called party, as to their choice */
		if (!res2) {
			/* XXX can we have both, or they are mutually exclusive ? */
			if (ast_test_flag64(opts, OPT_PRIVACY))
				res2 = ast_play_and_wait(peer, "priv-callee-options");
			if (ast_test_flag64(opts, OPT_SCREENING))
				res2 = ast_play_and_wait(peer, "screen-callee-options");
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
		if (valid_priv_reply(opts, res2))
			break;
		/* invalid option */
		res2 = ast_play_and_wait(peer, "vm-sorry");
	}

	if (ast_test_flag64(opts, OPT_MUSICBACK)) {
		ast_moh_stop(chan);
	} else if (ast_test_flag64(opts, OPT_RINGBACK)) {
		ast_indicate(chan, -1);
		pa->sentringing = 0;
	}
	ast_autoservice_stop(chan);
	if (ast_test_flag64(opts, OPT_PRIVACY) && (res2 >= '1' && res2 <= '5')) {
		/* map keypresses to various things, the index is res2 - '1' */
		static const char *_val[] = { "ALLOW", "DENY", "TORTURE", "KILL", "ALLOW" };
		static const int _flag[] = { AST_PRIVACY_ALLOW, AST_PRIVACY_DENY, AST_PRIVACY_TORTURE, AST_PRIVACY_KILL, AST_PRIVACY_ALLOW};
		int i = res2 - '1';
		ast_verb(3, "--Set privacy database entry %s/%s to %s\n",
			opt_args[OPT_ARG_PRIVACY], pa->privcid, _val[i]);
		ast_privacy_set(opt_args[OPT_ARG_PRIVACY], pa->privcid, _flag[i]);
	}
	switch (res2) {
	case '1':
		break;
	case '2':
		ast_copy_string(pa->status, "NOANSWER", sizeof(pa->status));
		break;
	case '3':
		ast_copy_string(pa->status, "TORTURE", sizeof(pa->status));
		break;
	case '4':
		ast_copy_string(pa->status, "DONTCALL", sizeof(pa->status));
		break;
	case '5':
		/* XXX should we set status to DENY ? */
		if (ast_test_flag64(opts, OPT_PRIVACY))
			break;
		/* if not privacy, then 5 is the same as "default" case */
	default: /* bad input or -1 if failure to start autoservice */
		/* well, if the user messes up, ... he had his chance... What Is The Best Thing To Do?  */
		/* well, there seems basically two choices. Just patch the caller thru immediately,
			  or,... put 'em thru to voicemail. */
		/* since the callee may have hung up, let's do the voicemail thing, no database decision */
		ast_log(LOG_NOTICE, "privacy: no valid response from the callee. Sending the caller to voicemail, the callee isn't responding\n");
		/* XXX should we set status to DENY ? */
		/* XXX what about the privacy flags ? */
		break;
	}

	if (res2 == '1') { /* the only case where we actually connect */
		/* if the intro is NOCALLERID, then there's no reason to leave it on disk, it'll
		   just clog things up, and it's not useful information, not being tied to a CID */
		if (strncmp(pa->privcid, "NOCALLERID", 10) == 0 || ast_test_flag64(opts, OPT_SCREEN_NOINTRO)) {
			ast_filedelete(pa->privintro, NULL);
			if (ast_fileexists(pa->privintro, NULL, NULL) > 0)
				ast_log(LOG_NOTICE, "privacy: ast_filedelete didn't do its job on %s\n", pa->privintro);
			else
				ast_verb(3, "Successfully deleted %s intro file\n", pa->privintro);
		}
		return 0; /* the good exit path */
	} else {
		ast_hangup(peer); /* hang up on the callee -- he didn't want to talk anyway! */
		return -1;
	}
}

/*! \brief returns 1 if successful, 0 or <0 if the caller should 'goto out' */
static int setup_privacy_args(struct privacy_args *pa,
	struct ast_flags64 *opts, char *opt_args[], struct ast_channel *chan)
{
	char callerid[60];
	int res;
	char *l;

	if (!ast_strlen_zero(chan->cid.cid_num)) {
		l = ast_strdupa(chan->cid.cid_num);
		ast_shrink_phone_number(l);
		if (ast_test_flag64(opts, OPT_PRIVACY) ) {
			ast_verb(3, "Privacy DB is '%s', clid is '%s'\n", opt_args[OPT_ARG_PRIVACY], l);
			pa->privdb_val = ast_privacy_check(opt_args[OPT_ARG_PRIVACY], l);
		} else {
			ast_verb(3, "Privacy Screening, clid is '%s'\n", l);
			pa->privdb_val = AST_PRIVACY_UNKNOWN;
		}
	} else {
		char *tnam, *tn2;

		tnam = ast_strdupa(chan->name);
		/* clean the channel name so slashes don't try to end up in disk file name */
		for (tn2 = tnam; *tn2; tn2++) {
			if (*tn2 == '/')  /* any other chars to be afraid of? */
				*tn2 = '=';
		}
		ast_verb(3, "Privacy-- callerid is empty\n");

		snprintf(callerid, sizeof(callerid), "NOCALLERID_%s%s", chan->exten, tnam);
		l = callerid;
		pa->privdb_val = AST_PRIVACY_UNKNOWN;
	}

	ast_copy_string(pa->privcid, l, sizeof(pa->privcid));

	if (strncmp(pa->privcid, "NOCALLERID", 10) != 0 && ast_test_flag64(opts, OPT_SCREEN_NOCLID)) {
		/* if callerid is set and OPT_SCREEN_NOCLID is set also */
		ast_verb(3, "CallerID set (%s); N option set; Screening should be off\n", pa->privcid);
		pa->privdb_val = AST_PRIVACY_ALLOW;
	} else if (ast_test_flag64(opts, OPT_SCREEN_NOCLID) && strncmp(pa->privcid, "NOCALLERID", 10) == 0) {
		ast_verb(3, "CallerID blank; N option set; Screening should happen; dbval is %d\n", pa->privdb_val);
	}
	
	if (pa->privdb_val == AST_PRIVACY_DENY) {
		ast_verb(3, "Privacy DB reports PRIVACY_DENY for this callerid. Dial reports unavailable\n");
		ast_copy_string(pa->status, "NOANSWER", sizeof(pa->status));
		return 0;
	} else if (pa->privdb_val == AST_PRIVACY_KILL) {
		ast_copy_string(pa->status, "DONTCALL", sizeof(pa->status));
		return 0; /* Is this right? */
	} else if (pa->privdb_val == AST_PRIVACY_TORTURE) {
		ast_copy_string(pa->status, "TORTURE", sizeof(pa->status));
		return 0; /* is this right??? */
	} else if (pa->privdb_val == AST_PRIVACY_UNKNOWN) {
		/* Get the user's intro, store it in priv-callerintros/$CID,
		   unless it is already there-- this should be done before the
		   call is actually dialed  */

		/* make sure the priv-callerintros dir actually exists */
		snprintf(pa->privintro, sizeof(pa->privintro), "%s/sounds/priv-callerintros", ast_config_AST_DATA_DIR);
		if ((res = ast_mkdir(pa->privintro, 0755))) {
			ast_log(LOG_WARNING, "privacy: can't create directory priv-callerintros: %s\n", strerror(res));
			return -1;
		}

		snprintf(pa->privintro, sizeof(pa->privintro), "priv-callerintros/%s", pa->privcid);
		if (ast_fileexists(pa->privintro, NULL, NULL ) > 0 && strncmp(pa->privcid, "NOCALLERID", 10) != 0) {
			/* the DELUX version of this code would allow this caller the
			   option to hear and retape their previously recorded intro.
			*/
		} else {
			int duration; /* for feedback from play_and_wait */
			/* the file doesn't exist yet. Let the caller submit his
			   vocal intro for posterity */
			/* priv-recordintro script:

			   "At the tone, please say your name:"

			*/
			ast_answer(chan);
			res = ast_play_and_record(chan, "priv-recordintro", pa->privintro, 4, "gsm", &duration, 128, 2000, 0);  /* NOTE: I've reduced the total time to 4 sec */
									/* don't think we'll need a lock removed, we took care of
									   conflicts by naming the pa.privintro file */
			if (res == -1) {
				/* Delete the file regardless since they hung up during recording */
				ast_filedelete(pa->privintro, NULL);
				if (ast_fileexists(pa->privintro, NULL, NULL) > 0)
					ast_log(LOG_NOTICE, "privacy: ast_filedelete didn't do its job on %s\n", pa->privintro);
				else
					ast_verb(3, "Successfully deleted %s intro file\n", pa->privintro);
				return -1;
			}
			if (!ast_streamfile(chan, "vm-dialout", chan->language) )
				ast_waitstream(chan, "");
		}
	}
	return 1; /* success */
}

static void set_dial_features(struct ast_flags64 *opts, struct ast_dial_features *features)
{
	struct ast_flags64 perm_opts = {.flags = 0};

	ast_copy_flags64(&perm_opts, opts,
		OPT_CALLER_TRANSFER | OPT_CALLER_PARK | OPT_CALLER_MONITOR | OPT_CALLER_MIXMONITOR | OPT_CALLER_HANGUP |
		OPT_CALLEE_TRANSFER | OPT_CALLEE_PARK | OPT_CALLEE_MONITOR | OPT_CALLEE_MIXMONITOR | OPT_CALLEE_HANGUP);

	memset(features->options, 0, sizeof(features->options));

	ast_app_options2str64(dial_exec_options, &perm_opts, features->options, sizeof(features->options));
}

static int dial_exec_full(struct ast_channel *chan, void *data, struct ast_flags64 *peerflags, int *continue_exec)
{
	int res = -1; /* default: error */
	char *rest, *cur; /* scan the list of destinations */
	struct chanlist *outgoing = NULL; /* list of destinations */
	struct ast_channel *peer;
	int to; /* timeout */
	struct cause_args num = { chan, 0, 0, 0 };
	int cause;
	char numsubst[256];
	char cidname[AST_MAX_EXTENSION] = "";

	struct ast_bridge_config config = { { 0, } };
	unsigned int calldurationlimit = 0;
	char *dtmfcalled = NULL, *dtmfcalling = NULL;
	struct privacy_args pa = {
		.sentringing = 0,
		.privdb_val = 0,
		.status = "INVALIDARGS",
	};
	int sentringing = 0, moh = 0;
	const char *outbound_group = NULL;
	int result = 0;
	time_t start_time;
	char *parse;
	int opermode = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(peers);
		AST_APP_ARG(timeout);
		AST_APP_ARG(options);
		AST_APP_ARG(url);
	);
	struct ast_flags64 opts = { 0, };
	char *opt_args[OPT_ARG_ARRAY_SIZE];
	struct ast_datastore *datastore = NULL;
	struct ast_datastore *ds_caller_features = NULL;
	struct ast_datastore *ds_callee_features = NULL;
	struct ast_dial_features *caller_features;
	int fulldial = 0, num_dialed = 0;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Dial requires an argument (technology/number)\n");
		pbx_builtin_setvar_helper(chan, "DIALSTATUS", pa.status);
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (!ast_strlen_zero(args.options) &&
		ast_app_parse_options64(dial_exec_options, &opts, opt_args, args.options)) {
		pbx_builtin_setvar_helper(chan, "DIALSTATUS", pa.status);
		goto done;
	}

	if (ast_strlen_zero(args.peers)) {
		ast_log(LOG_WARNING, "Dial requires an argument (technology/number)\n");
		pbx_builtin_setvar_helper(chan, "DIALSTATUS", pa.status);
		goto done;
	}

	if (ast_test_flag64(&opts, OPT_OPERMODE)) {
		opermode = ast_strlen_zero(opt_args[OPT_ARG_OPERMODE]) ? 1 : atoi(opt_args[OPT_ARG_OPERMODE]);
		ast_verb(3, "Setting operator services mode to %d.\n", opermode);
	}
	
	if (ast_test_flag64(&opts, OPT_DURATION_STOP) && !ast_strlen_zero(opt_args[OPT_ARG_DURATION_STOP])) {
		calldurationlimit = atoi(opt_args[OPT_ARG_DURATION_STOP]);
		if (!calldurationlimit) {
			ast_log(LOG_WARNING, "Dial does not accept S(%s), hanging up.\n", opt_args[OPT_ARG_DURATION_STOP]);
			pbx_builtin_setvar_helper(chan, "DIALSTATUS", pa.status);
			goto done;
		}
		ast_verb(3, "Setting call duration limit to %d seconds.\n", calldurationlimit);
	}

	if (ast_test_flag64(&opts, OPT_SENDDTMF) && !ast_strlen_zero(opt_args[OPT_ARG_SENDDTMF])) {
		dtmfcalling = opt_args[OPT_ARG_SENDDTMF];
		dtmfcalled = strsep(&dtmfcalling, ":");
	}

	if (ast_test_flag64(&opts, OPT_DURATION_LIMIT) && !ast_strlen_zero(opt_args[OPT_ARG_DURATION_LIMIT])) {
		if (do_timelimit(chan, &config, opt_args[OPT_ARG_DURATION_LIMIT], &calldurationlimit))
			goto done;
	}

	if (ast_test_flag64(&opts, OPT_RESETCDR) && chan->cdr)
		ast_cdr_reset(chan->cdr, NULL);
	if (ast_test_flag64(&opts, OPT_PRIVACY) && ast_strlen_zero(opt_args[OPT_ARG_PRIVACY]))
		opt_args[OPT_ARG_PRIVACY] = ast_strdupa(chan->exten);

	if (ast_test_flag64(&opts, OPT_PRIVACY) || ast_test_flag64(&opts, OPT_SCREENING)) {
		res = setup_privacy_args(&pa, &opts, opt_args, chan);
		if (res <= 0)
			goto out;
		res = -1; /* reset default */
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

	ast_copy_flags64(peerflags, &opts, OPT_DTMF_EXIT | OPT_GO_ON | OPT_ORIGINAL_CLID | OPT_CALLER_HANGUP | OPT_IGNORE_FORWARDING);

	/* Create datastore for channel dial features for caller */
	if (!(ds_caller_features = ast_channel_datastore_alloc(&dial_features_info, NULL))) {
		ast_log(LOG_WARNING, "Unable to create channel datastore for dial features. Aborting!\n");
		goto out;
	}

	if (!(caller_features = ast_malloc(sizeof(*caller_features)))) {
		ast_log(LOG_WARNING, "Unable to allocate memory for feature flags. Aborting!\n");
		goto out;
	}

	ast_copy_flags(&(caller_features->features_callee), &(config.features_caller), AST_FLAGS_ALL);
	caller_features->is_caller = 1;
	set_dial_features(&opts, caller_features);

	ds_caller_features->inheritance = DATASTORE_INHERIT_FOREVER;
	ds_caller_features->data = caller_features;

	ast_channel_lock(chan);
	ast_channel_datastore_add(chan, ds_caller_features);
	ast_channel_unlock(chan);

	/* loop through the list of dial destinations */
	rest = args.peers;
	while ((cur = strsep(&rest, "&")) ) {
		struct chanlist *tmp;
		struct ast_channel *tc; /* channel for this destination */
		/* Get a technology/[device:]number pair */
		char *number = cur;
		char *interface = ast_strdupa(number);
		char *tech = strsep(&number, "/");
		/* find if we already dialed this interface */
		struct ast_dialed_interface *di;
		struct ast_dial_features *callee_features;
		AST_LIST_HEAD(, ast_dialed_interface) *dialed_interfaces;
		num_dialed++;
		if (!number) {
			ast_log(LOG_WARNING, "Dial argument takes format (technology/[device:]number1)\n");
			goto out;
		}
		if (!(tmp = ast_calloc(1, sizeof(*tmp))))
			goto out;
		if (opts.flags) {
			ast_copy_flags64(tmp, &opts,
				OPT_CANCEL_ELSEWHERE |
				OPT_CALLEE_TRANSFER | OPT_CALLER_TRANSFER |
				OPT_CALLEE_HANGUP | OPT_CALLER_HANGUP |
				OPT_CALLEE_MONITOR | OPT_CALLER_MONITOR |
				OPT_CALLEE_PARK | OPT_CALLER_PARK |
				OPT_CALLEE_MIXMONITOR | OPT_CALLER_MIXMONITOR |
				OPT_RINGBACK | OPT_MUSICBACK | OPT_FORCECLID);
			ast_set2_flag64(tmp, args.url, DIAL_NOFORWARDHTML);
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
				ast_free(tmp);
				goto out;
			}

			datastore->inheritance = DATASTORE_INHERIT_FOREVER;

			if (!(dialed_interfaces = ast_calloc(1, sizeof(*dialed_interfaces)))) {
				ast_free(tmp);
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
			ast_free(tmp);
			continue;
		}

		/* It is always ok to dial a Local interface.  We only keep track of
		 * which "real" interfaces have been dialed.  The Local channel will
		 * inherit this list so that if it ends up dialing a real interface,
		 * it won't call one that has already been called. */
		if (strcasecmp(tech, "Local")) {
			if (!(di = ast_calloc(1, sizeof(*di) + strlen(interface)))) {
				AST_LIST_UNLOCK(dialed_interfaces);
				ast_free(tmp);
				goto out;
			}
			strcpy(di->interface, interface);

			AST_LIST_LOCK(dialed_interfaces);
			AST_LIST_INSERT_TAIL(dialed_interfaces, di, list);
			AST_LIST_UNLOCK(dialed_interfaces);
		}

		tc = ast_request(tech, chan->nativeformats, numsubst, &cause);
		if (!tc) {
			/* If we can't, just go on to the next call */
			ast_log(LOG_WARNING, "Unable to create channel of type '%s' (cause %d - %s)\n",
				tech, cause, ast_cause2str(cause));
			handle_cause(cause, &num);
			if (!rest) /* we are on the last destination */
				chan->hangupcause = cause;
			ast_free(tmp);
			continue;
		}
		pbx_builtin_setvar_helper(tc, "DIALEDPEERNUMBER", numsubst);

		/* Setup outgoing SDP to match incoming one */
		ast_rtp_make_compatible(tc, chan, !outgoing && !rest);
		
		/* Inherit specially named variables from parent channel */
		ast_channel_inherit_variables(chan, tc);

		tc->appl = "AppDial";
		tc->data = "(Outgoing Line)";
		tc->whentohangup = 0;

		S_REPLACE(tc->cid.cid_num, ast_strdup(chan->cid.cid_num));
		S_REPLACE(tc->cid.cid_name, ast_strdup(chan->cid.cid_name));
		S_REPLACE(tc->cid.cid_ani, ast_strdup(chan->cid.cid_ani));
		S_REPLACE(tc->cid.cid_rdnis, ast_strdup(chan->cid.cid_rdnis));
		
		/* Copy language from incoming to outgoing */
		ast_string_field_set(tc, language, chan->language);
		ast_string_field_set(tc, accountcode, chan->accountcode);
		tc->cdrflags = chan->cdrflags;
		if (ast_strlen_zero(tc->musicclass))
			ast_string_field_set(tc, musicclass, chan->musicclass);
		/* Pass callingpres, type of number, tns, ADSI CPE, transfer capability */
		tc->cid.cid_pres = chan->cid.cid_pres;
		tc->cid.cid_ton = chan->cid.cid_ton;
		tc->cid.cid_tns = chan->cid.cid_tns;
		tc->cid.cid_ani2 = chan->cid.cid_ani2;
		tc->adsicpe = chan->adsicpe;
		tc->transfercapability = chan->transfercapability;

		/* If we have an outbound group, set this peer channel to it */
		if (outbound_group)
			ast_app_group_set_channel(tc, outbound_group);

		/* Inherit context and extension */
		if (!ast_strlen_zero(chan->macrocontext))
			ast_copy_string(tc->dialcontext, chan->macrocontext, sizeof(tc->dialcontext));
		else
			ast_copy_string(tc->dialcontext, chan->context, sizeof(tc->dialcontext));
		if (!ast_strlen_zero(chan->macroexten))
			ast_copy_string(tc->exten, chan->macroexten, sizeof(tc->exten));
		else
			ast_copy_string(tc->exten, chan->exten, sizeof(tc->exten));

		/* Save callee features */
		if (!(ds_callee_features = ast_channel_datastore_alloc(&dial_features_info, NULL))) {
			ast_log(LOG_WARNING, "Unable to create channel datastore for dial features. Aborting!\n");
			ast_free(tmp);
			goto out;
		}

		if (!(callee_features = ast_malloc(sizeof(*callee_features)))) {
			ast_log(LOG_WARNING, "Unable to allocate memory for feature flags. Aborting!\n");
			ast_free(tmp);
			goto out;
		}

		ast_copy_flags(&(callee_features->features_callee), &(config.features_callee), AST_FLAGS_ALL);
		callee_features->is_caller = 0;
		set_dial_features(&opts, callee_features);

		ds_callee_features->inheritance = DATASTORE_INHERIT_FOREVER;
		ds_callee_features->data = callee_features;

		ast_channel_lock(chan);
		ast_channel_datastore_add(tc, ds_callee_features);
		ast_channel_unlock(chan);

		res = ast_call(tc, numsubst, 0); /* Place the call, but don't wait on the answer */

		/* Save the info in cdr's that we called them */
		if (chan->cdr)
			ast_cdr_setdestchan(chan->cdr, tc->name);

		/* check the results of ast_call */
		if (res) {
			/* Again, keep going even if there's an error */
			ast_debug(1, "ast call on peer returned %d\n", res);
			ast_verb(3, "Couldn't call %s\n", numsubst);
			ast_hangup(tc);
			tc = NULL;
			ast_free(tmp);
			continue;
		} else {
			senddialevent(chan, tc, numsubst);
			ast_verb(3, "Called %s\n", numsubst);
			if (!ast_test_flag64(peerflags, OPT_ORIGINAL_CLID))
				ast_set_callerid(tc, S_OR(chan->macroexten, chan->exten), get_cid_name(cidname, sizeof(cidname), chan), NULL);
		}
		/* Put them in the list of outgoing thingies...  We're ready now.
		   XXX If we're forcibly removed, these outgoing calls won't get
		   hung up XXX */
		ast_set_flag64(tmp, DIAL_STILLGOING);
		tmp->chan = tc;
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
		strcpy(pa.status, "CHANUNAVAIL");
		if (fulldial == num_dialed) {
			res = -1;
			goto out;
		}
	} else {
		/* Our status will at least be NOANSWER */
		strcpy(pa.status, "NOANSWER");
		if (ast_test_flag64(outgoing, OPT_MUSICBACK)) {
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
		} else if (ast_test_flag64(outgoing, OPT_RINGBACK)) {
			ast_indicate(chan, AST_CONTROL_RINGING);
			sentringing++;
		}
	}

	time(&start_time);
	peer = wait_for_answer(chan, outgoing, &to, peerflags, &pa, &num, &result);

	/* The ast_channel_datastore_remove() function could fail here if the
	 * datastore was moved to another channel during a masquerade. If this is
	 * the case, don't free the datastore here because later, when the channel
	 * to which the datastore was moved hangs up, it will attempt to free this
	 * datastore again, causing a crash
	 */
	if (!ast_channel_datastore_remove(chan, datastore))
		ast_channel_datastore_free(datastore);
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
		time_t end_time, answer_time = time(NULL);
		char toast[80]; /* buffer to set variables */

		strcpy(pa.status, "ANSWER");
		/* Ah ha!  Someone answered within the desired timeframe.  Of course after this
		   we will always return with -1 so that it is hung up properly after the
		   conversation.  */
		hanguptree(outgoing, peer, 1);
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
			ast_debug(1, "app_dial: sendurl=%s.\n", args.url);
			ast_channel_sendurl( peer, args.url );
		}
		if ( (ast_test_flag64(&opts, OPT_PRIVACY) || ast_test_flag64(&opts, OPT_SCREENING)) && pa.privdb_val == AST_PRIVACY_UNKNOWN) {
			if (do_privacy(chan, peer, &opts, opt_args, &pa)) {
				res = 0;
				goto out;
			}
		}
		if (!ast_test_flag64(&opts, OPT_ANNOUNCE) || ast_strlen_zero(opt_args[OPT_ARG_ANNOUNCE])) {
			res = 0;
		} else {
			int digit = 0;
			/* Start autoservice on the other chan */
			res = ast_autoservice_start(chan);
			/* Now Stream the File */
			if (!res)
				res = ast_streamfile(peer, opt_args[OPT_ARG_ANNOUNCE], peer->language);
			if (!res) {
				digit = ast_waitstream(peer, AST_DIGIT_ANY);
			}
			/* Ok, done. stop autoservice */
			res = ast_autoservice_stop(chan);
			if (digit > 0 && !res)
				res = ast_senddigit(chan, digit, 0);
			else
				res = digit;

		}

		if (chan && peer && ast_test_flag64(&opts, OPT_GOTO) && !ast_strlen_zero(opt_args[OPT_ARG_GOTO])) {
			replace_macro_delimiter(opt_args[OPT_ARG_GOTO]);
			ast_parseable_goto(chan, opt_args[OPT_ARG_GOTO]);
			/* peer goes to the same context and extension as chan, so just copy info from chan*/
			ast_copy_string(peer->context, chan->context, sizeof(peer->context));
			ast_copy_string(peer->exten, chan->exten, sizeof(peer->exten));
			peer->priority = chan->priority + 2;
			ast_pbx_start(peer);
			hanguptree(outgoing, NULL, ast_test_flag64(&opts, OPT_CANCEL_ELSEWHERE) ? 1 : 0);
			if (continue_exec)
				*continue_exec = 1;
			res = 0;
			goto done;
		}

		if (ast_test_flag64(&opts, OPT_CALLEE_MACRO) && !ast_strlen_zero(opt_args[OPT_ARG_CALLEE_MACRO])) {
			struct ast_app *theapp;
			const char *macro_result;

			res = ast_autoservice_start(chan);
			if (res) {
				ast_log(LOG_ERROR, "Unable to start autoservice on calling channel\n");
				res = -1;
			}

			theapp = pbx_findapp("Macro");

			if (theapp && !res) { /* XXX why check res here ? */
				replace_macro_delimiter(opt_args[OPT_ARG_CALLEE_MACRO]);
				res = pbx_exec(peer, theapp, opt_args[OPT_ARG_CALLEE_MACRO]);
				ast_debug(1, "Macro exited with status %d\n", res);
				res = 0;
			} else {
				ast_log(LOG_ERROR, "Could not find application Macro\n");
				res = -1;
			}

			if (ast_autoservice_stop(chan) < 0) {
				ast_log(LOG_ERROR, "Could not stop autoservice on calling channel\n");
				res = -1;
			}

			if (!res && (macro_result = pbx_builtin_getvar_helper(peer, "MACRO_RESULT"))) {
				char *macro_transfer_dest;

				if (!strcasecmp(macro_result, "BUSY")) {
					ast_copy_string(pa.status, macro_result, sizeof(pa.status));
					ast_set_flag64(peerflags, OPT_GO_ON);
					res = -1;
				} else if (!strcasecmp(macro_result, "CONGESTION") || !strcasecmp(macro_result, "CHANUNAVAIL")) {
					ast_copy_string(pa.status, macro_result, sizeof(pa.status));
					ast_set_flag64(peerflags, OPT_GO_ON);
					res = -1;
				} else if (!strcasecmp(macro_result, "CONTINUE")) {
					/* hangup peer and keep chan alive assuming the macro has changed
					   the context / exten / priority or perhaps
					   the next priority in the current exten is desired.
					*/
					ast_set_flag64(peerflags, OPT_GO_ON);
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
							ast_set_flag64(peerflags, OPT_GO_ON);
					}
				}
			}
		}

		if (ast_test_flag64(&opts, OPT_CALLEE_GOSUB) && !ast_strlen_zero(opt_args[OPT_ARG_CALLEE_GOSUB])) {
			struct ast_app *theapp;
			const char *gosub_result;
			char *gosub_args, *gosub_argstart;

			res = ast_autoservice_start(chan);
			if (res) {
				ast_log(LOG_ERROR, "Unable to start autoservice on calling channel\n");
				res = -1;
			}

			theapp = pbx_findapp("Gosub");

			if (theapp && !res) { /* XXX why check res here ? */
				replace_macro_delimiter(opt_args[OPT_ARG_CALLEE_GOSUB]);

				/* Set where we came from */
				ast_copy_string(peer->context, "app_dial_gosub_virtual_context", sizeof(peer->context));
				ast_copy_string(peer->exten, "s", sizeof(peer->exten));
				peer->priority = 0;

				gosub_argstart = strchr(opt_args[OPT_ARG_CALLEE_GOSUB], ',');
				if (gosub_argstart) {
					*gosub_argstart = 0;
					asprintf(&gosub_args, "%s,s,1(%s)", opt_args[OPT_ARG_CALLEE_GOSUB], gosub_argstart + 1);
					*gosub_argstart = ',';
				} else {
					asprintf(&gosub_args, "%s,s,1", opt_args[OPT_ARG_CALLEE_GOSUB]);
				}

				if (gosub_args) {
					res = pbx_exec(peer, theapp, gosub_args);
					ast_pbx_run(peer);
					ast_free(gosub_args);
					if (option_debug)
						ast_log(LOG_DEBUG, "Gosub exited with status %d\n", res);
				} else
					ast_log(LOG_ERROR, "Could not Allocate string for Gosub arguments -- Gosub Call Aborted!\n");

				res = 0;
			} else {
				ast_log(LOG_ERROR, "Could not find application Gosub\n");
				res = -1;
			}

			if (ast_autoservice_stop(chan) < 0) {
				ast_log(LOG_ERROR, "Could not stop autoservice on calling channel\n");
				res = -1;
			}

			if (!res && (gosub_result = pbx_builtin_getvar_helper(peer, "GOSUB_RESULT"))) {
				char *gosub_transfer_dest;

				if (!strcasecmp(gosub_result, "BUSY")) {
					ast_copy_string(pa.status, gosub_result, sizeof(pa.status));
					ast_set_flag64(peerflags, OPT_GO_ON);
					res = -1;
				} else if (!strcasecmp(gosub_result, "CONGESTION") || !strcasecmp(gosub_result, "CHANUNAVAIL")) {
					ast_copy_string(pa.status, gosub_result, sizeof(pa.status));
					ast_set_flag64(peerflags, OPT_GO_ON);
					res = -1;
				} else if (!strcasecmp(gosub_result, "CONTINUE")) {
					/* hangup peer and keep chan alive assuming the macro has changed
					   the context / exten / priority or perhaps
					   the next priority in the current exten is desired.
					*/
					ast_set_flag64(peerflags, OPT_GO_ON);
					res = -1;
				} else if (!strcasecmp(gosub_result, "ABORT")) {
					/* Hangup both ends unless the caller has the g flag */
					res = -1;
				} else if (!strncasecmp(gosub_result, "GOTO:", 5) && (gosub_transfer_dest = ast_strdupa(gosub_result + 5))) {
					res = -1;
					/* perform a transfer to a new extension */
					if (strchr(gosub_transfer_dest, '^')) { /* context^exten^priority*/
						replace_macro_delimiter(gosub_transfer_dest);
						if (!ast_parseable_goto(chan, gosub_transfer_dest))
							ast_set_flag64(peerflags, OPT_GO_ON);
					}
				}
			}
		}

		if (!res) {
			if (calldurationlimit > 0) {
				peer->whentohangup = time(NULL) + calldurationlimit;
			}
			if (!ast_strlen_zero(dtmfcalled)) {
				ast_verb(3, "Sending DTMF '%s' to the called party.\n", dtmfcalled);
				res = ast_dtmf_stream(peer, chan, dtmfcalled, 250, 0);
			}
			if (!ast_strlen_zero(dtmfcalling)) {
				ast_verb(3, "Sending DTMF '%s' to the calling party.\n", dtmfcalling);
				res = ast_dtmf_stream(chan, peer, dtmfcalling, 250, 0);
			}
		}
		
		if (res) { /* some error */
			res = -1;
			end_time = time(NULL);
		} else {
			if (ast_test_flag64(peerflags, OPT_CALLEE_TRANSFER))
				ast_set_flag(&(config.features_callee), AST_FEATURE_REDIRECT);
			if (ast_test_flag64(peerflags, OPT_CALLER_TRANSFER))
				ast_set_flag(&(config.features_caller), AST_FEATURE_REDIRECT);
			if (ast_test_flag64(peerflags, OPT_CALLEE_HANGUP))
				ast_set_flag(&(config.features_callee), AST_FEATURE_DISCONNECT);
			if (ast_test_flag64(peerflags, OPT_CALLER_HANGUP))
				ast_set_flag(&(config.features_caller), AST_FEATURE_DISCONNECT);
			if (ast_test_flag64(peerflags, OPT_CALLEE_MONITOR))
				ast_set_flag(&(config.features_callee), AST_FEATURE_AUTOMON);
			if (ast_test_flag64(peerflags, OPT_CALLER_MONITOR))
				ast_set_flag(&(config.features_caller), AST_FEATURE_AUTOMON);
			if (ast_test_flag64(peerflags, OPT_CALLEE_PARK))
				ast_set_flag(&(config.features_callee), AST_FEATURE_PARKCALL);
			if (ast_test_flag64(peerflags, OPT_CALLER_PARK))
				ast_set_flag(&(config.features_caller), AST_FEATURE_PARKCALL);
			if (ast_test_flag64(peerflags, OPT_CALLEE_MIXMONITOR))
				ast_set_flag(&(config.features_callee), AST_FEATURE_AUTOMIXMON);
			if (ast_test_flag64(peerflags, OPT_CALLER_MIXMONITOR))
				ast_set_flag(&(config.features_caller), AST_FEATURE_AUTOMIXMON);

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
				res = -1;
				goto done;
			}
			if (opermode && !strncmp(chan->tech->type, "DAHDI", 3) && !strncmp(peer->name, "DAHDI", 3)) {
				/* what's this special handling for dahdi <-> dahdi ?
				 * A: dahdi to dahdi calls are natively bridged at the kernel driver
				 * level, so we need to ensure that this mode gets propagated
				 * all the way down. */
				struct oprmode oprmode;

				oprmode.peer = peer;
				oprmode.mode = opermode;

				ast_channel_setoption(chan, AST_OPTION_OPRMODE, &oprmode, sizeof(oprmode), 0);
			}
			res = ast_bridge_call(chan, peer, &config);
			end_time = time(NULL);
			snprintf(toast, sizeof(toast), "%ld", (long)(end_time - answer_time));
			pbx_builtin_setvar_helper(chan, "ANSWEREDTIME", toast);
		}

		snprintf(toast, sizeof(toast), "%ld", (long)(end_time - start_time));
		pbx_builtin_setvar_helper(chan, "DIALEDTIME", toast);

		if (ast_test_flag64(&opts, OPT_PEER_H)) {
			ast_log(LOG_NOTICE, "PEER context: %s; PEER exten: %s;  PEER priority: %d\n",
				peer->context, peer->exten, peer->priority);
		}

		strcpy(peer->context, chan->context);

		if (ast_test_flag64(&opts, OPT_PEER_H) && ast_exists_extension(peer, peer->context, "h", 1, peer->cid.cid_num)) {
			int autoloopflag;
			int found;
			strcpy(peer->exten, "h");
			peer->priority = 1;
			autoloopflag = ast_test_flag(peer, AST_FLAG_IN_AUTOLOOP); /* save value to restore at the end */
			ast_set_flag(peer, AST_FLAG_IN_AUTOLOOP);

			while ((res = ast_spawn_extension(peer, peer->context, peer->exten, peer->priority, peer->cid.cid_num, &found, 1)))
				peer->priority++;

			if (found && res) {
				/* Something bad happened, or a hangup has been requested. */
				ast_debug(1, "Spawn extension (%s,%s,%d) exited non-zero on '%s'\n", peer->context, peer->exten, peer->priority, peer->name);
				ast_verb(2, "Spawn extension (%s, %s, %d) exited non-zero on '%s'\n", peer->context, peer->exten, peer->priority, peer->name);
			}
			ast_set2_flag(peer, autoloopflag, AST_FLAG_IN_AUTOLOOP);  /* set it back the way it was */
		}
		if (res != AST_PBX_NO_HANGUP_PEER) {
			if (!ast_check_hangup(chan))
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
	ast_channel_early_bridge(chan, NULL);
	hanguptree(outgoing, NULL, 0); /* In this case, there's no answer anywhere */
	pbx_builtin_setvar_helper(chan, "DIALSTATUS", pa.status);
	senddialendevent(chan, pa.status);
	ast_debug(1, "Exiting with DIALSTATUS=%s.\n", pa.status);

	if ((ast_test_flag64(peerflags, OPT_GO_ON)) && !ast_check_hangup(chan) && (res != AST_PBX_KEEPALIVE)) {
		if (calldurationlimit)
			chan->whentohangup = 0;
		res = 0;
	}

done:
	return res;
}

static int dial_exec(struct ast_channel *chan, void *data)
{
	struct ast_flags64 peerflags;

	memset(&peerflags, 0, sizeof(peerflags));

	return dial_exec_full(chan, data, &peerflags, NULL);
}

static int retrydial_exec(struct ast_channel *chan, void *data)
{
	char *parse;
	const char *context = NULL;
	int sleep = 0, loops = 0, res = -1;
	struct ast_flags64 peerflags = { 0, };
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(announce);
		AST_APP_ARG(sleep);
		AST_APP_ARG(retries);
		AST_APP_ARG(dialdata);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "RetryDial requires an argument!\n");
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if ((sleep = atoi(args.sleep)))
		sleep *= 1000;

	loops = atoi(args.retries);

	if (!args.dialdata) {
		ast_log(LOG_ERROR, "%s requires a 4th argument (dialdata)\n", rapp);
		goto done;
	}

	if (sleep < 1000)
		sleep = 10000;

	if (!loops)
		loops = -1; /* run forever */

	context = pbx_builtin_getvar_helper(chan, "EXITCONTEXT");

	res = 0;
	while (loops) {
		int continue_exec;

		chan->data = "Retrying";
		if (ast_test_flag(chan, AST_FLAG_MOH))
			ast_moh_stop(chan);

		res = dial_exec_full(chan, args.dialdata, &peerflags, &continue_exec);
		if (continue_exec)
			break;

		if (res == 0) {
			if (ast_test_flag64(&peerflags, OPT_DTMF_EXIT)) {
				if (!ast_strlen_zero(args.announce)) {
					if (ast_fileexists(args.announce, NULL, chan->language) > 0) {
						if (!(res = ast_streamfile(chan, args.announce, chan->language)))
							ast_waitstream(chan, AST_DIGIT_ANY);
					} else
						ast_log(LOG_WARNING, "Announce file \"%s\" specified in Retrydial does not exist\n", args.announce);
				}
				if (!res && sleep) {
					if (!ast_test_flag(chan, AST_FLAG_MOH))
						ast_moh_start(chan, NULL, NULL);
					res = ast_waitfordigit(chan, sleep);
				}
			} else {
				if (!ast_strlen_zero(args.announce)) {
					if (ast_fileexists(args.announce, NULL, chan->language) > 0) {
						if (!(res = ast_streamfile(chan, args.announce, chan->language)))
							res = ast_waitstream(chan, "");
					} else
						ast_log(LOG_WARNING, "Announce file \"%s\" specified in Retrydial does not exist\n", args.announce);
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
	return res;
}

static int unload_module(void)
{
	int res;
	struct ast_context *con;

	res = ast_unregister_application(app);
	res |= ast_unregister_application(rapp);

	if ((con = ast_context_find("app_dial_gosub_virtual_context"))) {
		ast_context_remove_extension2(con, "s", 1, NULL, 0);
		ast_context_destroy(con, "app_dial"); /* leave nothing behind */
	}

	return res;
}

static int load_module(void)
{
	int res;
	struct ast_context *con;

	con = ast_context_find("app_dial_gosub_virtual_context");
	if (!con)
		con = ast_context_create(NULL, "app_dial_gosub_virtual_context", "app_dial");
	if (!con)
		ast_log(LOG_ERROR, "Dial virtual context 'app_dial_gosub_virtual_context' does not exist and unable to create\n");
	else
		ast_add_extension2(con, 1, "s", 1, NULL, NULL, "KeepAlive", ast_strdup(""), ast_free_ptr, "app_dial");

	res = ast_register_application(app, dial_exec, synopsis, descrip);
	res |= ast_register_application(rapp, retrydial_exec, rsynopsis, rdescrip);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Dialing Application");
