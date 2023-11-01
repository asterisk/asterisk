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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <sys/time.h>
#include <signal.h>

#include "asterisk/channel.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/linkedlists.h"
#include "asterisk/dial.h"
#include "asterisk/pbx.h"
#include "asterisk/musiconhold.h"
#include "asterisk/app.h"
#include "asterisk/causes.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/max_forwards.h"

/*! \brief Main dialing structure. Contains global options, channels being dialed, and more! */
struct ast_dial {
	int num;                                           /*!< Current number to give to next dialed channel */
	int timeout;                                       /*!< Maximum time allowed for dial attempts */
	int actual_timeout;                                /*!< Actual timeout based on all factors (ie: channels) */
	enum ast_dial_result state;                        /*!< Status of dial */
	void *options[AST_DIAL_OPTION_MAX];                /*!< Global options */
	ast_dial_state_callback state_callback;            /*!< Status callback */
	void *user_data;                                   /*!< Attached user data */
	AST_LIST_HEAD(, ast_dial_channel) channels; /*!< Channels being dialed */
	pthread_t thread;                                  /*!< Thread (if running in async) */
	ast_callid callid;                                 /*!< callid (if running in async) */
	ast_mutex_t lock;                                  /*! Lock to protect the thread information above */
};

/*! \brief Dialing channel structure. Contains per-channel dialing options, asterisk channel, and more! */
struct ast_dial_channel {
	int num;				/*!< Unique number for dialed channel */
	int timeout;				/*!< Maximum time allowed for attempt */
	char *tech;				/*!< Technology being dialed */
	char *device;				/*!< Device being dialed */
	void *options[AST_DIAL_OPTION_MAX];	/*!< Channel specific options */
	int cause;				/*!< Cause code in case of failure */
	unsigned int is_running_app:1;		/*!< Is this running an application? */
	char *assignedid1;				/*!< UniqueID to assign channel */
	char *assignedid2;				/*!< UniqueID to assign 2nd channel */
	struct ast_channel *owner;		/*!< Asterisk channel */
	AST_LIST_ENTRY(ast_dial_channel) list;	/*!< Linked list information */
};

/*! \brief Typedef for dial option enable */
typedef void *(*ast_dial_option_cb_enable)(void *data);

/*! \brief Typedef for dial option disable */
typedef int (*ast_dial_option_cb_disable)(void *data);

/*! \brief Structure for 'ANSWER_EXEC' option */
struct answer_exec_struct {
	char app[AST_MAX_APP]; /*!< Application name */
	char *args;            /*!< Application arguments */
};

/*! \brief Enable function for 'ANSWER_EXEC' option */
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
	if ((args = strchr(app, ','))) {
		*args++ = '\0';
		answer_exec->args = ast_strdup(args);
	}

	/* Copy application name */
	ast_copy_string(answer_exec->app, app, sizeof(answer_exec->app));

	return answer_exec;
}

/*! \brief Disable function for 'ANSWER_EXEC' option */
static int answer_exec_disable(void *data)
{
	struct answer_exec_struct *answer_exec = data;

	/* Make sure we have a value */
	if (!answer_exec)
		return -1;

	/* If arguments are present, free them too */
	if (answer_exec->args)
		ast_free(answer_exec->args);

	/* This is simple - just free the structure */
	ast_free(answer_exec);

	return 0;
}

static void *music_enable(void *data)
{
	return ast_strdup(data);
}

static int music_disable(void *data)
{
	if (!data)
		return -1;

	ast_free(data);

	return 0;
}

static void *predial_enable(void *data)
{
	return ast_strdup(data);
}

static int predial_disable(void *data)
{
	if (!data) {
		return -1;
	}

	ast_free(data);

	return 0;
}

/*! \brief Application execution function for 'ANSWER_EXEC' option */
static void answer_exec_run(struct ast_dial *dial, struct ast_dial_channel *dial_channel, char *app, char *args)
{
	struct ast_channel *chan = dial_channel->owner;

	/* Execute the application, if available */
	if (ast_pbx_exec_application(chan, app, args)) {
		/* If the application was not found, return immediately */
		return;
	}

	/* If another thread is not taking over hang up the channel */
	ast_mutex_lock(&dial->lock);
	if (dial->thread != AST_PTHREADT_STOP) {
		ast_hangup(chan);
		dial_channel->owner = NULL;
	}
	ast_mutex_unlock(&dial->lock);

	return;
}

struct ast_option_types {
	enum ast_dial_option option;
	ast_dial_option_cb_enable enable;
	ast_dial_option_cb_disable disable;
};

/*!
 * \brief Map options to respective handlers (enable/disable).
 *
 * \note This list MUST be perfectly kept in order with enum
 * ast_dial_option, or else madness will happen.
 */
static const struct ast_option_types option_types[] = {
	{ AST_DIAL_OPTION_RINGING, NULL, NULL },                                  /*!< Always indicate ringing to caller */
	{ AST_DIAL_OPTION_ANSWER_EXEC, answer_exec_enable, answer_exec_disable }, /*!< Execute application upon answer in async mode */
	{ AST_DIAL_OPTION_MUSIC, music_enable, music_disable },                   /*!< Play music to the caller instead of ringing */
	{ AST_DIAL_OPTION_DISABLE_CALL_FORWARDING, NULL, NULL },                  /*!< Disable call forwarding on channels */
	{ AST_DIAL_OPTION_PREDIAL, predial_enable, predial_disable },             /*!< Execute a subroutine on the outbound channels prior to dialing */
	{ AST_DIAL_OPTION_DIAL_REPLACES_SELF, NULL, NULL },                       /*!< The dial operation is a replacement for the requester */
	{ AST_DIAL_OPTION_SELF_DESTROY, NULL, NULL},                              /*!< Destroy self at end of ast_dial_run */
	{ AST_DIAL_OPTION_MAX, NULL, NULL },                                      /*!< Terminator of list */
};

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

	/* No timeout exists... yet */
	dial->timeout = -1;
	dial->actual_timeout = -1;

	/* Can't forget about the lock */
	ast_mutex_init(&dial->lock);

	return dial;
}

static int dial_append_common(struct ast_dial *dial, struct ast_dial_channel *channel,
		const char *tech, const char *device, const struct ast_assigned_ids *assignedids)
{
	/* Record technology and device for when we actually dial */
	channel->tech = ast_strdup(tech);
	channel->device = ast_strdup(device);

	/* Store the assigned id */
	if (assignedids && !ast_strlen_zero(assignedids->uniqueid)) {
		channel->assignedid1 = ast_strdup(assignedids->uniqueid);

		if (!ast_strlen_zero(assignedids->uniqueid2)) {
			channel->assignedid2 = ast_strdup(assignedids->uniqueid2);
		}
	}

	/* Grab reference number from dial structure */
	channel->num = ast_atomic_fetchadd_int(&dial->num, +1);

	/* No timeout exists... yet */
	channel->timeout = -1;

	/* Insert into channels list */
	AST_LIST_INSERT_TAIL(&dial->channels, channel, list);

	return channel->num;

}

/*! \brief Append a channel
 * \note Appends a channel to a dialing structure
 * \return Returns channel reference number on success, -1 on failure
 */
int ast_dial_append(struct ast_dial *dial, const char *tech, const char *device, const struct ast_assigned_ids *assignedids)
{
	struct ast_dial_channel *channel = NULL;

	/* Make sure we have required arguments */
	if (!dial || !tech || !device)
		return -1;

	/* Allocate new memory for dialed channel structure */
	if (!(channel = ast_calloc(1, sizeof(*channel))))
		return -1;

	return dial_append_common(dial, channel, tech, device, assignedids);
}

int ast_dial_append_channel(struct ast_dial *dial, struct ast_channel *chan)
{
	struct ast_dial_channel *channel;
	char *tech;
	char *device;
	char *dash;

	if (!dial || !chan) {
		return -1;
	}

	channel = ast_calloc(1, sizeof(*channel));
	if (!channel) {
		return -1;
	}
	channel->owner = chan;

	tech = ast_strdupa(ast_channel_name(chan));

	device = strchr(tech, '/');
	if (!device) {
		ast_free(channel);
		return -1;
	}
	*device++ = '\0';

	dash = strrchr(device, '-');
	if (dash) {
		*dash = '\0';
	}

	return dial_append_common(dial, channel, tech, device, NULL);
}

/*! \brief Helper function that requests all channels */
static int begin_dial_prerun(struct ast_dial_channel *channel, struct ast_channel *chan, struct ast_format_cap *cap, const char *predial_string)
{
	struct ast_format_cap *cap_all_audio = NULL;
	struct ast_format_cap *cap_request;
	struct ast_format_cap *requester_cap = NULL;
	struct ast_assigned_ids assignedids = {
		.uniqueid = channel->assignedid1,
		.uniqueid2 = channel->assignedid2,
	};

	if (chan) {
		int max_forwards;

		ast_channel_lock(chan);
		max_forwards = ast_max_forwards_get(chan);
		requester_cap = ao2_bump(ast_channel_nativeformats(chan));
		ast_channel_unlock(chan);

		if (max_forwards <= 0) {
			ast_log(LOG_WARNING, "Cannot dial from channel '%s'. Max forwards exceeded\n",
					ast_channel_name(chan));
		}
	}

	if (!channel->owner) {
		if (cap && ast_format_cap_count(cap)) {
			cap_request = cap;
		} else if (requester_cap) {
			cap_request = requester_cap;
		} else {
			cap_all_audio = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
			ast_format_cap_append_by_type(cap_all_audio, AST_MEDIA_TYPE_AUDIO);
			cap_request = cap_all_audio;
		}

		/* If we fail to create our owner channel bail out */
		if (!(channel->owner = ast_request(channel->tech, cap_request, &assignedids, chan, channel->device, &channel->cause))) {
			ao2_cleanup(cap_all_audio);
			return -1;
		}
		cap_request = NULL;
		ao2_cleanup(requester_cap);
		ao2_cleanup(cap_all_audio);
	}

	if (chan) {
		ast_channel_lock_both(chan, channel->owner);
	} else {
		ast_channel_lock(channel->owner);
	}

	ast_channel_stage_snapshot(channel->owner);

	ast_channel_appl_set(channel->owner, "AppDial2");
	ast_channel_data_set(channel->owner, "(Outgoing Line)");

	memset(ast_channel_whentohangup(channel->owner), 0, sizeof(*ast_channel_whentohangup(channel->owner)));

	/* Inherit everything from he who spawned this dial */
	if (chan) {
		ast_channel_inherit_variables(chan, channel->owner);
		ast_channel_datastore_inherit(chan, channel->owner);
		ast_max_forwards_decrement(channel->owner);

		/* Copy over callerid information */
		ast_party_redirecting_copy(ast_channel_redirecting(channel->owner), ast_channel_redirecting(chan));

		ast_channel_dialed(channel->owner)->transit_network_select = ast_channel_dialed(chan)->transit_network_select;

		ast_connected_line_copy_from_caller(ast_channel_connected(channel->owner), ast_channel_caller(chan));

		ast_channel_language_set(channel->owner, ast_channel_language(chan));
		if (channel->options[AST_DIAL_OPTION_DIAL_REPLACES_SELF]) {
			ast_channel_req_accountcodes(channel->owner, chan, AST_CHANNEL_REQUESTOR_REPLACEMENT);
		} else {
			ast_channel_req_accountcodes(channel->owner, chan, AST_CHANNEL_REQUESTOR_BRIDGE_PEER);
		}
		if (ast_strlen_zero(ast_channel_musicclass(channel->owner)))
			ast_channel_musicclass_set(channel->owner, ast_channel_musicclass(chan));

		ast_channel_adsicpe_set(channel->owner, ast_channel_adsicpe(chan));
		ast_channel_transfercapability_set(channel->owner, ast_channel_transfercapability(chan));
		ast_channel_unlock(chan);
	}

	ast_channel_stage_snapshot_done(channel->owner);
	ast_channel_unlock(channel->owner);

	if (!ast_strlen_zero(predial_string)) {
		if (chan) {
			ast_autoservice_start(chan);
		}
		ast_pre_call(channel->owner, predial_string);
		if (chan) {
			ast_autoservice_stop(chan);
		}
	}

	return 0;
}

int ast_dial_prerun(struct ast_dial *dial, struct ast_channel *chan, struct ast_format_cap *cap)
{
	struct ast_dial_channel *channel;
	int res = -1;
	char *predial_string = dial->options[AST_DIAL_OPTION_PREDIAL];

	AST_LIST_LOCK(&dial->channels);
	AST_LIST_TRAVERSE(&dial->channels, channel, list) {
		if ((res = begin_dial_prerun(channel, chan, cap, predial_string))) {
			break;
		}
	}
	AST_LIST_UNLOCK(&dial->channels);

	return res;
}

/*! \brief Helper function that does the beginning dialing per-appended channel */
static int begin_dial_channel(struct ast_dial_channel *channel, struct ast_channel *chan, int async, const char *predial_string, struct ast_channel *forwarder_chan)
{
	int res = 1;
	char forwarder[AST_CHANNEL_NAME];

	/* If no owner channel exists yet execute pre-run */
	if (!channel->owner && begin_dial_prerun(channel, chan, NULL, predial_string)) {
		return 0;
	}

	if (forwarder_chan) {
		ast_copy_string(forwarder, ast_channel_name(forwarder_chan), sizeof(forwarder));
		ast_channel_lock(channel->owner);
		pbx_builtin_setvar_helper(channel->owner, "FORWARDERNAME", forwarder);
		ast_channel_unlock(channel->owner);
	}

	/* Attempt to actually call this device */
	if ((res = ast_call(channel->owner, channel->device, 0))) {
		res = 0;
		ast_hangup(channel->owner);
		channel->owner = NULL;
	} else {
		ast_channel_publish_dial(async ? NULL : chan, channel->owner, channel->device, NULL);
		res = 1;
		ast_verb(3, "Called %s\n", channel->device);
	}

	return res;
}

/*! \brief Helper function that does the beginning dialing per dial structure */
static int begin_dial(struct ast_dial *dial, struct ast_channel *chan, int async)
{
	struct ast_dial_channel *channel = NULL;
	int success = 0;
	char *predial_string = dial->options[AST_DIAL_OPTION_PREDIAL];

	/* Iterate through channel list, requesting and calling each one */
	AST_LIST_LOCK(&dial->channels);
	AST_LIST_TRAVERSE(&dial->channels, channel, list) {
		success += begin_dial_channel(channel, chan, async, predial_string, NULL);
	}
	AST_LIST_UNLOCK(&dial->channels);

	/* If number of failures matches the number of channels, then this truly failed */
	return success;
}

/*! \brief Helper function to handle channels that have been call forwarded */
static int handle_call_forward(struct ast_dial *dial, struct ast_dial_channel *channel, struct ast_channel *chan)
{
	struct ast_channel *original = channel->owner;
	char *tmp = ast_strdupa(ast_channel_call_forward(channel->owner));
	char *tech = "Local", *device = tmp, *stuff;
	char *predial_string = dial->options[AST_DIAL_OPTION_PREDIAL];

	/* If call forwarding is disabled just drop the original channel and don't attempt to dial the new one */
	if (FIND_RELATIVE_OPTION(dial, channel, AST_DIAL_OPTION_DISABLE_CALL_FORWARDING)) {
		ast_hangup(original);
		channel->owner = NULL;
		return 0;
	}

	/* Figure out the new destination */
	if ((stuff = strchr(tmp, '/'))) {
		*stuff++ = '\0';
		tech = tmp;
		device = stuff;
	} else {
		const char *forward_context;
		char destination[AST_MAX_CONTEXT + AST_MAX_EXTENSION + 1];

		ast_channel_lock(original);
		forward_context = pbx_builtin_getvar_helper(original, "FORWARD_CONTEXT");
		snprintf(destination, sizeof(destination), "%s@%s", tmp, S_OR(forward_context, ast_channel_context(original)));
		ast_channel_unlock(original);
		device = ast_strdupa(destination);
	}

	/* Drop old destination information */
	ast_free(channel->tech);
	ast_free(channel->device);
	ast_free(channel->assignedid1);
	channel->assignedid1 = NULL;
	ast_free(channel->assignedid2);
	channel->assignedid2 = NULL;

	/* Update the dial channel with the new destination information */
	channel->tech = ast_strdup(tech);
	channel->device = ast_strdup(device);
	AST_LIST_UNLOCK(&dial->channels);

	/* Drop the original channel */
	channel->owner = NULL;

	/* Finally give it a go... send it out into the world */
	begin_dial_channel(channel, chan, chan ? 0 : 1, predial_string, original);

	ast_channel_publish_dial_forward(chan, original, channel->owner, NULL, "CANCEL",
		ast_channel_call_forward(original));

	ast_hangup(original);

	return 0;
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

/*! \brief Helper function that handles frames */
static void handle_frame(struct ast_dial *dial, struct ast_dial_channel *channel, struct ast_frame *fr, struct ast_channel *chan)
{
	if (fr->frametype == AST_FRAME_CONTROL) {
		switch (fr->subclass.integer) {
		case AST_CONTROL_ANSWER:
			if (chan) {
				ast_verb(3, "%s answered %s\n", ast_channel_name(channel->owner), ast_channel_name(chan));
			} else {
				ast_verb(3, "%s answered\n", ast_channel_name(channel->owner));
			}
			AST_LIST_LOCK(&dial->channels);
			AST_LIST_REMOVE(&dial->channels, channel, list);
			AST_LIST_INSERT_HEAD(&dial->channels, channel, list);
			AST_LIST_UNLOCK(&dial->channels);
			ast_channel_publish_dial(chan, channel->owner, channel->device, "ANSWER");
			set_state(dial, AST_DIAL_RESULT_ANSWERED);
			break;
		case AST_CONTROL_BUSY:
			ast_verb(3, "%s is busy\n", ast_channel_name(channel->owner));
			ast_channel_publish_dial(chan, channel->owner, channel->device, "BUSY");
			ast_hangup(channel->owner);
			channel->cause = AST_CAUSE_USER_BUSY;
			channel->owner = NULL;
			break;
		case AST_CONTROL_CONGESTION:
			ast_verb(3, "%s is circuit-busy\n", ast_channel_name(channel->owner));
			ast_channel_publish_dial(chan, channel->owner, channel->device, "CONGESTION");
			ast_hangup(channel->owner);
			channel->cause = AST_CAUSE_NORMAL_CIRCUIT_CONGESTION;
			channel->owner = NULL;
			break;
		case AST_CONTROL_INCOMPLETE:
			ast_verb(3, "%s dialed Incomplete extension %s\n", ast_channel_name(channel->owner), ast_channel_exten(channel->owner));
			if (chan) {
				ast_indicate(chan, AST_CONTROL_INCOMPLETE);
			} else {
				ast_hangup(channel->owner);
				channel->cause = AST_CAUSE_UNALLOCATED;
				channel->owner = NULL;
			}
			break;
		case AST_CONTROL_RINGING:
			ast_verb(3, "%s is ringing\n", ast_channel_name(channel->owner));
			ast_channel_publish_dial(chan, channel->owner, channel->device, "RINGING");
			if (chan && !dial->options[AST_DIAL_OPTION_MUSIC])
				ast_indicate(chan, AST_CONTROL_RINGING);
			set_state(dial, AST_DIAL_RESULT_RINGING);
			break;
		case AST_CONTROL_PROGRESS:
			ast_channel_publish_dial(chan, channel->owner, channel->device, "PROGRESS");
			if (chan) {
				ast_verb(3, "%s is making progress, passing it to %s\n", ast_channel_name(channel->owner), ast_channel_name(chan));
				ast_indicate(chan, AST_CONTROL_PROGRESS);
			} else {
				ast_verb(3, "%s is making progress\n", ast_channel_name(channel->owner));
			}
			set_state(dial, AST_DIAL_RESULT_PROGRESS);
			break;
		case AST_CONTROL_VIDUPDATE:
			if (!chan) {
				break;
			}
			ast_verb(3, "%s requested a video update, passing it to %s\n", ast_channel_name(channel->owner), ast_channel_name(chan));
			ast_indicate(chan, AST_CONTROL_VIDUPDATE);
			break;
		case AST_CONTROL_SRCUPDATE:
			if (!chan) {
				break;
			}
			ast_verb(3, "%s requested a source update, passing it to %s\n", ast_channel_name(channel->owner), ast_channel_name(chan));
			ast_indicate(chan, AST_CONTROL_SRCUPDATE);
			break;
		case AST_CONTROL_CONNECTED_LINE:
			if (!chan) {
				break;
			}
			ast_verb(3, "%s connected line has changed, passing it to %s\n", ast_channel_name(channel->owner), ast_channel_name(chan));
			if (ast_channel_connected_line_sub(channel->owner, chan, fr, 1) &&
				ast_channel_connected_line_macro(channel->owner, chan, fr, 1, 1)) {
				ast_indicate_data(chan, AST_CONTROL_CONNECTED_LINE, fr->data.ptr, fr->datalen);
			}
			break;
		case AST_CONTROL_REDIRECTING:
			if (!chan) {
				break;
			}
			ast_verb(3, "%s redirecting info has changed, passing it to %s\n", ast_channel_name(channel->owner), ast_channel_name(chan));
			if (ast_channel_redirecting_sub(channel->owner, chan, fr, 1) &&
				ast_channel_redirecting_macro(channel->owner, chan, fr, 1, 1)) {
				ast_indicate_data(chan, AST_CONTROL_REDIRECTING, fr->data.ptr, fr->datalen);
			}
			break;
		case AST_CONTROL_PROCEEDING:
			ast_channel_publish_dial(chan, channel->owner, channel->device, "PROCEEDING");
			if (chan) {
				ast_verb(3, "%s is proceeding, passing it to %s\n", ast_channel_name(channel->owner), ast_channel_name(chan));
				ast_indicate(chan, AST_CONTROL_PROCEEDING);
			} else {
				ast_verb(3, "%s is proceeding\n", ast_channel_name(channel->owner));
			}
			set_state(dial, AST_DIAL_RESULT_PROCEEDING);
			break;
		case AST_CONTROL_HOLD:
			if (!chan) {
				break;
			}
			ast_verb(3, "Call on %s placed on hold\n", ast_channel_name(chan));
			ast_indicate_data(chan, AST_CONTROL_HOLD, fr->data.ptr, fr->datalen);
			break;
		case AST_CONTROL_UNHOLD:
			if (!chan) {
				break;
			}
			ast_verb(3, "Call on %s left from hold\n", ast_channel_name(chan));
			ast_indicate(chan, AST_CONTROL_UNHOLD);
			break;
		case AST_CONTROL_OFFHOOK:
		case AST_CONTROL_FLASH:
			break;
		case AST_CONTROL_PVT_CAUSE_CODE:
			if (chan) {
				ast_indicate_data(chan, AST_CONTROL_PVT_CAUSE_CODE, fr->data.ptr, fr->datalen);
			}
			break;
		case -1:
			if (chan) {
				/* Prod the channel */
				ast_indicate(chan, -1);
			}
			break;
		default:
			break;
		}
	}
}

/*! \brief Helper function to handle when a timeout occurs on dialing attempt */
static int handle_timeout_trip(struct ast_dial *dial, struct timeval start)
{
	struct ast_dial_channel *channel = NULL;
	int diff = ast_tvdiff_ms(ast_tvnow(), start), lowest_timeout = -1, new_timeout = -1;

	/* If there is no difference yet return the dial timeout so we can go again, we were likely interrupted */
	if (!diff) {
		return dial->timeout;
	}

	/* If the global dial timeout tripped switch the state to timeout so our channel loop will drop every channel */
	if (diff >= dial->timeout) {
		set_state(dial, AST_DIAL_RESULT_TIMEOUT);
		new_timeout = 0;
	}

	/* Go through dropping out channels that have met their timeout */
	AST_LIST_TRAVERSE(&dial->channels, channel, list) {
		if (dial->state == AST_DIAL_RESULT_TIMEOUT || diff >= channel->timeout) {
			ast_hangup(channel->owner);
			channel->cause = AST_CAUSE_NO_ANSWER;
			channel->owner = NULL;
		} else if ((lowest_timeout == -1) || (lowest_timeout > channel->timeout)) {
			lowest_timeout = channel->timeout;
		}
	}

	/* Calculate the new timeout using the lowest timeout found */
	if (lowest_timeout >= 0)
		new_timeout = lowest_timeout - diff;

	return new_timeout;
}

const char *ast_hangup_cause_to_dial_status(int hangup_cause)
{
	switch(hangup_cause) {
	case AST_CAUSE_BUSY:
		return "BUSY";
	case AST_CAUSE_CONGESTION:
		return "CONGESTION";
	case AST_CAUSE_NO_ROUTE_DESTINATION:
	case AST_CAUSE_UNREGISTERED:
		return "CHANUNAVAIL";
	case AST_CAUSE_NO_ANSWER:
	default:
		return "NOANSWER";
	}
}

/*! \brief Helper function that basically keeps tabs on dialing attempts */
static enum ast_dial_result monitor_dial(struct ast_dial *dial, struct ast_channel *chan)
{
	int timeout = -1;
	struct ast_channel *cs[AST_MAX_WATCHERS], *who = NULL;
	struct ast_dial_channel *channel = NULL;
	struct answer_exec_struct *answer_exec = NULL;
	struct timeval start;

	set_state(dial, AST_DIAL_RESULT_TRYING);

	/* If the "always indicate ringing" option is set, change state to ringing and indicate to the owner if present */
	if (dial->options[AST_DIAL_OPTION_RINGING]) {
		set_state(dial, AST_DIAL_RESULT_RINGING);
		if (chan)
			ast_indicate(chan, AST_CONTROL_RINGING);
	} else if (chan && dial->options[AST_DIAL_OPTION_MUSIC] &&
		!ast_strlen_zero(dial->options[AST_DIAL_OPTION_MUSIC])) {
		char *original_moh = ast_strdupa(ast_channel_musicclass(chan));
		ast_indicate(chan, -1);
		ast_channel_musicclass_set(chan, dial->options[AST_DIAL_OPTION_MUSIC]);
		ast_moh_start(chan, dial->options[AST_DIAL_OPTION_MUSIC], NULL);
		ast_channel_musicclass_set(chan, original_moh);
	}

	/* Record start time for timeout purposes */
	start = ast_tvnow();

	/* We actually figured out the maximum timeout we can do as they were added, so we can directly access the info */
	timeout = dial->actual_timeout;

	/* Go into an infinite loop while we are trying */
	while ((dial->state != AST_DIAL_RESULT_UNANSWERED) && (dial->state != AST_DIAL_RESULT_ANSWERED) && (dial->state != AST_DIAL_RESULT_HANGUP) && (dial->state != AST_DIAL_RESULT_TIMEOUT)) {
		int pos = 0, count = 0;
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

		/* Check to see if our thread is being canceled */
		if (dial->thread == AST_PTHREADT_STOP)
			break;

		/* If the timeout no longer exists OR if we got no channel it basically means the timeout was tripped, so handle it */
		if (!timeout || !who) {
			timeout = handle_timeout_trip(dial, start);
			continue;
		}

		/* Find relative dial channel */
		if (!chan || !IS_CALLER(chan, who))
			channel = find_relative_dial_channel(dial, who);

		/* See if this channel has been forwarded elsewhere */
		if (!ast_strlen_zero(ast_channel_call_forward(who))) {
			handle_call_forward(dial, channel, chan);
			continue;
		}

		/* Attempt to read in a frame */
		if (!(fr = ast_read(who))) {
			/* If this is the caller then we switch state to hangup and stop */
			if (chan && IS_CALLER(chan, who)) {
				set_state(dial, AST_DIAL_RESULT_HANGUP);
				break;
			}
			ast_channel_publish_dial(chan, who, channel->device, ast_hangup_cause_to_dial_status(ast_channel_hangupcause(who)));
			ast_hangup(who);
			channel->owner = NULL;
			continue;
		}

		/* Process the frame */
		handle_frame(dial, channel, fr, chan);

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
			ast_channel_publish_dial(chan, channel->owner, channel->device, "CANCEL");
			ast_hangup(channel->owner);
			channel->cause = AST_CAUSE_ANSWERED_ELSEWHERE;
			channel->owner = NULL;
		}
		AST_LIST_UNLOCK(&dial->channels);
		/* If ANSWER_EXEC is enabled as an option, execute application on answered channel */
		if ((channel = find_relative_dial_channel(dial, who)) && (answer_exec = FIND_RELATIVE_OPTION(dial, channel, AST_DIAL_OPTION_ANSWER_EXEC))) {
			channel->is_running_app = 1;
			answer_exec_run(dial, channel, answer_exec->app, answer_exec->args);
			channel->is_running_app = 0;
		}

		if (chan && dial->options[AST_DIAL_OPTION_MUSIC] &&
			!ast_strlen_zero(dial->options[AST_DIAL_OPTION_MUSIC])) {
			ast_moh_stop(chan);
		}
	} else if (dial->state == AST_DIAL_RESULT_HANGUP) {
		/* Hangup everything */
		AST_LIST_LOCK(&dial->channels);
		AST_LIST_TRAVERSE(&dial->channels, channel, list) {
			if (!channel->owner)
				continue;
			ast_channel_publish_dial(chan, channel->owner, channel->device, "CANCEL");
			ast_hangup(channel->owner);
			channel->cause = AST_CAUSE_NORMAL_CLEARING;
			channel->owner = NULL;
		}
		AST_LIST_UNLOCK(&dial->channels);
	}

	if (dial->options[AST_DIAL_OPTION_SELF_DESTROY]) {
		enum ast_dial_result state = dial->state;

		ast_dial_destroy(dial);
		return state;
	}

	return dial->state;
}

/*! \brief Dial async thread function */
static void *async_dial(void *data)
{
	struct ast_dial *dial = data;
	if (dial->callid) {
		ast_callid_threadassoc_add(dial->callid);
	}

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
	if (!dial) {
		ast_debug(1, "invalid #1\n");
		return AST_DIAL_RESULT_INVALID;
	}

	/* If there are no channels to dial we can't very well try to dial them */
	if (AST_LIST_EMPTY(&dial->channels)) {
		ast_debug(1, "invalid #2\n");
		return AST_DIAL_RESULT_INVALID;
	}

	/* Dial each requested channel */
	if (!begin_dial(dial, chan, async))
		return AST_DIAL_RESULT_FAILED;

	/* If we are running async spawn a thread and send it away... otherwise block here */
	if (async) {
		/* reference be released at dial destruction if it isn't NULL */
		dial->callid = ast_read_threadstorage_callid();
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

/*! \brief Steal the channel that answered
 * \note Returns the Asterisk channel that answered and removes it from the dialing structure
 * \param dial Dialing structure
 */
struct ast_channel *ast_dial_answered_steal(struct ast_dial *dial)
{
	struct ast_channel *chan = NULL;

	if (!dial)
		return NULL;

	if (dial->state == AST_DIAL_RESULT_ANSWERED) {
		chan = AST_LIST_FIRST(&dial->channels)->owner;
		AST_LIST_FIRST(&dial->channels)->owner = NULL;
	}

	return chan;
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
		struct ast_dial_channel *channel = NULL;

		/* Now we signal it with SIGURG so it will break out of it's waitfor */
		pthread_kill(thread, SIGURG);

		/* pthread_kill may not be enough, if outgoing channel has already got an answer (no more in waitfor) but is not yet running an application. Force soft hangup. */
		AST_LIST_TRAVERSE(&dial->channels, channel, list) {
			if (channel->owner) {
				ast_softhangup(channel->owner, AST_SOFTHANGUP_EXPLICIT);
			}
		}
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
		ast_hangup(channel->owner);
		channel->owner = NULL;
	}
	AST_LIST_UNLOCK(&dial->channels);

	return;
}

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
		ast_hangup(channel->owner);
		channel->owner = NULL;

		/* Free structure */
		ast_free(channel->tech);
		ast_free(channel->device);
		ast_free(channel->assignedid1);
		ast_free(channel->assignedid2);

		AST_LIST_REMOVE_CURRENT(list);
		ast_free(channel);
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
	ast_free(dial);

	return 0;
}

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

/*! \brief Helper function for finding a channel in a dial structure based on number
 */
static struct ast_dial_channel *find_dial_channel(struct ast_dial *dial, int num)
{
	struct ast_dial_channel *channel = AST_LIST_LAST(&dial->channels);

	/* We can try to predict programmer behavior, the last channel they added is probably the one they wanted to modify */
	if (channel->num == num)
		return channel;

	/* Hrm not at the end... looking through the list it is! */
	AST_LIST_LOCK(&dial->channels);
	AST_LIST_TRAVERSE(&dial->channels, channel, list) {
		if (channel->num == num)
			break;
	}
	AST_LIST_UNLOCK(&dial->channels);

	return channel;
}

int ast_dial_option_enable(struct ast_dial *dial, int num, enum ast_dial_option option, void *data)
{
	struct ast_dial_channel *channel = NULL;

	/* Ensure we have required arguments */
	if (!dial || AST_LIST_EMPTY(&dial->channels))
		return -1;

	if (!(channel = find_dial_channel(dial, num)))
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

int ast_dial_option_global_disable(struct ast_dial *dial, enum ast_dial_option option)
{
	/* If the option is not enabled, return failure */
	if (!dial->options[option]) {
		return -1;
	}

	/* Execute callback of option to disable if it exists */
	if (option_types[option].disable)
		option_types[option].disable(dial->options[option]);

	/* Finally disable option on the structure */
	dial->options[option] = NULL;

	return 0;
}

int ast_dial_option_disable(struct ast_dial *dial, int num, enum ast_dial_option option)
{
	struct ast_dial_channel *channel = NULL;

	/* Ensure we have required arguments */
	if (!dial || AST_LIST_EMPTY(&dial->channels))
		return -1;

	if (!(channel = find_dial_channel(dial, num)))
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

int ast_dial_reason(struct ast_dial *dial, int num)
{
	struct ast_dial_channel *channel;

	if (!dial || AST_LIST_EMPTY(&dial->channels) || !(channel = find_dial_channel(dial, num))) {
		return -1;
	}

	return channel->cause;
}

struct ast_channel *ast_dial_get_channel(struct ast_dial *dial, int num)
{
	struct ast_dial_channel *channel;

	if (!dial || AST_LIST_EMPTY(&dial->channels) || !(channel = find_dial_channel(dial, num))) {
		return NULL;
	}

	return channel->owner;
}

void ast_dial_set_state_callback(struct ast_dial *dial, ast_dial_state_callback callback)
{
	dial->state_callback = callback;
}

void ast_dial_set_user_data(struct ast_dial *dial, void *user_data)
{
	dial->user_data = user_data;
}

void *ast_dial_get_user_data(struct ast_dial *dial)
{
	return dial->user_data;
}

void ast_dial_set_global_timeout(struct ast_dial *dial, int timeout)
{
	dial->timeout = timeout;

	if (dial->timeout > 0 && (dial->actual_timeout > dial->timeout || dial->actual_timeout == -1))
		dial->actual_timeout = dial->timeout;

	return;
}

void ast_dial_set_timeout(struct ast_dial *dial, int num, int timeout)
{
	struct ast_dial_channel *channel = NULL;

	if (!(channel = find_dial_channel(dial, num)))
		return;

	channel->timeout = timeout;

	if (channel->timeout > 0 && (dial->actual_timeout > channel->timeout || dial->actual_timeout == -1))
		dial->actual_timeout = channel->timeout;

	return;
}
