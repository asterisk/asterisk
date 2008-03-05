/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2007, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief Dialing API
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/options.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/linkedlists.h"
#include "asterisk/dial.h"
#include "asterisk/pbx.h"

/*! \brief Main dialing structure. Contains global options, channels being dialed, and more! */
struct ast_dial {
	int num;                                           /*! Current number to give to next dialed channel */
	enum ast_dial_result state;                       /*! Status of dial */
	void *options[AST_DIAL_OPTION_MAX];                /*! Global options */
	ast_dial_state_callback state_callback;          /*! Status callback */
	AST_LIST_HEAD(, ast_dial_channel) channels; /*! Channels being dialed */
	pthread_t thread;                                  /*! Thread (if running in async) */
	ast_mutex_t lock;                                  /*! Lock to protect the thread information above */
};

/*! \brief Dialing channel structure. Contains per-channel dialing options, asterisk channel, and more! */
struct ast_dial_channel {
	int num;                               /*! Unique number for dialed channel */
	const char *tech;                      /*! Technology being dialed */
	const char *device;                    /*! Device being dialed */
	void *options[AST_DIAL_OPTION_MAX];    /*! Channel specific options */
	int cause;                             /*! Cause code in case of failure */
	int is_running_app:1;                  /*! Is this running an application? */
	struct ast_channel *owner;             /*! Asterisk channel */
	AST_LIST_ENTRY(ast_dial_channel) list; /*! Linked list information */
};

/*! \brief Typedef for dial option enable */
typedef void *(*ast_dial_option_cb_enable)(void *data);

/*! \brief Typedef for dial option disable */
typedef int (*ast_dial_option_cb_disable)(void *data);

/* Structure for 'ANSWER_EXEC' option */
struct answer_exec_struct {
	char app[AST_MAX_APP]; /* Application name */
	char *args;            /* Application arguments */
};

/* Enable function for 'ANSWER_EXEC' option */
static void *answer_exec_enable(void *data)
{
	struct answer_exec_struct *answer_exec = NULL;
	char *app = ast_strdupa((char*)data), *args = NULL;

	/* Not giving any data to this option is bad, mmmk? */
	if (ast_strlen_zero(app))
		return NULL;

	/* Create new data structure */
	if (!(answer_exec = ast_calloc(1, sizeof(*answer_exec))))
		return NULL;
	
	/* Parse out application and arguments */
	if ((args = strchr(app, '|'))) {
		*args++ = '\0';
		answer_exec->args = ast_strdup(args);
	}

	/* Copy application name */
	ast_copy_string(answer_exec->app, app, sizeof(answer_exec->app));

	return answer_exec;
}

/* Disable function for 'ANSWER_EXEC' option */
static int answer_exec_disable(void *data)
{
	struct answer_exec_struct *answer_exec = data;

	/* Make sure we have a value */
	if (!answer_exec)
		return -1;

	/* If arguments are present, free them too */
	if (answer_exec->args)
		free(answer_exec->args);

	/* This is simple - just free the structure */
	free(answer_exec);

	return 0;
}

/* Application execution function for 'ANSWER_EXEC' option */
static void answer_exec_run(struct ast_dial *dial, struct ast_dial_channel *dial_channel, char *app, char *args)
{
	struct ast_channel *chan = dial_channel->owner;
	struct ast_app *ast_app = pbx_findapp(app);

	/* If the application was not found, return immediately */
	if (!ast_app)
		return;

	/* All is well... execute the application */
	pbx_exec(chan, ast_app, args);

	/* If another thread is not taking over hang up the channel */
	ast_mutex_lock(&dial->lock);
	if (dial->thread != AST_PTHREADT_STOP) {
		ast_hangup(chan);
		dial_channel->owner = NULL;
	}
	ast_mutex_unlock(&dial->lock);

	return;
}

/*! \brief Options structure - maps options to respective handlers (enable/disable). This list MUST be perfectly kept in order, or else madness will happen. */
static const struct ast_option_types {
	enum ast_dial_option option;
	ast_dial_option_cb_enable enable;
	ast_dial_option_cb_disable disable;
} option_types[] = {
	{ AST_DIAL_OPTION_RINGING, NULL, NULL },                                  /*! Always indicate ringing to caller */
	{ AST_DIAL_OPTION_ANSWER_EXEC, answer_exec_enable, answer_exec_disable }, /*! Execute application upon answer in async mode */
	{ AST_DIAL_OPTION_MAX, NULL, NULL },                                      /*! Terminator of list */
};

/* free the buffer if allocated, and set the pointer to the second arg */
#define S_REPLACE(s, new_val)           \
        do {                            \
                if (s)                  \
                        free(s);        \
                s = (new_val);          \
        } while (0)

/*! \brief Maximum number of channels we can watch at a time */
#define AST_MAX_WATCHERS 256

/*! \brief Macro for finding the option structure to use on a dialed channel */
#define FIND_RELATIVE_OPTION(dial, dial_channel, ast_dial_option) (dial_channel->options[ast_dial_option] ? dial_channel->options[ast_dial_option] : dial->options[ast_dial_option])

/*! \brief Macro that determines whether a channel is the caller or not */
#define IS_CALLER(chan, owner) (chan == owner ? 1 : 0)

/*! \brief New dialing structure
 * \note Create a dialing structure
 * \return Returns a calloc'd ast_dial structure, NULL on failure
 */
struct ast_dial *ast_dial_create(void)
{
	struct ast_dial *dial = NULL;

	/* Allocate new memory for structure */
	if (!(dial = ast_calloc(1, sizeof(*dial))))
		return NULL;

	/* Initialize list of channels */
	AST_LIST_HEAD_INIT(&dial->channels);

	/* Initialize thread to NULL */
	dial->thread = AST_PTHREADT_NULL;

	/* Can't forget about the lock */
	ast_mutex_init(&dial->lock);

	return dial;
}

/*! \brief Append a channel
 * \note Appends a channel to a dialing structure
 * \return Returns channel reference number on success, -1 on failure
 */
int ast_dial_append(struct ast_dial *dial, const char *tech, const char *device)
{
	struct ast_dial_channel *channel = NULL;

	/* Make sure we have required arguments */
	if (!dial || !tech || !device)
		return -1;

	/* Allocate new memory for dialed channel structure */
	if (!(channel = ast_calloc(1, sizeof(*channel))))
		return -1;

	/* Record technology and device for when we actually dial */
	channel->tech = tech;
	channel->device = device;

	/* Grab reference number from dial structure */
	channel->num = ast_atomic_fetchadd_int(&dial->num, +1);

	/* Insert into channels list */
	AST_LIST_INSERT_TAIL(&dial->channels, channel, list);

	return channel->num;
}

/*! \brief Helper function that does the beginning dialing */
static int begin_dial(struct ast_dial *dial, struct ast_channel *chan)
{
	struct ast_dial_channel *channel = NULL;
	int success = 0, res = 0;

	/* Iterate through channel list, requesting and calling each one */
	AST_LIST_LOCK(&dial->channels);
	AST_LIST_TRAVERSE(&dial->channels, channel, list) {
		char numsubst[AST_MAX_EXTENSION];

		/* Copy device string over */
		ast_copy_string(numsubst, channel->device, sizeof(numsubst));

		/* Request that the channel be created */
		if (!(channel->owner = ast_request(channel->tech, 
			chan ? chan->nativeformats : AST_FORMAT_AUDIO_MASK, numsubst, &channel->cause))) {
			continue;
		}

		channel->owner->appl = "AppDial2";
                channel->owner->data = "(Outgoing Line)";
                channel->owner->whentohangup = 0;

		/* Inherit everything from he who spawned this Dial */
		if (chan) {
			ast_channel_inherit_variables(chan, channel->owner);

			/* Copy over callerid information */
			S_REPLACE(channel->owner->cid.cid_num, ast_strdup(chan->cid.cid_num));
			S_REPLACE(channel->owner->cid.cid_name, ast_strdup(chan->cid.cid_name));
			S_REPLACE(channel->owner->cid.cid_ani, ast_strdup(chan->cid.cid_ani));
			S_REPLACE(channel->owner->cid.cid_rdnis, ast_strdup(chan->cid.cid_rdnis));
	
			ast_string_field_set(channel->owner, language, chan->language);
			ast_string_field_set(channel->owner, accountcode, chan->accountcode);
			channel->owner->cdrflags = chan->cdrflags;
			if (ast_strlen_zero(channel->owner->musicclass))
				ast_string_field_set(channel->owner, musicclass, chan->musicclass);
	
			channel->owner->cid.cid_pres = chan->cid.cid_pres;
			channel->owner->cid.cid_ton = chan->cid.cid_ton;
			channel->owner->cid.cid_tns = chan->cid.cid_tns;
			channel->owner->adsicpe = chan->adsicpe;
			channel->owner->transfercapability = chan->transfercapability;
		}

		/* Actually call the device */
		if ((res = ast_call(channel->owner, numsubst, 0))) {
			ast_hangup(channel->owner);
			channel->owner = NULL;
		} else {
			success++;
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Called %s\n", numsubst);
		}
	}
	AST_LIST_UNLOCK(&dial->channels);

	/* If number of failures matches the number of channels, then this truly failed */
	return success;
}

/*! \brief Helper function that finds the dialed channel based on owner */
static struct ast_dial_channel *find_relative_dial_channel(struct ast_dial *dial, struct ast_channel *owner)
{
	struct ast_dial_channel *channel = NULL;

	AST_LIST_LOCK(&dial->channels);
	AST_LIST_TRAVERSE(&dial->channels, channel, list) {
		if (channel->owner == owner)
			break;
	}
	AST_LIST_UNLOCK(&dial->channels);

	return channel;
}

static void set_state(struct ast_dial *dial, enum ast_dial_result state)
{
	dial->state = state;

	if (dial->state_callback)
		dial->state_callback(dial);
}

/*! \brief Helper function that handles control frames WITH owner */
static void handle_frame(struct ast_dial *dial, struct ast_dial_channel *channel, struct ast_frame *fr, struct ast_channel *chan)
{
	if (fr->frametype == AST_FRAME_CONTROL) {
		switch (fr->subclass) {
		case AST_CONTROL_ANSWER:
			if (option_verbose > 2)
				ast_verbose( VERBOSE_PREFIX_3 "%s answered %s\n", channel->owner->name, chan->name);
			AST_LIST_LOCK(&dial->channels);
			AST_LIST_REMOVE(&dial->channels, channel, list);
			AST_LIST_INSERT_HEAD(&dial->channels, channel, list);
			AST_LIST_UNLOCK(&dial->channels);
			set_state(dial, AST_DIAL_RESULT_ANSWERED);
			break;
		case AST_CONTROL_BUSY:
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "%s is busy\n", channel->owner->name);
			ast_hangup(channel->owner);
			channel->owner = NULL;
			break;
		case AST_CONTROL_CONGESTION:
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "%s is circuit-busy\n", channel->owner->name);
			ast_hangup(channel->owner);
			channel->owner = NULL;
			break;
		case AST_CONTROL_RINGING:
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "%s is ringing\n", channel->owner->name);
			ast_indicate(chan, AST_CONTROL_RINGING);
			set_state(dial, AST_DIAL_RESULT_RINGING);
			break;
		case AST_CONTROL_PROGRESS:
			if (option_verbose > 2)
				ast_verbose (VERBOSE_PREFIX_3 "%s is making progress, passing it to %s\n", channel->owner->name, chan->name);
			ast_indicate(chan, AST_CONTROL_PROGRESS);
			set_state(dial, AST_DIAL_RESULT_PROGRESS);
			break;
		case AST_CONTROL_VIDUPDATE:
			if (option_verbose > 2)
				ast_verbose (VERBOSE_PREFIX_3 "%s requested a video update, passing it to %s\n", channel->owner->name, chan->name);
			ast_indicate(chan, AST_CONTROL_VIDUPDATE);
			break;
		case AST_CONTROL_SRCUPDATE:
			if (option_verbose > 2)
				ast_verbose (VERBOSE_PREFIX_3 "%s requested a source update, passing it to %s\n", channel->owner->name, chan->name);
			ast_indicate(chan, AST_CONTROL_SRCUPDATE);
		case AST_CONTROL_PROCEEDING:
			if (option_verbose > 2)
				ast_verbose (VERBOSE_PREFIX_3 "%s is proceeding, passing it to %s\n", channel->owner->name, chan->name);
			ast_indicate(chan, AST_CONTROL_PROCEEDING);
			set_state(dial, AST_DIAL_RESULT_PROCEEDING);
			break;
		case AST_CONTROL_HOLD:
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Call on %s placed on hold\n", chan->name);
			ast_indicate(chan, AST_CONTROL_HOLD);
			break;
		case AST_CONTROL_UNHOLD:
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Call on %s left from hold\n", chan->name);
			ast_indicate(chan, AST_CONTROL_UNHOLD);
			break;
		case AST_CONTROL_OFFHOOK:
		case AST_CONTROL_FLASH:
			break;
		case -1:
			/* Prod the channel */
			ast_indicate(chan, -1);
			break;
		default:
			break;
		}
	}

	return;
}

/*! \brief Helper function that handles control frames WITHOUT owner */
static void handle_frame_ownerless(struct ast_dial *dial, struct ast_dial_channel *channel, struct ast_frame *fr)
{
	/* If we have no owner we can only update the state of the dial structure, so only look at control frames */
	if (fr->frametype != AST_FRAME_CONTROL)
		return;

	switch (fr->subclass) {
	case AST_CONTROL_ANSWER:
		if (option_verbose > 2)
			ast_verbose( VERBOSE_PREFIX_3 "%s answered\n", channel->owner->name);
		AST_LIST_LOCK(&dial->channels);
		AST_LIST_REMOVE(&dial->channels, channel, list);
		AST_LIST_INSERT_HEAD(&dial->channels, channel, list);
		AST_LIST_UNLOCK(&dial->channels);
		set_state(dial, AST_DIAL_RESULT_ANSWERED);
		break;
	case AST_CONTROL_BUSY:
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "%s is busy\n", channel->owner->name);
		ast_hangup(channel->owner);
		channel->owner = NULL;
		break;
	case AST_CONTROL_CONGESTION:
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "%s is circuit-busy\n", channel->owner->name);
		ast_hangup(channel->owner);
		channel->owner = NULL;
		break;
	case AST_CONTROL_RINGING:
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_3 "%s is ringing\n", channel->owner->name);
		set_state(dial, AST_DIAL_RESULT_RINGING);
		break;
	case AST_CONTROL_PROGRESS:
		if (option_verbose > 2)
			ast_verbose (VERBOSE_PREFIX_3 "%s is making progress\n", channel->owner->name);
		set_state(dial, AST_DIAL_RESULT_PROGRESS);
		break;
	case AST_CONTROL_PROCEEDING:
		if (option_verbose > 2)
			ast_verbose (VERBOSE_PREFIX_3 "%s is proceeding\n", channel->owner->name);
		set_state(dial, AST_DIAL_RESULT_PROCEEDING);
		break;
	default:
		break;
	}

	return;
}

/*! \brief Helper function that basically keeps tabs on dialing attempts */
static enum ast_dial_result monitor_dial(struct ast_dial *dial, struct ast_channel *chan)
{
	int timeout = -1, count = 0;
	struct ast_channel *cs[AST_MAX_WATCHERS], *who = NULL;
	struct ast_dial_channel *channel = NULL;
	struct answer_exec_struct *answer_exec = NULL;

	set_state(dial, AST_DIAL_RESULT_TRYING);

	/* If the "always indicate ringing" option is set, change state to ringing and indicate to the owner if present */
	if (dial->options[AST_DIAL_OPTION_RINGING]) {
		set_state(dial, AST_DIAL_RESULT_RINGING);
		if (chan)
			ast_indicate(chan, AST_CONTROL_RINGING);
	}

	/* Go into an infinite loop while we are trying */
	while ((dial->state != AST_DIAL_RESULT_UNANSWERED) && (dial->state != AST_DIAL_RESULT_ANSWERED) && (dial->state != AST_DIAL_RESULT_HANGUP) && (dial->state != AST_DIAL_RESULT_TIMEOUT)) {
		int pos = 0;
		struct ast_frame *fr = NULL;

		/* Set up channel structure array */
		pos = count = 0;
		if (chan)
			cs[pos++] = chan;

		/* Add channels we are attempting to dial */
		AST_LIST_LOCK(&dial->channels);
		AST_LIST_TRAVERSE(&dial->channels, channel, list) {
			if (channel->owner) {
				cs[pos++] = channel->owner;
				count++;
			}
		}
		AST_LIST_UNLOCK(&dial->channels);

		/* If we have no outbound channels in progress, switch state to unanswered and stop */
		if (!count) {
			set_state(dial, AST_DIAL_RESULT_UNANSWERED);
			break;
		}

		/* Just to be safe... */
		if (dial->thread == AST_PTHREADT_STOP)
			break;

		/* Wait for frames from channels */
		who = ast_waitfor_n(cs, pos, &timeout);

		/* Check to see if our thread is being cancelled */
		if (dial->thread == AST_PTHREADT_STOP)
			break;

		/* If we are not being cancelled and we have no channel, then timeout was tripped */
		if (!who)
			continue;

		/* Find relative dial channel */
		if (!chan || !IS_CALLER(chan, who))
			channel = find_relative_dial_channel(dial, who);

		/* Attempt to read in a frame */
		if (!(fr = ast_read(who))) {
			/* If this is the caller then we switch state to hangup and stop */
			if (chan && IS_CALLER(chan, who)) {
				set_state(dial, AST_DIAL_RESULT_HANGUP);
				break;
			}
			ast_hangup(who);
			channel->owner = NULL;
			continue;
		}

		/* Process the frame */
		if (chan)
			handle_frame(dial, channel, fr, chan);
		else
			handle_frame_ownerless(dial, channel, fr);

		/* Free the received frame and start all over */
		ast_frfree(fr);
	}

	/* Do post-processing from loop */
	if (dial->state == AST_DIAL_RESULT_ANSWERED) {
		/* Hangup everything except that which answered */
		AST_LIST_LOCK(&dial->channels);
		AST_LIST_TRAVERSE(&dial->channels, channel, list) {
			if (!channel->owner || channel->owner == who)
				continue;
			ast_hangup(channel->owner);
			channel->owner = NULL;
		}
		AST_LIST_UNLOCK(&dial->channels);
		/* If ANSWER_EXEC is enabled as an option, execute application on answered channel */
		if ((channel = find_relative_dial_channel(dial, who)) && (answer_exec = FIND_RELATIVE_OPTION(dial, channel, AST_DIAL_OPTION_ANSWER_EXEC))) {
			channel->is_running_app = 1;
			answer_exec_run(dial, channel, answer_exec->app, answer_exec->args);
			channel->is_running_app = 0;
		}
	} else if (dial->state == AST_DIAL_RESULT_HANGUP) {
		/* Hangup everything */
		AST_LIST_LOCK(&dial->channels);
		AST_LIST_TRAVERSE(&dial->channels, channel, list) {
			if (!channel->owner)
				continue;
			ast_hangup(channel->owner);
			channel->owner = NULL;
		}
		AST_LIST_UNLOCK(&dial->channels);
	}

	return dial->state;
}

/*! \brief Dial async thread function */
static void *async_dial(void *data)
{
	struct ast_dial *dial = data;

	/* This is really really simple... we basically pass monitor_dial a NULL owner and it changes it's behavior */
	monitor_dial(dial, NULL);

	return NULL;
}

/*! \brief Execute dialing synchronously or asynchronously
 * \note Dials channels in a dial structure.
 * \return Returns dial result code. (TRYING/INVALID/FAILED/ANSWERED/TIMEOUT/UNANSWERED).
 */
enum ast_dial_result ast_dial_run(struct ast_dial *dial, struct ast_channel *chan, int async)
{
	enum ast_dial_result res = AST_DIAL_RESULT_TRYING;

	/* Ensure required arguments are passed */
	if (!dial || (!chan && !async)) {
		ast_log(LOG_DEBUG, "invalid #1\n");
		return AST_DIAL_RESULT_INVALID;
	}

	/* If there are no channels to dial we can't very well try to dial them */
	if (AST_LIST_EMPTY(&dial->channels)) {
		ast_log(LOG_DEBUG, "invalid #2\n");
		return AST_DIAL_RESULT_INVALID;
	}

	/* Dial each requested channel */
	if (!begin_dial(dial, chan))
		return AST_DIAL_RESULT_FAILED;

	/* If we are running async spawn a thread and send it away... otherwise block here */
	if (async) {
		dial->state = AST_DIAL_RESULT_TRYING;
		/* Try to create a thread */
		if (ast_pthread_create(&dial->thread, NULL, async_dial, dial)) {
			/* Failed to create the thread - hangup all dialed channels and return failed */
			ast_dial_hangup(dial);
			res = AST_DIAL_RESULT_FAILED;
		}
	} else {
		res = monitor_dial(dial, chan);
	}

	return res;
}

/*! \brief Return channel that answered
 * \note Returns the Asterisk channel that answered
 * \param dial Dialing structure
 */
struct ast_channel *ast_dial_answered(struct ast_dial *dial)
{
	if (!dial)
		return NULL;

	return ((dial->state == AST_DIAL_RESULT_ANSWERED) ? AST_LIST_FIRST(&dial->channels)->owner : NULL);
}

/*! \brief Return state of dial
 * \note Returns the state of the dial attempt
 * \param dial Dialing structure
 */
enum ast_dial_result ast_dial_state(struct ast_dial *dial)
{
	return dial->state;
}

/*! \brief Cancel async thread
 * \note Cancel a running async thread
 * \param dial Dialing structure
 */
enum ast_dial_result ast_dial_join(struct ast_dial *dial)
{
	pthread_t thread;

	/* If the dial structure is not running in async, return failed */
	if (dial->thread == AST_PTHREADT_NULL)
		return AST_DIAL_RESULT_FAILED;

	/* Record thread */
	thread = dial->thread;

	/* Boom, commence locking */
	ast_mutex_lock(&dial->lock);

	/* Stop the thread */
	dial->thread = AST_PTHREADT_STOP;

	/* If the answered channel is running an application we have to soft hangup it, can't just poke the thread */
	AST_LIST_LOCK(&dial->channels);
	if (AST_LIST_FIRST(&dial->channels)->is_running_app) {
		struct ast_channel *chan = AST_LIST_FIRST(&dial->channels)->owner;
		if (chan) {
			ast_channel_lock(chan);
			ast_softhangup(chan, AST_SOFTHANGUP_EXPLICIT);
			ast_channel_unlock(chan);
		}
	} else {
		/* Now we signal it with SIGURG so it will break out of it's waitfor */
		pthread_kill(thread, SIGURG);
	}
	AST_LIST_UNLOCK(&dial->channels);

	/* Yay done with it */
	ast_mutex_unlock(&dial->lock);

	/* Finally wait for the thread to exit */
	pthread_join(thread, NULL);

	/* Yay thread is all gone */
	dial->thread = AST_PTHREADT_NULL;

	return dial->state;
}

/*! \brief Hangup channels
 * \note Hangup all active channels
 * \param dial Dialing structure
 */
void ast_dial_hangup(struct ast_dial *dial)
{
	struct ast_dial_channel *channel = NULL;

	if (!dial)
		return;
	
	AST_LIST_LOCK(&dial->channels);
	AST_LIST_TRAVERSE(&dial->channels, channel, list) {
		if (channel->owner) {
			ast_hangup(channel->owner);
			channel->owner = NULL;
		}
	}
	AST_LIST_UNLOCK(&dial->channels);

	return;
}

/*! \brief Destroys a dialing structure
 * \note Destroys (free's) the given ast_dial structure
 * \param dial Dialing structure to free
 * \return Returns 0 on success, -1 on failure
 */
int ast_dial_destroy(struct ast_dial *dial)
{
	int i = 0;
	struct ast_dial_channel *channel = NULL;

	if (!dial)
		return -1;
	
	/* Hangup and deallocate all the dialed channels */
	AST_LIST_LOCK(&dial->channels);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&dial->channels, channel, list) {
		/* Disable any enabled options */
		for (i = 0; i < AST_DIAL_OPTION_MAX; i++) {
			if (!channel->options[i])
				continue;
			if (option_types[i].disable)
				option_types[i].disable(channel->options[i]);
			channel->options[i] = NULL;
		}
		/* Hang up channel if need be */
		if (channel->owner) {
			ast_hangup(channel->owner);
			channel->owner = NULL;
		}
		/* Free structure */
		AST_LIST_REMOVE_CURRENT(&dial->channels, list);
		free(channel);
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&dial->channels);
       
	/* Disable any enabled options globally */
	for (i = 0; i < AST_DIAL_OPTION_MAX; i++) {
		if (!dial->options[i])
			continue;
		if (option_types[i].disable)
			option_types[i].disable(dial->options[i]);
		dial->options[i] = NULL;
	}

	/* Lock be gone! */
	ast_mutex_destroy(&dial->lock);

	/* Free structure */
	free(dial);

	return 0;
}

/*! \brief Enables an option globally
 * \param dial Dial structure to enable option on
 * \param option Option to enable
 * \param data Data to pass to this option (not always needed)
 * \return Returns 0 on success, -1 on failure
 */
int ast_dial_option_global_enable(struct ast_dial *dial, enum ast_dial_option option, void *data)
{
	/* If the option is already enabled, return failure */
	if (dial->options[option])
		return -1;

	/* Execute enable callback if it exists, if not simply make sure the value is set */
	if (option_types[option].enable)
		dial->options[option] = option_types[option].enable(data);
	else
		dial->options[option] = (void*)1;

	return 0;
}

/*! \brief Enables an option per channel
 * \param dial Dial structure
 * \param num Channel number to enable option on
 * \param option Option to enable
 * \param data Data to pass to this option (not always needed)
 * \return Returns 0 on success, -1 on failure
 */
int ast_dial_option_enable(struct ast_dial *dial, int num, enum ast_dial_option option, void *data)
{
	struct ast_dial_channel *channel = NULL;

	/* Ensure we have required arguments */
	if (!dial || AST_LIST_EMPTY(&dial->channels))
		return -1;
	
	/* Look for channel, we can sort of cheat and predict things - the last channel in the list will probably be what they want */
	AST_LIST_LOCK(&dial->channels);
	if (AST_LIST_LAST(&dial->channels)->num != num) {
		AST_LIST_TRAVERSE(&dial->channels, channel, list) {
			if (channel->num == num)
				break;
		}
	} else {
		channel = AST_LIST_LAST(&dial->channels);
	}
	AST_LIST_UNLOCK(&dial->channels);

	/* If none found, return failure */
	if (!channel)
		return -1;

	/* If the option is already enabled, return failure */
	if (channel->options[option])
		return -1;

        /* Execute enable callback if it exists, if not simply make sure the value is set */
	if (option_types[option].enable)
		channel->options[option] = option_types[option].enable(data);
	else
		channel->options[option] = (void*)1;

	return 0;
}

/*! \brief Disables an option globally
 * \param dial Dial structure to disable option on
 * \param option Option to disable
 * \return Returns 0 on success, -1 on failure
 */
int ast_dial_option_global_disable(struct ast_dial *dial, enum ast_dial_option option)
{
        /* If the option is not enabled, return failure */
        if (!dial->options[option])
                return -1;

	/* Execute callback of option to disable if it exists */
	if (option_types[option].disable)
		option_types[option].disable(dial->options[option]);

	/* Finally disable option on the structure */
	dial->options[option] = NULL;

        return 0;
}

/*! \brief Disables an option per channel
 * \param dial Dial structure
 * \param num Channel number to disable option on
 * \param option Option to disable
 * \return Returns 0 on success, -1 on failure
 */
int ast_dial_option_disable(struct ast_dial *dial, int num, enum ast_dial_option option)
{
	struct ast_dial_channel *channel = NULL;

	/* Ensure we have required arguments */
	if (!dial || AST_LIST_EMPTY(&dial->channels))
		return -1;

	/* Look for channel, we can sort of cheat and predict things - the last channel in the list will probably be what they want */
	AST_LIST_LOCK(&dial->channels);
	if (AST_LIST_LAST(&dial->channels)->num != num) {
		AST_LIST_TRAVERSE(&dial->channels, channel, list) {
			if (channel->num == num)
				break;
		}
	} else {
		channel = AST_LIST_LAST(&dial->channels);
	}
	AST_LIST_UNLOCK(&dial->channels);

	/* If none found, return failure */
	if (!channel)
		return -1;

	/* If the option is not enabled, return failure */
	if (!channel->options[option])
		return -1;

	/* Execute callback of option to disable it if it exists */
	if (option_types[option].disable)
		option_types[option].disable(channel->options[option]);

	/* Finally disable the option on the structure */
	channel->options[option] = NULL;

	return 0;
}

void ast_dial_set_state_callback(struct ast_dial *dial, ast_dial_state_callback callback)
{
	dial->state_callback = callback;
}
