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
 * \brief Device state management
 *
 * \author Mark Spencer <markster@digium.com> 
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "asterisk/channel.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/linkedlists.h"
#include "asterisk/logger.h"
#include "asterisk/devicestate.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/options.h"

/*! \brief Device state strings for printing */
static const char *devstatestring[] = {
	/* 0 AST_DEVICE_UNKNOWN */    "Unknown",    /*!< Valid, but unknown state */
	/* 1 AST_DEVICE_NOT_INUSE */  "Not in use", /*!< Not used */
	/* 2 AST_DEVICE IN USE */     "In use",     /*!< In use */
	/* 3 AST_DEVICE_BUSY */	      "Busy",       /*!< Busy */
	/* 4 AST_DEVICE_INVALID */    "Invalid",    /*!< Invalid - not known to Asterisk */
	/* 5 AST_DEVICE_UNAVAILABLE */"Unavailable",/*!< Unavailable (not registered) */
	/* 6 AST_DEVICE_RINGING */    "Ringing",    /*!< Ring, ring, ring */
	/* 7 AST_DEVICE_RINGINUSE */  "Ring+Inuse", /*!< Ring and in use */
	/* 8 AST_DEVICE_ONHOLD */     "On Hold"     /*!< On Hold */
};

/*! \brief  A device state provider (not a channel) */
struct devstate_prov {
	char label[40];
	ast_devstate_prov_cb_type callback;
	AST_LIST_ENTRY(devstate_prov) list;
};

/*! \brief A list of providers */
static AST_LIST_HEAD_STATIC(devstate_provs, devstate_prov);

/*! \brief  A device state watcher (callback) */
struct devstate_cb {
	void *data;
	ast_devstate_cb_type callback;
	AST_LIST_ENTRY(devstate_cb) list;
};

/*! \brief A device state watcher list */
static AST_LIST_HEAD_STATIC(devstate_cbs, devstate_cb);

struct state_change {
	AST_LIST_ENTRY(state_change) list;
	char device[1];
};

/*! \brief The state change queue. State changes are queued
	for processing by a separate thread */
static AST_LIST_HEAD_STATIC(state_changes, state_change);

/*! \brief The device state change notification thread */
static pthread_t change_thread = AST_PTHREADT_NULL;

/*! \brief Flag for the queue */
static ast_cond_t change_pending;

/* Forward declarations */
static int getproviderstate(const char *provider, const char *address);

/*! \brief Find devicestate as text message for output */
const char *devstate2str(enum ast_device_state devstate) 
{
	return devstatestring[devstate];
}

/*! \brief Find out if device is active in a call or not 
	\note find channels with the device's name in it
	This function is only used for channels that does not implement 
	devicestate natively
*/
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
	
	ast_channel_unlock(chan);

	return res;
}

/*! \brief Check device state through channel specific function or generic function */
int ast_device_state(const char *device)
{
	char *buf;
	char *number;
	const struct ast_channel_tech *chan_tech;
	int res = 0;
	/*! \brief Channel driver that provides device state */
	char *tech;
	/*! \brief Another provider of device state */
	char *provider = NULL;
	
	buf = ast_strdupa(device);
	tech = strsep(&buf, "/");
	number = buf;
	if (!number) {
		provider = strsep(&tech, ":");
		if (!provider)
			return AST_DEVICE_INVALID;
		/* We have a provider */
		number = tech;
		tech = NULL;
	}

	if (provider)  {
		if(option_debug > 2)
			ast_log(LOG_DEBUG, "Checking if I can find provider for \"%s\" - number: %s\n", provider, number);
		return getproviderstate(provider, number);
	}
	if (option_debug > 3)
		ast_log(LOG_DEBUG, "No provider found, checking channel drivers for %s - %s\n", tech, number);

	chan_tech = ast_get_channel_tech(tech);
	if (!chan_tech)
		return AST_DEVICE_INVALID;

	if (!chan_tech->devicestate) 	/* Does the channel driver support device state notification? */
		return ast_parse_device_state(device);	/* No, try the generic function */
	else {
		res = chan_tech->devicestate(number);	/* Ask the channel driver for device state */
		if (res == AST_DEVICE_UNKNOWN) {
			res = ast_parse_device_state(device);
			/* at this point we know the device exists, but the channel driver
			   could not give us a state; if there is no channel state available,
			   it must be 'not in use'
			*/
			if (res == AST_DEVICE_UNKNOWN)
				res = AST_DEVICE_NOT_INUSE;
			return res;
		} else
			return res;
	}
}

/*! \brief Add device state provider */
int ast_devstate_prov_add(const char *label, ast_devstate_prov_cb_type callback)
{
	struct devstate_prov *devprov;

	if (!callback || !(devprov = ast_calloc(1, sizeof(*devprov))))
		return -1;

	devprov->callback = callback;
	ast_copy_string(devprov->label, label, sizeof(devprov->label));

	AST_LIST_LOCK(&devstate_provs);
	AST_LIST_INSERT_HEAD(&devstate_provs, devprov, list);
	AST_LIST_UNLOCK(&devstate_provs);

	return 0;
}

/*! \brief Remove device state provider */
void ast_devstate_prov_del(const char *label)
{
	struct devstate_prov *devcb;

	AST_LIST_LOCK(&devstate_provs);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&devstate_provs, devcb, list) {
		if (!strcasecmp(devcb->label, label)) {
			AST_LIST_REMOVE_CURRENT(&devstate_provs, list);
			free(devcb);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&devstate_provs);
}

/*! \brief Get provider device state */
static int getproviderstate(const char *provider, const char *address)
{
	struct devstate_prov *devprov;
	int res = AST_DEVICE_INVALID;


	AST_LIST_LOCK(&devstate_provs);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&devstate_provs, devprov, list) {
		if(option_debug > 4)
			ast_log(LOG_DEBUG, "Checking provider %s with %s\n", devprov->label, provider);

		if (!strcasecmp(devprov->label, provider)) {
			res = devprov->callback(address);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&devstate_provs);
	return res;
}

/*! \brief Add device state watcher */
int ast_devstate_add(ast_devstate_cb_type callback, void *data)
{
	struct devstate_cb *devcb;

	if (!callback || !(devcb = ast_calloc(1, sizeof(*devcb))))
		return -1;

	devcb->data = data;
	devcb->callback = callback;

	AST_LIST_LOCK(&devstate_cbs);
	AST_LIST_INSERT_HEAD(&devstate_cbs, devcb, list);
	AST_LIST_UNLOCK(&devstate_cbs);

	return 0;
}

/*! \brief Remove device state watcher */
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

/*! \brief Notify callback watchers of change, and notify PBX core for hint updates
	Normally executed within a separate thread
*/
static void do_state_change(const char *device)
{
	int state;
	struct devstate_cb *devcb;

	state = ast_device_state(device);
	if (option_debug > 2)
		ast_log(LOG_DEBUG, "Changing state for %s - state %d (%s)\n", device, state, devstate2str(state));

	AST_LIST_LOCK(&devstate_cbs);
	AST_LIST_TRAVERSE(&devstate_cbs, devcb, list)
		devcb->callback(device, state, devcb->data);
	AST_LIST_UNLOCK(&devstate_cbs);

	ast_hint_state_changed(device);
}

int ast_device_state_changed_literal(const char *device)
{
	struct state_change *change;

	if (option_debug > 2)
		ast_log(LOG_DEBUG, "Notification of state change to be queued on device/channel %s\n", device);

	if (change_thread == AST_PTHREADT_NULL || !(change = ast_calloc(1, sizeof(*change) + strlen(device)))) {
		/* we could not allocate a change struct, or */
		/* there is no background thread, so process the change now */
		do_state_change(device);
	} else {
		/* queue the change */
		strcpy(change->device, device);
		AST_LIST_LOCK(&state_changes);
		AST_LIST_INSERT_TAIL(&state_changes, change, list);
		if (AST_LIST_FIRST(&state_changes) == change)
			/* the list was empty, signal the thread */
			ast_cond_signal(&change_pending);
		AST_LIST_UNLOCK(&state_changes);
	}

	return 1;
}

/*! \brief Accept change notification, add it to change queue */
int ast_device_state_changed(const char *fmt, ...) 
{
	char buf[AST_MAX_EXTENSION];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return ast_device_state_changed_literal(buf);
}

/*! \brief Go through the dev state change queue and update changes in the dev state thread */
static void *do_devstate_changes(void *data)
{
	struct state_change *cur;

	AST_LIST_LOCK(&state_changes);
	for(;;) {
		/* the list lock will _always_ be held at this point in the loop */
		cur = AST_LIST_REMOVE_HEAD(&state_changes, list);
		if (cur) {
			/* we got an entry, so unlock the list while we process it */
			AST_LIST_UNLOCK(&state_changes);
			do_state_change(cur->device);
			free(cur);
			AST_LIST_LOCK(&state_changes);
		} else {
			/* there was no entry, so atomically unlock the list and wait for
			   the condition to be signalled (returns with the lock held) */
			ast_cond_wait(&change_pending, &state_changes.lock);
		}
	}

	return NULL;
}

/*! \brief Initialize the device state engine in separate thread */
int ast_device_state_engine_init(void)
{
	ast_cond_init(&change_pending, NULL);
	if (ast_pthread_create_background(&change_thread, NULL, do_devstate_changes, NULL) < 0) {
		ast_log(LOG_ERROR, "Unable to start device state change thread.\n");
		return -1;
	}

	return 0;
}

void ast_devstate_aggregate_init(struct ast_devstate_aggregate *agg)
{
	memset(agg, 0, sizeof(*agg));
	agg->all_unknown = 1;
	agg->all_unavail = 1;
	agg->all_busy = 1;
	agg->all_free = 1;
}

void ast_devstate_aggregate_add(struct ast_devstate_aggregate *agg, enum ast_device_state state)
{
	switch (state) {
	case AST_DEVICE_NOT_INUSE:
		agg->all_unknown = 0;
		agg->all_unavail = 0;
		agg->all_busy = 0;
		break;
	case AST_DEVICE_INUSE:
		agg->in_use = 1;
		agg->all_unavail = 0;
		agg->all_free = 0;
		agg->all_unknown = 0;
		break;
	case AST_DEVICE_RINGING:
		agg->ring = 1;
		agg->all_unavail = 0;
		agg->all_free = 0;
		agg->all_unknown = 0;
		break;
	case AST_DEVICE_RINGINUSE:
		agg->in_use = 1;
		agg->ring = 1;
		agg->all_unavail = 0;
		agg->all_free = 0;
		agg->all_unknown = 0;
		break;
	case AST_DEVICE_ONHOLD:
		agg->all_unknown = 0;
		agg->all_unavail = 0;
		agg->all_free = 0;
		agg->on_hold = 1;
		break;
	case AST_DEVICE_BUSY:
		agg->all_unknown = 0;
		agg->all_unavail = 0;
		agg->all_free = 0;
		agg->busy = 1;
		agg->in_use = 1;
		break;
	case AST_DEVICE_UNAVAILABLE:
		agg->all_unknown = 0;
	case AST_DEVICE_INVALID:
		agg->all_busy = 0;
		agg->all_free = 0;
		break;
	case AST_DEVICE_UNKNOWN:
		agg->all_busy = 0;
		agg->all_free = 0;
		break;
	case AST_DEVICE_TOTAL: /* not a device state, included for completeness. */
		break;
	}
}

enum ast_device_state ast_devstate_aggregate_result(struct ast_devstate_aggregate *agg)
{
	if (agg->all_free)
		return AST_DEVICE_NOT_INUSE;
	if ((agg->in_use || agg->on_hold) && agg->ring)
		return AST_DEVICE_RINGINUSE;
	if (agg->ring)
		return AST_DEVICE_RINGING;
	if (agg->busy)
		return AST_DEVICE_BUSY;
	if (agg->in_use)
		return AST_DEVICE_INUSE;
	if (agg->on_hold)
		return AST_DEVICE_ONHOLD;
	if (agg->all_busy)
		return AST_DEVICE_BUSY;
	if (agg->all_unknown)
		return AST_DEVICE_UNKNOWN;
	if (agg->all_unavail)
		return AST_DEVICE_UNAVAILABLE;

	return AST_DEVICE_NOT_INUSE;
}
