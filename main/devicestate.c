/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2008, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * Russell Bryant <russell@digium.com>
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
 * \author Russell Bryant <russell@digium.com>
 *
 *	\arg \ref AstExtState
 */

/*! \page AstExtState Extension and device states in Asterisk
 *
 * (Note that these descriptions of device states and extension
 * states have not been updated to the way things work
 * in Asterisk 1.6.)
 *
 *	Asterisk has an internal system that reports states
 *	for an extension. By using the dialplan priority -1,
 *	also called a \b hint, a connection can be made from an
 *	extension to one or many devices. The state of the extension
 *	now depends on the combined state of the devices.
 *
 *	The device state is basically based on the current calls.
 *	If the devicestate engine can find a call from or to the
 *	device, it's in use.
 *
 *	Some channel drivers implement a callback function for
 *	a better level of reporting device states. The SIP channel
 *	has a complicated system for this, which is improved
 *	by adding call limits to the configuration.
 *
 *	Functions that want to check the status of an extension
 *	register themself as a \b watcher.
 *	Watchers in this system can subscribe either to all extensions
 *	or just a specific extensions.
 *
 *	For non-device related states, there's an API called
 *	devicestate providers. This is an extendible system for
 *	delivering state information from outside sources or
 *	functions within Asterisk. Currently we have providers
 *	for app_meetme.c - the conference bridge - and call
 *	parking (metermaids).
 *
 *	There are manly three subscribers to extension states
 *	within Asterisk:
 *	- AMI, the manager interface
 *	- app_queue.c - the Queue dialplan application
 *	- SIP subscriptions, a.k.a. "blinking lamps" or
 *	  "buddy lists"
 *
 *	The CLI command "show hints" show last known state
 *
 *	\note None of these handle user states, like an IM presence
 *	system. res_xmpp.c can subscribe and watch such states
 *	in jabber/xmpp based systems.
 *
 *	\section AstDevStateArch Architecture for devicestates
 *
 *	When a channel driver or asterisk app changes state for
 *	a watched object, it alerts the core. The core queues
 *	a change. When the change is processed, there's a query
 *	sent to the channel driver/provider if there's a function
 *	to handle that, otherwise a channel walk is issued to find
 *	a channel that involves the object.
 *
 *	The changes are queued and processed by a separate thread.
 *	This thread calls the watchers subscribing to status
 *	changes for the object. For manager, this results
 *	in events. For SIP, NOTIFY requests.
 *
 *	- Device states
 *		\arg \ref devicestate.c
 *		\arg \ref devicestate.h
 *
 *	\section AstExtStateArch Architecture for extension states
 *
 *	Hints are connected to extension. If an extension changes state
 *	it checks the hint devices. If there is a hint, the callbacks into
 *	device states are checked. The aggregated state is set for the hint
 *	and reported back.
 *
 *	- Extension states
 *		\arg \ref AstENUM ast_extension_states
 *		\arg \ref pbx.c
 *		\arg \ref pbx.h
 *	- Structures
 *		- \ref ast_state_cb struct.  Callbacks for watchers
 *		- Callback ast_state_cb_type
 *		- \ref ast_hint struct.
 *	- Functions
 *		- ast_extension_state_add()
 *		- ast_extension_state_del()
 *		- ast_get_hint()
 *
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

/*** DOCUMENTATION
	<managerEvent language="en_US" name="DeviceStateChange">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a device state changes</synopsis>
			<syntax>
				<parameter name="Device">
					<para>The device whose state has changed</para>
				</parameter>
				<parameter name="State">
					<para>The new state of the device</para>
				</parameter>
			</syntax>
			<description>
				<para>This differs from the <literal>ExtensionStatus</literal>
				event because this event is raised for all device state changes,
				not only for changes that affect dialplan hints.</para>
			</description>
			<see-also>
				<ref type="managerEvent">ExtensionStatus</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
***/

#include "asterisk.h"

#include "asterisk/_private.h"
#include "asterisk/channel.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/linkedlists.h"
#include "asterisk/devicestate.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/astobj2.h"
#include "asterisk/stasis.h"
#include "asterisk/devicestate.h"

#define DEVSTATE_TOPIC_BUCKETS 57

/*! \brief Device state strings for printing */
static const char * const devstatestring[][2] = {
	{ /* 0 AST_DEVICE_UNKNOWN */     "Unknown",     "UNKNOWN"     }, /*!< Valid, but unknown state */
	{ /* 1 AST_DEVICE_NOT_INUSE */   "Not in use",  "NOT_INUSE"   }, /*!< Not used */
	{ /* 2 AST_DEVICE IN USE */      "In use",      "INUSE"       }, /*!< In use */
	{ /* 3 AST_DEVICE_BUSY */        "Busy",        "BUSY"        }, /*!< Busy */
	{ /* 4 AST_DEVICE_INVALID */     "Invalid",     "INVALID"     }, /*!< Invalid - not known to Asterisk */
	{ /* 5 AST_DEVICE_UNAVAILABLE */ "Unavailable", "UNAVAILABLE" }, /*!< Unavailable (not registered) */
	{ /* 6 AST_DEVICE_RINGING */     "Ringing",     "RINGING"     }, /*!< Ring, ring, ring */
	{ /* 7 AST_DEVICE_RINGINUSE */   "Ring+Inuse",  "RINGINUSE"   }, /*!< Ring and in use */
	{ /* 8 AST_DEVICE_ONHOLD */      "On Hold",     "ONHOLD"      }, /*!< On Hold */
};

/*!\brief Mapping for channel states to device states */
static const struct chan2dev {
	enum ast_channel_state chan;
	enum ast_device_state dev;
} chan2dev[] = {
	{ AST_STATE_DOWN,            AST_DEVICE_NOT_INUSE },
	{ AST_STATE_RESERVED,        AST_DEVICE_INUSE },
	{ AST_STATE_OFFHOOK,         AST_DEVICE_INUSE },
	{ AST_STATE_DIALING,         AST_DEVICE_INUSE },
	{ AST_STATE_RING,            AST_DEVICE_INUSE },
	{ AST_STATE_RINGING,         AST_DEVICE_RINGING },
	{ AST_STATE_UP,              AST_DEVICE_INUSE },
	{ AST_STATE_BUSY,            AST_DEVICE_BUSY },
	{ AST_STATE_DIALING_OFFHOOK, AST_DEVICE_INUSE },
	{ AST_STATE_PRERING,         AST_DEVICE_RINGING },
};

/*! \brief  A device state provider (not a channel) */
struct devstate_prov {
	char label[40];
	ast_devstate_prov_cb_type callback;
	AST_RWLIST_ENTRY(devstate_prov) list;
};

/*! \brief A list of providers */
static AST_RWLIST_HEAD_STATIC(devstate_provs, devstate_prov);

struct state_change {
	AST_LIST_ENTRY(state_change) list;
	enum ast_devstate_cache cachable;
	char device[1];
};

/*! \brief The state change queue. State changes are queued
	for processing by a separate thread */
static AST_LIST_HEAD_STATIC(state_changes, state_change);

/*! \brief The device state change notification thread */
static pthread_t change_thread = AST_PTHREADT_NULL;

/*! \brief Flag for the queue */
static ast_cond_t change_pending;
static volatile int shuttingdown;

struct stasis_subscription *devstate_message_sub;

static struct stasis_topic *device_state_topic_all;
static struct stasis_cache *device_state_cache;
static struct stasis_caching_topic *device_state_topic_cached;
static struct stasis_topic_pool *device_state_topic_pool;

static struct ast_manager_event_blob *devstate_to_ami(struct stasis_message *msg);
static struct ast_event *devstate_to_event(struct stasis_message *msg);


STASIS_MESSAGE_TYPE_DEFN(ast_device_state_message_type,
	.to_ami = devstate_to_ami,
	.to_event = devstate_to_event,
);

/* Forward declarations */
static int getproviderstate(const char *provider, const char *address);

/*! \brief Find devicestate as text message for output */
const char *ast_devstate2str(enum ast_device_state devstate)
{
	return devstatestring[devstate][0];
}

enum ast_device_state ast_state_chan2dev(enum ast_channel_state chanstate)
{
	int i;
	chanstate &= 0xFFFF;
	for (i = 0; i < ARRAY_LEN(chan2dev); i++) {
		if (chan2dev[i].chan == chanstate) {
			return chan2dev[i].dev;
		}
	}
	return AST_DEVICE_UNKNOWN;
}

/* Parseable */
const char *ast_devstate_str(enum ast_device_state state)
{
	return devstatestring[state][1];
}

enum ast_device_state ast_devstate_val(const char *val)
{
	if (!strcasecmp(val, "NOT_INUSE"))
		return AST_DEVICE_NOT_INUSE;
	else if (!strcasecmp(val, "INUSE"))
		return AST_DEVICE_INUSE;
	else if (!strcasecmp(val, "BUSY"))
		return AST_DEVICE_BUSY;
	else if (!strcasecmp(val, "INVALID"))
		return AST_DEVICE_INVALID;
	else if (!strcasecmp(val, "UNAVAILABLE"))
		return AST_DEVICE_UNAVAILABLE;
	else if (!strcasecmp(val, "RINGING"))
		return AST_DEVICE_RINGING;
	else if (!strcasecmp(val, "RINGINUSE"))
		return AST_DEVICE_RINGINUSE;
	else if (!strcasecmp(val, "ONHOLD"))
		return AST_DEVICE_ONHOLD;

	return AST_DEVICE_UNKNOWN;
}

/*! \brief Find out if device is active in a call or not
	\note find channels with the device's name in it
	This function is only used for channels that does not implement
	devicestate natively
*/
enum ast_device_state ast_parse_device_state(const char *device)
{
	struct ast_channel *chan;
	char match[AST_CHANNEL_NAME];
	enum ast_device_state res;

	snprintf(match, sizeof(match), "%s-", device);

	if (!(chan = ast_channel_get_by_name_prefix(match, strlen(match)))) {
		return AST_DEVICE_UNKNOWN;
	}

	if (ast_channel_hold_state(chan) == AST_CONTROL_HOLD) {
		res = AST_DEVICE_ONHOLD;
	} else {
		res = ast_state_chan2dev(ast_channel_state(chan));
	}
	ast_channel_unref(chan);

	return res;
}

static enum ast_device_state devstate_cached(const char *device)
{
	struct stasis_message *cached_msg;
	struct ast_device_state_message *device_state;
	enum ast_device_state state;

	cached_msg = stasis_cache_get_by_eid(ast_device_state_cache(),
		ast_device_state_message_type(), device, NULL);
	if (!cached_msg) {
		return AST_DEVICE_UNKNOWN;
	}
	device_state = stasis_message_data(cached_msg);
	state = device_state->state;
	ao2_cleanup(cached_msg);

	return state;
}

/*! \brief Check device state through channel specific function or generic function */
static enum ast_device_state _ast_device_state(const char *device, int check_cache)
{
	char *number;
	const struct ast_channel_tech *chan_tech;
	enum ast_device_state res;
	/*! \brief Channel driver that provides device state */
	char *tech;

	/* If the last known state is cached, just return that */
	if (check_cache) {
		res = devstate_cached(device);
		if (res != AST_DEVICE_UNKNOWN) {
			return res;
		}
	}

	number = ast_strdupa(device);
	tech = strsep(&number, "/");
	if (!number) {
		/*! \brief Another provider of device state */
		char *provider;

		provider = strsep(&tech, ":");
		if (!tech) {
			return AST_DEVICE_INVALID;
		}
		/* We have a provider */
		number = tech;

		ast_debug(3, "Checking if I can find provider for \"%s\" - number: %s\n", provider, number);
		return getproviderstate(provider, number);
	}

	ast_debug(4, "No provider found, checking channel drivers for %s - %s\n", tech, number);

	chan_tech = ast_get_channel_tech(tech);
	if (!chan_tech) {
		return AST_DEVICE_INVALID;
	}

	/* Does the channel driver support device state notification? */
	if (!chan_tech->devicestate) {
		/* No, try the generic function */
		return ast_parse_device_state(device);
	}

	res = chan_tech->devicestate(number);
	if (res == AST_DEVICE_UNKNOWN) {
		res = ast_parse_device_state(device);
	}

	return res;
}

enum ast_device_state ast_device_state(const char *device)
{
	/* This function is called from elsewhere in the code to find out the
	 * current state of a device.  Check the cache, first. */

	return _ast_device_state(device, 1);
}

/*! \brief Add device state provider */
int ast_devstate_prov_add(const char *label, ast_devstate_prov_cb_type callback)
{
	struct devstate_prov *devcb;
	struct devstate_prov *devprov;

	if (!callback || !(devprov = ast_calloc(1, sizeof(*devprov))))
		return -1;

	devprov->callback = callback;
	ast_copy_string(devprov->label, label, sizeof(devprov->label));

	AST_RWLIST_WRLOCK(&devstate_provs);
	AST_RWLIST_TRAVERSE(&devstate_provs, devcb, list) {
		if (!strcasecmp(devcb->label, label)) {
			ast_log(LOG_WARNING, "Device state provider '%s' already registered\n", label);
			ast_free(devprov);
			AST_RWLIST_UNLOCK(&devstate_provs);
			return -1;
		}
	}
	AST_RWLIST_INSERT_HEAD(&devstate_provs, devprov, list);
	AST_RWLIST_UNLOCK(&devstate_provs);

	return 0;
}

/*! \brief Remove device state provider */
int ast_devstate_prov_del(const char *label)
{
	struct devstate_prov *devcb;
	int res = -1;

	AST_RWLIST_WRLOCK(&devstate_provs);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&devstate_provs, devcb, list) {
		if (!strcasecmp(devcb->label, label)) {
			AST_RWLIST_REMOVE_CURRENT(list);
			ast_free(devcb);
			res = 0;
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	AST_RWLIST_UNLOCK(&devstate_provs);

	return res;
}

/*! \brief Get provider device state */
static int getproviderstate(const char *provider, const char *address)
{
	struct devstate_prov *devprov;
	int res = AST_DEVICE_INVALID;

	AST_RWLIST_RDLOCK(&devstate_provs);
	AST_RWLIST_TRAVERSE(&devstate_provs, devprov, list) {
		ast_debug(5, "Checking provider %s with %s\n", devprov->label, provider);

		if (!strcasecmp(devprov->label, provider)) {
			res = devprov->callback(address);
			break;
		}
	}
	AST_RWLIST_UNLOCK(&devstate_provs);

	return res;
}

/*! Called by the state change thread to find out what the state is, and then
 *  to queue up the state change event */
static void do_state_change(const char *device, enum ast_devstate_cache cachable)
{
	enum ast_device_state state;

	state = _ast_device_state(device, 0);

	ast_debug(3, "Changing state for %s - state %u (%s)\n", device, state, ast_devstate2str(state));

	ast_publish_device_state(device, state, cachable);
}

int ast_devstate_changed_literal(enum ast_device_state state, enum ast_devstate_cache cachable, const char *device)
{
	struct state_change *change;

	/*
	 * If we know the state change (how nice of the caller of this function!)
	 * then we can just generate a device state event.
	 *
	 * Otherwise, we do the following:
	 *   - Queue an event up to another thread that the state has changed
	 *   - In the processing thread, it calls the callback provided by the
	 *     device state provider (which may or may not be a channel driver)
	 *     to determine the state.
	 *   - If the device state provider does not know the state, or this is
	 *     for a channel and the channel driver does not implement a device
	 *     state callback, then we will look through the channel list to
	 *     see if we can determine a state based on active calls.
	 *   - Once a state has been determined, a device state event is generated.
	 */

	if (state != AST_DEVICE_UNKNOWN) {
		ast_publish_device_state(device, state, cachable);
	} else if (change_thread == AST_PTHREADT_NULL || !(change = ast_calloc(1, sizeof(*change) + strlen(device)))) {
		/* we could not allocate a change struct, or */
		/* there is no background thread, so process the change now */
		do_state_change(device, cachable);
	} else {
		/* queue the change */
		strcpy(change->device, device);
		change->cachable = cachable;
		AST_LIST_LOCK(&state_changes);
		AST_LIST_INSERT_TAIL(&state_changes, change, list);
		ast_cond_signal(&change_pending);
		AST_LIST_UNLOCK(&state_changes);
	}

	return 0;
}

int ast_devstate_changed(enum ast_device_state state, enum ast_devstate_cache cachable, const char *fmt, ...)
{
	char buf[AST_MAX_EXTENSION];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	return ast_devstate_changed_literal(state, cachable, buf);
}

/*! \brief Go through the dev state change queue and update changes in the dev state thread */
static void *do_devstate_changes(void *data)
{
	struct state_change *next, *current;

	while (!shuttingdown) {
		/* This basically pops off any state change entries, resets the list back to NULL, unlocks, and processes each state change */
		AST_LIST_LOCK(&state_changes);
		if (AST_LIST_EMPTY(&state_changes))
			ast_cond_wait(&change_pending, &state_changes.lock);
		next = AST_LIST_FIRST(&state_changes);
		AST_LIST_HEAD_INIT_NOLOCK(&state_changes);
		AST_LIST_UNLOCK(&state_changes);

		/* Process each state change */
		while ((current = next)) {
			next = AST_LIST_NEXT(current, list);
			do_state_change(current->device, current->cachable);
			ast_free(current);
		}
	}

	return NULL;
}

static struct ast_device_state_message *device_state_alloc(const char *device, enum ast_device_state state, enum ast_devstate_cache cachable, const struct ast_eid *eid)
{
	struct ast_device_state_message *new_device_state;
	char *pos;
	size_t stuff_len;

	ast_assert(!ast_strlen_zero(device));

	stuff_len = strlen(device) + 1;
	if (eid) {
		stuff_len += sizeof(*eid);
	}
	new_device_state = ao2_alloc_options(sizeof(*new_device_state) + stuff_len, NULL,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!new_device_state) {
		return NULL;
	}

	if (eid) {
		/* non-aggregate device state. */
		new_device_state->stuff[0] = *eid;
		new_device_state->eid = &new_device_state->stuff[0];
		pos = (char *) &new_device_state->stuff[1];
	} else {
		pos = (char *) &new_device_state->stuff[0];
	}

	strcpy(pos, device);/* Safe */
	new_device_state->device = pos;

	new_device_state->state = state;
	new_device_state->cachable = cachable;

	return new_device_state;
}

static void devstate_change_cb(void *data, struct stasis_subscription *sub, struct stasis_message *msg)
{
	struct ast_device_state_message *device_state;

	if (ast_device_state_message_type() != stasis_message_type(msg)) {
		return;
	}

	device_state = stasis_message_data(msg);
	if (device_state->cachable == AST_DEVSTATE_CACHABLE || !device_state->eid) {
		/* Ignore cacheable and aggregate messages. */
		return;
	}

	/*
	 * Non-cacheable device state aggregates are just the
	 * device state republished as the aggregate.
	 */
	ast_publish_device_state_full(device_state->device, device_state->state,
		device_state->cachable, NULL);
}

static void device_state_engine_cleanup(void)
{
	shuttingdown = 1;
	AST_LIST_LOCK(&state_changes);
	ast_cond_signal(&change_pending);
	AST_LIST_UNLOCK(&state_changes);

	if (change_thread != AST_PTHREADT_NULL) {
		pthread_join(change_thread, NULL);
	}
}

/*! \brief Initialize the device state engine in separate thread */
int ast_device_state_engine_init(void)
{
	ast_cond_init(&change_pending, NULL);
	if (ast_pthread_create_background(&change_thread, NULL, do_devstate_changes, NULL) < 0) {
		ast_log(LOG_ERROR, "Unable to start device state change thread.\n");
		return -1;
	}
	ast_register_cleanup(device_state_engine_cleanup);

	return 0;
}

void ast_devstate_aggregate_init(struct ast_devstate_aggregate *agg)
{
	memset(agg, 0, sizeof(*agg));
	agg->state = AST_DEVICE_INVALID;
}

void ast_devstate_aggregate_add(struct ast_devstate_aggregate *agg, enum ast_device_state state)
{
	static enum ast_device_state state_order[] = {
		1, /* AST_DEVICE_UNKNOWN */
		3, /* AST_DEVICE_NOT_INUSE */
		6, /* AST_DEVICE_INUSE */
		7, /* AST_DEVICE_BUSY */
		0, /* AST_DEVICE_INVALID */
		2, /* AST_DEVICE_UNAVAILABLE */
		5, /* AST_DEVICE_RINGING */
		8, /* AST_DEVICE_RINGINUSE */
		4, /* AST_DEVICE_ONHOLD */
	};

	if (state == AST_DEVICE_RINGING) {
		agg->ringing = 1;
	} else if (state == AST_DEVICE_INUSE || state == AST_DEVICE_ONHOLD || state == AST_DEVICE_BUSY) {
		agg->inuse = 1;
	}

	if (agg->ringing && agg->inuse) {
		agg->state = AST_DEVICE_RINGINUSE;
	} else if (state_order[state] > state_order[agg->state]) {
		agg->state = state;
	}
}

enum ast_device_state ast_devstate_aggregate_result(struct ast_devstate_aggregate *agg)
{
	return agg->state;
}

struct stasis_topic *ast_device_state_topic_all(void)
{
	return device_state_topic_all;
}

struct stasis_cache *ast_device_state_cache(void)
{
	return device_state_cache;
}

struct stasis_topic *ast_device_state_topic_cached(void)
{
	return stasis_caching_get_topic(device_state_topic_cached);
}

struct stasis_topic *ast_device_state_topic(const char *device)
{
	return stasis_topic_pool_get_topic(device_state_topic_pool, device);
}

int ast_device_state_clear_cache(const char *device)
{
	struct stasis_message *cached_msg;
	struct stasis_message *msg;

	cached_msg = stasis_cache_get_by_eid(ast_device_state_cache(),
		ast_device_state_message_type(), device, &ast_eid_default);
	if (!cached_msg) {
		/* nothing to clear */
		return -1;
	}

	msg = stasis_cache_clear_create(cached_msg);
	if (msg) {
		stasis_publish(ast_device_state_topic(device), msg);
	}
	ao2_cleanup(msg);
	ao2_cleanup(cached_msg);
	return 0;
}

int ast_publish_device_state_full(
	const char *device,
	enum ast_device_state state,
	enum ast_devstate_cache cachable,
	struct ast_eid *eid)
{
	RAII_VAR(struct ast_device_state_message *, device_state, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);
	struct stasis_topic *topic;

	ast_assert(!ast_strlen_zero(device));

	if (!ast_device_state_message_type()) {
		return -1;
	}

	device_state = device_state_alloc(device, state, cachable, eid);
	if (!device_state) {
		return -1;
	}

	message = stasis_message_create_full(ast_device_state_message_type(), device_state,
		eid);
	if (!message) {
		return -1;
	}

	/* When a device state is to be cached it is likely that something
	 * external will either be monitoring it or will want to pull the
	 * information from the cache, so we always publish to the device
	 * specific topic. Cachable updates traditionally come from such things
	 * as a SIP or PJSIP device.
	 * When a device state is not to be cached we only publish to its
	 * specific topic if something has already created the topic. Publishing
	 * to its topic otherwise would create the topic, which may not be
	 * necessary as it could be an ephemeral device. Uncachable updates
	 * traditionally come from such things as Local channels.
	 */
	if (cachable || stasis_topic_pool_topic_exists(device_state_topic_pool, device)) {
		topic = ast_device_state_topic(device);
	} else {
		topic = ast_device_state_topic_all();
	}

	if (!topic) {
		return -1;
	}

	stasis_publish(topic, message);
	return 0;
}

static const char *device_state_get_id(struct stasis_message *message)
{
	struct ast_device_state_message *device_state;

	if (ast_device_state_message_type() != stasis_message_type(message)) {
		return NULL;
	}

	device_state = stasis_message_data(message);
	if (device_state->cachable == AST_DEVSTATE_NOT_CACHABLE) {
		return NULL;
	}

	return device_state->device;
}

/*!
 * \internal
 * \brief Callback to publish the aggregate device state cache entry message.
 * \since 12.2.0
 *
 * \param cache_topic Caching topic the aggregate message may be published over.
 * \param aggregate The aggregate shapshot message to publish.
 *
 * \return Nothing
 */
static void device_state_aggregate_publish(struct stasis_topic *cache_topic, struct stasis_message *aggregate)
{
	const char *device;
	struct stasis_topic *device_specific_topic;

	device = device_state_get_id(aggregate);
	if (!device) {
		return;
	}
	device_specific_topic = ast_device_state_topic(device);
	if (!device_specific_topic) {
		return;
	}

	stasis_publish(device_specific_topic, aggregate);
}

/*!
 * \internal
 * \brief Callback to calculate the aggregate device state cache entry.
 * \since 12.2.0
 *
 * \param entry Cache entry to calculate a new aggregate snapshot.
 * \param new_snapshot The shapshot that is being updated.
 *
 * \note Return a ref bumped pointer from stasis_cache_entry_get_aggregate()
 * if a new aggregate could not be calculated because of error.
 *
 * \return New aggregate-snapshot calculated on success.
 * Caller has a reference on return.
 */
static struct stasis_message *device_state_aggregate_calc(struct stasis_cache_entry *entry, struct stasis_message *new_snapshot)
{
	struct stasis_message *aggregate_snapshot;
	struct stasis_message *snapshot;
	struct ast_device_state_message *device_state;
	const char *device = NULL;
	struct ast_devstate_aggregate aggregate;
	int idx;

	if (!ast_device_state_message_type()) {
		return NULL;
	}

	/* Determine the new aggregate device state. */
	ast_devstate_aggregate_init(&aggregate);
	snapshot = stasis_cache_entry_get_local(entry);
	if (snapshot) {
		device_state = stasis_message_data(snapshot);
		device = device_state->device;
		ast_devstate_aggregate_add(&aggregate, device_state->state);
	}
	for (idx = 0; ; ++idx) {
		snapshot = stasis_cache_entry_get_remote(entry, idx);
		if (!snapshot) {
			break;
		}

		device_state = stasis_message_data(snapshot);
		device = device_state->device;
		ast_devstate_aggregate_add(&aggregate, device_state->state);
	}

	if (!device) {
		/* There are no device states cached.  Delete the aggregate. */
		return NULL;
	}

	snapshot = stasis_cache_entry_get_aggregate(entry);
	if (snapshot) {
		device_state = stasis_message_data(snapshot);
		if (device_state->state == ast_devstate_aggregate_result(&aggregate)) {
			/* Aggregate device state did not change. */
			return ao2_bump(snapshot);
		}
	}

	device_state = device_state_alloc(device, ast_devstate_aggregate_result(&aggregate),
		AST_DEVSTATE_CACHABLE, NULL);
	if (!device_state) {
		/* Bummer.  We have to keep the old aggregate snapshot. */
		return ao2_bump(snapshot);
	}
	aggregate_snapshot = stasis_message_create_full(ast_device_state_message_type(),
		device_state, NULL);
	ao2_cleanup(device_state);
	if (!aggregate_snapshot) {
		/* Bummer.  We have to keep the old aggregate snapshot. */
		return ao2_bump(snapshot);
	}

	return aggregate_snapshot;
}

static void devstate_cleanup(void)
{
	devstate_message_sub = stasis_unsubscribe_and_join(devstate_message_sub);
	device_state_topic_cached = stasis_caching_unsubscribe_and_join(device_state_topic_cached);

	ao2_cleanup(device_state_cache);
	device_state_cache = NULL;

	ao2_cleanup(device_state_topic_pool);
	device_state_topic_pool = NULL;

	ao2_cleanup(device_state_topic_all);
	device_state_topic_all = NULL;

	STASIS_MESSAGE_TYPE_CLEANUP(ast_device_state_message_type);
}

int devstate_init(void)
{
	ast_register_cleanup(devstate_cleanup);

	if (STASIS_MESSAGE_TYPE_INIT(ast_device_state_message_type) != 0) {
		return -1;
	}
	device_state_topic_all = stasis_topic_create("devicestate:all");
	if (!device_state_topic_all) {
		return -1;
	}
	device_state_topic_pool = stasis_topic_pool_create(ast_device_state_topic_all());
	if (!device_state_topic_pool) {
		return -1;
	}
	device_state_cache = stasis_cache_create_full(device_state_get_id,
		device_state_aggregate_calc, device_state_aggregate_publish);
	if (!device_state_cache) {
		return -1;
	}
	device_state_topic_cached = stasis_caching_topic_create(ast_device_state_topic_all(),
		device_state_cache);
	if (!device_state_topic_cached) {
		return -1;
	}
	stasis_caching_accept_message_type(device_state_topic_cached, ast_device_state_message_type());
	stasis_caching_set_filter(device_state_topic_cached, STASIS_SUBSCRIPTION_FILTER_SELECTIVE);

	devstate_message_sub = stasis_subscribe(ast_device_state_topic_all(),
		devstate_change_cb, NULL);
	if (!devstate_message_sub) {
		ast_log(LOG_ERROR, "Failed to create subscription creating uncached device state aggregate events.\n");
		return -1;
	}
	stasis_subscription_accept_message_type(devstate_message_sub, ast_device_state_message_type());
	stasis_subscription_set_filter(devstate_message_sub, STASIS_SUBSCRIPTION_FILTER_SELECTIVE);

	return 0;
}

static struct ast_manager_event_blob *devstate_to_ami(struct stasis_message *msg)
{
	struct ast_device_state_message *dev_state;

	dev_state = stasis_message_data(msg);

	/* Ignore non-aggregate states */
	if (dev_state->eid) {
		return NULL;
	}

	return ast_manager_event_blob_create(EVENT_FLAG_CALL, "DeviceStateChange",
		"Device: %s\r\n"
		"State: %s\r\n",
		dev_state->device, ast_devstate_str(dev_state->state));
}

/*! \brief Convert a \ref stasis_message to a \ref ast_event */
static struct ast_event *devstate_to_event(struct stasis_message *message)
{
	struct ast_event *event;
	struct ast_device_state_message *device_state;

	if (!message) {
		return NULL;
	}

	device_state = stasis_message_data(message);

	if (device_state->eid) {
		event = ast_event_new(AST_EVENT_DEVICE_STATE_CHANGE,
					    AST_EVENT_IE_DEVICE, AST_EVENT_IE_PLTYPE_STR, device_state->device,
					    AST_EVENT_IE_STATE, AST_EVENT_IE_PLTYPE_UINT, device_state->state,
					    AST_EVENT_IE_CACHABLE, AST_EVENT_IE_PLTYPE_UINT, device_state->cachable,
					    AST_EVENT_IE_EID, AST_EVENT_IE_PLTYPE_RAW, device_state->eid, sizeof(*device_state->eid),
					    AST_EVENT_IE_END);
	} else {
		event = ast_event_new(AST_EVENT_DEVICE_STATE,
					    AST_EVENT_IE_DEVICE, AST_EVENT_IE_PLTYPE_STR, device_state->device,
					    AST_EVENT_IE_STATE, AST_EVENT_IE_PLTYPE_UINT, device_state->state,
					    AST_EVENT_IE_CACHABLE, AST_EVENT_IE_PLTYPE_UINT, device_state->cachable,
					    AST_EVENT_IE_END);
	}

	return event;
}
