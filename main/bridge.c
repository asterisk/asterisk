/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007 - 2009, Digium, Inc.
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
 * \brief Bridging API
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

/*** DOCUMENTATION
	<manager name="BridgeTechnologyList" language="en_US">
		<synopsis>
			List available bridging technologies and their statuses.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
		</syntax>
		<description>
			<para>Returns detailed information about the available bridging technologies.</para>
		</description>
	</manager>
	<manager name="BridgeTechnologySuspend" language="en_US">
		<synopsis>
			Suspend a bridging technology.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="BridgeTechnology" required="true">
				<para>The name of the bridging technology to suspend.</para>
			</parameter>
		</syntax>
		<description>
			<para>Marks a bridging technology as suspended, which prevents subsequently created bridges from using it.</para>
		</description>
	</manager>
	<manager name="BridgeTechnologyUnsuspend" language="en_US">
		<synopsis>
			Unsuspend a bridging technology.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="BridgeTechnology" required="true">
				<para>The name of the bridging technology to unsuspend.</para>
			</parameter>
		</syntax>
		<description>
			<para>Clears a previously suspended bridging technology, which allows subsequently created bridges to use it.</para>
		</description>
	</manager>
***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/options.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/linkedlists.h"
#include "asterisk/bridge.h"
#include "asterisk/bridge_internal.h"
#include "asterisk/bridge_channel_internal.h"
#include "asterisk/bridge_features.h"
#include "asterisk/bridge_basic.h"
#include "asterisk/bridge_technology.h"
#include "asterisk/bridge_channel.h"
#include "asterisk/bridge_after.h"
#include "asterisk/stasis_bridges.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/stasis_cache_pattern.h"
#include "asterisk/app.h"
#include "asterisk/file.h"
#include "asterisk/module.h"
#include "asterisk/astobj2.h"
#include "asterisk/pbx.h"
#include "asterisk/test.h"
#include "asterisk/_private.h"
#include "asterisk/heap.h"
#include "asterisk/say.h"
#include "asterisk/timing.h"
#include "asterisk/stringfields.h"
#include "asterisk/musiconhold.h"
#include "asterisk/features.h"
#include "asterisk/cli.h"
#include "asterisk/parking.h"
#include "asterisk/core_local.h"
#include "asterisk/core_unreal.h"
#include "asterisk/causes.h"

/*! All bridges container. */
static struct ao2_container *bridges;

static AST_RWLIST_HEAD_STATIC(bridge_technologies, ast_bridge_technology);

static unsigned int optimization_id;

/* Initial starting point for the bridge array of channels */
#define BRIDGE_ARRAY_START 128

/* Grow rate of bridge array of channels */
#define BRIDGE_ARRAY_GROW 32

/* Variable name - stores peer information about the most recent blind transfer */
#define BLINDTRANSFER "BLINDTRANSFER"

/* Variable name - stores peer information about the most recent attended transfer */
#define ATTENDEDTRANSFER "ATTENDEDTRANSFER"

static void cleanup_video_mode(struct ast_bridge *bridge);
static int bridge_make_compatible(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel);

/*! Default DTMF keys for built in features */
static char builtin_features_dtmf[AST_BRIDGE_BUILTIN_END][MAXIMUM_DTMF_FEATURE_STRING];

/*! Function handlers for the built in features */
static ast_bridge_hook_callback builtin_features_handlers[AST_BRIDGE_BUILTIN_END];

/*! Function handlers for built in interval features */
static ast_bridge_builtin_set_limits_fn builtin_interval_handlers[AST_BRIDGE_BUILTIN_INTERVAL_END];

/*! Bridge manager service request */
struct bridge_manager_request {
	/*! List of bridge service requests. */
	AST_LIST_ENTRY(bridge_manager_request) node;
	/*! Refed bridge requesting service. */
	struct ast_bridge *bridge;
};

struct bridge_manager_controller {
	/*! Condition, used to wake up the bridge manager thread. */
	ast_cond_t cond;
	/*! Queue of bridge service requests. */
	AST_LIST_HEAD_NOLOCK(, bridge_manager_request) service_requests;
	/*! Manager thread */
	pthread_t thread;
	/*! TRUE if the manager needs to stop. */
	unsigned int stop:1;
};

/*! Bridge manager controller. */
static struct bridge_manager_controller *bridge_manager;

/*!
 * \internal
 * \brief Request service for a bridge from the bridge manager.
 * \since 12.0.0
 *
 * \param bridge Requesting service.
 *
 * \return Nothing
 */
static void bridge_manager_service_req(struct ast_bridge *bridge)
{
	struct bridge_manager_request *request;

	ao2_lock(bridge_manager);
	if (bridge_manager->stop) {
		ao2_unlock(bridge_manager);
		return;
	}

	/* Create the service request. */
	request = ast_calloc(1, sizeof(*request));
	if (!request) {
		/* Well. This isn't good. */
		ao2_unlock(bridge_manager);
		return;
	}
	ao2_ref(bridge, +1);
	request->bridge = bridge;

	/* Put request into the queue and wake the bridge manager. */
	AST_LIST_INSERT_TAIL(&bridge_manager->service_requests, request, node);
	ast_cond_signal(&bridge_manager->cond);
	ao2_unlock(bridge_manager);
}

int __ast_bridge_technology_register(struct ast_bridge_technology *technology, struct ast_module *module)
{
	struct ast_bridge_technology *current;

	/* Perform a sanity check to make sure the bridge technology conforms to our needed requirements */
	if (ast_strlen_zero(technology->name)
		|| !technology->capabilities
		|| !technology->write) {
		ast_log(LOG_WARNING, "Bridge technology %s failed registration sanity check.\n",
			technology->name);
		return -1;
	}

	AST_RWLIST_WRLOCK(&bridge_technologies);

	/* Look for duplicate bridge technology already using this name, or already registered */
	AST_RWLIST_TRAVERSE(&bridge_technologies, current, entry) {
		if ((!strcasecmp(current->name, technology->name)) || (current == technology)) {
			ast_log(LOG_WARNING, "A bridge technology of %s already claims to exist in our world.\n",
				technology->name);
			AST_RWLIST_UNLOCK(&bridge_technologies);
			return -1;
		}
	}

	/* Copy module pointer so reference counting can keep the module from unloading */
	technology->mod = module;

	/* Insert our new bridge technology into the list and print out a pretty message */
	AST_RWLIST_INSERT_TAIL(&bridge_technologies, technology, entry);

	AST_RWLIST_UNLOCK(&bridge_technologies);

	ast_verb(2, "Registered bridge technology %s\n", technology->name);

	return 0;
}

int ast_bridge_technology_unregister(struct ast_bridge_technology *technology)
{
	struct ast_bridge_technology *current;

	AST_RWLIST_WRLOCK(&bridge_technologies);

	/* Ensure the bridge technology is registered before removing it */
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&bridge_technologies, current, entry) {
		if (current == technology) {
			AST_RWLIST_REMOVE_CURRENT(entry);
			ast_verb(2, "Unregistered bridge technology %s\n", technology->name);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;

	AST_RWLIST_UNLOCK(&bridge_technologies);

	return current ? 0 : -1;
}

/*!
 * \internal
 * \brief Put an action onto the specified bridge. Don't dup the action frame.
 * \since 12.0.0
 *
 * \param bridge What to queue the action on.
 * \param action What to do.
 *
 * \return Nothing
 */
static void bridge_queue_action_nodup(struct ast_bridge *bridge, struct ast_frame *action)
{
	ast_debug(1, "Bridge %s: queueing action type:%u sub:%d\n",
		bridge->uniqueid, action->frametype, action->subclass.integer);

	ast_bridge_lock(bridge);
	AST_LIST_INSERT_TAIL(&bridge->action_queue, action, frame_list);
	ast_bridge_unlock(bridge);
	bridge_manager_service_req(bridge);
}

int ast_bridge_queue_action(struct ast_bridge *bridge, struct ast_frame *action)
{
	struct ast_frame *dup;

	dup = ast_frdup(action);
	if (!dup) {
		return -1;
	}
	bridge_queue_action_nodup(bridge, dup);
	return 0;
}

void bridge_dissolve(struct ast_bridge *bridge, int cause)
{
	struct ast_bridge_channel *bridge_channel;
	struct ast_frame action = {
		.frametype = AST_FRAME_BRIDGE_ACTION,
		.subclass.integer = BRIDGE_CHANNEL_ACTION_DEFERRED_DISSOLVING,
	};

	if (bridge->dissolved) {
		return;
	}
	bridge->dissolved = 1;

	if (cause <= 0) {
		cause = AST_CAUSE_NORMAL_CLEARING;
	}
	bridge->cause = cause;

	ast_debug(1, "Bridge %s: dissolving bridge with cause %d(%s)\n",
		bridge->uniqueid, cause, ast_cause2str(cause));

	AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
		ast_bridge_channel_leave_bridge(bridge_channel,
			BRIDGE_CHANNEL_STATE_END_NO_DISSOLVE, cause);
	}

	/* Must defer dissolving bridge because it is already locked. */
	ast_bridge_queue_action(bridge, &action);
}

/*!
 * \internal
 * \brief Check if a bridge should dissolve because of a stolen channel and do it.
 * \since 12.0.0
 *
 * \param bridge Bridge to check.
 * \param bridge_channel Stolen channel causing the check.  It is not in the bridge to check and may be in another bridge.
 *
 * \note On entry, bridge and bridge_channel->bridge are already locked.
 *
 * \return Nothing
 */
static void bridge_dissolve_check_stolen(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	if (bridge->dissolved) {
		return;
	}

	if (bridge_channel->features->usable
		&& ast_test_flag(&bridge_channel->features->feature_flags,
			AST_BRIDGE_CHANNEL_FLAG_DISSOLVE_HANGUP)) {
		/* The stolen channel controlled the bridge it was stolen from. */
		bridge_dissolve(bridge, 0);
		return;
	}
	if (bridge->num_channels < 2
		&& ast_test_flag(&bridge->feature_flags, AST_BRIDGE_FLAG_DISSOLVE_HANGUP)) {
		/*
		 * The stolen channel has not left enough channels to keep the
		 * bridge alive.  Assume the stolen channel hung up.
		 */
		bridge_dissolve(bridge, 0);
		return;
	}
}

/*!
 * \internal
 * \brief Update connected line information after a bridge has been reconfigured.
 *
 * \param bridge The bridge itself.
 *
 * \return Nothing
 */
static void bridge_reconfigured_connected_line_update(struct ast_bridge *bridge)
{
	struct ast_party_connected_line connected;
	struct ast_bridge_channel *bridge_channel = AST_LIST_FIRST(&bridge->channels), *peer;
	unsigned char data[1024];
	size_t datalen;

	if (!bridge_channel ||
		!(bridge->technology->capabilities & (AST_BRIDGE_CAPABILITY_1TO1MIX | AST_BRIDGE_CAPABILITY_NATIVE)) ||
		!(peer = ast_bridge_channel_peer(bridge_channel)) ||
		ast_test_flag(ast_channel_flags(bridge_channel->chan), AST_FLAG_ZOMBIE) ||
		ast_test_flag(ast_channel_flags(peer->chan), AST_FLAG_ZOMBIE) ||
		ast_check_hangup_locked(bridge_channel->chan) ||
		ast_check_hangup_locked(peer->chan)) {
		return;
	}

	ast_party_connected_line_init(&connected);

	ast_channel_lock(bridge_channel->chan);
	ast_connected_line_copy_from_caller(&connected, ast_channel_caller(bridge_channel->chan));
	ast_channel_unlock(bridge_channel->chan);

	if ((datalen = ast_connected_line_build_data(data, sizeof(data), &connected, NULL)) != (size_t) -1) {
		ast_bridge_channel_queue_control_data(peer, AST_CONTROL_CONNECTED_LINE, data, datalen);
	}

	ast_channel_lock(peer->chan);
	ast_connected_line_copy_from_caller(&connected, ast_channel_caller(peer->chan));
	ast_channel_unlock(peer->chan);

	if ((datalen = ast_connected_line_build_data(data, sizeof(data), &connected, NULL)) != (size_t) -1) {
		ast_bridge_channel_queue_control_data(bridge_channel, AST_CONTROL_CONNECTED_LINE, data, datalen);
	}

	ast_party_connected_line_free(&connected);
}

/*!
 * \internal
 * \brief Complete joining a channel to the bridge.
 * \since 12.0.0
 *
 * \param bridge What to operate upon.
 * \param bridge_channel What is joining the bridge technology.
 *
 * \note On entry, bridge is already locked.
 *
 * \return Nothing
 */
static void bridge_channel_complete_join(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	/* Make the channel compatible with the bridge */
	bridge_make_compatible(bridge, bridge_channel);

	/* Tell the bridge technology we are joining so they set us up */
	ast_debug(1, "Bridge %s: %p(%s) is joining %s technology\n",
		bridge->uniqueid, bridge_channel, ast_channel_name(bridge_channel->chan),
		bridge->technology->name);
	if (bridge->technology->join
		&& bridge->technology->join(bridge, bridge_channel)) {
		ast_debug(1, "Bridge %s: %p(%s) failed to join %s technology\n",
			bridge->uniqueid, bridge_channel, ast_channel_name(bridge_channel->chan),
			bridge->technology->name);
		bridge_channel->just_joined = 1;
		return;
	}

	bridge_channel->just_joined = 0;
}

/*!
 * \internal
 * \brief Complete joining new channels to the bridge.
 * \since 12.0.0
 *
 * \param bridge Check for new channels on this bridge.
 *
 * \note On entry, bridge is already locked.
 *
 * \return Nothing
 */
static void bridge_complete_join(struct ast_bridge *bridge)
{
	struct ast_bridge_channel *bridge_channel;

	if (bridge->dissolved) {
		/*
		 * No sense in completing the join on channels for a dissolved
		 * bridge.  They are just going to be removed soon anyway.
		 * However, we do have reason to abort here because the bridge
		 * technology may not be able to handle the number of channels
		 * still in the bridge.
		 */
		return;
	}

	AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
		if (!bridge_channel->just_joined) {
			continue;
		}
		bridge_channel_complete_join(bridge, bridge_channel);
	}
}

/*! \brief Helper function used to find the "best" bridge technology given specified capabilities */
static struct ast_bridge_technology *find_best_technology(uint32_t capabilities, struct ast_bridge *bridge)
{
	struct ast_bridge_technology *current;
	struct ast_bridge_technology *best = NULL;

	AST_RWLIST_RDLOCK(&bridge_technologies);
	AST_RWLIST_TRAVERSE(&bridge_technologies, current, entry) {
		if (current->suspended) {
			ast_debug(1, "Bridge technology %s is suspended. Skipping.\n",
				current->name);
			continue;
		}
		if (!(current->capabilities & capabilities)) {
			ast_debug(1, "Bridge technology %s does not have any capabilities we want.\n",
				current->name);
			continue;
		}
		if (best && current->preference <= best->preference) {
			ast_debug(1, "Bridge technology %s has less preference than %s (%u <= %u). Skipping.\n",
				current->name, best->name, current->preference, best->preference);
			continue;
		}
		if (current->compatible && !current->compatible(bridge)) {
			ast_debug(1, "Bridge technology %s is not compatible with properties of existing bridge.\n",
				current->name);
			continue;
		}
		best = current;
	}

	if (best) {
		/* Increment it's module reference count if present so it does not get unloaded while in use */
		ast_module_ref(best->mod);
		ast_debug(1, "Chose bridge technology %s\n", best->name);
	}

	AST_RWLIST_UNLOCK(&bridge_technologies);

	return best;
}

struct tech_deferred_destroy {
	struct ast_bridge_technology *tech;
	void *tech_pvt;
};

/*!
 * \internal
 * \brief Deferred destruction of bridge tech private structure.
 * \since 12.0.0
 *
 * \param bridge What to execute the action on.
 * \param action Deferred bridge tech destruction.
 *
 * \note On entry, bridge must not be locked.
 *
 * \return Nothing
 */
static void bridge_tech_deferred_destroy(struct ast_bridge *bridge, struct ast_frame *action)
{
	struct tech_deferred_destroy *deferred = action->data.ptr;
	struct ast_bridge dummy_bridge = {
		.technology = deferred->tech,
		.tech_pvt = deferred->tech_pvt,
		.creator = bridge->creator,
		.name = bridge->name,
		.uniqueid = bridge->uniqueid,
		};

	ast_debug(1, "Bridge %s: calling %s technology destructor (deferred, dummy)\n",
		dummy_bridge.uniqueid, dummy_bridge.technology->name);
	dummy_bridge.technology->destroy(&dummy_bridge);
	ast_module_unref(dummy_bridge.technology->mod);
}

/*!
 * \internal
 * \brief Handle bridge action frame.
 * \since 12.0.0
 *
 * \param bridge What to execute the action on.
 * \param action What to do.
 *
 * \note On entry, bridge is already locked.
 * \note Can be called by the bridge destructor.
 *
 * \return Nothing
 */
static void bridge_action_bridge(struct ast_bridge *bridge, struct ast_frame *action)
{
#if 0	/* In case we need to know when the destructor is calling us. */
	int in_destructor = !ao2_ref(bridge, 0);
#endif

	switch (action->subclass.integer) {
	case BRIDGE_CHANNEL_ACTION_DEFERRED_TECH_DESTROY:
		ast_bridge_unlock(bridge);
		bridge_tech_deferred_destroy(bridge, action);
		ast_bridge_lock(bridge);
		break;
	case BRIDGE_CHANNEL_ACTION_DEFERRED_DISSOLVING:
		ast_bridge_unlock(bridge);
		bridge->v_table->dissolving(bridge);
		ast_bridge_lock(bridge);
		break;
	default:
		/* Unexpected deferred action type.  Should never happen. */
		ast_assert(0);
		break;
	}
}

/*!
 * \internal
 * \brief Do any pending bridge actions.
 * \since 12.0.0
 *
 * \param bridge What to do actions on.
 *
 * \note On entry, bridge is already locked.
 * \note Can be called by the bridge destructor.
 *
 * \return Nothing
 */
static void bridge_handle_actions(struct ast_bridge *bridge)
{
	struct ast_frame *action;

	while ((action = AST_LIST_REMOVE_HEAD(&bridge->action_queue, frame_list))) {
		switch (action->frametype) {
		case AST_FRAME_BRIDGE_ACTION:
			bridge_action_bridge(bridge, action);
			break;
		default:
			/* Unexpected deferred frame type.  Should never happen. */
			ast_assert(0);
			break;
		}
		ast_frfree(action);
	}
}

static struct stasis_message *create_bridge_snapshot_message(struct ast_bridge *bridge)
{
	RAII_VAR(struct ast_bridge_snapshot *, snapshot, NULL, ao2_cleanup);

	if (!ast_bridge_snapshot_type()) {
		return NULL;
	}

	ast_bridge_lock(bridge);
	snapshot = ast_bridge_snapshot_create(bridge);
	ast_bridge_unlock(bridge);

	if (!snapshot) {
		return NULL;
	}

	return stasis_message_create(ast_bridge_snapshot_type(), snapshot);
}

static void destroy_bridge(void *obj)
{
	struct ast_bridge *bridge = obj;

	ast_debug(1, "Bridge %s: actually destroying %s bridge, nobody wants it anymore\n",
		bridge->uniqueid, bridge->v_table->name);

	if (bridge->construction_completed) {
		RAII_VAR(struct stasis_message *, clear_msg, NULL, ao2_cleanup);

		clear_msg = create_bridge_snapshot_message(bridge);
		if (clear_msg) {
			RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);

			msg = stasis_cache_clear_create(clear_msg);
			if (msg) {
				stasis_publish(ast_bridge_topic(bridge), msg);
			}
		}
	}

	/* Do any pending actions in the context of destruction. */
	ast_bridge_lock(bridge);
	bridge_handle_actions(bridge);
	ast_bridge_unlock(bridge);

	/* There should not be any channels left in the bridge. */
	ast_assert(AST_LIST_EMPTY(&bridge->channels));

	ast_debug(1, "Bridge %s: calling %s bridge destructor\n",
		bridge->uniqueid, bridge->v_table->name);
	bridge->v_table->destroy(bridge);

	/* Pass off the bridge to the technology to destroy if needed */
	if (bridge->technology) {
		ast_debug(1, "Bridge %s: calling %s technology stop\n",
			bridge->uniqueid, bridge->technology->name);
		if (bridge->technology->stop) {
			ast_bridge_lock(bridge);
			bridge->technology->stop(bridge);
			ast_bridge_unlock(bridge);
		}
		ast_debug(1, "Bridge %s: calling %s technology destructor\n",
			bridge->uniqueid, bridge->technology->name);
		if (bridge->technology->destroy) {
			bridge->technology->destroy(bridge);
		}
		ast_module_unref(bridge->technology->mod);
		bridge->technology = NULL;
	}

	if (bridge->callid) {
		bridge->callid = ast_callid_unref(bridge->callid);
	}

	cleanup_video_mode(bridge);

	stasis_cp_single_unsubscribe(bridge->topics);

	ast_string_field_free_memory(bridge);
}

struct ast_bridge *bridge_register(struct ast_bridge *bridge)
{
	if (bridge) {
		bridge->construction_completed = 1;
		ast_bridge_lock(bridge);
		ast_bridge_publish_state(bridge);
		ast_bridge_unlock(bridge);
		if (!ao2_link(bridges, bridge)) {
			ast_bridge_destroy(bridge, 0);
			bridge = NULL;
		}
	}
	return bridge;
}

struct ast_bridge *bridge_alloc(size_t size, const struct ast_bridge_methods *v_table)
{
	struct ast_bridge *bridge;

	/* Check v_table that all methods are present. */
	if (!v_table
		|| !v_table->name
		|| !v_table->destroy
		|| !v_table->dissolving
		|| !v_table->push
		|| !v_table->pull
		|| !v_table->notify_masquerade
		|| !v_table->get_merge_priority) {
		ast_log(LOG_ERROR, "Virtual method table for bridge class %s not complete.\n",
			v_table && v_table->name ? v_table->name : "<unknown>");
		ast_assert(0);
		return NULL;
	}

	bridge = ao2_alloc(size, destroy_bridge);
	if (!bridge) {
		return NULL;
	}

	if (ast_string_field_init(bridge, 80)) {
		ao2_cleanup(bridge);
		return NULL;
	}

	bridge->v_table = v_table;

	return bridge;
}

struct ast_bridge *bridge_base_init(struct ast_bridge *self, uint32_t capabilities, unsigned int flags, const char *creator, const char *name, const char *id)
{
	char uuid_hold[AST_UUID_STR_LEN];

	if (!self) {
		return NULL;
	}

	if (!ast_strlen_zero(id)) {
		ast_string_field_set(self, uniqueid, id);
	} else {
		ast_uuid_generate_str(uuid_hold, AST_UUID_STR_LEN);
		ast_string_field_set(self, uniqueid, uuid_hold);
	}
	ast_string_field_set(self, creator, creator);
	if (!ast_strlen_zero(creator)) {
		ast_string_field_set(self, name, name);
	}

	ast_set_flag(&self->feature_flags, flags);
	self->allowed_capabilities = capabilities;

	if (bridge_topics_init(self) != 0) {
		ast_log(LOG_WARNING, "Bridge %s: Could not initialize topics\n",
			self->uniqueid);
		ao2_ref(self, -1);
		return NULL;
	}

	/* Use our helper function to find the "best" bridge technology. */
	self->technology = find_best_technology(capabilities, self);
	if (!self->technology) {
		ast_log(LOG_WARNING, "Bridge %s: Could not create class %s.  No technology to support it.\n",
			self->uniqueid, self->v_table->name);
		ao2_ref(self, -1);
		return NULL;
	}

	/* Pass off the bridge to the technology to manipulate if needed */
	ast_debug(1, "Bridge %s: calling %s technology constructor\n",
		self->uniqueid, self->technology->name);
	if (self->technology->create && self->technology->create(self)) {
		ast_log(LOG_WARNING, "Bridge %s: failed to setup bridge technology %s\n",
			self->uniqueid, self->technology->name);
		ao2_ref(self, -1);
		return NULL;
	}
	ast_debug(1, "Bridge %s: calling %s technology start\n",
		self->uniqueid, self->technology->name);
	if (self->technology->start && self->technology->start(self)) {
		ast_log(LOG_WARNING, "Bridge %s: failed to start bridge technology %s\n",
			self->uniqueid, self->technology->name);
		ao2_ref(self, -1);
		return NULL;
	}

	if (!ast_bridge_topic(self)) {
		ao2_ref(self, -1);
		return NULL;
	}

	return self;
}

/*!
 * \internal
 * \brief ast_bridge base class destructor.
 * \since 12.0.0
 *
 * \param self Bridge to operate upon.
 *
 * \note Stub because of nothing to do.
 *
 * \return Nothing
 */
static void bridge_base_destroy(struct ast_bridge *self)
{
}

/*!
 * \internal
 * \brief The bridge is being dissolved.
 * \since 12.0.0
 *
 * \param self Bridge to operate upon.
 *
 * \return Nothing
 */
static void bridge_base_dissolving(struct ast_bridge *self)
{
	ao2_unlink(bridges, self);
}

/*!
 * \internal
 * \brief ast_bridge base push method.
 * \since 12.0.0
 *
 * \param self Bridge to operate upon.
 * \param bridge_channel Bridge channel to push.
 * \param swap Bridge channel to swap places with if not NULL.
 *
 * \note On entry, self is already locked.
 * \note Stub because of nothing to do.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
static int bridge_base_push(struct ast_bridge *self, struct ast_bridge_channel *bridge_channel, struct ast_bridge_channel *swap)
{
	return 0;
}

/*!
 * \internal
 * \brief ast_bridge base pull method.
 * \since 12.0.0
 *
 * \param self Bridge to operate upon.
 * \param bridge_channel Bridge channel to pull.
 *
 * \note On entry, self is already locked.
 *
 * \return Nothing
 */
static void bridge_base_pull(struct ast_bridge *self, struct ast_bridge_channel *bridge_channel)
{
	ast_bridge_features_remove(bridge_channel->features, AST_BRIDGE_HOOK_REMOVE_ON_PULL);
}

/*!
 * \internal
 * \brief ast_bridge base notify_masquerade method.
 * \since 12.0.0
 *
 * \param self Bridge to operate upon.
 * \param bridge_channel Bridge channel that was masqueraded.
 *
 * \note On entry, self is already locked.
 *
 * \return Nothing
 */
static void bridge_base_notify_masquerade(struct ast_bridge *self, struct ast_bridge_channel *bridge_channel)
{
	self->reconfigured = 1;
}

/*!
 * \internal
 * \brief Get the merge priority of this bridge.
 * \since 12.0.0
 *
 * \param self Bridge to operate upon.
 *
 * \note On entry, self is already locked.
 *
 * \return Merge priority
 */
static int bridge_base_get_merge_priority(struct ast_bridge *self)
{
	return 0;
}

struct ast_bridge_methods ast_bridge_base_v_table = {
	.name = "base",
	.destroy = bridge_base_destroy,
	.dissolving = bridge_base_dissolving,
	.push = bridge_base_push,
	.pull = bridge_base_pull,
	.notify_masquerade = bridge_base_notify_masquerade,
	.get_merge_priority = bridge_base_get_merge_priority,
};

struct ast_bridge *ast_bridge_base_new(uint32_t capabilities, unsigned int flags, const char *creator, const char *name, const char *id)
{
	void *bridge;

	bridge = bridge_alloc(sizeof(struct ast_bridge), &ast_bridge_base_v_table);
	bridge = bridge_base_init(bridge, capabilities, flags, creator, name, id);
	bridge = bridge_register(bridge);
	return bridge;
}

int ast_bridge_destroy(struct ast_bridge *bridge, int cause)
{
	ast_debug(1, "Bridge %s: telling all channels to leave the party\n", bridge->uniqueid);
	ast_bridge_lock(bridge);
	bridge_dissolve(bridge, cause);
	ast_bridge_unlock(bridge);

	ao2_ref(bridge, -1);

	return 0;
}

static int bridge_make_compatible(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct ast_str *codec_buf = ast_str_alloca(64);
	struct ast_format *best_format;
	RAII_VAR(struct ast_format *, read_format, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format *, write_format, NULL, ao2_cleanup);

	ast_channel_lock(bridge_channel->chan);
	read_format = ao2_bump(ast_channel_readformat(bridge_channel->chan));
	write_format = ao2_bump(ast_channel_writeformat(bridge_channel->chan));
	ast_channel_unlock(bridge_channel->chan);

	/* Are the formats currently in use something this bridge can handle? */
	if (ast_format_cap_iscompatible_format(bridge->technology->format_capabilities, read_format) == AST_FORMAT_CMP_NOT_EQUAL) {
		best_format = ast_format_cap_get_format(bridge->technology->format_capabilities, 0);

		/* Read format is a no go... */
		ast_debug(1, "Bridge technology %s wants to read any of formats %s but channel has %s\n",
			bridge->technology->name,
			ast_format_cap_get_names(bridge->technology->format_capabilities, &codec_buf),
			ast_format_get_name(read_format));

		/* Switch read format to the best one chosen */
		if (ast_set_read_format(bridge_channel->chan, best_format)) {
			ast_log(LOG_WARNING, "Failed to set channel %s to read format %s\n",
				ast_channel_name(bridge_channel->chan), ast_format_get_name(best_format));
			ao2_cleanup(best_format);
			return -1;
		}
		ast_debug(1, "Bridge %s put channel %s into read format %s\n",
			bridge->uniqueid, ast_channel_name(bridge_channel->chan),
			ast_format_get_name(best_format));
		ao2_cleanup(best_format);
	} else {
		ast_debug(1, "Bridge %s is happy that channel %s already has read format %s\n",
			bridge->uniqueid, ast_channel_name(bridge_channel->chan),
			ast_format_get_name(read_format));
	}

	if (ast_format_cap_iscompatible_format(bridge->technology->format_capabilities, write_format) == AST_FORMAT_CMP_NOT_EQUAL) {
		best_format = ast_format_cap_get_format(bridge->technology->format_capabilities, 0);

		/* Write format is a no go... */
		ast_debug(1, "Bridge technology %s wants to write any of formats %s but channel has %s\n",
			bridge->technology->name,
			ast_format_cap_get_names(bridge->technology->format_capabilities, &codec_buf),
			ast_format_get_name(write_format));

		/* Switch write format to the best one chosen */
		if (ast_set_write_format(bridge_channel->chan, best_format)) {
			ast_log(LOG_WARNING, "Failed to set channel %s to write format %s\n",
				ast_channel_name(bridge_channel->chan), ast_format_get_name(best_format));
			ao2_cleanup(best_format);
			return -1;
		}
		ast_debug(1, "Bridge %s put channel %s into write format %s\n",
			bridge->uniqueid, ast_channel_name(bridge_channel->chan),
			ast_format_get_name(best_format));
		ao2_cleanup(best_format);
	} else {
		ast_debug(1, "Bridge %s is happy that channel %s already has write format %s\n",
			bridge->uniqueid, ast_channel_name(bridge_channel->chan),
			ast_format_get_name(write_format));
	}

	return 0;
}

/*!
 * \internal
 * \brief Perform the smart bridge operation.
 * \since 12.0.0
 *
 * \param bridge Work on this bridge.
 *
 * \details
 * Basically see if a new bridge technology should be used instead
 * of the current one.
 *
 * \note On entry, bridge is already locked.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int smart_bridge_operation(struct ast_bridge *bridge)
{
	uint32_t new_capabilities;
	struct ast_bridge_technology *new_technology;
	struct ast_bridge_technology *old_technology = bridge->technology;
	struct ast_bridge_channel *bridge_channel;
	struct ast_frame *deferred_action;
	struct ast_bridge dummy_bridge = {
		.technology = bridge->technology,
		.tech_pvt = bridge->tech_pvt,
		.creator = bridge->creator,
		.name = bridge->name,
		.uniqueid = bridge->uniqueid,
	};

	if (bridge->dissolved) {
		ast_debug(1, "Bridge %s is dissolved, not performing smart bridge operation.\n",
			bridge->uniqueid);
		return 0;
	}

	/* Determine new bridge technology capabilities needed. */
	if (2 < bridge->num_channels) {
		new_capabilities = AST_BRIDGE_CAPABILITY_MULTIMIX;
		new_capabilities &= bridge->allowed_capabilities;
	} else {
		new_capabilities = AST_BRIDGE_CAPABILITY_NATIVE | AST_BRIDGE_CAPABILITY_1TO1MIX;
		new_capabilities &= bridge->allowed_capabilities;
		if (!new_capabilities
			&& (bridge->allowed_capabilities & AST_BRIDGE_CAPABILITY_MULTIMIX)) {
			/* Allow switching between different multimix bridge technologies. */
			new_capabilities = AST_BRIDGE_CAPABILITY_MULTIMIX;
		}
	}

	/* Find a bridge technology to satisfy the new capabilities. */
	new_technology = find_best_technology(new_capabilities, bridge);
	if (!new_technology) {
		int is_compatible = 0;

		if (old_technology->compatible) {
			is_compatible = old_technology->compatible(bridge);
		} else if (old_technology->capabilities & AST_BRIDGE_CAPABILITY_MULTIMIX) {
			is_compatible = 1;
		} else if (bridge->num_channels <= 2
			&& (old_technology->capabilities & AST_BRIDGE_CAPABILITY_1TO1MIX)) {
			is_compatible = 1;
		}

		if (is_compatible) {
			ast_debug(1, "Bridge %s could not get a new technology, staying with old technology.\n",
				bridge->uniqueid);
			return 0;
		}
		ast_log(LOG_WARNING, "Bridge %s has no technology available to support it.\n",
			bridge->uniqueid);
		return -1;
	}
	if (new_technology == old_technology) {
		ast_debug(1, "Bridge %s is already using the new technology.\n",
			bridge->uniqueid);
		ast_module_unref(old_technology->mod);
		return 0;
	}

	if (old_technology->destroy) {
		struct tech_deferred_destroy deferred_tech_destroy = {
			.tech = dummy_bridge.technology,
			.tech_pvt = dummy_bridge.tech_pvt,
		};
		struct ast_frame action = {
			.frametype = AST_FRAME_BRIDGE_ACTION,
			.subclass.integer = BRIDGE_CHANNEL_ACTION_DEFERRED_TECH_DESTROY,
			.data.ptr = &deferred_tech_destroy,
			.datalen = sizeof(deferred_tech_destroy),
		};

		/*
		 * We need to defer the bridge technology destroy callback
		 * because we have the bridge locked.
		 */
		deferred_action = ast_frdup(&action);
		if (!deferred_action) {
			ast_module_unref(new_technology->mod);
			return -1;
		}
	} else {
		deferred_action = NULL;
	}

	/*
	 * We are now committed to changing the bridge technology.  We
	 * must not release the bridge lock until we have installed the
	 * new bridge technology.
	 */
	ast_verb(4, "Bridge %s: switching from %s technology to %s\n",
		bridge->uniqueid, old_technology->name, new_technology->name);

	/*
	 * Since we are soon going to pass this bridge to a new
	 * technology we need to NULL out the tech_pvt pointer but
	 * don't worry as it still exists in dummy_bridge, ditto for the
	 * old technology.
	 */
	bridge->tech_pvt = NULL;
	bridge->technology = new_technology;

	/* Setup the new bridge technology. */
	ast_debug(1, "Bridge %s: calling %s technology constructor\n",
		bridge->uniqueid, new_technology->name);
	if (new_technology->create && new_technology->create(bridge)) {
		ast_log(LOG_WARNING, "Bridge %s: failed to setup bridge technology %s\n",
			bridge->uniqueid, new_technology->name);
		bridge->tech_pvt = dummy_bridge.tech_pvt;
		bridge->technology = dummy_bridge.technology;
		ast_module_unref(new_technology->mod);
		return -1;
	}

	ast_debug(1, "Bridge %s: calling %s technology stop\n",
		dummy_bridge.uniqueid, old_technology->name);
	if (old_technology->stop) {
		old_technology->stop(&dummy_bridge);
	}

	/*
	 * Move existing channels over to the new technology and
	 * complete joining any new channels to the bridge.
	 */
	AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
		if (!bridge_channel->just_joined) {
			/* Take existing channel from the old technology. */
			ast_debug(1, "Bridge %s: %p(%s) is leaving %s technology (dummy)\n",
				dummy_bridge.uniqueid, bridge_channel, ast_channel_name(bridge_channel->chan),
				old_technology->name);
			if (old_technology->leave) {
				old_technology->leave(&dummy_bridge, bridge_channel);
			}
		}

		/* Add any new channels or re-add an existing channel to the bridge. */
		bridge_channel_complete_join(bridge, bridge_channel);
	}

	ast_debug(1, "Bridge %s: calling %s technology start\n",
		bridge->uniqueid, new_technology->name);
	if (new_technology->start && new_technology->start(bridge)) {
		ast_log(LOG_WARNING, "Bridge %s: failed to start bridge technology %s\n",
			bridge->uniqueid, new_technology->name);
	}

	/*
	 * Now that all the channels have been moved over we need to get
	 * rid of all the information the old technology may have left
	 * around.
	 */
	if (old_technology->destroy) {
		ast_debug(1, "Bridge %s: deferring %s technology destructor\n",
			dummy_bridge.uniqueid, old_technology->name);
		bridge_queue_action_nodup(bridge, deferred_action);
	} else {
		ast_debug(1, "Bridge %s: calling %s technology destructor\n",
			dummy_bridge.uniqueid, old_technology->name);
		ast_module_unref(old_technology->mod);
	}

	return 0;
}

/*!
 * \internal
 * \brief Bridge channel to check if a BRIDGE_PLAY_SOUND needs to be played.
 * \since 12.0.0
 *
 * \param bridge_channel What to check.
 *
 * \return Nothing
 */
static void check_bridge_play_sound(struct ast_bridge_channel *bridge_channel)
{
	const char *play_file;

	ast_channel_lock(bridge_channel->chan);
	play_file = pbx_builtin_getvar_helper(bridge_channel->chan, "BRIDGE_PLAY_SOUND");
	if (!ast_strlen_zero(play_file)) {
		play_file = ast_strdupa(play_file);
		pbx_builtin_setvar_helper(bridge_channel->chan, "BRIDGE_PLAY_SOUND", NULL);
	} else {
		play_file = NULL;
	}
	ast_channel_unlock(bridge_channel->chan);

	if (play_file) {
		ast_bridge_channel_queue_playfile(bridge_channel, NULL, play_file, NULL);
	}
}

/*!
 * \internal
 * \brief Check for any BRIDGE_PLAY_SOUND channel variables in the bridge.
 * \since 12.0.0
 *
 * \param bridge What to operate on.
 *
 * \note On entry, the bridge is already locked.
 *
 * \return Nothing
 */
static void check_bridge_play_sounds(struct ast_bridge *bridge)
{
	struct ast_bridge_channel *bridge_channel;

	AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
		check_bridge_play_sound(bridge_channel);
	}
}

static void update_bridge_vars_set(struct ast_channel *chan, const char *name, const char *pvtid)
{
	ast_channel_stage_snapshot(chan);
	pbx_builtin_setvar_helper(chan, "BRIDGEPEER", name);
	pbx_builtin_setvar_helper(chan, "BRIDGEPVTCALLID", pvtid);
	ast_channel_stage_snapshot_done(chan);
}

/*!
 * \internal
 * \brief Set BRIDGEPEER and BRIDGEPVTCALLID channel variables in a 2 party bridge.
 * \since 12.0.0
 *
 * \param c0 Party of the first part.
 * \param c1 Party of the second part.
 *
 * \note On entry, the bridge is already locked.
 * \note The bridge is expected to have exactly two parties.
 *
 * \return Nothing
 */
static void set_bridge_peer_vars_2party(struct ast_channel *c0, struct ast_channel *c1)
{
	const char *c0_name;
	const char *c1_name;
	const char *c0_pvtid = NULL;
	const char *c1_pvtid = NULL;
#define UPDATE_BRIDGE_VARS_GET(chan, name, pvtid)									\
	do {																			\
		name = ast_strdupa(ast_channel_name(chan));									\
		if (ast_channel_tech(chan)->get_pvt_uniqueid) {								\
			pvtid = ast_strdupa(ast_channel_tech(chan)->get_pvt_uniqueid(chan));	\
		}																			\
	} while (0)

	ast_channel_lock(c1);
	UPDATE_BRIDGE_VARS_GET(c1, c1_name, c1_pvtid);
	ast_channel_unlock(c1);

	ast_channel_lock(c0);
	update_bridge_vars_set(c0, c1_name, c1_pvtid);
	UPDATE_BRIDGE_VARS_GET(c0, c0_name, c0_pvtid);
	ast_channel_unlock(c0);

	ast_channel_lock(c1);
	update_bridge_vars_set(c1, c0_name, c0_pvtid);
	ast_channel_unlock(c1);
}

/*!
 * \internal
 * \brief Fill the BRIDGEPEER value buffer with a comma separated list of channel names.
 * \since 12.0.0
 *
 * \param buf Buffer to fill.  The caller must guarantee the buffer is large enough.
 * \param cur_idx Which index into names[] to skip.
 * \param names Channel names to put in the buffer.
 * \param num_names Number of names in the array.
 *
 * \return Nothing
 */
static void fill_bridgepeer_buf(char *buf, unsigned int cur_idx, const char *names[], unsigned int num_names)
{
	int need_separator = 0;
	unsigned int idx;
	const char *src;
	char *pos;

	pos = buf;
	for (idx = 0; idx < num_names; ++idx) {
		if (idx == cur_idx) {
			continue;
		}

		if (need_separator) {
			*pos++ = ',';
		}
		need_separator = 1;

		/* Copy name into buffer. */
		src = names[idx];
		while (*src) {
			*pos++ = *src++;
		}
	}
	*pos = '\0';
}

/*!
 * \internal
 * \brief Set BRIDGEPEER and BRIDGEPVTCALLID channel variables in a multi-party bridge.
 * \since 12.0.0
 *
 * \param bridge What to operate on.
 *
 * \note On entry, the bridge is already locked.
 * \note The bridge is expected to have more than two parties.
 *
 * \return Nothing
 */
static void set_bridge_peer_vars_multiparty(struct ast_bridge *bridge)
{
/*
 * Set a maximum number of channel names for the BRIDGEPEER
 * list.  The plus one is for the current channel which is not
 * put in the list.
 */
#define MAX_BRIDGEPEER_CHANS	(10 + 1)

	unsigned int idx;
	unsigned int num_names;
	unsigned int len;
	const char **names;
	char *buf;
	struct ast_bridge_channel *bridge_channel;

	/* Get first MAX_BRIDGEPEER_CHANS channel names. */
	num_names = MIN(bridge->num_channels, MAX_BRIDGEPEER_CHANS);
	names = ast_alloca(num_names * sizeof(*names));
	idx = 0;
	AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
		if (num_names <= idx) {
			break;
		}
		ast_channel_lock(bridge_channel->chan);
		names[idx++] = ast_strdupa(ast_channel_name(bridge_channel->chan));
		ast_channel_unlock(bridge_channel->chan);
	}

	/* Determine maximum buf size needed. */
	len = num_names;
	for (idx = 0; idx < num_names; ++idx) {
		len += strlen(names[idx]);
	}
	buf = ast_alloca(len);

	/* Set the bridge channel variables. */
	idx = 0;
	buf[0] = '\0';
	AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
		if (idx < num_names) {
			fill_bridgepeer_buf(buf, idx, names, num_names);
		}
		++idx;

		ast_channel_lock(bridge_channel->chan);
		update_bridge_vars_set(bridge_channel->chan, buf, NULL);
		ast_channel_unlock(bridge_channel->chan);
	}
}

/*!
 * \internal
 * \brief Set BRIDGEPEER and BRIDGEPVTCALLID channel variables in a holding bridge.
 * \since 12.0.0
 *
 * \param bridge What to operate on.
 *
 * \note On entry, the bridge is already locked.
 *
 * \return Nothing
 */
static void set_bridge_peer_vars_holding(struct ast_bridge *bridge)
{
	struct ast_bridge_channel *bridge_channel;

	AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
		ast_channel_lock(bridge_channel->chan);
		update_bridge_vars_set(bridge_channel->chan, NULL, NULL);
		ast_channel_unlock(bridge_channel->chan);
	}
}

/*!
 * \internal
 * \brief Set BRIDGEPEER and BRIDGEPVTCALLID channel variables in the bridge.
 * \since 12.0.0
 *
 * \param bridge What to operate on.
 *
 * \note On entry, the bridge is already locked.
 *
 * \return Nothing
 */
static void set_bridge_peer_vars(struct ast_bridge *bridge)
{
	if (bridge->technology->capabilities & AST_BRIDGE_CAPABILITY_HOLDING) {
		set_bridge_peer_vars_holding(bridge);
		return;
	}
	if (bridge->num_channels < 2) {
		return;
	}
	if (bridge->num_channels == 2) {
		set_bridge_peer_vars_2party(AST_LIST_FIRST(&bridge->channels)->chan,
			AST_LIST_LAST(&bridge->channels)->chan);
	} else {
		set_bridge_peer_vars_multiparty(bridge);
	}
}

void bridge_reconfigured(struct ast_bridge *bridge, unsigned int colp_update)
{
	if (!bridge->reconfigured) {
		return;
	}
	bridge->reconfigured = 0;
	if (ast_test_flag(&bridge->feature_flags, AST_BRIDGE_FLAG_SMART)
		&& smart_bridge_operation(bridge)) {
		/* Smart bridge failed. */
		bridge_dissolve(bridge, 0);
		return;
	}
	bridge_complete_join(bridge);

	if (bridge->dissolved) {
		return;
	}
	check_bridge_play_sounds(bridge);
	set_bridge_peer_vars(bridge);
	ast_bridge_publish_state(bridge);

	if (colp_update) {
		bridge_reconfigured_connected_line_update(bridge);
	}
}

struct ast_bridge_channel *bridge_find_channel(struct ast_bridge *bridge, struct ast_channel *chan)
{
	struct ast_bridge_channel *bridge_channel;

	AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
		if (bridge_channel->chan == chan) {
			break;
		}
	}

	return bridge_channel;
}

void ast_bridge_notify_masquerade(struct ast_channel *chan)
{
	struct ast_bridge_channel *bridge_channel;
	struct ast_bridge *bridge;

	/* Safely get the bridge_channel pointer for the chan. */
	ast_channel_lock(chan);
	bridge_channel = ast_channel_get_bridge_channel(chan);
	ast_channel_unlock(chan);
	if (!bridge_channel) {
		/* Not in a bridge */
		return;
	}

	ast_bridge_channel_lock_bridge(bridge_channel);
	bridge = bridge_channel->bridge;
	if (bridge_channel == bridge_find_channel(bridge, chan)) {
/*
 * XXX ASTERISK-22366 this needs more work.  The channels need
 * to be made compatible again if the formats change. The
 * bridge_channel thread needs to monitor for this case.
 */
		/* The channel we want to notify is still in a bridge. */
		bridge->v_table->notify_masquerade(bridge, bridge_channel);
		bridge_reconfigured(bridge, 1);
	}
	ast_bridge_unlock(bridge);
	ao2_ref(bridge_channel, -1);
}

/*
 * XXX ASTERISK-21271 make ast_bridge_join() require features to be allocated just like ast_bridge_impart() and not expect the struct back.
 *
 * This change is really going to break ConfBridge.  All other
 * users are easily changed.  However, it is needed so the
 * bridging code can manipulate features on all channels
 * consistently no matter how they joined.
 *
 * Need to update the features parameter doxygen when this
 * change is made to be like ast_bridge_impart().
 */
int ast_bridge_join(struct ast_bridge *bridge,
	struct ast_channel *chan,
	struct ast_channel *swap,
	struct ast_bridge_features *features,
	struct ast_bridge_tech_optimizations *tech_args,
	enum ast_bridge_join_flags flags)
{
	struct ast_bridge_channel *bridge_channel;
	int res = 0;

	bridge_channel = bridge_channel_internal_alloc(bridge);
	if (flags & AST_BRIDGE_JOIN_PASS_REFERENCE) {
		ao2_ref(bridge, -1);
	}
	if (!bridge_channel) {
		res = -1;
		goto join_exit;
	}
/* XXX ASTERISK-21271 features cannot be NULL when passed in. When it is changed to allocated we can do like ast_bridge_impart() and allocate one. */
	ast_assert(features != NULL);
	if (!features) {
		ao2_ref(bridge_channel, -1);
		res = -1;
		goto join_exit;
	}
	if (tech_args) {
		bridge_channel->tech_args = *tech_args;
	}

	ast_channel_lock(chan);
	if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_ZOMBIE)) {
		res = -1;
	} else {
		ast_channel_internal_bridge_channel_set(chan, bridge_channel);
	}
	ast_channel_unlock(chan);
	bridge_channel->thread = pthread_self();
	bridge_channel->chan = chan;
	bridge_channel->swap = swap;
	bridge_channel->features = features;
	bridge_channel->inhibit_colp = !!(flags & AST_BRIDGE_JOIN_INHIBIT_JOIN_COLP);

	if (!res) {
		res = bridge_channel_internal_join(bridge_channel);
	}

	/* Cleanup all the data in the bridge channel after it leaves the bridge. */
	ast_channel_lock(chan);
	ast_channel_internal_bridge_channel_set(chan, NULL);
	ast_channel_unlock(chan);
	bridge_channel->chan = NULL;
	bridge_channel->swap = NULL;
	bridge_channel->features = NULL;

	ao2_ref(bridge_channel, -1);

join_exit:;
	ast_bridge_run_after_callback(chan);
	if (!(ast_channel_softhangup_internal_flag(chan) & AST_SOFTHANGUP_ASYNCGOTO)
		&& !ast_bridge_setup_after_goto(chan)) {
		/* Claim the after bridge goto is an async goto destination. */
		ast_channel_lock(chan);
		ast_softhangup_nolock(chan, AST_SOFTHANGUP_ASYNCGOTO);
		ast_channel_unlock(chan);
	}
	return res;
}

/*! \brief Thread responsible for imparted bridged channels to be departed */
static void *bridge_channel_depart_thread(void *data)
{
	struct ast_bridge_channel *bridge_channel = data;

	if (bridge_channel->callid) {
		ast_callid_threadassoc_add(bridge_channel->callid);
	}

	bridge_channel_internal_join(bridge_channel);

	/* cleanup */
	bridge_channel->swap = NULL;
	ast_bridge_features_destroy(bridge_channel->features);
	bridge_channel->features = NULL;

	ast_bridge_discard_after_callback(bridge_channel->chan, AST_BRIDGE_AFTER_CB_REASON_DEPART);
	ast_bridge_discard_after_goto(bridge_channel->chan);

	return NULL;
}

/*! \brief Thread responsible for independent imparted bridged channels */
static void *bridge_channel_ind_thread(void *data)
{
	struct ast_bridge_channel *bridge_channel = data;
	struct ast_channel *chan;

	if (bridge_channel->callid) {
		ast_callid_threadassoc_add(bridge_channel->callid);
	}

	bridge_channel_internal_join(bridge_channel);
	chan = bridge_channel->chan;

	/* cleanup */
	ast_channel_lock(chan);
	ast_channel_internal_bridge_channel_set(chan, NULL);
	ast_channel_unlock(chan);
	bridge_channel->chan = NULL;
	bridge_channel->swap = NULL;
	ast_bridge_features_destroy(bridge_channel->features);
	bridge_channel->features = NULL;

	ao2_ref(bridge_channel, -1);

	ast_bridge_run_after_callback(chan);
	ast_bridge_run_after_goto(chan);
	return NULL;
}

int ast_bridge_impart(struct ast_bridge *bridge,
	struct ast_channel *chan,
	struct ast_channel *swap,
	struct ast_bridge_features *features,
	enum ast_bridge_impart_flags flags)
{
	int res = 0;
	struct ast_bridge_channel *bridge_channel;

	/* Imparted channels cannot have a PBX. */
	if (ast_channel_pbx(chan)) {
		ast_log(AST_LOG_WARNING, "Channel %s has a PBX thread and cannot be imparted into bridge %s\n",
			ast_channel_name(chan), bridge->uniqueid);
		return -1;
	}

	/* Supply an empty features structure if the caller did not. */
	if (!features) {
		features = ast_bridge_features_new();
		if (!features) {
			return -1;
		}
	}

	/* Try to allocate a structure for the bridge channel */
	bridge_channel = bridge_channel_internal_alloc(bridge);
	if (!bridge_channel) {
		ast_bridge_features_destroy(features);
		return -1;
	}

	ast_channel_lock(chan);
	if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_ZOMBIE)) {
		ast_log(AST_LOG_NOTICE, "Channel %s is a zombie and cannot be imparted into bridge %s\n",
			ast_channel_name(chan), bridge->uniqueid);
		res = -1;
	} else {
		ast_channel_internal_bridge_channel_set(chan, bridge_channel);
	}
	ast_channel_unlock(chan);
	bridge_channel->chan = chan;
	bridge_channel->swap = swap;
	bridge_channel->features = features;
	bridge_channel->inhibit_colp = !!(flags & AST_BRIDGE_IMPART_INHIBIT_JOIN_COLP);
	bridge_channel->depart_wait =
		(flags & AST_BRIDGE_IMPART_CHAN_MASK) == AST_BRIDGE_IMPART_CHAN_DEPARTABLE;
	bridge_channel->callid = ast_read_threadstorage_callid();

	/* Actually create the thread that will handle the channel */
	if (!res) {
		if ((flags & AST_BRIDGE_IMPART_CHAN_MASK) == AST_BRIDGE_IMPART_CHAN_INDEPENDENT) {
			res = ast_pthread_create_detached(&bridge_channel->thread, NULL,
				bridge_channel_ind_thread, bridge_channel);
		} else {
			res = ast_pthread_create(&bridge_channel->thread, NULL,
				bridge_channel_depart_thread, bridge_channel);
		}
	}

	if (res) {
		/* cleanup */
		ast_channel_lock(chan);
		ast_channel_internal_bridge_channel_set(chan, NULL);
		ast_channel_unlock(chan);
		bridge_channel->chan = NULL;
		bridge_channel->swap = NULL;
		ast_bridge_features_destroy(bridge_channel->features);
		bridge_channel->features = NULL;

		ao2_ref(bridge_channel, -1);
		return -1;
	}

	return 0;
}

int ast_bridge_depart(struct ast_channel *chan)
{
	struct ast_bridge_channel *bridge_channel;
	int departable;

	ast_channel_lock(chan);
	bridge_channel = ast_channel_internal_bridge_channel(chan);
	departable = bridge_channel && bridge_channel->depart_wait;
	ast_channel_unlock(chan);
	if (!departable) {
		ast_log(LOG_ERROR, "Channel %s cannot be departed.\n",
			ast_channel_name(chan));
		/*
		 * Should never happen.  It likely means that
		 * ast_bridge_depart() is called by two threads for the same
		 * channel, the channel was never imparted to be departed, or it
		 * has already been departed.
		 */
		ast_assert(0);
		return -1;
	}

	/*
	 * We are claiming the reference held by the depart bridge
	 * channel thread.
	 */

	ast_bridge_channel_leave_bridge(bridge_channel,
		BRIDGE_CHANNEL_STATE_END_NO_DISSOLVE, AST_CAUSE_NORMAL_CLEARING);

	/* Wait for the depart thread to die */
	ast_debug(1, "Waiting for %p(%s) bridge thread to die.\n",
		bridge_channel, ast_channel_name(bridge_channel->chan));
	pthread_join(bridge_channel->thread, NULL);

	ast_channel_lock(chan);
	ast_channel_internal_bridge_channel_set(chan, NULL);
	ast_channel_unlock(chan);

	/* We can get rid of the bridge_channel after the depart thread has died. */
	ao2_ref(bridge_channel, -1);
	return 0;
}

int ast_bridge_remove(struct ast_bridge *bridge, struct ast_channel *chan)
{
	struct ast_bridge_channel *bridge_channel;

	ast_bridge_lock(bridge);

	/* Try to find the channel that we want to remove */
	if (!(bridge_channel = bridge_find_channel(bridge, chan))) {
		ast_bridge_unlock(bridge);
		return -1;
	}

	ast_bridge_channel_leave_bridge(bridge_channel,
		BRIDGE_CHANNEL_STATE_END_NO_DISSOLVE, AST_CAUSE_NORMAL_CLEARING);

	ast_bridge_unlock(bridge);

	return 0;
}

static void kick_it(struct ast_bridge_channel *bridge_channel, const void *payload, size_t payload_size)
{
	ast_bridge_channel_kick(bridge_channel, AST_CAUSE_NORMAL_CLEARING);
}

int ast_bridge_kick(struct ast_bridge *bridge, struct ast_channel *chan)
{
	struct ast_bridge_channel *bridge_channel;
	int res;

	ast_bridge_lock(bridge);

	/* Try to find the channel that we want to kick. */
	if (!(bridge_channel = bridge_find_channel(bridge, chan))) {
		ast_bridge_unlock(bridge);
		return -1;
	}

	res = ast_bridge_channel_queue_callback(bridge_channel, 0, kick_it, NULL, 0);

	ast_bridge_unlock(bridge);

	return res;
}

/*!
 * \internal
 * \brief Point the bridge_channel to a new bridge.
 * \since 12.0.0
 *
 * \param bridge_channel What is to point to a new bridge.
 * \param new_bridge Where the bridge channel should point.
 *
 * \return Nothing
 */
static void bridge_channel_change_bridge(struct ast_bridge_channel *bridge_channel, struct ast_bridge *new_bridge)
{
	struct ast_bridge *old_bridge;

	ao2_ref(new_bridge, +1);
	ast_bridge_channel_lock(bridge_channel);
	ast_channel_lock(bridge_channel->chan);
	old_bridge = bridge_channel->bridge;
	bridge_channel->bridge = new_bridge;
	ast_channel_internal_bridge_set(bridge_channel->chan, new_bridge);
	ast_channel_unlock(bridge_channel->chan);
	ast_bridge_channel_unlock(bridge_channel);
	ao2_ref(old_bridge, -1);
}

static void bridge_channel_moving(struct ast_bridge_channel *bridge_channel, struct ast_bridge *src, struct ast_bridge *dst)
{
	struct ast_bridge_features *features = bridge_channel->features;
	struct ast_bridge_hook *hook;
	struct ao2_iterator iter;

	/* Run any moving hooks. */
	iter = ao2_iterator_init(features->other_hooks, 0);
	for (; (hook = ao2_iterator_next(&iter)); ao2_ref(hook, -1)) {
		int remove_me;
		ast_bridge_move_indicate_callback move_cb;

		if (hook->type != AST_BRIDGE_HOOK_TYPE_MOVE) {
			continue;
		}
		move_cb = (ast_bridge_move_indicate_callback) hook->callback;
		remove_me = move_cb(bridge_channel, hook->hook_pvt, src, dst);
		if (remove_me) {
			ast_debug(1, "Move detection hook %p is being removed from %p(%s)\n",
				hook, bridge_channel, ast_channel_name(bridge_channel->chan));
			ao2_unlink(features->other_hooks, hook);
		}
	}
	ao2_iterator_destroy(&iter);
}

void bridge_do_merge(struct ast_bridge *dst_bridge, struct ast_bridge *src_bridge, struct ast_bridge_channel **kick_me, unsigned int num_kick,
	unsigned int optimized)
{
	struct ast_bridge_channel *bridge_channel;
	unsigned int idx;

	ast_debug(1, "Merging bridge %s into bridge %s\n",
		src_bridge->uniqueid, dst_bridge->uniqueid);

	ast_bridge_publish_merge(dst_bridge, src_bridge);

	/*
	 * Move channels from src_bridge over to dst_bridge.
	 *
	 * We must use AST_LIST_TRAVERSE_SAFE_BEGIN() because
	 * bridge_channel_internal_pull() alters the list we are traversing.
	 */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&src_bridge->channels, bridge_channel, entry) {
		if (bridge_channel->state != BRIDGE_CHANNEL_STATE_WAIT) {
			/*
			 * The channel is already leaving let it leave normally because
			 * pulling it may delete hooks that should run for this channel.
			 */
			continue;
		}
		if (ast_test_flag(&bridge_channel->features->feature_flags,
			AST_BRIDGE_CHANNEL_FLAG_IMMOVABLE)) {
			continue;
		}

		if (kick_me) {
			for (idx = 0; idx < num_kick; ++idx) {
				if (bridge_channel == kick_me[idx]) {
					ast_bridge_channel_leave_bridge(bridge_channel,
						BRIDGE_CHANNEL_STATE_END_NO_DISSOLVE, AST_CAUSE_NORMAL_CLEARING);
					break;
				}
			}
		}
		bridge_channel_internal_pull(bridge_channel);
		if (bridge_channel->state != BRIDGE_CHANNEL_STATE_WAIT) {
			/*
			 * The channel died as a result of being pulled or it was
			 * kicked.  Leave it pointing to the original bridge.
			 */
			continue;
		}

		bridge_channel_moving(bridge_channel, bridge_channel->bridge, dst_bridge);

		/* Point to new bridge.*/
		bridge_channel_change_bridge(bridge_channel, dst_bridge);

		if (bridge_channel_internal_push(bridge_channel)) {
			ast_bridge_features_remove(bridge_channel->features,
				AST_BRIDGE_HOOK_REMOVE_ON_PULL);
			ast_bridge_channel_leave_bridge(bridge_channel,
				BRIDGE_CHANNEL_STATE_END_NO_DISSOLVE, bridge_channel->bridge->cause);
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	if (kick_me) {
		/*
		 * Now we can kick any channels in the dst_bridge without
		 * potentially dissolving the bridge.
		 */
		for (idx = 0; idx < num_kick; ++idx) {
			bridge_channel = kick_me[idx];
			ast_bridge_channel_lock(bridge_channel);
			if (bridge_channel->state == BRIDGE_CHANNEL_STATE_WAIT) {
				ast_bridge_channel_leave_bridge_nolock(bridge_channel,
					BRIDGE_CHANNEL_STATE_END_NO_DISSOLVE, AST_CAUSE_NORMAL_CLEARING);
				bridge_channel_internal_pull(bridge_channel);
			}
			ast_bridge_channel_unlock(bridge_channel);
		}
	}

	bridge_reconfigured(dst_bridge, !optimized);
	bridge_reconfigured(src_bridge, !optimized);

	ast_debug(1, "Merged bridge %s into bridge %s\n",
		src_bridge->uniqueid, dst_bridge->uniqueid);
}

struct merge_direction {
	/*! Destination merge bridge. */
	struct ast_bridge *dest;
	/*! Source merge bridge. */
	struct ast_bridge *src;
};

/*!
 * \internal
 * \brief Determine which bridge should merge into the other.
 * \since 12.0.0
 *
 * \param bridge1 A bridge for merging
 * \param bridge2 A bridge for merging
 *
 * \note The two bridges are assumed already locked.
 *
 * \return Which bridge merges into which or NULL bridges if cannot merge.
 */
static struct merge_direction bridge_merge_determine_direction(struct ast_bridge *bridge1, struct ast_bridge *bridge2)
{
	struct merge_direction merge = { NULL, NULL };
	int bridge1_priority;
	int bridge2_priority;

	if (!ast_test_flag(&bridge1->feature_flags,
			AST_BRIDGE_FLAG_MERGE_INHIBIT_TO | AST_BRIDGE_FLAG_MERGE_INHIBIT_FROM)
		&& !ast_test_flag(&bridge2->feature_flags,
			AST_BRIDGE_FLAG_MERGE_INHIBIT_TO | AST_BRIDGE_FLAG_MERGE_INHIBIT_FROM)) {
		/*
		 * Can merge either way.  Merge to the higher priority merge
		 * bridge.  Otherwise merge to the larger bridge.
		 */
		bridge1_priority = bridge1->v_table->get_merge_priority(bridge1);
		bridge2_priority = bridge2->v_table->get_merge_priority(bridge2);
		if (bridge2_priority < bridge1_priority) {
			merge.dest = bridge1;
			merge.src = bridge2;
		} else if (bridge1_priority < bridge2_priority) {
			merge.dest = bridge2;
			merge.src = bridge1;
		} else {
			/* Merge to the larger bridge. */
			if (bridge2->num_channels <= bridge1->num_channels) {
				merge.dest = bridge1;
				merge.src = bridge2;
			} else {
				merge.dest = bridge2;
				merge.src = bridge1;
			}
		}
	} else if (!ast_test_flag(&bridge1->feature_flags, AST_BRIDGE_FLAG_MERGE_INHIBIT_TO)
		&& !ast_test_flag(&bridge2->feature_flags, AST_BRIDGE_FLAG_MERGE_INHIBIT_FROM)) {
		/* Can merge only one way. */
		merge.dest = bridge1;
		merge.src = bridge2;
	} else if (!ast_test_flag(&bridge2->feature_flags, AST_BRIDGE_FLAG_MERGE_INHIBIT_TO)
		&& !ast_test_flag(&bridge1->feature_flags, AST_BRIDGE_FLAG_MERGE_INHIBIT_FROM)) {
		/* Can merge only one way. */
		merge.dest = bridge2;
		merge.src = bridge1;
	}

	return merge;
}

/*!
 * \internal
 * \brief Merge two bridges together
 * \since 12.0.0
 *
 * \param dst_bridge Destination bridge of merge.
 * \param src_bridge Source bridge of merge.
 * \param merge_best_direction TRUE if don't care about which bridge merges into the other.
 * \param kick_me Array of channels to kick from the bridges.
 * \param num_kick Number of channels in the kick_me array.
 *
 * \note The dst_bridge and src_bridge are assumed already locked.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
static int bridge_merge_locked(struct ast_bridge *dst_bridge, struct ast_bridge *src_bridge, int merge_best_direction, struct ast_channel **kick_me, unsigned int num_kick)
{
	struct merge_direction merge;
	struct ast_bridge_channel **kick_them = NULL;

	/* Sanity check. */
	ast_assert(dst_bridge && src_bridge && dst_bridge != src_bridge && (!num_kick || kick_me));

	if (dst_bridge->dissolved || src_bridge->dissolved) {
		ast_debug(1, "Can't merge bridges %s and %s, at least one bridge is dissolved.\n",
			src_bridge->uniqueid, dst_bridge->uniqueid);
		return -1;
	}
	if (ast_test_flag(&dst_bridge->feature_flags, AST_BRIDGE_FLAG_MASQUERADE_ONLY)
		|| ast_test_flag(&src_bridge->feature_flags, AST_BRIDGE_FLAG_MASQUERADE_ONLY)) {
		ast_debug(1, "Can't merge bridges %s and %s, masquerade only.\n",
			src_bridge->uniqueid, dst_bridge->uniqueid);
		return -1;
	}
	if (dst_bridge->inhibit_merge || src_bridge->inhibit_merge) {
		ast_debug(1, "Can't merge bridges %s and %s, merging temporarily inhibited.\n",
			src_bridge->uniqueid, dst_bridge->uniqueid);
		return -1;
	}

	if (merge_best_direction) {
		merge = bridge_merge_determine_direction(dst_bridge, src_bridge);
	} else {
		merge.dest = dst_bridge;
		merge.src = src_bridge;
	}

	if (!merge.dest
		|| ast_test_flag(&merge.dest->feature_flags, AST_BRIDGE_FLAG_MERGE_INHIBIT_TO)
		|| ast_test_flag(&merge.src->feature_flags, AST_BRIDGE_FLAG_MERGE_INHIBIT_FROM)) {
		ast_debug(1, "Can't merge bridges %s and %s, merging inhibited.\n",
			src_bridge->uniqueid, dst_bridge->uniqueid);
		return -1;
	}
	if (merge.src->num_channels < 2) {
		/*
		 * For a two party bridge, a channel may be temporarily removed
		 * from the source bridge or the initial bridge members have not
		 * joined yet.
		 */
		ast_debug(1, "Can't merge bridge %s into bridge %s, not enough channels in source bridge.\n",
			merge.src->uniqueid, merge.dest->uniqueid);
		return -1;
	}
	if (2 + num_kick < merge.dest->num_channels + merge.src->num_channels
		&& !(merge.dest->technology->capabilities & AST_BRIDGE_CAPABILITY_MULTIMIX)
		&& (!ast_test_flag(&merge.dest->feature_flags, AST_BRIDGE_FLAG_SMART)
			|| !(merge.dest->allowed_capabilities & AST_BRIDGE_CAPABILITY_MULTIMIX))) {
		ast_debug(1, "Can't merge bridge %s into bridge %s, multimix is needed and it cannot be acquired.\n",
			merge.src->uniqueid, merge.dest->uniqueid);
		return -1;
	}

	if (num_kick) {
		unsigned int num_to_kick = 0;
		unsigned int idx;

		kick_them = ast_alloca(num_kick * sizeof(*kick_them));
		for (idx = 0; idx < num_kick; ++idx) {
			kick_them[num_to_kick] = bridge_find_channel(merge.src, kick_me[idx]);
			if (!kick_them[num_to_kick]) {
				kick_them[num_to_kick] = bridge_find_channel(merge.dest, kick_me[idx]);
			}
			if (kick_them[num_to_kick]) {
				++num_to_kick;
			}
		}

		if (num_to_kick != num_kick) {
			ast_debug(1, "Can't merge bridge %s into bridge %s, at least one kicked channel is not in either bridge.\n",
				merge.src->uniqueid, merge.dest->uniqueid);
			return -1;
		}
	}

	bridge_do_merge(merge.dest, merge.src, kick_them, num_kick, 0);
	return 0;
}

int ast_bridge_merge(struct ast_bridge *dst_bridge, struct ast_bridge *src_bridge, int merge_best_direction, struct ast_channel **kick_me, unsigned int num_kick)
{
	int res;

	/* Sanity check. */
	ast_assert(dst_bridge && src_bridge);

	ast_bridge_lock_both(dst_bridge, src_bridge);
	res = bridge_merge_locked(dst_bridge, src_bridge, merge_best_direction, kick_me, num_kick);
	ast_bridge_unlock(src_bridge);
	ast_bridge_unlock(dst_bridge);
	return res;
}

int bridge_do_move(struct ast_bridge *dst_bridge, struct ast_bridge_channel *bridge_channel, int attempt_recovery,
	unsigned int optimized)
{
	struct ast_bridge *orig_bridge;
	int was_in_bridge;
	int res = 0;

	if (bridge_channel->swap) {
		ast_debug(1, "Moving %p(%s) into bridge %s swapping with %s\n",
			bridge_channel, ast_channel_name(bridge_channel->chan), dst_bridge->uniqueid,
			ast_channel_name(bridge_channel->swap));
	} else {
		ast_debug(1, "Moving %p(%s) into bridge %s\n",
			bridge_channel, ast_channel_name(bridge_channel->chan), dst_bridge->uniqueid);
	}

	orig_bridge = bridge_channel->bridge;
	was_in_bridge = bridge_channel->in_bridge;

	bridge_channel_internal_pull(bridge_channel);
	if (bridge_channel->state != BRIDGE_CHANNEL_STATE_WAIT) {
		/*
		 * The channel died as a result of being pulled.  Leave it
		 * pointing to the original bridge.
		 */
		bridge_reconfigured(orig_bridge, 0);
		return -1;
	}

	/* Point to new bridge.*/
	ao2_ref(orig_bridge, +1);/* Keep a ref in case the push fails. */
	bridge_channel_change_bridge(bridge_channel, dst_bridge);

	bridge_channel_moving(bridge_channel, orig_bridge, dst_bridge);

	if (bridge_channel_internal_push(bridge_channel)) {
		/* Try to put the channel back into the original bridge. */
		ast_bridge_features_remove(bridge_channel->features,
			AST_BRIDGE_HOOK_REMOVE_ON_PULL);
		if (attempt_recovery && was_in_bridge) {
			/* Point back to original bridge. */
			bridge_channel_change_bridge(bridge_channel, orig_bridge);

			if (bridge_channel_internal_push(bridge_channel)) {
				ast_bridge_features_remove(bridge_channel->features,
					AST_BRIDGE_HOOK_REMOVE_ON_PULL);
				ast_bridge_channel_leave_bridge(bridge_channel,
					BRIDGE_CHANNEL_STATE_END_NO_DISSOLVE, bridge_channel->bridge->cause);
				bridge_channel_settle_owed_events(orig_bridge, bridge_channel);
			}
		} else {
			ast_bridge_channel_leave_bridge(bridge_channel,
				BRIDGE_CHANNEL_STATE_END_NO_DISSOLVE, bridge_channel->bridge->cause);
			bridge_channel_settle_owed_events(orig_bridge, bridge_channel);
		}
		res = -1;
	} else {
		bridge_channel_settle_owed_events(orig_bridge, bridge_channel);
	}

	bridge_reconfigured(dst_bridge, !optimized);
	bridge_reconfigured(orig_bridge, !optimized);
	ao2_ref(orig_bridge, -1);
	return res;
}

/*!
 * \internal
 * \brief Move a channel from one bridge to another.
 * \since 12.0.0
 *
 * \param dst_bridge Destination bridge of bridge channel move.
 * \param src_bridge Source bridge of bridge channel move.
 * \param chan Channel to move.
 * \param swap Channel to replace in dst_bridge.
 * \param attempt_recovery TRUE if failure attempts to push channel back into original bridge.
 *
 * \note The dst_bridge and src_bridge are assumed already locked.
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 */
static int bridge_move_locked(struct ast_bridge *dst_bridge, struct ast_bridge *src_bridge, struct ast_channel *chan, struct ast_channel *swap, int attempt_recovery)
{
	struct ast_bridge_channel *bridge_channel;

	if (dst_bridge->dissolved || src_bridge->dissolved) {
		ast_debug(1, "Can't move channel %s from bridge %s into bridge %s, at least one bridge is dissolved.\n",
			ast_channel_name(chan), src_bridge->uniqueid, dst_bridge->uniqueid);
		return -1;
	}
	if (ast_test_flag(&dst_bridge->feature_flags, AST_BRIDGE_FLAG_MASQUERADE_ONLY)
		|| ast_test_flag(&src_bridge->feature_flags, AST_BRIDGE_FLAG_MASQUERADE_ONLY)) {
		ast_debug(1, "Can't move channel %s from bridge %s into bridge %s, masquerade only.\n",
			ast_channel_name(chan), src_bridge->uniqueid, dst_bridge->uniqueid);
		return -1;
	}
	if (dst_bridge->inhibit_merge || src_bridge->inhibit_merge) {
		ast_debug(1, "Can't move channel %s from bridge %s into bridge %s, temporarily inhibited.\n",
			ast_channel_name(chan), src_bridge->uniqueid, dst_bridge->uniqueid);
		return -1;
	}

	bridge_channel = bridge_find_channel(src_bridge, chan);
	if (!bridge_channel) {
		ast_debug(1, "Can't move channel %s from bridge %s into bridge %s, channel not in bridge.\n",
			ast_channel_name(chan), src_bridge->uniqueid, dst_bridge->uniqueid);
		return -1;
	}
	if (bridge_channel->state != BRIDGE_CHANNEL_STATE_WAIT) {
		ast_debug(1, "Can't move channel %s from bridge %s into bridge %s, channel leaving bridge.\n",
			ast_channel_name(chan), src_bridge->uniqueid, dst_bridge->uniqueid);
		return -1;
	}
	if (ast_test_flag(&bridge_channel->features->feature_flags,
		AST_BRIDGE_CHANNEL_FLAG_IMMOVABLE)) {
		ast_debug(1, "Can't move channel %s from bridge %s into bridge %s, channel immovable.\n",
			ast_channel_name(chan), src_bridge->uniqueid, dst_bridge->uniqueid);
		return -1;
	}

	if (swap) {
		struct ast_bridge_channel *bridge_channel_swap;

		bridge_channel_swap = bridge_find_channel(dst_bridge, swap);
		if (!bridge_channel_swap) {
			ast_debug(1, "Can't move channel %s from bridge %s into bridge %s, swap channel %s not in bridge.\n",
				ast_channel_name(chan), src_bridge->uniqueid, dst_bridge->uniqueid,
				ast_channel_name(swap));
			return -1;
		}
		if (bridge_channel_swap->state != BRIDGE_CHANNEL_STATE_WAIT) {
			ast_debug(1, "Can't move channel %s from bridge %s into bridge %s, swap channel %s leaving bridge.\n",
				ast_channel_name(chan), src_bridge->uniqueid, dst_bridge->uniqueid,
				ast_channel_name(swap));
			return -1;
		}
	}

	bridge_channel->swap = swap;
	return bridge_do_move(dst_bridge, bridge_channel, attempt_recovery, 0);
}

int ast_bridge_move(struct ast_bridge *dst_bridge, struct ast_bridge *src_bridge, struct ast_channel *chan, struct ast_channel *swap, int attempt_recovery)
{
	int res;

	ast_bridge_lock_both(dst_bridge, src_bridge);
	res = bridge_move_locked(dst_bridge, src_bridge, chan, swap, attempt_recovery);
	ast_bridge_unlock(src_bridge);
	ast_bridge_unlock(dst_bridge);
	return res;
}

int ast_bridge_add_channel(struct ast_bridge *bridge, struct ast_channel *chan,
	struct ast_bridge_features *features, int play_tone, const char *xfersound)
{
	RAII_VAR(struct ast_bridge *, chan_bridge, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel *, yanked_chan, NULL, ao2_cleanup);

	ast_channel_lock(chan);
	chan_bridge = ast_channel_get_bridge(chan);
	ast_channel_unlock(chan);

	if (chan_bridge) {
		struct ast_bridge_channel *bridge_channel;

		ast_bridge_lock_both(bridge, chan_bridge);
		bridge_channel = bridge_find_channel(chan_bridge, chan);

		if (bridge_move_locked(bridge, chan_bridge, chan, NULL, 1)) {
			ast_bridge_unlock(chan_bridge);
			ast_bridge_unlock(bridge);
			return -1;
		}

		/*
		 * bridge_move_locked() will implicitly ensure that
		 * bridge_channel is not NULL.
		 */
		ast_assert(bridge_channel != NULL);

		/*
		 * Additional checks if the channel we just stole dissolves the
		 * original bridge.
		 */
		bridge_dissolve_check_stolen(chan_bridge, bridge_channel);
		ast_bridge_unlock(chan_bridge);
		ast_bridge_unlock(bridge);

		/* The channel was in a bridge so it is not getting any new features. */
		ast_bridge_features_destroy(features);
	} else {
		/* Slightly less easy case. We need to yank channel A from
		 * where he currently is and impart him into our bridge.
		 */
		yanked_chan = ast_channel_yank(chan);
		if (!yanked_chan) {
			ast_log(LOG_WARNING, "Could not gain control of channel %s\n", ast_channel_name(chan));
			return -1;
		}
		if (ast_channel_state(yanked_chan) != AST_STATE_UP) {
			ast_answer(yanked_chan);
		}
		ast_channel_ref(yanked_chan);
		if (ast_bridge_impart(bridge, yanked_chan, NULL, features,
			AST_BRIDGE_IMPART_CHAN_INDEPENDENT)) {
			/* It is possible for us to yank a channel and have some other
			 * thread start a PBX on the channl after we yanked it. In particular,
			 * this can theoretically happen on the ;2 of a Local channel if we
			 * yank it prior to the ;1 being answered. Make sure that it isn't
			 * executing a PBX before hanging it up.
			 */
			if (ast_channel_pbx(yanked_chan)) {
				ast_channel_unref(yanked_chan);
			} else {
				ast_hangup(yanked_chan);
			}
			return -1;
		}
	}

	if (play_tone && !ast_strlen_zero(xfersound)) {
		struct ast_channel *play_chan = yanked_chan ?: chan;
		RAII_VAR(struct ast_bridge_channel *, play_bridge_channel, NULL, ao2_cleanup);

		ast_channel_lock(play_chan);
		play_bridge_channel = ast_channel_get_bridge_channel(play_chan);
		ast_channel_unlock(play_chan);

		if (!play_bridge_channel) {
			ast_log(LOG_WARNING, "Unable to play tone for channel %s. No longer in a bridge.\n",
				ast_channel_name(play_chan));
		} else {
			ast_bridge_channel_queue_playfile(play_bridge_channel, NULL, xfersound, NULL);
		}
	}
	return 0;
}

static int bridge_allows_optimization(struct ast_bridge *bridge)
{
	return !(bridge->inhibit_merge
		|| bridge->dissolved
		|| ast_test_flag(&bridge->feature_flags, AST_BRIDGE_FLAG_MASQUERADE_ONLY));
}

/*!
 * \internal
 * \brief Lock the unreal channel stack for chan and prequalify it.
 * \since 12.0.0
 *
 * \param chan Unreal channel writing a frame into the channel driver.
 *
 * \note It is assumed that chan is already locked.
 *
 * \retval bridge on success with bridge and bridge_channel locked.
 * \retval NULL if cannot do optimization now.
 */
static struct ast_bridge *optimize_lock_chan_stack(struct ast_channel *chan)
{
	struct ast_bridge *bridge;
	struct ast_bridge_channel *bridge_channel;

	if (!AST_LIST_EMPTY(ast_channel_readq(chan))) {
		return NULL;
	}
	if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_EMULATE_DTMF)) {
		return NULL;
	}
	if (ast_channel_has_audio_frame_or_monitor(chan)) {
		/* Channel has an active monitor, audiohook, or framehook. */
		return NULL;
	}
	bridge_channel = ast_channel_internal_bridge_channel(chan);
	if (!bridge_channel || ast_bridge_channel_trylock(bridge_channel)) {
		return NULL;
	}
	bridge = bridge_channel->bridge;
	if (bridge_channel->activity != BRIDGE_CHANNEL_THREAD_SIMPLE
		|| bridge_channel->state != BRIDGE_CHANNEL_STATE_WAIT
		|| ast_bridge_trylock(bridge)) {
		ast_bridge_channel_unlock(bridge_channel);
		return NULL;
	}
	if (!bridge_channel_internal_allows_optimization(bridge_channel) ||
			!bridge_allows_optimization(bridge)) {
		ast_bridge_unlock(bridge);
		ast_bridge_channel_unlock(bridge_channel);
		return NULL;
	}
	return bridge;
}

/*!
 * \internal
 * \brief Lock the unreal channel stack for peer and prequalify it.
 * \since 12.0.0
 *
 * \param peer Other unreal channel in the pair.
 *
 * \retval bridge on success with bridge, bridge_channel, and peer locked.
 * \retval NULL if cannot do optimization now.
 */
static struct ast_bridge *optimize_lock_peer_stack(struct ast_channel *peer)
{
	struct ast_bridge *bridge;
	struct ast_bridge_channel *bridge_channel;

	if (ast_channel_trylock(peer)) {
		return NULL;
	}
	if (!AST_LIST_EMPTY(ast_channel_readq(peer))) {
		ast_channel_unlock(peer);
		return NULL;
	}
	if (ast_test_flag(ast_channel_flags(peer), AST_FLAG_EMULATE_DTMF)) {
		ast_channel_unlock(peer);
		return NULL;
	}
	if (ast_channel_has_audio_frame_or_monitor(peer)) {
		/* Peer has an active monitor, audiohook, or framehook. */
		ast_channel_unlock(peer);
		return NULL;
	}
	bridge_channel = ast_channel_internal_bridge_channel(peer);
	if (!bridge_channel || ast_bridge_channel_trylock(bridge_channel)) {
		ast_channel_unlock(peer);
		return NULL;
	}
	bridge = bridge_channel->bridge;
	if (bridge_channel->activity != BRIDGE_CHANNEL_THREAD_IDLE
		|| bridge_channel->state != BRIDGE_CHANNEL_STATE_WAIT
		|| ast_bridge_trylock(bridge)) {
		ast_bridge_channel_unlock(bridge_channel);
		ast_channel_unlock(peer);
		return NULL;
	}
	if (!bridge_allows_optimization(bridge) ||
			!bridge_channel_internal_allows_optimization(bridge_channel)) {
		ast_bridge_unlock(bridge);
		ast_bridge_channel_unlock(bridge_channel);
		ast_channel_unlock(peer);
		return NULL;
	}
	return bridge;
}

/*!
 * \internal
 * \brief Indicates allowability of a swap optimization
 */
enum bridge_allow_swap {
	/*! Bridges cannot allow for a swap optimization to occur */
	SWAP_PROHIBITED,
	/*! Bridge swap optimization can occur into the chan_bridge */
	SWAP_TO_CHAN_BRIDGE,
	/*! Bridge swap optimization can occur into the peer_bridge */
	SWAP_TO_PEER_BRIDGE,
};

/*!
 * \internal
 * \brief Determine if two bridges allow for swap optimization to occur
 *
 * \param chan_bridge First bridge being tested
 * \param peer_bridge Second bridge being tested
 * \return Allowability of swap optimization
 */
static enum bridge_allow_swap bridges_allow_swap_optimization(struct ast_bridge *chan_bridge,
		struct ast_bridge *peer_bridge)
{
	int chan_priority;
	int peer_priority;

	if (!ast_test_flag(&chan_bridge->feature_flags,
			AST_BRIDGE_FLAG_SWAP_INHIBIT_TO | AST_BRIDGE_FLAG_SWAP_INHIBIT_FROM |
			AST_BRIDGE_FLAG_TRANSFER_BRIDGE_ONLY)
		&& !ast_test_flag(&peer_bridge->feature_flags,
			AST_BRIDGE_FLAG_SWAP_INHIBIT_TO | AST_BRIDGE_FLAG_SWAP_INHIBIT_FROM |
			AST_BRIDGE_FLAG_TRANSFER_BRIDGE_ONLY)) {
		/*
		 * Can swap either way.  Swap to the higher priority merge
		 * bridge.
		 */
		chan_priority = chan_bridge->v_table->get_merge_priority(chan_bridge);
		peer_priority = peer_bridge->v_table->get_merge_priority(peer_bridge);
		if (chan_bridge->num_channels == 2
			&& chan_priority <= peer_priority) {
			return SWAP_TO_PEER_BRIDGE;
		} else if (peer_bridge->num_channels == 2
			&& peer_priority <= chan_priority) {
			return SWAP_TO_CHAN_BRIDGE;
		}
	} else if (chan_bridge->num_channels == 2
		&& !ast_test_flag(&chan_bridge->feature_flags, AST_BRIDGE_FLAG_SWAP_INHIBIT_FROM | AST_BRIDGE_FLAG_TRANSFER_BRIDGE_ONLY)
		&& !ast_test_flag(&peer_bridge->feature_flags, AST_BRIDGE_FLAG_SWAP_INHIBIT_TO)) {
		/* Can swap optimize only one way. */
		return SWAP_TO_PEER_BRIDGE;
	} else if (peer_bridge->num_channels == 2
		&& !ast_test_flag(&peer_bridge->feature_flags, AST_BRIDGE_FLAG_SWAP_INHIBIT_FROM | AST_BRIDGE_FLAG_TRANSFER_BRIDGE_ONLY)
		&& !ast_test_flag(&chan_bridge->feature_flags, AST_BRIDGE_FLAG_SWAP_INHIBIT_TO)) {
		/* Can swap optimize only one way. */
		return SWAP_TO_CHAN_BRIDGE;
	}

	return SWAP_PROHIBITED;
}

/*!
 * \internal
 * \brief Check and attempt to swap optimize out the unreal channels.
 * \since 12.0.0
 *
 * \param chan_bridge
 * \param chan_bridge_channel
 * \param peer_bridge
 * \param peer_bridge_channel
 * \param pvt Unreal data containing callbacks to call if the optimization actually
 * happens
 *
 * \retval 1 if unreal channels failed to optimize out.
 * \retval 0 if unreal channels were not optimized out.
 * \retval -1 if unreal channels were optimized out.
 */
static int try_swap_optimize_out(struct ast_bridge *chan_bridge,
	struct ast_bridge_channel *chan_bridge_channel, struct ast_bridge *peer_bridge,
	struct ast_bridge_channel *peer_bridge_channel,
	struct ast_unreal_pvt *pvt)
{
	struct ast_bridge *dst_bridge;
	struct ast_bridge_channel *dst_bridge_channel;
	struct ast_bridge_channel *src_bridge_channel;
	struct ast_bridge_channel *other;
	int res = 1;

	switch (bridges_allow_swap_optimization(chan_bridge, peer_bridge)) {
	case SWAP_TO_CHAN_BRIDGE:
		dst_bridge = chan_bridge;
		dst_bridge_channel = chan_bridge_channel;
		src_bridge_channel = peer_bridge_channel;
		break;
	case SWAP_TO_PEER_BRIDGE:
		dst_bridge = peer_bridge;
		dst_bridge_channel = peer_bridge_channel;
		src_bridge_channel = chan_bridge_channel;
		break;
	case SWAP_PROHIBITED:
	default:
		return 0;
	}

	other = ast_bridge_channel_peer(src_bridge_channel);
	if (other && other->state == BRIDGE_CHANNEL_STATE_WAIT) {
		unsigned int id;

		if (ast_channel_trylock(other->chan)) {
			return 1;
		}

		id = ast_atomic_fetchadd_int((int *) &optimization_id, +1);

		ast_verb(4, "Move-swap optimizing %s <-- %s.\n",
			ast_channel_name(dst_bridge_channel->chan),
			ast_channel_name(other->chan));

		if (pvt && !ast_test_flag(pvt, AST_UNREAL_OPTIMIZE_BEGUN) && pvt->callbacks
				&& pvt->callbacks->optimization_started) {
			pvt->callbacks->optimization_started(pvt, other->chan,
					dst_bridge_channel->chan == pvt->owner ? AST_UNREAL_OWNER : AST_UNREAL_CHAN,
					id);
			ast_set_flag(pvt, AST_UNREAL_OPTIMIZE_BEGUN);
		}
		other->swap = dst_bridge_channel->chan;
		if (!bridge_do_move(dst_bridge, other, 1, 1)) {
			ast_bridge_channel_leave_bridge(src_bridge_channel,
				BRIDGE_CHANNEL_STATE_END_NO_DISSOLVE, AST_CAUSE_NORMAL_CLEARING);
			res = -1;
		}
		if (pvt && pvt->callbacks && pvt->callbacks->optimization_finished) {
			pvt->callbacks->optimization_finished(pvt, res == 1, id);
		}
		ast_channel_unlock(other->chan);
	}
	return res;
}

/*!
 * \internal
 * \brief Indicates allowability of a merge optimization
 */
enum bridge_allow_merge {
	/*! Bridge properties prohibit merge optimization */
	MERGE_PROHIBITED,
	/*! Merge optimization cannot occur because the source bridge has too few channels */
	MERGE_NOT_ENOUGH_CHANNELS,
	/*! Merge optimization cannot occur because multimix capability could not be requested */
	MERGE_NO_MULTIMIX,
	/*! Merge optimization allowed between bridges */
	MERGE_ALLOWED,
};

/*!
 * \internal
 * \brief Determines allowability of a merge optimization
 *
 * \note The merge output parameter is undefined if MERGE_PROHIBITED is returned. For success
 * and other failure returns, a merge direction was determined, and the parameter is safe to
 * access.
 *
 * \param chan_bridge First bridge being tested
 * \param peer_bridge Second bridge being tested
 * \param num_kick_channels The number of channels to remove from the bridges during merging
 * \param[out] merge Indicates the recommended direction for the bridge merge
 */
static enum bridge_allow_merge bridges_allow_merge_optimization(struct ast_bridge *chan_bridge,
		struct ast_bridge *peer_bridge, int num_kick_channels, struct merge_direction *merge)
{
	*merge = bridge_merge_determine_direction(chan_bridge, peer_bridge);
	if (!merge->dest) {
		return MERGE_PROHIBITED;
	}
	if (merge->src->num_channels < 2) {
		return MERGE_NOT_ENOUGH_CHANNELS;
	} else if ((2 + num_kick_channels) < merge->dest->num_channels + merge->src->num_channels
		&& !(merge->dest->technology->capabilities & AST_BRIDGE_CAPABILITY_MULTIMIX)
		&& (!ast_test_flag(&merge->dest->feature_flags, AST_BRIDGE_FLAG_SMART)
			|| !(merge->dest->allowed_capabilities & AST_BRIDGE_CAPABILITY_MULTIMIX))) {
		return MERGE_NO_MULTIMIX;
	}

	return MERGE_ALLOWED;
}

/*!
 * \internal
 * \brief Check and attempt to merge optimize out the unreal channels.
 * \since 12.0.0
 *
 * \param chan_bridge
 * \param chan_bridge_channel
 * \param peer_bridge
 * \param peer_bridge_channel
 * \param pvt Unreal data containing callbacks to call if the optimization actually
 * happens
 *
 * \retval 0 if unreal channels were not optimized out.
 * \retval -1 if unreal channels were optimized out.
 */
static int try_merge_optimize_out(struct ast_bridge *chan_bridge,
	struct ast_bridge_channel *chan_bridge_channel, struct ast_bridge *peer_bridge,
	struct ast_bridge_channel *peer_bridge_channel,
	struct ast_unreal_pvt *pvt)
{
	struct merge_direction merge;
	struct ast_bridge_channel *kick_me[] = {
		chan_bridge_channel,
		peer_bridge_channel,
	};
	unsigned int id;

	switch (bridges_allow_merge_optimization(chan_bridge, peer_bridge, ARRAY_LEN(kick_me), &merge)) {
	case MERGE_ALLOWED:
		break;
	case MERGE_PROHIBITED:
		return 0;
	case MERGE_NOT_ENOUGH_CHANNELS:
		ast_debug(4, "Can't optimize %s -- %s out, not enough channels in bridge %s.\n",
			ast_channel_name(chan_bridge_channel->chan),
			ast_channel_name(peer_bridge_channel->chan),
			merge.src->uniqueid);
		return 0;
	case MERGE_NO_MULTIMIX:
		ast_debug(4, "Can't optimize %s -- %s out, multimix is needed and it cannot be acquired.\n",
			ast_channel_name(chan_bridge_channel->chan),
			ast_channel_name(peer_bridge_channel->chan));
		return 0;
	}

	ast_verb(4, "Merge optimizing %s -- %s out.\n",
		ast_channel_name(chan_bridge_channel->chan),
		ast_channel_name(peer_bridge_channel->chan));

	id = ast_atomic_fetchadd_int((int *) &optimization_id, +1);

	if (pvt && !ast_test_flag(pvt, AST_UNREAL_OPTIMIZE_BEGUN) && pvt->callbacks
			&& pvt->callbacks->optimization_started) {
		pvt->callbacks->optimization_started(pvt, NULL,
				merge.dest == ast_channel_internal_bridge(pvt->owner) ? AST_UNREAL_OWNER : AST_UNREAL_CHAN,
				id);
		ast_set_flag(pvt, AST_UNREAL_OPTIMIZE_BEGUN);
	}
	bridge_do_merge(merge.dest, merge.src, kick_me, ARRAY_LEN(kick_me), 1);
	if (pvt && pvt->callbacks && pvt->callbacks->optimization_finished) {
		pvt->callbacks->optimization_finished(pvt, 1, id);
	}

	return -1;
}

int ast_bridge_unreal_optimize_out(struct ast_channel *chan, struct ast_channel *peer, struct ast_unreal_pvt *pvt)
{
	struct ast_bridge *chan_bridge;
	struct ast_bridge *peer_bridge;
	struct ast_bridge_channel *chan_bridge_channel;
	struct ast_bridge_channel *peer_bridge_channel;
	int res = 0;

	chan_bridge = optimize_lock_chan_stack(chan);
	if (!chan_bridge) {
		return res;
	}
	chan_bridge_channel = ast_channel_internal_bridge_channel(chan);

	peer_bridge = optimize_lock_peer_stack(peer);
	if (peer_bridge) {
		peer_bridge_channel = ast_channel_internal_bridge_channel(peer);

		res = try_swap_optimize_out(chan_bridge, chan_bridge_channel,
			peer_bridge, peer_bridge_channel, pvt);
		if (!res) {
			res = try_merge_optimize_out(chan_bridge, chan_bridge_channel,
				peer_bridge, peer_bridge_channel, pvt);
		} else if (0 < res) {
			res = 0;
		}

		/* Release peer locks. */
		ast_bridge_unlock(peer_bridge);
		ast_bridge_channel_unlock(peer_bridge_channel);
		ast_channel_unlock(peer);
	}

	/* Release chan locks. */
	ast_bridge_unlock(chan_bridge);
	ast_bridge_channel_unlock(chan_bridge_channel);

	return res;
}

enum ast_bridge_optimization ast_bridges_allow_optimization(struct ast_bridge *chan_bridge,
		struct ast_bridge *peer_bridge)
{
	struct merge_direction merge;

	if (!bridge_allows_optimization(chan_bridge) || !bridge_allows_optimization(peer_bridge)) {
		return AST_BRIDGE_OPTIMIZE_PROHIBITED;
	}

	switch (bridges_allow_swap_optimization(chan_bridge, peer_bridge)) {
	case SWAP_TO_CHAN_BRIDGE:
		return AST_BRIDGE_OPTIMIZE_SWAP_TO_CHAN_BRIDGE;
	case SWAP_TO_PEER_BRIDGE:
		return AST_BRIDGE_OPTIMIZE_SWAP_TO_PEER_BRIDGE;
	case SWAP_PROHIBITED:
	default:
		break;
	}

	/* Two channels will be kicked from the bridges, the unreal;1 and unreal;2 channels */
	if (bridges_allow_merge_optimization(chan_bridge, peer_bridge, 2, &merge) != MERGE_ALLOWED) {
		return AST_BRIDGE_OPTIMIZE_PROHIBITED;
	}

	if (merge.dest == chan_bridge) {
		return AST_BRIDGE_OPTIMIZE_MERGE_TO_CHAN_BRIDGE;
	} else {
		return AST_BRIDGE_OPTIMIZE_MERGE_TO_PEER_BRIDGE;
	}
}

/*!
 * \internal
 * \brief Adjust the bridge merge inhibit request count.
 * \since 12.0.0
 *
 * \param bridge What to operate on.
 * \param request Inhibit request increment.
 *     (Positive to add requests.  Negative to remove requests.)
 *
 * \note This function assumes bridge is locked.
 *
 * \return Nothing
 */
void bridge_merge_inhibit_nolock(struct ast_bridge *bridge, int request)
{
	int new_request;

	new_request = bridge->inhibit_merge + request;
	ast_assert(0 <= new_request);
	bridge->inhibit_merge = new_request;
}

void ast_bridge_merge_inhibit(struct ast_bridge *bridge, int request)
{
	ast_bridge_lock(bridge);
	bridge_merge_inhibit_nolock(bridge, request);
	ast_bridge_unlock(bridge);
}

int ast_bridge_suspend(struct ast_bridge *bridge, struct ast_channel *chan)
{
	struct ast_bridge_channel *bridge_channel;
/* XXX ASTERISK-21271 the case of a disolved bridge while channel is suspended is not handled. */
/* XXX ASTERISK-21271 suspend/unsuspend needs to be rethought. The caller must block until it has successfully suspended the channel for temporary control. */
/* XXX ASTERISK-21271 external suspend/unsuspend needs to be eliminated. The channel may be playing a file at the time and stealing it then is not good. */

	ast_bridge_lock(bridge);

	if (!(bridge_channel = bridge_find_channel(bridge, chan))) {
		ast_bridge_unlock(bridge);
		return -1;
	}

	bridge_channel_internal_suspend_nolock(bridge_channel);

	ast_bridge_unlock(bridge);

	return 0;
}

int ast_bridge_unsuspend(struct ast_bridge *bridge, struct ast_channel *chan)
{
	struct ast_bridge_channel *bridge_channel;
/* XXX ASTERISK-21271 the case of a disolved bridge while channel is suspended is not handled. */

	ast_bridge_lock(bridge);

	if (!(bridge_channel = bridge_find_channel(bridge, chan))) {
		ast_bridge_unlock(bridge);
		return -1;
	}

	bridge_channel_internal_unsuspend_nolock(bridge_channel);

	ast_bridge_unlock(bridge);

	return 0;
}

void ast_bridge_technology_suspend(struct ast_bridge_technology *technology)
{
	technology->suspended = 1;
}

void ast_bridge_technology_unsuspend(struct ast_bridge_technology *technology)
{
	/*
	 * XXX We may want the act of unsuspending a bridge technology
	 * to prod all existing bridges to see if they should start
	 * using it.
	 */
	technology->suspended = 0;
}

int ast_bridge_features_register(enum ast_bridge_builtin_feature feature, ast_bridge_hook_callback callback, const char *dtmf)
{
	if (ARRAY_LEN(builtin_features_handlers) <= feature
		|| builtin_features_handlers[feature]) {
		return -1;
	}

	if (!ast_strlen_zero(dtmf)) {
		ast_copy_string(builtin_features_dtmf[feature], dtmf, sizeof(builtin_features_dtmf[feature]));
	}

	builtin_features_handlers[feature] = callback;

	return 0;
}

int ast_bridge_features_unregister(enum ast_bridge_builtin_feature feature)
{
	if (ARRAY_LEN(builtin_features_handlers) <= feature
		|| !builtin_features_handlers[feature]) {
		return -1;
	}

	builtin_features_handlers[feature] = NULL;

	return 0;
}

int ast_bridge_features_do(enum ast_bridge_builtin_feature feature, struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	ast_bridge_hook_callback callback;

	if (ARRAY_LEN(builtin_features_handlers) <= feature) {
		return -1;
	}

	callback = builtin_features_handlers[feature];
	if (!callback) {
		return -1;
	}
	callback(bridge_channel, hook_pvt);

	return 0;
}

int ast_bridge_interval_register(enum ast_bridge_builtin_interval interval, ast_bridge_builtin_set_limits_fn callback)
{
	if (ARRAY_LEN(builtin_interval_handlers) <= interval
		|| builtin_interval_handlers[interval]) {
		return -1;
	}

	builtin_interval_handlers[interval] = callback;

	return 0;
}

int ast_bridge_interval_unregister(enum ast_bridge_builtin_interval interval)
{
	if (ARRAY_LEN(builtin_interval_handlers) <= interval
		|| !builtin_interval_handlers[interval]) {
		return -1;
	}

	builtin_interval_handlers[interval] = NULL;

	return 0;

}

/*!
 * \internal
 * \brief Bridge hook destructor.
 * \since 12.0.0
 *
 * \param vhook Object to destroy.
 *
 * \return Nothing
 */
static void bridge_hook_destroy(void *vhook)
{
	struct ast_bridge_hook *hook = vhook;

	if (hook->destructor) {
		hook->destructor(hook->hook_pvt);
	}
}

/*!
 * \internal
 * \brief Allocate and setup a generic bridge hook.
 * \since 12.0.0
 *
 * \param size How big an object to allocate.
 * \param callback Function to execute upon activation
 * \param hook_pvt Unique data
 * \param destructor Optional destructor callback for hook_pvt data
 * \param remove_flags Dictates what situations the hook should be removed.
 *
 * \retval hook on success.
 * \retval NULL on error.
 */
static struct ast_bridge_hook *bridge_hook_generic(size_t size,
	ast_bridge_hook_callback callback,
	void *hook_pvt,
	ast_bridge_hook_pvt_destructor destructor,
	enum ast_bridge_hook_remove_flags remove_flags)
{
	struct ast_bridge_hook *hook;

	/* Allocate new hook and setup it's basic variables */
	hook = ao2_alloc_options(size, bridge_hook_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (hook) {
		hook->callback = callback;
		hook->destructor = destructor;
		hook->hook_pvt = hook_pvt;
		ast_set_flag(&hook->remove_flags, remove_flags);
	}

	return hook;
}

int ast_bridge_dtmf_hook(struct ast_bridge_features *features,
	const char *dtmf,
	ast_bridge_hook_callback callback,
	void *hook_pvt,
	ast_bridge_hook_pvt_destructor destructor,
	enum ast_bridge_hook_remove_flags remove_flags)
{
	struct ast_bridge_hook_dtmf *hook;
	int res;

	/* Allocate new hook and setup it's various variables */
	hook = (struct ast_bridge_hook_dtmf *) bridge_hook_generic(sizeof(*hook), callback,
		hook_pvt, destructor, remove_flags);
	if (!hook) {
		return -1;
	}
	hook->generic.type = AST_BRIDGE_HOOK_TYPE_DTMF;
	ast_copy_string(hook->dtmf.code, dtmf, sizeof(hook->dtmf.code));

	/* Once done we put it in the container. */
	res = ao2_link(features->dtmf_hooks, hook) ? 0 : -1;
	if (res) {
		/*
		 * Could not link the hook into the container.
		 *
		 * Remove the hook_pvt destructor call from the hook since we
		 * are returning failure to install the hook.
		 */
		hook->generic.destructor = NULL;
	}
	ao2_ref(hook, -1);

	return res;
}

/*!
 * \internal
 * \brief Attach an other hook to a bridge features structure
 *
 * \param features Bridge features structure
 * \param callback Function to execute upon activation
 * \param hook_pvt Unique data
 * \param destructor Optional destructor callback for hook_pvt data
 * \param remove_flags Dictates what situations the hook should be removed.
 * \param type What type of hook is being attached.
 *
 * \retval 0 on success
 * \retval -1 on failure (The caller must cleanup any hook_pvt resources.)
 */
static int bridge_other_hook(struct ast_bridge_features *features,
	ast_bridge_hook_callback callback,
	void *hook_pvt,
	ast_bridge_hook_pvt_destructor destructor,
	enum ast_bridge_hook_remove_flags remove_flags,
	enum ast_bridge_hook_type type)
{
	struct ast_bridge_hook *hook;
	int res;

	/* Allocate new hook and setup it's various variables */
	hook = bridge_hook_generic(sizeof(*hook), callback, hook_pvt, destructor,
		remove_flags);
	if (!hook) {
		return -1;
	}
	hook->type = type;

	/* Once done we put it in the container. */
	res = ao2_link(features->other_hooks, hook) ? 0 : -1;
	if (res) {
		/*
		 * Could not link the hook into the container.
		 *
		 * Remove the hook_pvt destructor call from the hook since we
		 * are returning failure to install the hook.
		 */
		hook->destructor = NULL;
	}
	ao2_ref(hook, -1);

	return res;
}

int ast_bridge_hangup_hook(struct ast_bridge_features *features,
	ast_bridge_hook_callback callback,
	void *hook_pvt,
	ast_bridge_hook_pvt_destructor destructor,
	enum ast_bridge_hook_remove_flags remove_flags)
{
	return bridge_other_hook(features, callback, hook_pvt, destructor, remove_flags,
		AST_BRIDGE_HOOK_TYPE_HANGUP);
}

int ast_bridge_join_hook(struct ast_bridge_features *features,
	ast_bridge_hook_callback callback,
	void *hook_pvt,
	ast_bridge_hook_pvt_destructor destructor,
	enum ast_bridge_hook_remove_flags remove_flags)
{
	return bridge_other_hook(features, callback, hook_pvt, destructor, remove_flags,
		AST_BRIDGE_HOOK_TYPE_JOIN);
}

int ast_bridge_leave_hook(struct ast_bridge_features *features,
	ast_bridge_hook_callback callback,
	void *hook_pvt,
	ast_bridge_hook_pvt_destructor destructor,
	enum ast_bridge_hook_remove_flags remove_flags)
{
	return bridge_other_hook(features, callback, hook_pvt, destructor, remove_flags,
		AST_BRIDGE_HOOK_TYPE_LEAVE);
}

int ast_bridge_talk_detector_hook(struct ast_bridge_features *features,
	ast_bridge_talking_indicate_callback callback,
	void *hook_pvt,
	ast_bridge_hook_pvt_destructor destructor,
	enum ast_bridge_hook_remove_flags remove_flags)
{
	ast_bridge_hook_callback hook_cb = (ast_bridge_hook_callback) callback;

	return bridge_other_hook(features, hook_cb, hook_pvt, destructor, remove_flags,
		AST_BRIDGE_HOOK_TYPE_TALK);
}

int ast_bridge_move_hook(struct ast_bridge_features *features,
	ast_bridge_move_indicate_callback callback,
	void *hook_pvt,
	ast_bridge_hook_pvt_destructor destructor,
	enum ast_bridge_hook_remove_flags remove_flags)
{
	ast_bridge_hook_callback hook_cb = (ast_bridge_hook_callback) callback;

	return bridge_other_hook(features, hook_cb, hook_pvt, destructor, remove_flags,
		AST_BRIDGE_HOOK_TYPE_MOVE);
}

int ast_bridge_interval_hook(struct ast_bridge_features *features,
	enum ast_bridge_hook_timer_option flags,
	unsigned int interval,
	ast_bridge_hook_callback callback,
	void *hook_pvt,
	ast_bridge_hook_pvt_destructor destructor,
	enum ast_bridge_hook_remove_flags remove_flags)
{
	struct ast_bridge_hook_timer *hook;
	int res;

	if (!features ||!interval || !callback) {
		return -1;
	}

	/* Allocate new hook and setup it's various variables */
	hook = (struct ast_bridge_hook_timer *) bridge_hook_generic(sizeof(*hook), callback,
		hook_pvt, destructor, remove_flags);
	if (!hook) {
		return -1;
	}
	hook->generic.type = AST_BRIDGE_HOOK_TYPE_TIMER;
	hook->timer.interval = interval;
	hook->timer.trip_time = ast_tvadd(ast_tvnow(), ast_samp2tv(interval, 1000));
	hook->timer.seqno = ast_atomic_fetchadd_int((int *) &features->interval_sequence, +1);
	hook->timer.flags = flags;

	ast_debug(1, "Putting interval hook %p with interval %u in the heap on features %p\n",
		hook, hook->timer.interval, features);
	ast_heap_wrlock(features->interval_hooks);
	res = ast_heap_push(features->interval_hooks, hook);
	ast_heap_unlock(features->interval_hooks);
	if (res) {
		/*
		 * Could not push the hook into the heap
		 *
		 * Remove the hook_pvt destructor call from the hook since we
		 * are returning failure to install the hook.
		 */
		hook->generic.destructor = NULL;
		ao2_ref(hook, -1);
	}

	return res ? -1 : 0;
}

int ast_bridge_features_enable(struct ast_bridge_features *features,
	enum ast_bridge_builtin_feature feature,
	const char *dtmf,
	void *config,
	ast_bridge_hook_pvt_destructor destructor,
	enum ast_bridge_hook_remove_flags remove_flags)
{
	if (ARRAY_LEN(builtin_features_handlers) <= feature
		|| !builtin_features_handlers[feature]) {
		return -1;
	}

	/* If no alternate DTMF stream was provided use the default one */
	if (ast_strlen_zero(dtmf)) {
		dtmf = builtin_features_dtmf[feature];
		/* If no DTMF is still available (ie: it has been disabled) then error out now */
		if (ast_strlen_zero(dtmf)) {
			ast_debug(1, "Failed to enable built in feature %u on %p, no DTMF string is available for it.\n",
				feature, features);
			return -1;
		}
	}

	/*
	 * The rest is basically pretty easy.  We create another hook
	 * using the built in feature's DTMF callback.  Easy as pie.
	 */
	return ast_bridge_dtmf_hook(features, dtmf, builtin_features_handlers[feature],
		config, destructor, remove_flags);
}

int ast_bridge_features_limits_construct(struct ast_bridge_features_limits *limits)
{
	memset(limits, 0, sizeof(*limits));

	if (ast_string_field_init(limits, 256)) {
		return -1;
	}

	return 0;
}

void ast_bridge_features_limits_destroy(struct ast_bridge_features_limits *limits)
{
	ast_string_field_free_memory(limits);
}

int ast_bridge_features_set_limits(struct ast_bridge_features *features,
	struct ast_bridge_features_limits *limits,
	enum ast_bridge_hook_remove_flags remove_flags)
{
	if (builtin_interval_handlers[AST_BRIDGE_BUILTIN_INTERVAL_LIMITS]) {
		ast_bridge_builtin_set_limits_fn callback;

		callback = builtin_interval_handlers[AST_BRIDGE_BUILTIN_INTERVAL_LIMITS];
		return callback(features, limits, remove_flags);
	}

	ast_log(LOG_ERROR, "Attempted to set limits without an AST_BRIDGE_BUILTIN_INTERVAL_LIMITS callback registered.\n");
	return -1;
}

void ast_bridge_features_set_flag(struct ast_bridge_features *features, unsigned int flag)
{
	ast_set_flag(&features->feature_flags, flag);
	features->usable = 1;
}

/*!
 * \internal
 * \brief ao2 object match hooks with appropriate remove_flags.
 * \since 12.0.0
 *
 * \param obj Feature hook object.
 * \param arg Removal flags
 * \param flags Not used
 *
 * \retval CMP_MATCH if hook's remove_flags match the removal flags set.
 * \retval 0 if not match.
 */
static int hook_remove_match(void *obj, void *arg, int flags)
{
	struct ast_bridge_hook *hook = obj;
	enum ast_bridge_hook_remove_flags *remove_flags = arg;

	if (ast_test_flag(&hook->remove_flags, *remove_flags)) {
		return CMP_MATCH;
	} else {
		return 0;
	}
}

/*!
 * \internal
 * \brief Remove all hooks with appropriate remove_flags in the container.
 * \since 12.0.0
 *
 * \param hooks Hooks container to work on.
 * \param remove_flags Determinator for whether hook is removed
 *
 * \return Nothing
 */
static void hooks_remove_container(struct ao2_container *hooks, enum ast_bridge_hook_remove_flags remove_flags)
{
	ao2_callback(hooks, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE,
		hook_remove_match, &remove_flags);
}

/*!
 * \internal
 * \brief Remove all hooks in the heap with appropriate remove_flags set.
 * \since 12.0.0
 *
 * \param hooks Hooks heap to work on.
 * \param remove_flags Determinator for whether hook is removed
 *
 * \return Nothing
 */
static void hooks_remove_heap(struct ast_heap *hooks, enum ast_bridge_hook_remove_flags remove_flags)
{
	struct ast_bridge_hook *hook;
	int changed;

	ast_heap_wrlock(hooks);
	do {
		int idx;

		changed = 0;
		for (idx = ast_heap_size(hooks); idx; --idx) {
			hook = ast_heap_peek(hooks, idx);
			if (ast_test_flag(&hook->remove_flags, remove_flags)) {
				ast_heap_remove(hooks, hook);
				ao2_ref(hook, -1);
				changed = 1;
			}
		}
	} while (changed);
	ast_heap_unlock(hooks);
}

void ast_bridge_features_remove(struct ast_bridge_features *features, enum ast_bridge_hook_remove_flags remove_flags)
{
	hooks_remove_container(features->dtmf_hooks, remove_flags);
	hooks_remove_container(features->other_hooks, remove_flags);
	hooks_remove_heap(features->interval_hooks, remove_flags);
}

static int interval_hook_time_cmp(void *a, void *b)
{
	struct ast_bridge_hook_timer *hook_a = a;
	struct ast_bridge_hook_timer *hook_b = b;
	int cmp;

	cmp = ast_tvcmp(hook_b->timer.trip_time, hook_a->timer.trip_time);
	if (cmp) {
		return cmp;
	}

	cmp = hook_b->timer.seqno - hook_a->timer.seqno;
	return cmp;
}

/*!
 * \internal
 * \brief DTMF hook container sort comparison function.
 * \since 12.0.0
 *
 * \param obj_left pointer to the (user-defined part) of an object.
 * \param obj_right pointer to the (user-defined part) of an object.
 * \param flags flags from ao2_callback()
 *   OBJ_POINTER - if set, 'obj_right', is an object.
 *   OBJ_KEY - if set, 'obj_right', is a search key item that is not an object.
 *   OBJ_PARTIAL_KEY - if set, 'obj_right', is a partial search key item that is not an object.
 *
 * \retval <0 if obj_left < obj_right
 * \retval =0 if obj_left == obj_right
 * \retval >0 if obj_left > obj_right
 */
static int bridge_dtmf_hook_sort(const void *obj_left, const void *obj_right, int flags)
{
	const struct ast_bridge_hook_dtmf *hook_left = obj_left;
	const struct ast_bridge_hook_dtmf *hook_right = obj_right;
	const char *right_key = obj_right;
	int cmp;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	default:
	case OBJ_POINTER:
		right_key = hook_right->dtmf.code;
		/* Fall through */
	case OBJ_KEY:
		cmp = strcasecmp(hook_left->dtmf.code, right_key);
		break;
	case OBJ_PARTIAL_KEY:
		cmp = strncasecmp(hook_left->dtmf.code, right_key, strlen(right_key));
		break;
	}
	return cmp;
}

/*! \brief Callback for merging hook ao2_containers */
static int merge_container_cb(void *obj, void *data, int flags)
{
	ao2_link(data, obj);
	return 0;
}

/*! \brief Wrapper for interval hooks that calls into the wrapped hook */
static int interval_wrapper_cb(struct ast_bridge_channel *bridge_channel, void *obj)
{
	struct ast_bridge_hook_timer *hook = obj;

	return hook->generic.callback(bridge_channel, hook->generic.hook_pvt);
}

/*! \brief Destructor for the hook wrapper */
static void interval_wrapper_pvt_dtor(void *obj)
{
	ao2_cleanup(obj);
}

/*! \brief Wrap the provided interval hook and add it to features */
static void wrap_hook(struct ast_bridge_features *features, struct ast_bridge_hook_timer *hook)
{
	/* Break out of the current wrapper if it exists to avoid multiple layers */
	if (hook->generic.callback == interval_wrapper_cb) {
		hook = hook->generic.hook_pvt;
	}

	ast_bridge_interval_hook(features, hook->timer.flags, hook->timer.interval,
		interval_wrapper_cb, ao2_bump(hook), interval_wrapper_pvt_dtor,
		hook->generic.remove_flags.flags);
}

void ast_bridge_features_merge(struct ast_bridge_features *into, const struct ast_bridge_features *from)
{
	struct ast_bridge_hook_timer *hook;
	int idx;

	/* Merge hook containers */
	ao2_callback(from->dtmf_hooks, 0, merge_container_cb, into->dtmf_hooks);
	ao2_callback(from->other_hooks, 0, merge_container_cb, into->other_hooks);

	/* Merge hook heaps */
	ast_heap_wrlock(from->interval_hooks);
	for (idx = 1; (hook = ast_heap_peek(from->interval_hooks, idx)); idx++) {
		wrap_hook(into, hook);
	}
	ast_heap_unlock(from->interval_hooks);

	/* Merge feature flags */
	into->feature_flags.flags |= from->feature_flags.flags;
	into->usable |= from->usable;

	into->mute |= from->mute;
	into->dtmf_passthrough |= from->dtmf_passthrough;
}

/* XXX ASTERISK-21271 make ast_bridge_features_init() static when make ast_bridge_join() requires features to be allocated. */
int ast_bridge_features_init(struct ast_bridge_features *features)
{
	/* Zero out the structure */
	memset(features, 0, sizeof(*features));

	/* Initialize the DTMF hooks container */
	features->dtmf_hooks = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX,
		AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE, bridge_dtmf_hook_sort, NULL);
	if (!features->dtmf_hooks) {
		return -1;
	}

	/* Initialize the miscellaneous other hooks container */
	features->other_hooks = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL,
		NULL);
	if (!features->other_hooks) {
		return -1;
	}

	/* Initialize the interval hooks heap */
	features->interval_hooks = ast_heap_create(8, interval_hook_time_cmp,
		offsetof(struct ast_bridge_hook_timer, timer.heap_index));
	if (!features->interval_hooks) {
		return -1;
	}

	features->dtmf_passthrough = 1;

	return 0;
}

/* XXX ASTERISK-21271 make ast_bridge_features_cleanup() static when make ast_bridge_join() requires features to be allocated. */
void ast_bridge_features_cleanup(struct ast_bridge_features *features)
{
	struct ast_bridge_hook_timer *hook;

	/* Destroy the interval hooks heap. */
	if (features->interval_hooks) {
		while ((hook = ast_heap_pop(features->interval_hooks))) {
			ao2_ref(hook, -1);
		}
		features->interval_hooks = ast_heap_destroy(features->interval_hooks);
	}

	/* Destroy the miscellaneous other hooks container. */
	ao2_cleanup(features->other_hooks);
	features->other_hooks = NULL;

	/* Destroy the DTMF hooks container. */
	ao2_cleanup(features->dtmf_hooks);
	features->dtmf_hooks = NULL;
}

void ast_bridge_features_destroy(struct ast_bridge_features *features)
{
	if (!features) {
		return;
	}
	ast_bridge_features_cleanup(features);
	ast_free(features);
}

struct ast_bridge_features *ast_bridge_features_new(void)
{
	struct ast_bridge_features *features;

	features = ast_malloc(sizeof(*features));
	if (features) {
		if (ast_bridge_features_init(features)) {
			ast_bridge_features_destroy(features);
			features = NULL;
		}
	}

	return features;
}

void ast_bridge_set_mixing_interval(struct ast_bridge *bridge, unsigned int mixing_interval)
{
	ast_bridge_lock(bridge);
	bridge->softmix.internal_mixing_interval = mixing_interval;
	ast_bridge_unlock(bridge);
}

void ast_bridge_set_internal_sample_rate(struct ast_bridge *bridge, unsigned int sample_rate)
{
	ast_bridge_lock(bridge);
	bridge->softmix.internal_sample_rate = sample_rate;
	ast_bridge_unlock(bridge);
}

static void cleanup_video_mode(struct ast_bridge *bridge)
{
	switch (bridge->softmix.video_mode.mode) {
	case AST_BRIDGE_VIDEO_MODE_NONE:
		break;
	case AST_BRIDGE_VIDEO_MODE_SINGLE_SRC:
		if (bridge->softmix.video_mode.mode_data.single_src_data.chan_vsrc) {
			ast_channel_unref(bridge->softmix.video_mode.mode_data.single_src_data.chan_vsrc);
		}
		break;
	case AST_BRIDGE_VIDEO_MODE_TALKER_SRC:
		if (bridge->softmix.video_mode.mode_data.talker_src_data.chan_vsrc) {
			ast_channel_unref(bridge->softmix.video_mode.mode_data.talker_src_data.chan_vsrc);
		}
		if (bridge->softmix.video_mode.mode_data.talker_src_data.chan_old_vsrc) {
			ast_channel_unref(bridge->softmix.video_mode.mode_data.talker_src_data.chan_old_vsrc);
		}
	}
	memset(&bridge->softmix.video_mode, 0, sizeof(bridge->softmix.video_mode));
}

void ast_bridge_set_single_src_video_mode(struct ast_bridge *bridge, struct ast_channel *video_src_chan)
{
	ast_bridge_lock(bridge);
	cleanup_video_mode(bridge);
	bridge->softmix.video_mode.mode = AST_BRIDGE_VIDEO_MODE_SINGLE_SRC;
	bridge->softmix.video_mode.mode_data.single_src_data.chan_vsrc = ast_channel_ref(video_src_chan);
	ast_test_suite_event_notify("BRIDGE_VIDEO_MODE", "Message: video mode set to single source\r\nVideo Mode: %u\r\nVideo Channel: %s",
		bridge->softmix.video_mode.mode, ast_channel_name(video_src_chan));
	ast_indicate(video_src_chan, AST_CONTROL_VIDUPDATE);
	ast_bridge_unlock(bridge);
}

void ast_bridge_set_talker_src_video_mode(struct ast_bridge *bridge)
{
	ast_bridge_lock(bridge);
	cleanup_video_mode(bridge);
	bridge->softmix.video_mode.mode = AST_BRIDGE_VIDEO_MODE_TALKER_SRC;
	ast_test_suite_event_notify("BRIDGE_VIDEO_MODE", "Message: video mode set to talker source\r\nVideo Mode: %u",
		bridge->softmix.video_mode.mode);
	ast_bridge_unlock(bridge);
}

void ast_bridge_update_talker_src_video_mode(struct ast_bridge *bridge, struct ast_channel *chan, int talker_energy, int is_keyframe)
{
	struct ast_bridge_video_talker_src_data *data;

	/* If the channel doesn't support video, we don't care about it */
	if (!ast_format_cap_has_type(ast_channel_nativeformats(chan), AST_MEDIA_TYPE_VIDEO)) {
		return;
	}

	ast_bridge_lock(bridge);
	data = &bridge->softmix.video_mode.mode_data.talker_src_data;

	if (data->chan_vsrc == chan) {
		data->average_talking_energy = talker_energy;
	} else if ((data->average_talking_energy < talker_energy) && is_keyframe) {
		if (data->chan_old_vsrc) {
			ast_channel_unref(data->chan_old_vsrc);
		}
		if (data->chan_vsrc) {
			data->chan_old_vsrc = data->chan_vsrc;
			ast_indicate(data->chan_old_vsrc, AST_CONTROL_VIDUPDATE);
		}
		data->chan_vsrc = ast_channel_ref(chan);
		data->average_talking_energy = talker_energy;
		ast_test_suite_event_notify("BRIDGE_VIDEO_SRC", "Message: video source updated\r\nVideo Channel: %s", ast_channel_name(data->chan_vsrc));
		ast_indicate(data->chan_vsrc, AST_CONTROL_VIDUPDATE);
	} else if ((data->average_talking_energy < talker_energy) && !is_keyframe) {
		ast_indicate(chan, AST_CONTROL_VIDUPDATE);
	} else if (!data->chan_vsrc && is_keyframe) {
		data->chan_vsrc = ast_channel_ref(chan);
		data->average_talking_energy = talker_energy;
		ast_test_suite_event_notify("BRIDGE_VIDEO_SRC", "Message: video source updated\r\nVideo Channel: %s", ast_channel_name(data->chan_vsrc));
		ast_indicate(chan, AST_CONTROL_VIDUPDATE);
	} else if (!data->chan_old_vsrc && is_keyframe) {
		data->chan_old_vsrc = ast_channel_ref(chan);
		ast_indicate(chan, AST_CONTROL_VIDUPDATE);
	}
	ast_bridge_unlock(bridge);
}

int ast_bridge_number_video_src(struct ast_bridge *bridge)
{
	int res = 0;

	ast_bridge_lock(bridge);
	switch (bridge->softmix.video_mode.mode) {
	case AST_BRIDGE_VIDEO_MODE_NONE:
		break;
	case AST_BRIDGE_VIDEO_MODE_SINGLE_SRC:
		if (bridge->softmix.video_mode.mode_data.single_src_data.chan_vsrc) {
			res = 1;
		}
		break;
	case AST_BRIDGE_VIDEO_MODE_TALKER_SRC:
		if (bridge->softmix.video_mode.mode_data.talker_src_data.chan_vsrc) {
			res++;
		}
		if (bridge->softmix.video_mode.mode_data.talker_src_data.chan_old_vsrc) {
			res++;
		}
	}
	ast_bridge_unlock(bridge);
	return res;
}

int ast_bridge_is_video_src(struct ast_bridge *bridge, struct ast_channel *chan)
{
	int res = 0;

	ast_bridge_lock(bridge);
	switch (bridge->softmix.video_mode.mode) {
	case AST_BRIDGE_VIDEO_MODE_NONE:
		break;
	case AST_BRIDGE_VIDEO_MODE_SINGLE_SRC:
		if (bridge->softmix.video_mode.mode_data.single_src_data.chan_vsrc == chan) {
			res = 1;
		}
		break;
	case AST_BRIDGE_VIDEO_MODE_TALKER_SRC:
		if (bridge->softmix.video_mode.mode_data.talker_src_data.chan_vsrc == chan) {
			res = 1;
		} else if (bridge->softmix.video_mode.mode_data.talker_src_data.chan_old_vsrc == chan) {
			res = 2;
		}

	}
	ast_bridge_unlock(bridge);
	return res;
}

void ast_bridge_remove_video_src(struct ast_bridge *bridge, struct ast_channel *chan)
{
	ast_bridge_lock(bridge);
	switch (bridge->softmix.video_mode.mode) {
	case AST_BRIDGE_VIDEO_MODE_NONE:
		break;
	case AST_BRIDGE_VIDEO_MODE_SINGLE_SRC:
		if (bridge->softmix.video_mode.mode_data.single_src_data.chan_vsrc == chan) {
			if (bridge->softmix.video_mode.mode_data.single_src_data.chan_vsrc) {
				ast_channel_unref(bridge->softmix.video_mode.mode_data.single_src_data.chan_vsrc);
			}
			bridge->softmix.video_mode.mode_data.single_src_data.chan_vsrc = NULL;
		}
		break;
	case AST_BRIDGE_VIDEO_MODE_TALKER_SRC:
		if (bridge->softmix.video_mode.mode_data.talker_src_data.chan_vsrc == chan) {
			if (bridge->softmix.video_mode.mode_data.talker_src_data.chan_vsrc) {
				ast_channel_unref(bridge->softmix.video_mode.mode_data.talker_src_data.chan_vsrc);
			}
			bridge->softmix.video_mode.mode_data.talker_src_data.chan_vsrc = NULL;
			bridge->softmix.video_mode.mode_data.talker_src_data.average_talking_energy = 0;
		}
		if (bridge->softmix.video_mode.mode_data.talker_src_data.chan_old_vsrc == chan) {
			if (bridge->softmix.video_mode.mode_data.talker_src_data.chan_old_vsrc) {
				ast_channel_unref(bridge->softmix.video_mode.mode_data.talker_src_data.chan_old_vsrc);
			}
			bridge->softmix.video_mode.mode_data.talker_src_data.chan_old_vsrc = NULL;
		}
	}
	ast_bridge_unlock(bridge);
}

static int channel_hash(const void *obj, int flags)
{
	const struct ast_channel *chan = obj;
	const char *name = obj;
	int hash;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	default:
	case OBJ_POINTER:
		name = ast_channel_name(chan);
		/* Fall through */
	case OBJ_KEY:
		hash = ast_str_hash(name);
		break;
	case OBJ_PARTIAL_KEY:
		/* Should never happen in hash callback. */
		ast_assert(0);
		hash = 0;
		break;
	}
	return hash;
}

static int channel_cmp(void *obj, void *arg, int flags)
{
	const struct ast_channel *left = obj;
	const struct ast_channel *right = arg;
	const char *right_name = arg;
	int cmp;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	default:
	case OBJ_POINTER:
		right_name = ast_channel_name(right);
		/* Fall through */
	case OBJ_KEY:
		cmp = strcmp(ast_channel_name(left), right_name);
		break;
	case OBJ_PARTIAL_KEY:
		cmp = strncmp(ast_channel_name(left), right_name, strlen(right_name));
		break;
	}
	return cmp ? 0 : CMP_MATCH;
}

struct ao2_container *ast_bridge_peers_nolock(struct ast_bridge *bridge)
{
	struct ao2_container *channels;
	struct ast_bridge_channel *iter;

	channels = ao2_container_alloc_options(AO2_ALLOC_OPT_LOCK_NOLOCK,
		13, channel_hash, channel_cmp);
	if (!channels) {
		return NULL;
	}

	AST_LIST_TRAVERSE(&bridge->channels, iter, entry) {
		ao2_link(channels, iter->chan);
	}

	return channels;
}

struct ao2_container *ast_bridge_peers(struct ast_bridge *bridge)
{
	struct ao2_container *channels;

	ast_bridge_lock(bridge);
	channels = ast_bridge_peers_nolock(bridge);
	ast_bridge_unlock(bridge);

	return channels;
}

struct ast_channel *ast_bridge_peer_nolock(struct ast_bridge *bridge, struct ast_channel *chan)
{
	struct ast_channel *peer = NULL;
	struct ast_bridge_channel *iter;

	/* Asking for the peer channel only makes sense on a two-party bridge. */
	if (bridge->num_channels == 2
		&& bridge->technology->capabilities
			& (AST_BRIDGE_CAPABILITY_NATIVE | AST_BRIDGE_CAPABILITY_1TO1MIX)) {
		int in_bridge = 0;

		AST_LIST_TRAVERSE(&bridge->channels, iter, entry) {
			if (iter->chan != chan) {
				peer = iter->chan;
			} else {
				in_bridge = 1;
			}
		}
		if (in_bridge && peer) {
			ast_channel_ref(peer);
		} else {
			peer = NULL;
		}
	}

	return peer;
}

struct ast_channel *ast_bridge_peer(struct ast_bridge *bridge, struct ast_channel *chan)
{
	struct ast_channel *peer;

	ast_bridge_lock(bridge);
	peer = ast_bridge_peer_nolock(bridge, chan);
	ast_bridge_unlock(bridge);

	return peer;
}

static void publish_blind_transfer_full(int is_external, enum ast_transfer_result result,
		struct ast_channel *transferer, struct ast_bridge *bridge,
		const char *context, const char *exten, struct ast_channel *transferee_channel,
		struct ast_channel *replace_channel)
{
	struct ast_bridge_channel_pair pair;

	pair.channel = transferer;
	pair.bridge = bridge;

	if (bridge) {
		ast_bridge_lock(bridge);
	}
	ast_bridge_publish_blind_transfer(is_external, result, &pair, context, exten,
		transferee_channel, replace_channel);
	if (bridge) {
		ast_bridge_unlock(bridge);
	}
}

/*!
 * \internal
 * \brief Transfer an entire bridge to a specific destination.
 *
 * This creates a local channel to dial out and swaps the called local channel
 * with the transferer channel. By doing so, all participants in the bridge are
 * connected to the specified destination.
 *
 * While this means of transferring would work for both two-party and multi-party
 * bridges, this method is only used for multi-party bridges since this method would
 * be less efficient for two-party bridges.
 *
 * \param is_external Whether the transfer is externally initiated
 * \param transferer The channel performing a transfer
 * \param bridge The bridge where the transfer is being performed
 * \param exten The destination extension for the blind transfer
 * \param transferee The party being transferred if there is only one
 * \param context The destination context for the blind transfer
 * \param hook Framehook to attach to local channel
 *
 * \return The success or failure of the operation
 */
static enum ast_transfer_result blind_transfer_bridge(int is_external,
		struct ast_channel *transferer, struct ast_bridge *bridge,
		const char *exten, const char *context, struct ast_channel *transferee,
		transfer_channel_cb new_channel_cb,
		struct transfer_channel_data *user_data_wrapper)
{
	struct ast_channel *local;
	char chan_name[AST_MAX_EXTENSION + AST_MAX_CONTEXT + 2];
	int cause;

	snprintf(chan_name, sizeof(chan_name), "%s@%s", exten, context);
	local = ast_request("Local", ast_channel_nativeformats(transferer), NULL, transferer,
			chan_name, &cause);
	if (!local) {
		return AST_BRIDGE_TRANSFER_FAIL;
	}

	ast_channel_lock_both(local, transferer);
	ast_channel_req_accountcodes(local, transferer, AST_CHANNEL_REQUESTOR_REPLACEMENT);
	pbx_builtin_setvar_helper(local, BLINDTRANSFER, ast_channel_name(transferer));
	ast_channel_unlock(local);
	ast_channel_unlock(transferer);

	if (new_channel_cb) {
		new_channel_cb(local, user_data_wrapper, AST_BRIDGE_TRANSFER_MULTI_PARTY);
	}

	if (ast_call(local, chan_name, 0)) {
		ast_hangup(local);
		return AST_BRIDGE_TRANSFER_FAIL;
	}
	if (ast_bridge_impart(bridge, local, transferer, NULL,
		AST_BRIDGE_IMPART_CHAN_INDEPENDENT)) {
		ast_hangup(local);
		return AST_BRIDGE_TRANSFER_FAIL;
	}
	publish_blind_transfer_full(is_external, AST_BRIDGE_TRANSFER_SUCCESS, transferer, bridge,
		context, exten, transferee, local);
	return AST_BRIDGE_TRANSFER_SUCCESS;
}

/*!
 * \internal
 * \brief Base data to publish for stasis attended transfer messages
 */
struct stasis_attended_transfer_publish_data {
	/* The bridge between the transferer and transferee, and the transferer channel in this bridge */
	struct ast_bridge_channel_pair to_transferee;
	/* The bridge between the transferer and transfer target, and the transferer channel in this bridge */
	struct ast_bridge_channel_pair to_transfer_target;
	/* The Local;1 that will replace the transferee bridge transferer channel */
	struct ast_channel *replace_channel;
	/* The transferee channel. NULL if there is no transferee channel or if multiple parties are transferred */
	struct ast_channel *transferee_channel;
	/* The transfer target channel. NULL if there is no transfer target channel or if multiple parties are transferred */
	struct ast_channel *target_channel;
};

/*!
 * \internal
 * \brief Get the transferee channel
 *
 * This is only applicable to cases where a transfer is occurring on a
 * two-party bridge. The channels container passed in is expected to only
 * contain two channels, the transferer and the transferee. The transferer
 * channel is passed in as a parameter to ensure we don't return it as
 * the transferee channel.
 *
 * \param channels A two-channel container containing the transferer and transferee
 * \param transferer The party that is transfering the call
 * \return The party that is being transferred
 */
static struct ast_channel *get_transferee(struct ao2_container *channels, struct ast_channel *transferer)
{
	struct ao2_iterator channel_iter;
	struct ast_channel *transferee;

	for (channel_iter = ao2_iterator_init(channels, 0);
			(transferee = ao2_iterator_next(&channel_iter));
			ao2_cleanup(transferee)) {
		if (transferee != transferer) {
			break;
		}
	}

	ao2_iterator_destroy(&channel_iter);
	return transferee;
}


static void stasis_publish_data_cleanup(struct stasis_attended_transfer_publish_data *publication)
{
	ast_channel_unref(publication->to_transferee.channel);
	ast_channel_unref(publication->to_transfer_target.channel);
	ast_channel_cleanup(publication->transferee_channel);
	ast_channel_cleanup(publication->target_channel);
	ao2_cleanup(publication->to_transferee.bridge);
	ao2_cleanup(publication->to_transfer_target.bridge);
	ao2_cleanup(publication->replace_channel);
}

/*!
 * \internal
 * \brief Set up base data for an attended transfer stasis publication
 *
 * \param to_transferee The original transferer channel, which may be bridged to a transferee
 * \param to_transferee_bridge The bridge that to_transferee is in.
 * \param to_transfer_target The second transferer channel, which may be bridged to a transfer target
 * \param to_target_bridge The bridge that to_transfer_target_is in.
 * \param[out] publication A structure to hold the other parameters
 */
static void stasis_publish_data_init(struct ast_channel *to_transferee,
		struct ast_bridge *to_transferee_bridge, struct ast_channel *to_transfer_target,
		struct ast_bridge *to_target_bridge,
		struct stasis_attended_transfer_publish_data *publication)
{
	memset(publication, 0, sizeof(*publication));
	publication->to_transferee.channel = ast_channel_ref(to_transferee);
	if (to_transferee_bridge) {
		ao2_ref(to_transferee_bridge, +1);
		publication->to_transferee.bridge = to_transferee_bridge;
	}

	publication->to_transfer_target.channel = ast_channel_ref(to_transfer_target);
	if (to_target_bridge) {
		ao2_ref(to_target_bridge, +1);
		publication->to_transfer_target.bridge = to_target_bridge;
	}

	if (to_transferee_bridge) {
		publication->transferee_channel = ast_bridge_peer(to_transferee_bridge, to_transferee);
	}
	if (to_target_bridge) {
		publication->target_channel = ast_bridge_peer(to_target_bridge, to_transfer_target);
	}
}

/*
 * \internal
 * \brief Publish a stasis attended transfer resulting in a bridge merge
 *
 * \param publication Base data about the attended transfer
 * \param final_bridge The surviving bridge of the attended transfer
 */
static void publish_attended_transfer_bridge_merge(struct stasis_attended_transfer_publish_data *publication,
		struct ast_bridge *final_bridge)
{
	ast_bridge_publish_attended_transfer_bridge_merge(1, AST_BRIDGE_TRANSFER_SUCCESS,
			&publication->to_transferee, &publication->to_transfer_target, final_bridge,
			publication->transferee_channel, publication->target_channel);
}

/*
 * \internal
 * \brief Publish a stasis attended transfer to an application
 *
 * \param publication Base data about the attended transfer
 * \param app The app that is running at the conclusion of the transfer
 */
static void publish_attended_transfer_app(struct stasis_attended_transfer_publish_data *publication,
		const char *app)
{
	ast_bridge_publish_attended_transfer_app(1, AST_BRIDGE_TRANSFER_SUCCESS,
			&publication->to_transferee, &publication->to_transfer_target,
			publication->replace_channel, app,
			publication->transferee_channel, publication->target_channel);
}

/*
 * \internal
 * \brief Publish a stasis attended transfer showing a link between bridges
 *
 * \param publication Base data about the attended transfer
 * \param local_channel1 Local channel in the original bridge
 * \param local_channel2 Local channel in the second bridge
 */
static void publish_attended_transfer_link(struct stasis_attended_transfer_publish_data *publication,
		struct ast_channel *local_channel1, struct ast_channel *local_channel2)
{
	struct ast_channel *locals[2] = { local_channel1, local_channel2 };

	ast_bridge_publish_attended_transfer_link(1, AST_BRIDGE_TRANSFER_SUCCESS,
			&publication->to_transferee, &publication->to_transfer_target, locals,
			publication->transferee_channel, publication->target_channel);
}

/*
 * \internal
 * \brief Publish a stasis attended transfer failure
 *
 * \param publication Base data about the attended transfer
 * \param result The transfer result
 */
static void publish_attended_transfer_fail(struct stasis_attended_transfer_publish_data *publication,
		enum ast_transfer_result result)
{
	ast_bridge_publish_attended_transfer_fail(1, result, &publication->to_transferee,
			&publication->to_transfer_target, publication->transferee_channel,
			publication->target_channel);
}

/*!
 * \brief Perform an attended transfer of a bridge
 *
 * This performs an attended transfer of an entire bridge to a target.
 * The target varies, depending on what bridges exist during the transfer
 * attempt.
 *
 * If two bridges exist, then a local channel is created to link the two
 * bridges together.
 *
 * If only one bridge exists, then a local channel is created with one end
 * placed into the existing bridge and the other end masquerading into
 * the unbridged channel.
 *
 * \param chan1 Transferer channel. Guaranteed to be bridged.
 * \param chan2 Other transferer channel. May or may not be bridged.
 * \param bridge1 Bridge that chan1 is in. Guaranteed to be non-NULL.
 * \param bridge2 Bridge that chan2 is in. If NULL, then chan2 is not bridged.
 * \param publication Data to publish for a stasis attended transfer message.
 * \retval AST_BRIDGE_TRANSFER_FAIL Internal error occurred
 * \retval AST_BRIDGE_TRANSFER_SUCCESS Succesfully transferred the bridge
 */
static enum ast_transfer_result attended_transfer_bridge(struct ast_channel *chan1,
		struct ast_channel *chan2, struct ast_bridge *bridge1, struct ast_bridge *bridge2,
		struct stasis_attended_transfer_publish_data *publication)
{
	static const char *dest = "_attended@transfer/m";
	struct ast_channel *local_chan;
	int cause;
	int res;
	const char *app = NULL;

	local_chan = ast_request("Local", ast_channel_nativeformats(chan1), NULL, chan1,
			dest, &cause);
	if (!local_chan) {
		return AST_BRIDGE_TRANSFER_FAIL;
	}

	ast_channel_lock_both(local_chan, chan1);
	ast_channel_req_accountcodes(local_chan, chan1, AST_CHANNEL_REQUESTOR_REPLACEMENT);
	pbx_builtin_setvar_helper(local_chan, ATTENDEDTRANSFER, ast_channel_name(chan1));
	ast_channel_unlock(local_chan);
	ast_channel_unlock(chan1);

	if (bridge2) {
		res = ast_local_setup_bridge(local_chan, bridge2, chan2, NULL);
	} else {
		app = ast_strdupa(ast_channel_appl(chan2));
		res = ast_local_setup_masquerade(local_chan, chan2);
	}

	if (res) {
		ast_hangup(local_chan);
		return AST_BRIDGE_TRANSFER_FAIL;
	}

	if (ast_call(local_chan, dest, 0)) {
		ast_hangup(local_chan);
		return AST_BRIDGE_TRANSFER_FAIL;
	}

	/* Get a ref for use later since this one is being stolen */
	ao2_ref(local_chan, +1);
	if (ast_bridge_impart(bridge1, local_chan, chan1, NULL,
		AST_BRIDGE_IMPART_CHAN_INDEPENDENT)) {
		ast_hangup(local_chan);
		ao2_cleanup(local_chan);
		return AST_BRIDGE_TRANSFER_FAIL;
	}

	if (bridge2) {
		RAII_VAR(struct ast_channel *, local_chan2, NULL, ao2_cleanup);

		ast_channel_lock(local_chan);
		local_chan2 = ast_local_get_peer(local_chan);
		ast_channel_unlock(local_chan);

		ast_assert(local_chan2 != NULL);

		publish_attended_transfer_link(publication,
				local_chan, local_chan2);
	} else {
		publication->replace_channel = ao2_bump(local_chan);
		publish_attended_transfer_app(publication, app);
	}

	ao2_cleanup(local_chan);
	return AST_BRIDGE_TRANSFER_SUCCESS;
}

static enum ast_transfer_result try_parking(struct ast_channel *transferer,
	const char *context, const char *exten, transfer_channel_cb new_channel_cb,
	struct transfer_channel_data *user_data_wrapper)
{
	RAII_VAR(struct ast_bridge_channel *, transferer_bridge_channel, NULL, ao2_cleanup);

	if (!ast_parking_provider_registered()) {
		return AST_BRIDGE_TRANSFER_FAIL;
	}

	ast_channel_lock(transferer);
	transferer_bridge_channel = ast_channel_get_bridge_channel(transferer);
	ast_channel_unlock(transferer);

	if (!transferer_bridge_channel) {
		return AST_BRIDGE_TRANSFER_FAIL;
	}

	if (ast_parking_blind_transfer_park(transferer_bridge_channel,
		context, exten, new_channel_cb, user_data_wrapper)) {
		return AST_BRIDGE_TRANSFER_FAIL;
	}

	return AST_BRIDGE_TRANSFER_SUCCESS;
}

void ast_bridge_set_transfer_variables(struct ast_channel *chan, const char *value, int attended)
{
	char *writevar;
	char *erasevar;

	if (attended) {
		writevar = ATTENDEDTRANSFER;
		erasevar = BLINDTRANSFER;
	} else {
		writevar = BLINDTRANSFER;
		erasevar = ATTENDEDTRANSFER;
	}

	pbx_builtin_setvar_helper(chan, writevar, value);
	pbx_builtin_setvar_helper(chan, erasevar, NULL);
}

/*!
 * \internal
 * \brief Set the transfer variable as appropriate on channels involved in the transfer
 *
 * The transferer channel will have its variable set the same as its BRIDGEPEER
 * variable. This will account for all channels that it is bridged to. The other channels
 * involved in the transfer will have their variable set to the transferer
 * channel's name.
 *
 * \param transferer The channel performing the transfer
 * \param channels The channels belonging to the bridge
 * \param is_attended false  set BLINDTRANSFER and unset ATTENDEDTRANSFER
 *                    true   set ATTENDEDTRANSFER and unset BLINDTRANSFER
 */
static void set_transfer_variables_all(struct ast_channel *transferer, struct ao2_container *channels, int is_attended)
{
	struct ao2_iterator iter;
	struct ast_channel *chan;
	const char *transferer_name;
	const char *transferer_bridgepeer;

	ast_channel_lock(transferer);
	transferer_name = ast_strdupa(ast_channel_name(transferer));
	transferer_bridgepeer = ast_strdupa(S_OR(pbx_builtin_getvar_helper(transferer, "BRIDGEPEER"), ""));
	ast_channel_unlock(transferer);

	for (iter = ao2_iterator_init(channels, 0);
			(chan = ao2_iterator_next(&iter));
			ao2_cleanup(chan)) {
		if (chan == transferer) {
			ast_bridge_set_transfer_variables(chan, transferer_bridgepeer, is_attended);
		} else {
			ast_bridge_set_transfer_variables(chan, transferer_name, is_attended);
		}
	}

	ao2_iterator_destroy(&iter);
}

static struct ast_bridge *acquire_bridge(struct ast_channel *chan)
{
	struct ast_bridge *bridge;

	ast_channel_lock(chan);
	bridge = ast_channel_get_bridge(chan);
	ast_channel_unlock(chan);

	if (bridge
		&& ast_test_flag(&bridge->feature_flags, AST_BRIDGE_FLAG_MASQUERADE_ONLY)) {
		ao2_ref(bridge, -1);
		bridge = NULL;
	}

	return bridge;
}

static void publish_blind_transfer(int is_external, enum ast_transfer_result result,
		struct ast_channel *transferer, struct ast_bridge *bridge,
		const char *context, const char *exten, struct ast_channel *transferee_channel)
{
	publish_blind_transfer_full(is_external, result, transferer, bridge, context,
		exten, transferee_channel, NULL);
}

enum ast_transfer_result ast_bridge_transfer_blind(int is_external,
		struct ast_channel *transferer, const char *exten, const char *context,
		transfer_channel_cb new_channel_cb, void *user_data)
{
	RAII_VAR(struct ast_bridge *, bridge, NULL, ao2_cleanup);
	RAII_VAR(struct ast_bridge_channel *, bridge_channel, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, channels, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel *, transferee, NULL, ast_channel_cleanup);
	RAII_VAR(struct transfer_channel_data *, user_data_wrapper, NULL, ao2_cleanup);
	int do_bridge_transfer;
	int transfer_prohibited;
	enum ast_transfer_result transfer_result;

	bridge = acquire_bridge(transferer);
	if (!bridge) {
		transfer_result = AST_BRIDGE_TRANSFER_INVALID;
		goto publish;
	}

	transferee = ast_bridge_peer(bridge, transferer);

	ast_channel_lock(transferer);
	bridge_channel = ast_channel_get_bridge_channel(transferer);
	ast_channel_unlock(transferer);
	if (!bridge_channel) {
		transfer_result = AST_BRIDGE_TRANSFER_INVALID;
		goto publish;
	}

	user_data_wrapper = ao2_alloc(sizeof(*user_data_wrapper), NULL);
	if (!user_data_wrapper) {
		transfer_result = AST_BRIDGE_TRANSFER_FAIL;
		goto publish;
	}

	user_data_wrapper->data = user_data;

	/* Take off hold if they are on hold. */
	ast_bridge_channel_write_unhold(bridge_channel);

	transfer_result = try_parking(transferer, context, exten, new_channel_cb, user_data_wrapper);
	if (transfer_result == AST_BRIDGE_TRANSFER_SUCCESS) {
		goto publish;
	}

	/* Since parking didn't take control of the user_data_wrapper, we are just going to raise the completed flag now. */
	user_data_wrapper->completed = 1;

	{
		SCOPED_LOCK(lock, bridge, ast_bridge_lock, ast_bridge_unlock);

		channels = ast_bridge_peers_nolock(bridge);
		if (!channels) {
			transfer_result = AST_BRIDGE_TRANSFER_FAIL;
			goto publish;
		}
		if (ao2_container_count(channels) <= 1) {
			transfer_result = AST_BRIDGE_TRANSFER_INVALID;
			goto publish;
		}
		transfer_prohibited = ast_test_flag(&bridge->feature_flags,
				AST_BRIDGE_FLAG_TRANSFER_PROHIBITED);
		do_bridge_transfer = ast_test_flag(&bridge->feature_flags,
				AST_BRIDGE_FLAG_TRANSFER_BRIDGE_ONLY) ||
				ao2_container_count(channels) > 2;
	}

	if (transfer_prohibited) {
		transfer_result = AST_BRIDGE_TRANSFER_NOT_PERMITTED;
		goto publish;
	}

	set_transfer_variables_all(transferer, channels, 0);

	if (do_bridge_transfer) {
		/* if blind_transfer_bridge succeeds, it publishes its own message */
		transfer_result = blind_transfer_bridge(is_external, transferer, bridge,
			exten, context, transferee, new_channel_cb, user_data_wrapper);
		if (transfer_result == AST_BRIDGE_TRANSFER_SUCCESS)  {
			return transfer_result;
		}
		goto publish;
	}

	/* Reaching this portion means that we're dealing with a two-party bridge */

	if (!transferee) {
		transfer_result = AST_BRIDGE_TRANSFER_FAIL;
		goto publish;
	}

	if (bridge_channel_internal_queue_blind_transfer(transferee, exten, context,
				new_channel_cb, user_data_wrapper)) {
		transfer_result = AST_BRIDGE_TRANSFER_FAIL;
		goto publish;
	}

	ast_bridge_remove(bridge, transferer);
	transfer_result = AST_BRIDGE_TRANSFER_SUCCESS;

publish:
	publish_blind_transfer(is_external, transfer_result, transferer, bridge, context, exten, transferee);
	return transfer_result;
}

/*!
 * \internal
 * \brief Performs an attended transfer by moving a channel from one bridge to another
 *
 * The channel that is bridged to the source_channel is moved into the dest_bridge from
 * the source_bridge_channel's bridge. The swap_channel is swapped out of the dest_bridge and placed in
 * the source_bridge_channel's bridge.
 *
 * \note dest_bridge and source_bridge_channel's bridge MUST be locked before calling this function.
 *
 * \param dest_bridge The final bridge for the attended transfer
 * \param source_channel Channel who is bridged to the channel that will move
 * \param swap_channel Channel to be swapped out of the dest_bridge
 * \return The success or failure of the swap attempt
 */
static enum ast_transfer_result bridge_swap_attended_transfer(struct ast_bridge *dest_bridge,
		struct ast_bridge_channel *source_bridge_channel, struct ast_channel *swap_channel)
{
	struct ast_bridge_channel *bridged_to_source;

	bridged_to_source = ast_bridge_channel_peer(source_bridge_channel);
	if (bridged_to_source
		&& bridged_to_source->state == BRIDGE_CHANNEL_STATE_WAIT
		&& !ast_test_flag(&bridged_to_source->features->feature_flags,
			AST_BRIDGE_CHANNEL_FLAG_IMMOVABLE)) {
		bridged_to_source->swap = swap_channel;
		if (bridge_do_move(dest_bridge, bridged_to_source, 1, 0)) {
			return AST_BRIDGE_TRANSFER_FAIL;
		}
		/* Must kick the source channel out of its bridge. */
		ast_bridge_channel_leave_bridge(source_bridge_channel,
			BRIDGE_CHANNEL_STATE_END_NO_DISSOLVE, AST_CAUSE_NORMAL_CLEARING);
		return AST_BRIDGE_TRANSFER_SUCCESS;
	} else {
		return AST_BRIDGE_TRANSFER_INVALID;
	}
}

/*!
 * \internal
 * \brief Function that performs an attended transfer when both transferer channels are bridged
 *
 * The method by which the transfer is performed is dependent on whether the bridges allow for
 * optimization to occur between them. If no optimization is permitted, then an unreal channel
 * is placed as a link between the two bridges. If optimization is permitted, then that means
 * we are free to perform move or merge operations in order to perform the transfer.
 *
 * \note to_transferee_bridge and to_target_bridge MUST be locked before calling this function
 *
 * \param to_transferee The channel that is bridged to the transferee
 * \param to_transferee_bridge_channel to_transferee's bridge_channel
 * \param to_transfer_target The channel that is bridged to the transfer target
 * \param to_target_bridge_channel to_transfer_target's bridge_channel
 * \param to_transferee_bridge The bridge between to_transferee and the transferee
 * \param to_target_bridge The bridge between to_transfer_target and the transfer_target
 * \param publication Data to publish for a stasis attended transfer message
 * \return The success or failure of the attended transfer
 */
static enum ast_transfer_result two_bridge_attended_transfer(struct ast_channel *to_transferee,
		struct ast_bridge_channel *to_transferee_bridge_channel,
		struct ast_channel *to_transfer_target,
		struct ast_bridge_channel *to_target_bridge_channel,
		struct ast_bridge *to_transferee_bridge, struct ast_bridge *to_target_bridge,
		struct stasis_attended_transfer_publish_data *publication)
{
	struct ast_bridge_channel *kick_me[] = {
			to_transferee_bridge_channel,
			to_target_bridge_channel,
	};
	enum ast_transfer_result res;
	struct ast_bridge *final_bridge = NULL;
	RAII_VAR(struct ao2_container *, channels, NULL, ao2_cleanup);

	channels = ast_bridge_peers_nolock(to_transferee_bridge);

	if (!channels) {
		res = AST_BRIDGE_TRANSFER_FAIL;
		goto end;
	}

	set_transfer_variables_all(to_transferee, channels, 1);

	switch (ast_bridges_allow_optimization(to_transferee_bridge, to_target_bridge)) {
	case AST_BRIDGE_OPTIMIZE_SWAP_TO_CHAN_BRIDGE:
		final_bridge = to_transferee_bridge;
		res = bridge_swap_attended_transfer(to_transferee_bridge, to_target_bridge_channel, to_transferee);
		goto end;
	case AST_BRIDGE_OPTIMIZE_SWAP_TO_PEER_BRIDGE:
		final_bridge = to_target_bridge;
		res = bridge_swap_attended_transfer(to_target_bridge, to_transferee_bridge_channel, to_transfer_target);
		goto end;
	case AST_BRIDGE_OPTIMIZE_MERGE_TO_CHAN_BRIDGE:
		final_bridge = to_transferee_bridge;
		bridge_do_merge(to_transferee_bridge, to_target_bridge, kick_me, ARRAY_LEN(kick_me), 0);
		res = AST_BRIDGE_TRANSFER_SUCCESS;
		goto end;
	case AST_BRIDGE_OPTIMIZE_MERGE_TO_PEER_BRIDGE:
		final_bridge = to_target_bridge;
		bridge_do_merge(to_target_bridge, to_transferee_bridge, kick_me, ARRAY_LEN(kick_me), 0);
		res = AST_BRIDGE_TRANSFER_SUCCESS;
		goto end;
	case AST_BRIDGE_OPTIMIZE_PROHIBITED:
	default:
		/* Just because optimization wasn't doable doesn't necessarily mean
		 * that we can actually perform the transfer. Some reasons for non-optimization
		 * indicate bridge invalidity, so let's check those before proceeding.
		 */
		if (to_transferee_bridge->inhibit_merge || to_transferee_bridge->dissolved ||
				to_target_bridge->inhibit_merge || to_target_bridge->dissolved) {
			res = AST_BRIDGE_TRANSFER_INVALID;
			goto end;
		}

		/* Don't goto end here. attended_transfer_bridge will publish its own
		 * stasis message if it succeeds
		 */
		return attended_transfer_bridge(to_transferee, to_transfer_target,
			to_transferee_bridge, to_target_bridge, publication);
	}

end:
	if (res == AST_BRIDGE_TRANSFER_SUCCESS) {
		publish_attended_transfer_bridge_merge(publication, final_bridge);
	}

	return res;
}

enum ast_transfer_result ast_bridge_transfer_attended(struct ast_channel *to_transferee,
		struct ast_channel *to_transfer_target)
{
	RAII_VAR(struct ast_bridge *, to_transferee_bridge, NULL, ao2_cleanup);
	RAII_VAR(struct ast_bridge *, to_target_bridge, NULL, ao2_cleanup);
	RAII_VAR(struct ast_bridge_channel *, to_transferee_bridge_channel, NULL, ao2_cleanup);
	RAII_VAR(struct ast_bridge_channel *, to_target_bridge_channel, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, channels, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel *, transferee, NULL, ao2_cleanup);
	struct ast_bridge *the_bridge = NULL;
	struct ast_channel *chan_bridged;
	struct ast_channel *chan_unbridged;
	int transfer_prohibited;
	int do_bridge_transfer;
	enum ast_transfer_result res;
	const char *app = NULL;
	struct stasis_attended_transfer_publish_data publication;

	to_transferee_bridge = acquire_bridge(to_transferee);
	to_target_bridge = acquire_bridge(to_transfer_target);

	stasis_publish_data_init(to_transferee, to_transferee_bridge,
			to_transfer_target, to_target_bridge, &publication);

	/* They can't both be unbridged, you silly goose! */
	if (!to_transferee_bridge && !to_target_bridge) {
		res = AST_BRIDGE_TRANSFER_INVALID;
		goto end;
	}

	ast_channel_lock(to_transferee);
	to_transferee_bridge_channel = ast_channel_get_bridge_channel(to_transferee);
	ast_channel_unlock(to_transferee);

	ast_channel_lock(to_transfer_target);
	to_target_bridge_channel = ast_channel_get_bridge_channel(to_transfer_target);
	ast_channel_unlock(to_transfer_target);

	if (to_transferee_bridge_channel) {
		/* Take off hold if they are on hold. */
		ast_bridge_channel_write_unhold(to_transferee_bridge_channel);
	}

	if (to_target_bridge_channel) {
		const char *target_complete_sound;

		/* Take off hold if they are on hold. */
		ast_bridge_channel_write_unhold(to_target_bridge_channel);

		/* Is there a courtesy sound to play to the target? */
		ast_channel_lock(to_transfer_target);
		target_complete_sound = pbx_builtin_getvar_helper(to_transfer_target,
			"ATTENDED_TRANSFER_COMPLETE_SOUND");
		if (!ast_strlen_zero(target_complete_sound)) {
			target_complete_sound = ast_strdupa(target_complete_sound);
		} else {
			target_complete_sound = NULL;
		}
		ast_channel_unlock(to_transfer_target);
		if (!target_complete_sound) {
			ast_channel_lock(to_transferee);
			target_complete_sound = pbx_builtin_getvar_helper(to_transferee,
				"ATTENDED_TRANSFER_COMPLETE_SOUND");
			if (!ast_strlen_zero(target_complete_sound)) {
				target_complete_sound = ast_strdupa(target_complete_sound);
			} else {
				target_complete_sound = NULL;
			}
			ast_channel_unlock(to_transferee);
		}
		if (target_complete_sound) {
			ast_bridge_channel_write_playfile(to_target_bridge_channel, NULL,
				target_complete_sound, NULL);
		}
	}

	/* Let's get the easy one out of the way first */
	if (to_transferee_bridge && to_target_bridge) {

		if (!to_transferee_bridge_channel || !to_target_bridge_channel) {
			res = AST_BRIDGE_TRANSFER_INVALID;
			goto end;
		}

		ast_bridge_lock_both(to_transferee_bridge, to_target_bridge);
		res = two_bridge_attended_transfer(to_transferee, to_transferee_bridge_channel,
				to_transfer_target, to_target_bridge_channel,
				to_transferee_bridge, to_target_bridge, &publication);
		ast_bridge_unlock(to_transferee_bridge);
		ast_bridge_unlock(to_target_bridge);

		goto end;
	}

	the_bridge = to_transferee_bridge ?: to_target_bridge;
	chan_bridged = to_transferee_bridge ? to_transferee : to_transfer_target;
	chan_unbridged = to_transferee_bridge ? to_transfer_target : to_transferee;

	{
		int chan_count;
		SCOPED_LOCK(lock, the_bridge, ast_bridge_lock, ast_bridge_unlock);

		channels = ast_bridge_peers_nolock(the_bridge);
		if (!channels) {
			res = AST_BRIDGE_TRANSFER_FAIL;
			goto end;
		}
		chan_count = ao2_container_count(channels);
		if (chan_count <= 1) {
			res = AST_BRIDGE_TRANSFER_INVALID;
			goto end;
		}
		transfer_prohibited = ast_test_flag(&the_bridge->feature_flags,
				AST_BRIDGE_FLAG_TRANSFER_PROHIBITED);
		do_bridge_transfer = ast_test_flag(&the_bridge->feature_flags,
				AST_BRIDGE_FLAG_TRANSFER_BRIDGE_ONLY) ||
				chan_count > 2;
	}

	if (transfer_prohibited) {
		res = AST_BRIDGE_TRANSFER_NOT_PERMITTED;
		goto end;
	}

	set_transfer_variables_all(to_transferee, channels, 1);

	if (do_bridge_transfer) {
		ast_bridge_lock(the_bridge);
		res = attended_transfer_bridge(chan_bridged, chan_unbridged, the_bridge, NULL, &publication);
		ast_bridge_unlock(the_bridge);
		goto end;
	}

	transferee = get_transferee(channels, chan_bridged);
	if (!transferee) {
		res = AST_BRIDGE_TRANSFER_FAIL;
		goto end;
	}

	app = ast_strdupa(ast_channel_appl(chan_unbridged));
	if (bridge_channel_internal_queue_attended_transfer(transferee, chan_unbridged)) {
		res = AST_BRIDGE_TRANSFER_FAIL;
		goto end;
	}

	ast_bridge_remove(the_bridge, chan_bridged);

	ast_bridge_lock(the_bridge);
	publish_attended_transfer_app(&publication, app);
	ast_bridge_unlock(the_bridge);
	res = AST_BRIDGE_TRANSFER_SUCCESS;

end:
	/* All successful transfer paths have published an appropriate stasis message.
	 * All failure paths have deferred publishing a stasis message until this point
	 */
	if (res != AST_BRIDGE_TRANSFER_SUCCESS) {
		if (to_transferee_bridge && to_target_bridge) {
			ast_bridge_lock_both(to_transferee_bridge, to_target_bridge);
		} else if (the_bridge) {
			ast_bridge_lock(the_bridge);
		}

		publish_attended_transfer_fail(&publication, res);

		if (to_transferee_bridge && to_target_bridge) {
			ast_bridge_unlock(to_transferee_bridge);
			ast_bridge_unlock(to_target_bridge);
		} else if (the_bridge) {
			ast_bridge_unlock(the_bridge);
		}
	}
	stasis_publish_data_cleanup(&publication);
	return res;
}

/*!
 * \internal
 * \brief Service the bridge manager request.
 * \since 12.0.0
 *
 * \param bridge requesting service.
 *
 * \return Nothing
 */
static void bridge_manager_service(struct ast_bridge *bridge)
{
	ast_bridge_lock(bridge);
	if (bridge->callid) {
		ast_callid_threadassoc_change(bridge->callid);
	}

	/* Do any pending bridge actions. */
	bridge_handle_actions(bridge);
	ast_bridge_unlock(bridge);
}

/*!
 * \internal
 * \brief Bridge manager service thread.
 * \since 12.0.0
 *
 * \return Nothing
 */
static void *bridge_manager_thread(void *data)
{
	struct bridge_manager_controller *manager = data;
	struct bridge_manager_request *request;

	ao2_lock(manager);
	while (!manager->stop) {
		request = AST_LIST_REMOVE_HEAD(&manager->service_requests, node);
		if (!request) {
			ast_cond_wait(&manager->cond, ao2_object_get_lockaddr(manager));
			continue;
		}
		ao2_unlock(manager);

		/* Service the bridge. */
		bridge_manager_service(request->bridge);
		ao2_ref(request->bridge, -1);
		ast_free(request);

		ao2_lock(manager);
	}
	ao2_unlock(manager);

	return NULL;
}

/*!
 * \internal
 * \brief Destroy the bridge manager controller.
 * \since 12.0.0
 *
 * \param obj Bridge manager to destroy.
 *
 * \return Nothing
 */
static void bridge_manager_destroy(void *obj)
{
	struct bridge_manager_controller *manager = obj;
	struct bridge_manager_request *request;

	if (manager->thread != AST_PTHREADT_NULL) {
		/* Stop the manager thread. */
		ao2_lock(manager);
		manager->stop = 1;
		ast_cond_signal(&manager->cond);
		ao2_unlock(manager);
		ast_debug(1, "Waiting for bridge manager thread to die.\n");
		pthread_join(manager->thread, NULL);
	}

	/* Destroy the service request queue. */
	while ((request = AST_LIST_REMOVE_HEAD(&manager->service_requests, node))) {
		ao2_ref(request->bridge, -1);
		ast_free(request);
	}

	ast_cond_destroy(&manager->cond);
}

/*!
 * \internal
 * \brief Create the bridge manager controller.
 * \since 12.0.0
 *
 * \retval manager on success.
 * \retval NULL on error.
 */
static struct bridge_manager_controller *bridge_manager_create(void)
{
	struct bridge_manager_controller *manager;

	manager = ao2_alloc(sizeof(*manager), bridge_manager_destroy);
	if (!manager) {
		/* Well. This isn't good. */
		return NULL;
	}
	ast_cond_init(&manager->cond, NULL);
	AST_LIST_HEAD_INIT_NOLOCK(&manager->service_requests);

	/* Create the bridge manager thread. */
	if (ast_pthread_create(&manager->thread, NULL, bridge_manager_thread, manager)) {
		/* Well. This isn't good either. */
		manager->thread = AST_PTHREADT_NULL;
		ao2_ref(manager, -1);
		manager = NULL;
	}

	return manager;
}

/*!
 * \internal
 * \brief Bridge ao2 container sort function.
 * \since 12.0.0
 *
 * \param obj_left pointer to the (user-defined part) of an object.
 * \param obj_right pointer to the (user-defined part) of an object.
 * \param flags flags from ao2_callback()
 *   OBJ_POINTER - if set, 'obj_right', is an object.
 *   OBJ_KEY - if set, 'obj_right', is a search key item that is not an object.
 *   OBJ_PARTIAL_KEY - if set, 'obj_right', is a partial search key item that is not an object.
 *
 * \retval <0 if obj_left < obj_right
 * \retval =0 if obj_left == obj_right
 * \retval >0 if obj_left > obj_right
 */
static int bridge_sort_cmp(const void *obj_left, const void *obj_right, int flags)
{
	const struct ast_bridge *bridge_left = obj_left;
	const struct ast_bridge *bridge_right = obj_right;
	const char *right_key = obj_right;
	int cmp;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	default:
	case OBJ_POINTER:
		right_key = bridge_right->uniqueid;
		/* Fall through */
	case OBJ_KEY:
		cmp = strcmp(bridge_left->uniqueid, right_key);
		break;
	case OBJ_PARTIAL_KEY:
		cmp = strncmp(bridge_left->uniqueid, right_key, strlen(right_key));
		break;
	}
	return cmp;
}

struct ast_bridge *ast_bridge_find_by_id(const char *bridge_id)
{
	return ao2_find(bridges, bridge_id, OBJ_SEARCH_KEY);
}

struct bridge_complete {
	/*! Nth match to return. */
	int state;
	/*! Which match currently on. */
	int which;
};

static int complete_bridge_live_search(void *obj, void *arg, void *data, int flags)
{
	struct bridge_complete *search = data;

	if (++search->which > search->state) {
		return CMP_MATCH;
	}
	return 0;
}

static char *complete_bridge_live(const char *word, int state)
{
	char *ret;
	struct ast_bridge *bridge;
	struct bridge_complete search = {
		.state = state,
		};

	bridge = ao2_callback_data(bridges, ast_strlen_zero(word) ? 0 : OBJ_PARTIAL_KEY,
		complete_bridge_live_search, (char *) word, &search);
	if (!bridge) {
		return NULL;
	}
	ret = ast_strdup(bridge->uniqueid);
	ao2_ref(bridge, -1);
	return ret;
}

static char *complete_bridge_stasis(const char *word, int state)
{
	char *ret = NULL;
	int wordlen = strlen(word), which = 0;
	RAII_VAR(struct ao2_container *, cached_bridges, NULL, ao2_cleanup);
	struct ao2_iterator iter;
	struct stasis_message *msg;

	cached_bridges = stasis_cache_dump(ast_bridge_cache(), ast_bridge_snapshot_type());
	if (!cached_bridges) {
		return NULL;
	}

	iter = ao2_iterator_init(cached_bridges, 0);
	for (; (msg = ao2_iterator_next(&iter)); ao2_ref(msg, -1)) {
		struct ast_bridge_snapshot *snapshot = stasis_message_data(msg);

		if (!strncasecmp(word, snapshot->uniqueid, wordlen) && (++which > state)) {
			ret = ast_strdup(snapshot->uniqueid);
			ao2_ref(msg, -1);
			break;
		}
	}
	ao2_iterator_destroy(&iter);

	return ret;
}

static char *handle_bridge_show_all(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMAT_HDR "%-36s %5s %-15s %s\n"
#define FORMAT_ROW "%-36s %5u %-15s %s\n"

	RAII_VAR(struct ao2_container *, cached_bridges, NULL, ao2_cleanup);
	struct ao2_iterator iter;
	struct stasis_message *msg;

	switch (cmd) {
	case CLI_INIT:
		e->command = "bridge show all";
		e->usage =
			"Usage: bridge show all\n"
			"       List all bridges\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	cached_bridges = stasis_cache_dump(ast_bridge_cache(), ast_bridge_snapshot_type());
	if (!cached_bridges) {
		ast_cli(a->fd, "Failed to retrieve cached bridges\n");
		return CLI_SUCCESS;
	}

	ast_cli(a->fd, FORMAT_HDR, "Bridge-ID", "Chans", "Type", "Technology");

	iter = ao2_iterator_init(cached_bridges, 0);
	for (; (msg = ao2_iterator_next(&iter)); ao2_ref(msg, -1)) {
		struct ast_bridge_snapshot *snapshot = stasis_message_data(msg);

		ast_cli(a->fd, FORMAT_ROW,
			snapshot->uniqueid,
			snapshot->num_channels,
			S_OR(snapshot->subclass, "<unknown>"),
			S_OR(snapshot->technology, "<unknown>"));
	}
	ao2_iterator_destroy(&iter);
	return CLI_SUCCESS;

#undef FORMAT_HDR
#undef FORMAT_ROW
}

/*! \brief Internal callback function for sending channels in a bridge to the CLI */
static int bridge_show_specific_print_channel(void *obj, void *arg, int flags)
{
	const char *uniqueid = obj;
	struct ast_cli_args *a = arg;
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	struct ast_channel_snapshot *snapshot;

	msg = stasis_cache_get(ast_channel_cache(), ast_channel_snapshot_type(), uniqueid);
	if (!msg) {
		return 0;
	}
	snapshot = stasis_message_data(msg);

	ast_cli(a->fd, "Channel: %s\n", snapshot->name);

	return 0;
}

static char *handle_bridge_show_specific(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	struct ast_bridge_snapshot *snapshot;

	switch (cmd) {
	case CLI_INIT:
		e->command = "bridge show";
		e->usage =
			"Usage: bridge show <bridge-id>\n"
			"       Show information about the <bridge-id> bridge\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 2) {
			return complete_bridge_stasis(a->word, a->n);
		}
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	msg = stasis_cache_get(ast_bridge_cache(), ast_bridge_snapshot_type(), a->argv[2]);
	if (!msg) {
		ast_cli(a->fd, "Bridge '%s' not found\n", a->argv[2]);
		return CLI_SUCCESS;
	}

	snapshot = stasis_message_data(msg);
	ast_cli(a->fd, "Id: %s\n", snapshot->uniqueid);
	ast_cli(a->fd, "Type: %s\n", S_OR(snapshot->subclass, "<unknown>"));
	ast_cli(a->fd, "Technology: %s\n", S_OR(snapshot->technology, "<unknown>"));
	ast_cli(a->fd, "Num-Channels: %u\n", snapshot->num_channels);
	ao2_callback(snapshot->channels, OBJ_NODATA, bridge_show_specific_print_channel, a);

	return CLI_SUCCESS;
}

#ifdef AST_DEVMODE
static char *handle_bridge_destroy_specific(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_bridge *bridge;

	switch (cmd) {
	case CLI_INIT:
		e->command = "bridge destroy";
		e->usage =
			"Usage: bridge destroy <bridge-id>\n"
			"       Destroy the <bridge-id> bridge\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 2) {
			return complete_bridge_live(a->word, a->n);
		}
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	bridge = ast_bridge_find_by_id(a->argv[2]);
	if (!bridge) {
		ast_cli(a->fd, "Bridge '%s' not found\n", a->argv[2]);
		return CLI_SUCCESS;
	}

	ast_cli(a->fd, "Destroying bridge '%s'\n", a->argv[2]);
	ast_bridge_destroy(bridge, 0);

	return CLI_SUCCESS;
}
#endif

static char *complete_bridge_participant(const char *bridge_name, const char *line, const char *word, int pos, int state)
{
	struct ast_bridge *bridge;
	struct ast_bridge_channel *bridge_channel;
	int which;
	int wordlen;

	bridge = ast_bridge_find_by_id(bridge_name);
	if (!bridge) {
		return NULL;
	}

	if (!state) {
		ao2_ref(bridge, -1);
		return ast_strdup("all");
	}
	state--;

	{
		SCOPED_LOCK(bridge_lock, bridge, ast_bridge_lock, ast_bridge_unlock);

		which = 0;
		wordlen = strlen(word);
		AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
			if (!strncasecmp(ast_channel_name(bridge_channel->chan), word, wordlen)
				&& ++which > state) {
				ao2_ref(bridge, -1);
				return ast_strdup(ast_channel_name(bridge_channel->chan));
			}
		}
	}

	ao2_ref(bridge, -1);

	return NULL;
}

static char *handle_bridge_kick_channel(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_bridge *bridge;

	switch (cmd) {
	case CLI_INIT:
		e->command = "bridge kick";
		e->usage =
			"Usage: bridge kick <bridge-id> <channel-name | all>\n"
			"       Kick the <channel-name> channel out of the <bridge-id> bridge\n"
			"       If all is specified as the channel name then all channels will be\n"
			"       kicked out of the bridge.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 2) {
			return complete_bridge_live(a->word, a->n);
		}
		if (a->pos == 3) {
			return complete_bridge_participant(a->argv[2], a->line, a->word, a->pos, a->n);
		}
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	bridge = ast_bridge_find_by_id(a->argv[2]);
	if (!bridge) {
		ast_cli(a->fd, "Bridge '%s' not found\n", a->argv[2]);
		return CLI_SUCCESS;
	}

	if (!strcasecmp(a->argv[3], "all")) {
		struct ast_bridge_channel *bridge_channel;

		ast_cli(a->fd, "Kicking all channels from bridge '%s'\n", a->argv[2]);

		ast_bridge_lock(bridge);
		AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
			ast_bridge_channel_queue_callback(bridge_channel, 0, kick_it, NULL, 0);
		}
		ast_bridge_unlock(bridge);
	} else {
		struct ast_channel *chan;

		chan = ast_channel_get_by_name_prefix(a->argv[3], strlen(a->argv[3]));
		if (!chan) {
			ast_cli(a->fd, "Channel '%s' not found\n", a->argv[3]);
			ao2_ref(bridge, -1);
			return CLI_SUCCESS;
		}

		ast_cli(a->fd, "Kicking channel '%s' from bridge '%s'\n",
			ast_channel_name(chan), a->argv[2]);
		ast_bridge_kick(bridge, chan);
		ast_channel_unref(chan);
	}

	ao2_ref(bridge, -1);
	return CLI_SUCCESS;
}

/*! Bridge technology capabilities to string. */
static const char *tech_capability2str(uint32_t capabilities)
{
	const char *type;

	if (capabilities & AST_BRIDGE_CAPABILITY_HOLDING) {
		type = "Holding";
	} else if (capabilities & AST_BRIDGE_CAPABILITY_EARLY) {
		type = "Early";
	} else if (capabilities & AST_BRIDGE_CAPABILITY_NATIVE) {
		type = "Native";
	} else if (capabilities & AST_BRIDGE_CAPABILITY_1TO1MIX) {
		type = "1to1Mix";
	} else if (capabilities & AST_BRIDGE_CAPABILITY_MULTIMIX) {
		type = "MultiMix";
	} else {
		type = "<Unknown>";
	}
	return type;
}

static char *handle_bridge_technology_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMAT_HDR "%-20s %-20s %8s %s\n"
#define FORMAT_ROW "%-20s %-20s %8u %s\n"

	struct ast_bridge_technology *cur;

	switch (cmd) {
	case CLI_INIT:
		e->command = "bridge technology show";
		e->usage =
			"Usage: bridge technology show\n"
			"       List registered bridge technologies\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, FORMAT_HDR, "Name", "Type", "Priority", "Suspended");
	AST_RWLIST_RDLOCK(&bridge_technologies);
	AST_RWLIST_TRAVERSE(&bridge_technologies, cur, entry) {
		const char *type;

		/* Decode type for display */
		type = tech_capability2str(cur->capabilities);

		ast_cli(a->fd, FORMAT_ROW, cur->name, type, cur->preference,
			AST_CLI_YESNO(cur->suspended));
	}
	AST_RWLIST_UNLOCK(&bridge_technologies);
	return CLI_SUCCESS;

#undef FORMAT
}

static char *complete_bridge_technology(const char *word, int state)
{
	struct ast_bridge_technology *cur;
	char *res;
	int which;
	int wordlen;

	which = 0;
	wordlen = strlen(word);
	AST_RWLIST_RDLOCK(&bridge_technologies);
	AST_RWLIST_TRAVERSE(&bridge_technologies, cur, entry) {
		if (!strncasecmp(cur->name, word, wordlen) && ++which > state) {
			res = ast_strdup(cur->name);
			AST_RWLIST_UNLOCK(&bridge_technologies);
			return res;
		}
	}
	AST_RWLIST_UNLOCK(&bridge_technologies);
	return NULL;
}

static char *handle_bridge_technology_suspend(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_bridge_technology *cur;
	int suspend;
	int successful;

	switch (cmd) {
	case CLI_INIT:
		e->command = "bridge technology {suspend|unsuspend}";
		e->usage =
			"Usage: bridge technology {suspend|unsuspend} <technology-name>\n"
			"       Suspend or unsuspend a bridge technology.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			return complete_bridge_technology(a->word, a->n);
		}
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	suspend = !strcasecmp(a->argv[2], "suspend");
	successful = 0;
	AST_RWLIST_WRLOCK(&bridge_technologies);
	AST_RWLIST_TRAVERSE(&bridge_technologies, cur, entry) {
		if (!strcasecmp(cur->name, a->argv[3])) {
			successful = 1;
			if (suspend) {
				ast_bridge_technology_suspend(cur);
			} else {
				ast_bridge_technology_unsuspend(cur);
			}
			break;
		}
	}
	AST_RWLIST_UNLOCK(&bridge_technologies);

	if (successful) {
		if (suspend) {
			ast_cli(a->fd, "Suspended bridge technology '%s'\n", a->argv[3]);
		} else {
			ast_cli(a->fd, "Unsuspended bridge technology '%s'\n", a->argv[3]);
		}
	} else {
		ast_cli(a->fd, "Bridge technology '%s' not found\n", a->argv[3]);
	}

	return CLI_SUCCESS;
}

static struct ast_cli_entry bridge_cli[] = {
	AST_CLI_DEFINE(handle_bridge_show_all, "List all bridges"),
	AST_CLI_DEFINE(handle_bridge_show_specific, "Show information about a bridge"),
#ifdef AST_DEVMODE
	AST_CLI_DEFINE(handle_bridge_destroy_specific, "Destroy a bridge"),
#endif
	AST_CLI_DEFINE(handle_bridge_kick_channel, "Kick a channel from a bridge"),
	AST_CLI_DEFINE(handle_bridge_technology_show, "List registered bridge technologies"),
	AST_CLI_DEFINE(handle_bridge_technology_suspend, "Suspend/unsuspend a bridge technology"),
};


static int handle_manager_bridge_tech_suspend(struct mansession *s, const struct message *m, int suspend)
{
	const char *name = astman_get_header(m, "BridgeTechnology");
	struct ast_bridge_technology *cur;
	int successful = 0;

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "BridgeTechnology must be provided");
		return 0;
	}

	AST_RWLIST_RDLOCK(&bridge_technologies);
	AST_RWLIST_TRAVERSE(&bridge_technologies, cur, entry) {

		if (!strcasecmp(cur->name, name)) {
			successful = 1;
			if (suspend) {
				ast_bridge_technology_suspend(cur);
			} else {
				ast_bridge_technology_unsuspend(cur);
			}
			break;
		}
	}
	AST_RWLIST_UNLOCK(&bridge_technologies);
	if (!successful) {
		astman_send_error(s, m, "BridgeTechnology not found");
		return 0;
	}

	astman_send_ack(s, m, (suspend ? "Suspended bridge technology" : "Unsuspended bridge technology"));
	return 0;
}

static int manager_bridge_tech_suspend(struct mansession *s, const struct message *m)
{
	return handle_manager_bridge_tech_suspend(s, m, 1);
}

static int manager_bridge_tech_unsuspend(struct mansession *s, const struct message *m)
{
	return handle_manager_bridge_tech_suspend(s, m, 0);
}

static int manager_bridge_tech_list(struct mansession *s, const struct message *m)
{
	const char *id = astman_get_header(m, "ActionID");
	RAII_VAR(struct ast_str *, id_text, ast_str_create(128), ast_free);
	struct ast_bridge_technology *cur;

	if (!id_text) {
		astman_send_error(s, m, "Internal error");
		return -1;
	}

	if (!ast_strlen_zero(id)) {
		ast_str_set(&id_text, 0, "ActionID: %s\r\n", id);
	}

	astman_send_ack(s, m, "Bridge technology listing will follow");

	AST_RWLIST_RDLOCK(&bridge_technologies);
	AST_RWLIST_TRAVERSE(&bridge_technologies, cur, entry) {
		const char *type;

		type = tech_capability2str(cur->capabilities);

		astman_append(s,
			"Event: BridgeTechnologyListItem\r\n"
			"BridgeTechnology: %s\r\n"
			"BridgeType: %s\r\n"
			"BridgePriority: %u\r\n"
			"BridgeSuspended: %s\r\n"
			"%s"
			"\r\n",
			cur->name, type, cur->preference, AST_YESNO(cur->suspended),
			ast_str_buffer(id_text));
	}
	AST_RWLIST_UNLOCK(&bridge_technologies);

	astman_append(s,
		"Event: BridgeTechnologyListComplete\r\n"
		"%s"
		"\r\n",
		ast_str_buffer(id_text));

	return 0;
}

/*!
 * \internal
 * \brief Print bridge object key (name).
 * \since 12.0.0
 *
 * \param v_obj A pointer to the object we want the key printed.
 * \param where User data needed by prnt to determine where to put output.
 * \param prnt Print output callback function to use.
 *
 * \return Nothing
 */
static void bridge_prnt_obj(void *v_obj, void *where, ao2_prnt_fn *prnt)
{
	struct ast_bridge *bridge = v_obj;

	if (!bridge) {
		return;
	}
	prnt(where, "%s %s chans:%u",
		bridge->uniqueid, bridge->v_table->name, bridge->num_channels);
}

/*!
 * \internal
 * \brief Shutdown the bridging system.
 * \since 12.0.0
 *
 * \return Nothing
 */
static void bridge_shutdown(void)
{
	ast_manager_unregister("BridgeTechnologyList");
	ast_manager_unregister("BridgeTechnologySuspend");
	ast_manager_unregister("BridgeTechnologyUnsuspend");
	ast_cli_unregister_multiple(bridge_cli, ARRAY_LEN(bridge_cli));
	ao2_container_unregister("bridges");
	ao2_cleanup(bridges);
	bridges = NULL;
	ao2_cleanup(bridge_manager);
	bridge_manager = NULL;
}

int ast_bridging_init(void)
{
	ast_register_atexit(bridge_shutdown);

	if (ast_stasis_bridging_init()) {
		return -1;
	}

	bridge_manager = bridge_manager_create();
	if (!bridge_manager) {
		return -1;
	}

	bridges = ao2_container_alloc_rbtree(AO2_ALLOC_OPT_LOCK_MUTEX,
		AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE, bridge_sort_cmp, NULL);
	if (!bridges) {
		return -1;
	}
	ao2_container_register("bridges", bridges, bridge_prnt_obj);

	ast_bridging_init_basic();

	ast_cli_register_multiple(bridge_cli, ARRAY_LEN(bridge_cli));

	ast_manager_register_xml_core("BridgeTechnologyList", 0, manager_bridge_tech_list);
	ast_manager_register_xml_core("BridgeTechnologySuspend", 0, manager_bridge_tech_suspend);
	ast_manager_register_xml_core("BridgeTechnologyUnsuspend", 0, manager_bridge_tech_unsuspend);

	return 0;
}
