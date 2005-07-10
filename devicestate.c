/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Device state management
 * 
 * Copyright (C) 2005, Digium, Inc.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/channel.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/linkedlists.h"
#include "asterisk/logger.h"
#include "asterisk/devicestate.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"

/* ast_devstate_cb: A device state watcher (callback) */
struct devstate_cb {
	void *data;
	ast_devstate_cb_type callback;
	AST_LIST_ENTRY(devstate_cb) list;
};

static AST_LIST_HEAD_STATIC(devstate_cbs, devstate_cb);

struct state_change {
	AST_LIST_ENTRY(state_change) list;
	char device[1];
};

static AST_LIST_HEAD_STATIC(state_changes, state_change);

static pthread_t change_thread = AST_PTHREADT_NULL;
AST_MUTEX_DEFINE_STATIC(change_pending_lock);
static pthread_cond_t change_pending;

int ast_parse_device_state(const char *device)
{
	struct ast_channel *chan;
	char match[AST_CHANNEL_NAME];
	int res;

	ast_copy_string(match, device, sizeof(match)-1);
	strcat(match, "-");
	chan = ast_get_channel_by_name_prefix_locked(match, strlen(match));

	if (!chan)
		return AST_DEVICE_UNKNOWN;

	if (chan->_state == AST_STATE_RINGING)
		res = AST_DEVICE_RINGING;
	else
		res = AST_DEVICE_INUSE;
	
	ast_mutex_unlock(&chan->lock);

	return res;
}

int ast_device_state(const char *device)
{
	char *buf;
	char *tech;
	char *number;
	const struct ast_channel_tech *chan_tech;
	int res = 0;
	
	buf = ast_strdupa(device);
	tech = strsep(&buf, "/");
	number = buf;
	if (!number)
	    return AST_DEVICE_INVALID;
		
	chan_tech = ast_get_channel_tech(tech);
	if (!chan_tech)
		return AST_DEVICE_INVALID;

	if (!chan_tech->devicestate) 
		return ast_parse_device_state(device);
	else {
		res = chan_tech->devicestate(number);
		if (res == AST_DEVICE_UNKNOWN)
			return ast_parse_device_state(device);
		else
			return res;
	}
}

/*--- ast_devstate_add: Add device state watcher */
int ast_devstate_add(ast_devstate_cb_type callback, void *data)
{
	struct devstate_cb *devcb;

	if (!callback)
		return -1;

	devcb = calloc(1, sizeof(*devcb));
	if (!devcb)
		return -1;

	devcb->data = data;
	devcb->callback = callback;

	AST_LIST_LOCK(&devstate_cbs);
	AST_LIST_INSERT_HEAD(&devstate_cbs, devcb, list);
	AST_LIST_UNLOCK(&devstate_cbs);

	return 0;
}

/*--- ast_devstate_del: Remove device state watcher */
void ast_devstate_del(ast_devstate_cb_type callback, void *data)
{
	struct devstate_cb *devcb;

	AST_LIST_LOCK(&devstate_cbs);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&devstate_cbs, devcb, list) {
		if ((devcb->callback == callback) && (devcb->data == data)) {
			AST_LIST_REMOVE_CURRENT(&devstate_cbs, list);
			free(devcb);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&devstate_cbs);
}

static void do_state_change(const char *device)
{
	int state;
	struct devstate_cb *devcb;

	state = ast_device_state(device);
	if (option_debug > 2)
		ast_log(LOG_DEBUG, "Changing state for %s - state %d\n", device, state);

	AST_LIST_LOCK(&devstate_cbs);
	AST_LIST_TRAVERSE(&devstate_cbs, devcb, list)
		devcb->callback(device, state, devcb->data);
	AST_LIST_UNLOCK(&devstate_cbs);

	ast_hint_state_changed(device);
}

/*--- ast_device_state_changed: Notify callback watchers of change, and notify PBX core for hint updates */
int ast_device_state_changed(const char *fmt, ...) 
{
	char buf[AST_MAX_EXTENSION];
	char *device;
	char *parse;
	struct state_change *change = NULL;
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	parse = buf;
	device = strsep(&parse, "-");

	if (change_thread != AST_PTHREADT_NULL)
		change = calloc(1, sizeof(*change) + strlen(device));

	if (!change) {
		/* we could not allocate a change struct, or */
		/* there is no background thread, so process the change now */
		do_state_change(device);
	} else {
		/* queue the change */
		strcpy(change->device, device);
		AST_LIST_LOCK(&state_changes);
		AST_LIST_INSERT_TAIL(&state_changes, change, list);
		if (AST_LIST_FIRST(&state_changes) == change) {
			AST_LIST_UNLOCK(&state_changes);
			/* the list was empty, signal the thread */
			ast_mutex_lock(&change_pending_lock);
			pthread_cond_signal(&change_pending);
			ast_mutex_unlock(&change_pending_lock);
		} else {
			AST_LIST_UNLOCK(&state_changes);
		}
	}

	return 1;
}

static void *do_changes(void *data)
{
	struct state_change *cur;

	for(;;) {
		ast_mutex_lock(&change_pending_lock);
		pthread_cond_wait(&change_pending, &change_pending_lock);
		for (;;) {
			AST_LIST_LOCK(&state_changes);
			cur = AST_LIST_REMOVE_HEAD(&state_changes, list);
			AST_LIST_UNLOCK(&state_changes);
			if (!cur)
				break;

			do_state_change(cur->device);
			free(cur);
		}
		ast_mutex_unlock(&change_pending_lock);
	}

	return NULL;
}

int ast_device_state_engine_init(void)
{
	pthread_attr_t attr;

	pthread_cond_init(&change_pending, NULL);
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (ast_pthread_create(&change_thread, &attr, do_changes, NULL) < 0) {
		ast_log(LOG_ERROR, "Unable to start device state change thread.\n");
		return -1;
	}

	return 0;
}
