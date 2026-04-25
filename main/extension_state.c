/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2026, Sangoma Technologies Corporation
 *
 * Joshua Colp <jcolp@sangoma.com>
 *
 * See https://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"
#include "asterisk/_private.h"
#include "asterisk/module.h"
#include "asterisk/extension_state.h"
#include "asterisk/pbx.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_message_router.h"
#include "asterisk/astobj2.h"
#include "asterisk/cli.h"
#include "pbx_private.h"

/*** DOCUMENTATION
	<manager name="ExtensionStateList" language="en_US">
		<since>
			<version>13.0.0</version>
		</since>
		<synopsis>
			List the current known extension states.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
		</syntax>
		<description>
			<para>This will list out all known extension states in a
			sequence of <replaceable>ExtensionStatus</replaceable> events.
			When finished, a <replaceable>ExtensionStateListComplete</replaceable> event
			will be emitted.</para>
		</description>
		<see-also>
			<ref type="manager">ExtensionState</ref>
			<ref type="function">HINT</ref>
			<ref type="function">EXTENSION_STATE</ref>
		</see-also>
		<responses>
			<list-elements>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='ExtensionStatus'])" />
			</list-elements>
			<managerEvent name="ExtensionStateListComplete" language="en_US">
				<managerEventInstance class="EVENT_FLAG_COMMAND">
					<since>
						<version>13.0.0</version>
					</since>
					<synopsis>
						Indicates the end of the list the current known extension states.
					</synopsis>
					<syntax>
						<parameter name="EventList">
							<para>Conveys the status of the event list.</para>
						</parameter>
						<parameter name="ListItems">
							<para>Conveys the number of statuses reported.</para>
						</parameter>
					</syntax>
				</managerEventInstance>
			</managerEvent>
		</responses>
	</manager>
 ***/

#define HINTDEVICE_DATA_LENGTH 16
AST_THREADSTORAGE(hintdevice_data);

/*! \brief Device state source feeding an extension state */
struct extension_state_device_source {
	/*! \brief The current state of the device - this is immutable */
	struct ast_extension_state_device_state_info *info;
	/*! \brief Synchronous subscription to the device state topic */
	struct stasis_subscription *device_state_subscription;
	/*! \brief The current version for this source */
	unsigned int version;
};

AST_VECTOR(device_state_sources_vector, struct extension_state_device_source *);

/*! \brief Extension state information */
struct extension_state {
	/*! \brief The current device snapshot for the extension */
	struct ast_extension_state_device_snapshot *device_snapshot;
	/*! \brief The current presence snapshot for the extension */
	struct ast_extension_state_presence_snapshot *presence_snapshot;
	/*! \brief The extension state topic for this extension */
	struct stasis_topic *extension_state_topic;
	/*! \brief Forwarder from per-extension topic to all topic */
	struct stasis_forward *extension_state_forwarder;
	/*! \brief Device state sources feeding the hint topic, and their forwarding */
	struct device_state_sources_vector device_state_sources;
	/*! \brief The string representation of all presence state sources feeding this extension state */
	char *presence_sources_string;
	/*! \brief The dialplan hint that last configured this extension state */
	struct ast_exten *hint_extension;
	/*! \brief The dialplan context */
	char dialplan_context[AST_MAX_CONTEXT];
	/*! \brief The dialplan extension */
	char dialplan_extension[AST_MAX_EXTENSION];
	/*! \brief The combined extension this state is for (extension@context) */
	char extension[0];
};

/*! \brief Number of buckets for extension states */
#ifdef LOW_MEMORY
#define EXTENSION_STATE_BUCKETS 17
#else
#define EXTENSION_STATE_BUCKETS 563
#endif

static const struct cfextension_states {
	int extension_state;
	const char * const text;
} extension_state_mappings[] = {
	{ AST_EXTENSION_NOT_INUSE,                     "Idle" },
	{ AST_EXTENSION_INUSE,                         "InUse" },
	{ AST_EXTENSION_BUSY,                          "Busy" },
	{ AST_EXTENSION_UNAVAILABLE,                   "Unavailable" },
	{ AST_EXTENSION_RINGING,                       "Ringing" },
	{ AST_EXTENSION_INUSE | AST_EXTENSION_RINGING, "InUse&Ringing" },
	{ AST_EXTENSION_ONHOLD,                        "Hold" },
	{ AST_EXTENSION_INUSE | AST_EXTENSION_ONHOLD,  "InUse&Hold" }
};

/*! \brief The global container of extension states */
static struct ao2_container *extension_states;

/*! \brief Topic which receives all extension state updates */
static struct stasis_topic *extension_state_topic_all;

/*! \brief Single presence state subscription, for all extension states */
static struct stasis_subscription *presence_state_sub;

/*! \brief Hash function for extension states */
AO2_STRING_FIELD_HASH_FN(extension_state, extension)

/*! \brief Compare function for extension states */
AO2_STRING_FIELD_CMP_FN(extension_state, extension)

/*! \brief Message type for extension state updates */
STASIS_MESSAGE_TYPE_DEFN(ast_extension_state_update_message_type);

/*!
 * \internal
 * \brief Destroy an extension state update message
 * \param obj The extension state update message to destroy
 */
static void extension_state_update_message_destroy(void *obj)
{
	struct ast_extension_state_update_message *update_message = obj;

	ao2_cleanup(update_message->old_device_snapshot);
	ao2_cleanup(update_message->new_device_snapshot);
	ao2_cleanup(update_message->old_presence_snapshot);
	ao2_cleanup(update_message->new_presence_snapshot);
}

/*!
 * \internal
 * \brief Create an extension state update message
 *
 * \param context The context of the extension
 * \param extension The extension
 * \param old_device_snapshot The old device state snapshot
 * \param new_device_snapshot The new device state snapshot
 * \param old_presence_snapshot The old presence state snapshot
 * \param new_presence_snapshot The new presence state snapshot
 * \retval An allocated extension state update message, or NULL on failure
 *
 * This function creates an extension state update message for the specified context, extension,
 * old device state snapshot, new device state snapshot, old presence state snapshot, and new presence state snapshot.
 */
static struct ast_extension_state_update_message *extension_state_update_message_create(const char *context,
	const char *extension, struct ast_extension_state_device_snapshot *old_device_snapshot,
	struct ast_extension_state_device_snapshot *new_device_snapshot, struct ast_extension_state_presence_snapshot *old_presence_snapshot,
	struct ast_extension_state_presence_snapshot *new_presence_snapshot)
{
	size_t context_len = strlen(context) + 1;
	size_t extension_len = strlen(extension) + 1;
	struct ast_extension_state_update_message *update_message;

	update_message = ao2_alloc_options(sizeof(*update_message) + context_len + extension_len,
		extension_state_update_message_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!update_message) {
		return NULL;
	}

	ast_copy_string(update_message->extension, extension, extension_len); /* Safe */
	update_message->context = update_message->extension + extension_len;
	ast_copy_string(update_message->context, context, context_len); /* Safe */

	update_message->old_device_snapshot = ao2_bump(old_device_snapshot);
	update_message->new_device_snapshot = ao2_bump(new_device_snapshot);
	update_message->old_presence_snapshot = ao2_bump(old_presence_snapshot);
	update_message->new_presence_snapshot = ao2_bump(new_presence_snapshot);

	return update_message;
}

/*!
 * \internal
 * \brief Destroy an extension state device source
 * \param source The extension state device source to destroy
 *
 * This function destroys an extension state device source by unsubscribing from the device state
 * topic and cleaning up the associated resources.
 */
static void extension_state_device_source_destroy(struct extension_state_device_source *source)
{
	if (source->device_state_subscription) {
		stasis_unsubscribe(source->device_state_subscription);
	}
	ao2_cleanup(source->info);
	ast_free(source);
}

/*!
 * \internal
 * \brief Allocate an extension device state info object
 *
 * \param device The device name
 * \param state The device state
 * \retval An allocated extension device state info object, or NULL on failure
 *
 * This function allocates an extension device state info object with the specified device name and state.
 */
static struct ast_extension_state_device_state_info *extension_state_device_state_info_alloc(const char *device,
	enum ast_device_state state)
{
	struct ast_extension_state_device_state_info *info;

	info = ao2_alloc_options(sizeof(*info) + strlen(device) + 1, NULL, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!info) {
		return NULL;
	}

	info->state = state;
	strcpy(info->device, device); /* Safe */

	return info;
}

/*!
 * \internal
 * \brief Destroy an extension state device snapshot
 *
 * \param obj The extension state device snapshot to destroy
 *
 * This function destroys an extension state device snapshot by cleaning up
 * the causing device and additional devices.
 */
static void extension_state_device_snapshot_destroy(void *obj)
{
	struct ast_extension_state_device_snapshot *device_snapshot = obj;

	ao2_cleanup(device_snapshot->causing_device);
	AST_VECTOR_CALLBACK_VOID(&device_snapshot->additional_devices, ao2_cleanup);
	AST_VECTOR_FREE(&device_snapshot->additional_devices);
}

/*!
 * \internal
 * \brief Create an extension state device snapshot
 *
 * \param device_state The device state
 * \param device_state_sources The device state sources
 * \param causing_device The causing device
 * \retval An allocated extension state device snapshot, or NULL on failure
 *
 * This function creates an extension state device snapshot with the device state,
 * device state sources, and causing device.
 */
static struct ast_extension_state_device_snapshot *extension_state_device_snapshot_create(
	enum ast_extension_states device_state, struct device_state_sources_vector *device_state_sources,
	struct ast_extension_state_device_state_info *causing_device)
{
	struct ast_extension_state_device_snapshot *device_snapshot;
	int i;

	device_snapshot = ao2_alloc_options(sizeof(*device_snapshot),
		extension_state_device_snapshot_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!device_snapshot) {
		return NULL;
	}

	device_snapshot->state = device_state;
	device_snapshot->causing_device = ao2_bump(causing_device);
	if (AST_VECTOR_INIT(&device_snapshot->additional_devices, AST_VECTOR_SIZE(device_state_sources))) {
		ao2_ref(device_snapshot, -1);
		return NULL;
	}

	for (i = 0; i < AST_VECTOR_SIZE(device_state_sources); i++) {
		struct extension_state_device_source *source = AST_VECTOR_GET(device_state_sources, i);

		if (causing_device && source->info == causing_device) {
			continue;
		}

		AST_VECTOR_APPEND(&device_snapshot->additional_devices, ao2_bump(source->info));
	}

	return device_snapshot;
}

/*!
 * \internal
 * \brief Callback for device state changes
 *
 * \param userdata The extension state to update
 * \param sub The subscription
 * \param msg The device state message
 *
 * This function is called when a device state changes and updates the extension state
 * accordingly by aggregating the device states and publishing the new state.
 */
static void extension_state_device_state_cb(void *userdata, struct stasis_subscription *sub,
		struct stasis_message *msg)
{
	struct extension_state *state = userdata;
	struct ast_device_state_message *device_state;
	struct ast_devstate_aggregate agg;
	struct ast_extension_state_device_state_info *extension_device_state_info;
	int i;
	unsigned int updated = 0;
	enum ast_extension_states new_device_state;

	if (stasis_message_type(msg) != ast_device_state_message_type()) {
		return;
	}

	device_state = stasis_message_data(msg);

	/* We only care about the aggregate state */
	if (device_state->eid) {
		return;
	}

	ast_devstate_aggregate_init(&agg);

	/*
	 * Alrighty, the reason that we store an extension_device_state_info is to reduce the memory allocation that
	 * has to occur every time we get a device state update and have to construct a new message. If the extension
	 * state contains only a single device source we have to do this anyway, but if there's multiple then if we
	 * didn't store the result we'd be creating new ones every message to put in the additional_devices vector.
	 */
	extension_device_state_info = extension_state_device_state_info_alloc(device_state->device, device_state->state);
	if (!extension_device_state_info) {
		return;
	}

	ao2_lock(state);

	for (i = 0; i < AST_VECTOR_SIZE(&state->device_state_sources); i++) {
		struct extension_state_device_source *source = AST_VECTOR_GET(&state->device_state_sources, i);

		if (!strcmp(source->info->device, device_state->device)) {
			ao2_replace(source->info, extension_device_state_info);
			updated = 1;
		}

		ast_devstate_aggregate_add(&agg, source->info->state);
	}

	/* We don't really care about the device state info contents now, so we can drop the reference */
	ao2_ref(extension_device_state_info, -1);

	/*
	 * It's possible for a device state update to come in for a device which is no longer feeding this
	 * extension state if it has been updated, so only actually care about the new device state if a
	 * source has actually been updated.
	 */
	if (!updated) {
		ao2_unlock(state);
		return;
	}

	/*
	 * We actually update things and raise a message if the state is different, or if the state is ringing
	 * as that can actually just be an update that someone else is ringing the same extension.
	 */
	new_device_state = ast_devstate_to_extenstate(ast_devstate_aggregate_result(&agg));
	if ((state->device_snapshot->state != new_device_state) || (new_device_state & AST_EXTENSION_RINGING)) {
		struct ast_extension_state_device_snapshot *device_snapshot;
		struct ast_extension_state_update_message *update_message;
		struct stasis_message *message;

		/* Now above you probably noticed I dropped the reference for extension_device_state_info but now I'm
		 * passing it in here. Don't panic - a reference exists on the device state source still and since we
		 * have the state locked it can't go away.
		 */
		device_snapshot = extension_state_device_snapshot_create(new_device_state, &state->device_state_sources,
			extension_device_state_info);
		if (!device_snapshot) {
			ao2_unlock(state);
			return;
		}

		update_message = extension_state_update_message_create(state->dialplan_context, state->dialplan_extension,
			state->device_snapshot, device_snapshot, state->presence_snapshot, state->presence_snapshot);

		/* Even if we can't publish an update message we still ensure the local cached snapshot is up to date */
		ao2_replace(state->device_snapshot, device_snapshot);
		ao2_ref(device_snapshot, -1);

		if (!update_message) {
			ao2_unlock(state);
			return;
		}

		/* Inform any subscribers of the change to the device snapshot */
		message = stasis_message_create(ast_extension_state_update_message_type(), update_message);
		if (message) {
			stasis_publish(state->extension_state_topic, message);
			ao2_ref(message, -1);
		}

		ao2_ref(update_message, -1);
	}

	ao2_unlock(state);
}

/*!
 * \internal
 * \brief Allocate a device source for an extension state
 *
 * \param state The extension state to allocate the device source for
 * \param device The device to allocate the source for
 * \retval An allocated device source, or NULL on failure
 *
 * This function allocates a device source for an extension state by creating a device state source
 * and setting up the necessary subscriptions and references.
 */
static struct extension_state_device_source *extension_state_device_source_alloc(struct extension_state *state, const char *device)
{
	struct extension_state_device_source *source;
	struct stasis_topic *topic;

	/*
	 * Ensure that we have a direct device state topic for the device, note this is returned without a reference but
	 * is guaranteed to exist regardless.
	 */
	topic = ast_device_state_topic(device);
	if (!topic) {
		return NULL;
	}

	/*
	 * The device state source is only used within the extension state and is never
	 * passed around so the overhead of an ao2 object with reference counting is unnecessary.
	 */
	source = ast_calloc(1, sizeof(*source));
	if (!source) {
		return NULL;
	}

	source->info = extension_state_device_state_info_alloc(device, ast_device_state(device));
	if (!source->info) {
		extension_state_device_source_destroy(source);
		return NULL;
	}

	/*
	 * We do a synchronous subscription to the device state topic, as our callback is extremely
	 * short lived and the added overhead of queueing to a taskprocessor for another thread to handle
	 * it is just not worth it.
	 */
	source->device_state_subscription = stasis_subscribe_synchronous(topic, extension_state_device_state_cb, state);
	if (!source->device_state_subscription) {
		extension_state_device_source_destroy(source);
		return NULL;
	}

	stasis_subscription_accept_message_type(source->device_state_subscription, ast_device_state_message_type());
	stasis_subscription_set_filter(source->device_state_subscription, STASIS_SUBSCRIPTION_FILTER_SELECTIVE);

	return source;
}

/*!
 * \internal
 * \brief Destroy an extension state presence snapshot
 *
 * \param obj The extension state presence snapshot to destroy
 *
 * This function destroys an extension state presence snapshot by cleaning up
 * the presence snapshot.
 */
static void extension_state_presence_snapshot_destroy(void *obj)
{
	struct ast_extension_state_presence_snapshot *presence_snapshot = obj;

	ast_free(presence_snapshot->presence_subtype);
	ast_free(presence_snapshot->presence_message);
}

/*!
 * \internal
 * \brief Create an extension state presence snapshot
 *
 * \param presence_state The presence state
 * \param presence_subtype The presence subtype (can be NULL)
 * \param presence_message The presence message (can be NULL)
 * \retval An allocated extension state presence snapshot, or NULL on failure
 *
 * This function creates an extension state presence snapshot for the specified presence state,
 * presence subtype, and presence message.
 */
static struct ast_extension_state_presence_snapshot *extension_state_presence_snapshot_create(enum ast_presence_state presence_state,
	const char *presence_subtype, const char *presence_message)
{
	struct ast_extension_state_presence_snapshot *presence_snapshot;

	presence_snapshot = ao2_alloc_options(sizeof(*presence_snapshot), extension_state_presence_snapshot_destroy,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!presence_snapshot) {
		return NULL;
	}

	/* To ensure that we don't give a partial snapshot we fail creation if any allocation fails */
	presence_snapshot->presence_state = presence_state;
	if (presence_subtype) {
		presence_snapshot->presence_subtype = ast_strdup(presence_subtype);
		if (!presence_snapshot->presence_subtype) {
			ao2_ref(presence_snapshot, -1);
			return NULL;
		}
	}
	if (presence_message) {
		presence_snapshot->presence_message = ast_strdup(presence_message);
		if (!presence_snapshot->presence_message) {
			ao2_ref(presence_snapshot, -1);
			return NULL;
		}
	}

	return presence_snapshot;
}

/*!
 * \brief device source non-matching version comparator for AST_VECTOR_REMOVE_CMP_UNORDERED()
 *
 * \param elem Element to compare against
 * \param value Value to compare with the vector element.
 *
 * \return 0 if element does not match.
 * \return Non-zero if element matches.
 */
#define DEVICE_SOURCE_ELEM_VERSION_CMP(elem, value) ((elem)->version != (value))

/*!
 * \internal
 * \brief Update the sources of an extension state
 *
 * \param state The extension state to update
 * \param exten The extension to update
 * \retval 0 on success, -1 on failure
 *
 * This function updates the sources of an extension state by parsing the app part
 * of the extension and updating the device and presence state sources.
 */
static int extension_state_update_sources(struct extension_state *state, struct ast_exten *exten)
{
	struct ast_str *str = ast_str_thread_get(&hintdevice_data, HINTDEVICE_DATA_LENGTH);
	char *devices, *device, *presence_state_sources;
	struct ast_devstate_aggregate agg;
	enum ast_extension_states new_device_state;
	unsigned int version;
	struct ast_extension_state_device_snapshot *device_snapshot = NULL;
	struct ast_extension_state_presence_snapshot *presence_snapshot = NULL;

	ast_str_set(&str, 0, "%s", ast_get_extension_app(exten));
	devices = ast_str_buffer(str);

	ao2_lock(state);

	/*
	 * The format of the app part of a hint is "[device[&device]],[presence[&presence]]" so
	 * we can just find the first occurrence of ',' in order to get to the presence sources.
	 */
	presence_state_sources = strchr(devices, ',');
	if (presence_state_sources) {
		*presence_state_sources++ = '\0';
	}

	ast_devstate_aggregate_init(&agg);

	version = ast_random();

	/* Devices are separated by '&' */
	while ((device = strsep(&devices, "&"))) {
		struct extension_state_device_source *source = NULL;
		int i;

		/* Skip any device names that are empty, as we can do nothing */
		if (ast_strlen_zero(device)) {
			continue;
		}

		for (i = 0; i < AST_VECTOR_SIZE(&state->device_state_sources); i++) {
			struct extension_state_device_source *existing_source = AST_VECTOR_GET(&state->device_state_sources, i);

			if (!strcmp(existing_source->info->device, device)) {
				source = existing_source;
				break;
			}
		}
		if (!source) {
			source = extension_state_device_source_alloc(state, device);
			if (!source) {
				ao2_unlock(state);
				return -1;
			}
			AST_VECTOR_APPEND(&state->device_state_sources, source);
		}

		ast_devstate_aggregate_add(&agg, source->info->state);
		source->version = version;
	}

	/* Do a pass and remove all old device sources */
	AST_VECTOR_REMOVE_ALL_CMP_UNORDERED(&state->device_state_sources, version,
		DEVICE_SOURCE_ELEM_VERSION_CMP, extension_state_device_source_destroy);

	/* If device state sources exist it is what produces the device state, otherwise we use the default */
	if (AST_VECTOR_SIZE(&state->device_state_sources)) {
		new_device_state = ast_devstate_to_extenstate(ast_devstate_aggregate_result(&agg));
	} else {
		new_device_state = AST_EXTENSION_UNAVAILABLE;
	}

	/* If the device state has changed, create a new snapshot */
	if (state->device_snapshot->state != new_device_state) {
		device_snapshot = extension_state_device_snapshot_create(new_device_state, &state->device_state_sources, NULL);
	}
	if (!device_snapshot) {
		/* If there's no new device snapshot we use the old one */
		device_snapshot = state->device_snapshot;
	}

	/* If the presence state has changed, create a new snapshot */
	if ((!state->presence_sources_string && presence_state_sources) ||
		(state->presence_sources_string && strcmp(state->presence_sources_string, presence_state_sources))) {
		enum ast_presence_state presence_state = AST_PRESENCE_NOT_SET;
		char *presence_subtype = NULL, *presence_message = NULL;

		ast_free(state->presence_sources_string);

		if (!ast_strlen_zero(presence_state_sources)) {
			state->presence_sources_string = ast_strdup(presence_state_sources);
			/* Presence state is also separated by & but only the presence state API can handle it and aggregate */
			presence_state = ast_presence_state(presence_state_sources, &presence_subtype,
				&presence_message);
		} else {
			state->presence_sources_string = NULL;
		}

		presence_snapshot = extension_state_presence_snapshot_create(presence_state, presence_subtype, presence_message);

		ast_free(presence_subtype);
		ast_free(presence_message);
	}
	if (!presence_snapshot) {
		/* If there's no new presence snapshot we use the old one */
		presence_snapshot = state->presence_snapshot;
	}

	/* If any snapshots have changed create an update message containing them */
	if (state->device_snapshot != device_snapshot || state->presence_snapshot != presence_snapshot) {
		struct ast_extension_state_update_message *update_message;

		update_message = extension_state_update_message_create(state->dialplan_context, state->dialplan_extension, state->device_snapshot,
			device_snapshot, state->presence_snapshot, presence_snapshot);
		if (update_message) {
			struct stasis_message *message = stasis_message_create(ast_extension_state_update_message_type(), update_message);

			if (message) {
				stasis_publish(state->extension_state_topic, message);
				ao2_ref(message, -1);
			}

			ao2_ref(update_message, -1);
		}

		/* If applicable, update the snapshots on the state to their new version */
		if (state->device_snapshot != device_snapshot) {
			ao2_replace(state->device_snapshot, device_snapshot);
			ao2_ref(device_snapshot, -1);
		}
		if (state->presence_snapshot != presence_snapshot) {
			ao2_replace(state->presence_snapshot, presence_snapshot);
			ao2_ref(presence_snapshot, -1);
		}
	}

	ao2_unlock(state);

	return 0;
}

/*!
 * \internal
 * \brief Destroy an extension state
 *
 * \param obj The extension state to destroy
 *
 * This function destroys an extension state by cleaning up its resources.
 */
static void extension_state_destroy(void *obj)
{
	struct extension_state *state = obj;

	ao2_cleanup(state->device_snapshot);
	ao2_cleanup(state->presence_snapshot);
	ao2_cleanup(state->extension_state_topic);

	ast_free(state->presence_sources_string);
}

/*! \brief Stasis message type for extension state remove messages */
STASIS_MESSAGE_TYPE_DEFN(ast_extension_state_remove_message_type);

/*!
 * \internal
 * \brief Create an extension state remove message
 *
 * \param context The context of the extension
 * \param extension The extension to remove
 * \retval A stasis message for the remove event, or NULL on failure
 *
 * This function creates an extension state remove message for the specified context and extension.
 */
static struct stasis_message *extension_state_remove_message_create(const char *context, const char *extension)
{
	size_t context_len = strlen(context) + 1;
	size_t extension_len = strlen(extension) + 1;
	struct ast_extension_state_remove_message *remove_message;
	struct stasis_message *message;

	remove_message = ao2_alloc_options(sizeof(*remove_message) + context_len + extension_len, NULL,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!remove_message) {
		return NULL;
	}

	ast_copy_string(remove_message->extension, extension, extension_len); /* Safe */
	remove_message->context = remove_message->extension + extension_len;
	ast_copy_string(remove_message->context, context, context_len); /* Safe */

	message = stasis_message_create(ast_extension_state_remove_message_type(), remove_message);
	ao2_ref(remove_message, -1);

	return message;
}

/*!
 * \internal
 * \brief Shut down an extension state
 *
 * \param state The extension state to shut down
 *
 * This function shuts down an extension state by publishing a remove message to its topic
 * and cleaning up its resources.
 */
static void extension_state_shutdown(struct extension_state *state)
{
	struct stasis_message *remove_message;

	/*
	 * Shutting down an extension state requires us to publish to its topic so all subscribers
	 * know that it is going away. However, if the topic failed to be created then we have nothing
	 * to publish to and can just return.
	 */
	if (!state->extension_state_topic) {
		return;
	}

	/* Inform all subscribers that this extension state is being removed */
	remove_message = extension_state_remove_message_create(state->dialplan_context,
		state->dialplan_extension);
	if (remove_message) {
		stasis_publish(state->extension_state_topic, remove_message);
		ao2_ref(remove_message, -1);
	}

	AST_VECTOR_CALLBACK_VOID(&state->device_state_sources, extension_state_device_source_destroy);
	AST_VECTOR_FREE(&state->device_state_sources);

	stasis_forward_cancel(state->extension_state_forwarder);
}

/*!
 * \internal
 * \brief Callback for presence state messages
 *
 * \param unused Unused parameter
 * \param sub The stasis subscription
 * \param msg The stasis message
 *
 * This callback is invoked when a presence state message is received. It updates
 * the presence state of all extension states that are interested in the presence
 * state provider and publishes an extension state update message if it has changed.
 */
static void extension_state_presence_state_cb(void *unused, struct stasis_subscription *sub,
		struct stasis_message *msg)
{
	struct ast_presence_state_message *presence_state;
	struct ao2_iterator iter;
	struct extension_state *state;

	if (stasis_message_type(msg) != ast_presence_state_message_type()) {
		return;
	}

	presence_state = stasis_message_data(msg);

	ao2_lock(extension_states);
	iter = ao2_iterator_init(extension_states, 0);
	for (; (state = ao2_iterator_next(&iter)); ao2_ref(state, -1)) {
		enum ast_presence_state presence_state_new;
		char *presence_subtype, *presence_message;
		struct ast_extension_state_presence_snapshot *presence_snapshot;
		struct ast_extension_state_update_message *update_message;

		ao2_lock(state);

		/*
		 * We determine if this update is relevant to this extension state by seeing if the presence sources string
		 * even remotely contains the provider for this update. Worst case it's a substring and the calculated presence
		 * state is the same as before in which case we ignore it.
		 */
		if (!state->presence_sources_string || !strcasestr(state->presence_sources_string, presence_state->provider)) {
			ao2_unlock(state);
			continue;
		}

		/*
		 * Aggregation of presence state is done by requesting the current presence state with passing in a complete
		 * list of providers. This means that a presence state change message is just a notification to us to go and
		 * retrieve the new presence state. We don't just take it from the message itself. Since presence state is not
		 * as common as device state this is not a problem despite being inefficient in comparison to the device state
		 * implementation.
		 */
		presence_state_new = ast_presence_state(state->presence_sources_string, &presence_subtype, &presence_message);
		if (presence_state_new == AST_PRESENCE_INVALID) {
			/* For the invalid case we just ignore this update */
			ao2_unlock(state);
			continue;
		}

		if ((state->presence_snapshot->presence_state == presence_state_new) &&
			((!presence_subtype && !state->presence_snapshot->presence_subtype) ||
				(presence_subtype && state->presence_snapshot->presence_subtype &&
					!strcmp(presence_subtype, state->presence_snapshot->presence_subtype))) &&
			((!presence_message && !state->presence_snapshot->presence_message) ||
				(presence_message && state->presence_snapshot->presence_message &&
					!strcmp(presence_message, state->presence_snapshot->presence_message)))) {
			/* No change in presence state, so ignore this update */
			ao2_unlock(state);
			ast_free(presence_subtype);
			ast_free(presence_message);
			continue;
		}

		presence_snapshot = extension_state_presence_snapshot_create(presence_state_new, presence_subtype, presence_message);
		if (!presence_snapshot) {
			ao2_unlock(state);
			ast_free(presence_subtype);
			ast_free(presence_message);
			continue;
		}

		update_message = extension_state_update_message_create(state->dialplan_context,
			state->dialplan_extension, state->device_snapshot, state->device_snapshot, state->presence_snapshot, presence_snapshot);
		ao2_replace(state->presence_snapshot, presence_snapshot);
		ao2_ref(presence_snapshot, -1);
		if (update_message) {
			struct stasis_message *message = stasis_message_create(ast_extension_state_update_message_type(), update_message);

			if (message) {
				stasis_publish(state->extension_state_topic, message);
				ao2_ref(message, -1);
			}

			ao2_ref(update_message, -1);
		}

		ao2_unlock(state);

		ast_free(presence_subtype);
		ast_free(presence_message);
	}
	ao2_iterator_destroy(&iter);
	ao2_unlock(extension_states);
}

/*!
 * \internal
 *
 * \brief Allocate an extension state object
 * \param exten The extension
 * \param context The context
 * \retval A pointer to the allocated extension state, or NULL on failure
 *
 * This function allocates an extension state object with the specified extension and context.
 */
static struct extension_state *extension_state_alloc(struct ast_exten *exten, struct ast_context *context)
{
	char extension[AST_MAX_EXTENSION + AST_MAX_CONTEXT + 2];
	struct extension_state *state;
	char *extension_state_topic_name;

	snprintf(extension, sizeof(extension), "%s@%s", ast_get_extension_name(exten), ast_get_context_name(context));

	/*
	 * Each individual extension state has its own lock to ensure that when updating it
	 * we do not cause problems for either the existing topic ingesting updates
	 * or any access to the extension state cached message.
	 */
	state = ao2_alloc(sizeof(*state) + strlen(extension) + 1, extension_state_destroy);
	if (!state) {
		return NULL;
	}

	ast_copy_string(state->dialplan_context, ast_get_context_name(context), sizeof(state->dialplan_context));
	ast_copy_string(state->dialplan_extension, ast_get_extension_name(exten), sizeof(state->dialplan_extension));
	strcpy(state->extension, extension); /* Safe */
	AST_VECTOR_INIT(&state->device_state_sources, 0);

	/* These are the default if no sources are present */
	state->device_snapshot = extension_state_device_snapshot_create(AST_EXTENSION_UNAVAILABLE,
		&state->device_state_sources, NULL);
	state->presence_snapshot = extension_state_presence_snapshot_create(AST_PRESENCE_NOT_SET, NULL, NULL);
	if (!state->device_snapshot || !state->presence_snapshot) {
		ao2_ref(state, -1);
		return NULL;
	}

	/*
	 * We don't actually access the contents of exten past guarantee of it being valid so we can safely
	 * store a pointer to just do pointer comparison.
	 */
	state->hint_extension = exten;

	/* Pattern match extensions don't have sources or a topic, so return early */
	if (extension[0] == '_') {
		return state;
	}

	/* We most likely have at least one device state source */
	if (AST_VECTOR_INIT(&state->device_state_sources, 1)) {
		ao2_ref(state, -1);
		return NULL;
	}

	if (ast_asprintf(&extension_state_topic_name, "extension_state:extension/%s", extension) < 0) {
		ao2_ref(state, -1);
		return NULL;
	}

	state->extension_state_topic = stasis_topic_create(extension_state_topic_name);
	ast_free(extension_state_topic_name);
	if (!state->extension_state_topic) {
		ao2_ref(state, -1);
		return NULL;
	}

	state->extension_state_forwarder = stasis_forward_all(state->extension_state_topic, extension_state_topic_all);
	if (!state->extension_state_forwarder) {
		ao2_ref(state, -1);
		return NULL;
	}

	return state;
}

/*!
 * \internal
 *
 * \brief Get an extension state object
 * \param chan The channel
 * \param context The context
 * \param extension The extension
 * \retval A pointer to the extension state, or NULL on failure
 *
 * This function gets an extension state object for the specified channel, context, and extension.
 * If the extension state does not exist due to being from a pattern match, it will be created.
 */
static struct extension_state *extension_state_get(struct ast_channel *chan, const char *context, const char *extension)
{
	struct extension_state *state;
	struct ast_exten *hint_exten;
	struct pbx_find_info q = { .stacklen = 0 };
	char location[AST_MAX_EXTENSION + AST_MAX_CONTEXT + 2];

	/* We optimistically search for the extension state using the provided context and extension */
	snprintf(location, sizeof(location), "%s@%s", extension, context);
	state = ao2_find(extension_states, location, OBJ_SEARCH_KEY);
	if (state) {
		return state;
	}

	/*
	 * Pattern match extensions do exist within extension state for the purposes of listing them out,
	 * but they can't resolve down to anything else
	 */
	if (extension[0] == '_') {
		return NULL;
	}

	ast_wrlock_contexts();

	/*
	 * We can't use the provided context and extension as-is because an include
	 * could have resulted in the context being different than what was provided.
	 * To handle this we query the dialplan to find where the hint actually is.
	 * We also need to do this to determine if this is a pattern match or an explicit
	 * extension.
	 */
	hint_exten = pbx_find_extension(chan, NULL, &q, context, extension,
		PRIORITY_HINT, NULL, "", E_MATCH);
	if (!hint_exten) {
		/*
		 * The extension must ALWAYS exist in the dialplan in some capacity. It is
		 * either in the dialplan as an explicit extension or a pattern match.
		 */
		ast_unlock_contexts();
		return NULL;
	}

	if (ast_get_extension_name(hint_exten)[0] == '_') {
		/*
		 * If this resolved down to a pattern match that means this is the first request
		 * for this explicit extension so we need to add it to the dialplan which will create
		 * an extension state for it. It's possible for us to conflict with another thread but
		 * in that case the ast_add_extension call will fail and be a no-op and we will return
		 * the extension state the other thread created.
		 */
		ast_add_extension(q.foundcontext, 0, extension, PRIORITY_HINT, ast_get_extension_label(hint_exten),
			ast_get_extension_matchcid(hint_exten) ? ast_get_extension_cidmatch(hint_exten) : NULL,
			ast_get_extension_app(hint_exten), ast_strdup(ast_get_extension_app_data(hint_exten)), ast_free_ptr,
			ast_get_extension_registrar(hint_exten));
	}

	/* The extension state should already exist at this point */
	snprintf(location, sizeof(location), "%s@%s", extension, q.foundcontext);
	ast_unlock_contexts();

	return ao2_find(extension_states, location, OBJ_SEARCH_KEY);
}

struct ast_channel *ast_extension_state_get_device_causing_channel(const char *device, enum ast_device_state device_state)
{
	enum ast_channel_state search_state = 0; /* prevent false uninit warning */
	char match[AST_CHANNEL_NAME];
	struct ast_channel_iterator *chan_iter;
	struct ast_channel *chan, *channel = NULL;
	struct timeval chantime = {0, }; /* prevent false uninit warning */

	switch (device_state) {
	case AST_DEVICE_RINGING:
	case AST_DEVICE_RINGINUSE:
		/* find ringing channel */
		search_state = AST_STATE_RINGING;
		break;
	case AST_DEVICE_BUSY:
		/* find busy channel */
		search_state = AST_STATE_BUSY;
		break;
	case AST_DEVICE_ONHOLD:
	case AST_DEVICE_INUSE:
		/* find up channel */
		search_state = AST_STATE_UP;
		break;
	case AST_DEVICE_UNKNOWN:
	case AST_DEVICE_NOT_INUSE:
	case AST_DEVICE_INVALID:
	case AST_DEVICE_UNAVAILABLE:
	case AST_DEVICE_TOTAL /* not a state */:
		/* no channels are of interest */
		return NULL;
	}

	/* iterate over all channels of the device */
	snprintf(match, sizeof(match), "%s-", device);
	chan_iter = ast_channel_iterator_by_name_new(match, strlen(match));
	for (; (chan = ast_channel_iterator_next(chan_iter)); ast_channel_unref(chan)) {
		ast_channel_lock(chan);
		/* this channel's state doesn't match */
		if (search_state != ast_channel_state(chan)) {
			ast_channel_unlock(chan);
			continue;
		}
		/* any non-ringing channel will fit */
		if (search_state != AST_STATE_RINGING) {
			ast_channel_unlock(chan);
			channel = chan;
			break;
		}
		/* but we need the oldest ringing channel of the device to match with undirected pickup */
		if (!channel) {
			chantime = ast_channel_creationtime(chan);
			ast_channel_ref(chan); /* must ref it! */
			channel = chan;
		} else if (ast_tvcmp(ast_channel_creationtime(chan), chantime) < 0) {
			chantime = ast_channel_creationtime(chan);
			ast_channel_unref(channel);
			ast_channel_ref(chan); /* must ref it! */
			channel = chan;
		}
		ast_channel_unlock(chan);
	}
	ast_channel_iterator_destroy(chan_iter);

	return channel;
}

enum ast_extension_states ast_devstate_to_extenstate(enum ast_device_state devstate)
{
	switch (devstate) {
	case AST_DEVICE_ONHOLD:
		return AST_EXTENSION_ONHOLD;
	case AST_DEVICE_BUSY:
		return AST_EXTENSION_BUSY;
	case AST_DEVICE_UNKNOWN:
		return AST_EXTENSION_NOT_INUSE;
	case AST_DEVICE_UNAVAILABLE:
	case AST_DEVICE_INVALID:
		return AST_EXTENSION_UNAVAILABLE;
	case AST_DEVICE_RINGINUSE:
		return (AST_EXTENSION_INUSE | AST_EXTENSION_RINGING);
	case AST_DEVICE_RINGING:
		return AST_EXTENSION_RINGING;
	case AST_DEVICE_INUSE:
		return AST_EXTENSION_INUSE;
	case AST_DEVICE_NOT_INUSE:
		return AST_EXTENSION_NOT_INUSE;
	case AST_DEVICE_TOTAL: /* not a device state, included for completeness */
		break;
	}

	return AST_EXTENSION_NOT_INUSE;
}

const char *ast_extension_state2str(int extension_state)
{
	int i;

	for (i = 0; (i < ARRAY_LEN(extension_state_mappings)); i++) {
		if (extension_state_mappings[i].extension_state == extension_state)
			return extension_state_mappings[i].text;
	}
	return "Unknown";
}

/*!
 * \internal
 * \brief Handle the ExtensionStateList Manager action
 *
 * \param s The manager session
 * \param m The manager message
 * \retval 0 on success, -1 on failure
 *
 * This function handles the ExtensionStateList Manager action by returning a list of all extension states.
 */
static int action_extensionstatelist(struct mansession *s, const struct message *m)
{
	const char *action_id = astman_get_header(m, "ActionID");

	if (!ao2_container_count(extension_states)) {
		astman_send_error(s, m, "No dialplan hints are available");
		return 0;
	}

	astman_send_listack(s, m, "Extension Statuses will follow", "start");

	ao2_lock(extension_states);
	if (ao2_container_count(extension_states)) {
		struct ao2_iterator it_states;
		struct extension_state *state;
		int count = 0;

		it_states = ao2_iterator_init(extension_states, 0);
		for (; (state = ao2_iterator_next(&it_states)); ao2_ref(state, -1)) {
			if (state->extension[0] == '_') {
				continue;
			}

			++count;

			astman_append(s, "Event: ExtensionStatus\r\n");
			if (!ast_strlen_zero(action_id)) {
				astman_append(s, "ActionID: %s\r\n", action_id);
			}
			ao2_lock(state);
			astman_append(s,
			   "Exten: %s\r\n"
			   "Context: %s\r\n"
			   "Hint: %s\r\n"
			   "Status: %d\r\n"
			   "StatusText: %s\r\n\r\n",
			   state->dialplan_extension,
			   state->dialplan_context,
			   state->hint_extension ? ast_get_extension_app(state->hint_extension) : "None",
			   state->device_snapshot->state,
			   ast_extension_state2str(state->device_snapshot->state));
			ao2_unlock(state);
		}
		ao2_iterator_destroy(&it_states);
		astman_send_list_complete_start(s, m, "ExtensionStateListComplete", count);
		astman_send_list_complete_end(s);
	} else {
		astman_send_error(s, m, "No dialplan hints are available");
	}

	ao2_unlock(extension_states);

	return 0;
}

void pbx_extension_state_hint_set(struct ast_exten *exten, struct ast_context *context)
{
	char extension[AST_MAX_EXTENSION + AST_MAX_CONTEXT + 2];
	struct extension_state *state;

	snprintf(extension, sizeof(extension), "%s@%s", ast_get_extension_name(exten), ast_get_context_name(context));

	ao2_lock(extension_states);

	state = ao2_find(extension_states, extension, OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (!state) {
		state = extension_state_alloc(exten, context);
		if (!state) {
			ast_log(LOG_WARNING, "Could not create extension state for hint '%s', it will be unavailable\n",
				extension);
			ao2_unlock(extension_states);
			return;
		}
		ao2_link_flags(extension_states, state, OBJ_NOLOCK);
	}

	state->hint_extension = exten;
	if (extension[0] != '_') {
		extension_state_update_sources(state, exten);
	}
	ao2_ref(state, -1);

	ao2_unlock(extension_states);
}

void pbx_extension_state_hint_remove(struct ast_exten *exten, struct ast_context *context)
{
	char extension[AST_MAX_EXTENSION + AST_MAX_CONTEXT + 2];
	struct extension_state *state;

	snprintf(extension, sizeof(extension), "%s@%s", ast_get_extension_name(exten), ast_get_context_name(context));

	ao2_lock(extension_states);

	state = ao2_find(extension_states, extension, OBJ_SEARCH_KEY | OBJ_NOLOCK);

	/* If this is not the latest hint extension that configured this extension state it can't remove it */
	if (!state || state->hint_extension != exten) {
		ao2_unlock(extension_states);
		ao2_cleanup(state);
		return;
	}

	extension_state_shutdown(state);
	ao2_unlink_flags(extension_states, state, OBJ_NOLOCK);
	ao2_ref(state, -1);

	ao2_unlock(extension_states);
}

struct stasis_topic *ast_extension_state_topic_all(void)
{
	return extension_state_topic_all;
}

struct stasis_topic *ast_extension_state_topic(const char *exten, const char *context)
{
	struct extension_state *state;
	struct stasis_topic *topic;

	state = extension_state_get(NULL, context, exten);
	if (!state) {
		return NULL;
	}

	ao2_lock(state);
	topic = ao2_bump(state->extension_state_topic);
	ao2_unlock(state);
	ao2_ref(state, -1);

	return topic;
}

struct ast_extension_state_device_snapshot *ast_extension_state_get_latest_device_snapshot(struct ast_channel *chan,
	const char *exten, const char *context)
{
	struct extension_state *state;
	struct ast_extension_state_device_snapshot *device_snapshot;

	state = extension_state_get(chan, context, exten);
	if (!state) {
		return NULL;
	}

	ao2_lock(state);
	device_snapshot = ao2_bump(state->device_snapshot);
	ao2_unlock(state);
	ao2_ref(state, -1);

	return device_snapshot;
}

struct ast_extension_state_presence_snapshot *ast_extension_state_get_latest_presence_snapshot(struct ast_channel *chan,
	const char *exten, const char *context)
{
	struct extension_state *state;
	struct ast_extension_state_presence_snapshot *presence_snapshot;

	state = extension_state_get(chan, context, exten);
	if (!state) {
		return NULL;
	}

	ao2_lock(state);
	presence_snapshot = ao2_bump(state->presence_snapshot);
	ao2_unlock(state);
	ao2_ref(state, -1);

	return presence_snapshot;
}

/*!
 * \internal
 * \brief CLI command to show hints
 *
 * \param e The CLI entry
 * \param cmd The command
 * \param a The CLI arguments
 *
 * This function shows all registered hints in the CLI.
 */
static char *handle_show_hints(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct extension_state *state;
	int num = 0;
	struct ao2_iterator i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show hints";
		e->usage =
			"Usage: core show hints\n"
			"       List registered hints.\n"
			"       Hint details are shown in five columns. In order from left to right, they are:\n"
			"       1. Hint extension URI.\n"
			"       2. List of mapped device or presence state identifiers.\n"
			"       3. Current extension state. The aggregate of mapped device states.\n"
			"       4. Current presence state for the mapped presence state provider.\n"
			"       5. Watchers - number of subscriptions and other entities watching this hint.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (ao2_container_count(extension_states) == 0) {
		ast_cli(a->fd, "There are no registered dialplan hints\n");
		return CLI_SUCCESS;
	}
	/* ... we have hints ... */
	ast_cli(a->fd, "\n    -= Registered Asterisk Dial Plan Hints =-\n");

	i = ao2_iterator_init(extension_states, 0);
	for (; (state = ao2_iterator_next(&i)); ao2_ref(state, -1)) {
		ao2_lock(state);
		ast_cli(a->fd, "%-30.30s: %-60.60s  State:%-15.15s Presence:%-15.15s Watchers %2zd\n",
			state->extension,
			state->hint_extension ? ast_get_extension_app(state->hint_extension) : "None",
			ast_extension_state2str(state->device_snapshot->state),
			ast_presence_state2str(state->presence_snapshot->presence_state),
			state->extension_state_topic ? stasis_topic_subscribers(state->extension_state_topic) : 0);
		ao2_unlock(state);

		num++;
	}
	ao2_iterator_destroy(&i);

	ast_cli(a->fd, "----------------\n");
	ast_cli(a->fd, "- %d hints registered\n", num);
	return CLI_SUCCESS;
}

/*!
 * \internal
 * \brief Complete the core show hint CLI command
 *
 * \param line The command line
 * \param word The word being completed
 * \param pos The position in the command
 * \param state The completion state
 *
 * This function completes the core show hint CLI command.
 */
static char *complete_core_show_hint(const char *line, const char *word, int pos, int state)
{
	struct extension_state *extstate;
	char *ret = NULL;
	int which = 0;
	int wordlen;
	struct ao2_iterator i;

	if (pos != 3)
		return NULL;

	wordlen = strlen(word);

	/* walk through all hints */
	i = ao2_iterator_init(extension_states, 0);
	for (; (extstate = ao2_iterator_next(&i)); ao2_ref(extstate, -1)) {
		ao2_lock(extstate);
		if (!strncasecmp(word, extstate->dialplan_extension, wordlen) && ++which > state) {
			ret = ast_strdup(extstate->dialplan_extension);
			ao2_unlock(extstate);
			ao2_ref(extstate, -1);
			break;
		}
		ao2_unlock(extstate);
	}
	ao2_iterator_destroy(&i);

	return ret;
}

/*!
 * \internal
 * \brief CLI support for listing registered dial plan hint
 *
 * \param e The CLI entry
 * \param cmd The command
 * \param a The CLI arguments
 *
 * This function handles the core show hint CLI command.
 */
static char *handle_show_hint(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct extension_state *extstate;
	int num = 0, extenlen;
	struct ao2_iterator i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show hint";
		e->usage =
			"Usage: core show hint <exten>\n"
			"       List registered hint.\n"
			"       Hint details are shown in five columns. In order from left to right, they are:\n"
			"       1. Hint extension URI.\n"
			"       2. List of mapped device or presence state identifiers.\n"
			"       3. Current extension state. The aggregate of mapped device states.\n"
			"       4. Current presence state for the mapped presence state provider.\n"
			"       5. Watchers - number of subscriptions and other entities watching this hint.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_core_show_hint(a->line, a->word, a->pos, a->n);
	}

	if (a->argc < 4)
		return CLI_SHOWUSAGE;

	if (ao2_container_count(extension_states) == 0) {
		ast_cli(a->fd, "There are no registered dialplan hints\n");
		return CLI_SUCCESS;
	}

	extenlen = strlen(a->argv[3]);
	i = ao2_iterator_init(extension_states, 0);
	for (; (extstate = ao2_iterator_next(&i)); ao2_ref(extstate, -1)) {
		ao2_lock(extstate);
		if (!strncasecmp(extstate->extension, a->argv[3], extenlen)) {
			ast_cli(a->fd, "%-30.30s: %-60.60s  State:%-15.15s Presence:%-15.15s Watchers %2zd\n",
				extstate->extension,
				extstate->hint_extension ? ast_get_extension_app(extstate->hint_extension) : "None",
				ast_extension_state2str(extstate->device_snapshot->state),
				ast_presence_state2str(extstate->presence_snapshot->presence_state),
				extstate->extension_state_topic ? stasis_topic_subscribers(extstate->extension_state_topic) : 0);
			num++;
		}
		ao2_unlock(extstate);
	}
	ao2_iterator_destroy(&i);
	if (!num)
		ast_cli(a->fd, "No hints matching extension %s\n", a->argv[3]);
	else
		ast_cli(a->fd, "%d hint%s matching extension %s\n", num, (num!=1 ? "s":""), a->argv[3]);
	return CLI_SUCCESS;
}

static struct ast_cli_entry extension_state_cli[] = {
	AST_CLI_DEFINE(handle_show_hints, "Show dialplan hints"),
	AST_CLI_DEFINE(handle_show_hint, "Show dialplan hint"),
};

/*!
 * \internal
 * \brief Callback function to clean up an individual extension state.
 *
 * \param obj The extension state object
 * \param arg Additional argument (not used)
 * \param flags Flags for the callback
 * \return CMP_MATCH if the object was processed, 0 otherwise
 */
static int extension_state_cleanup_individual(void *obj, void *arg, int flags)
{
	struct extension_state *state = obj;

	extension_state_shutdown(state);

	return CMP_MATCH;
}

/*!
 * \internal
 * \brief Clean up the extension state subsystem, called at shutdown.
 */
static void extension_state_cleanup(void)
{
	ast_cli_unregister_multiple(extension_state_cli, ARRAY_LEN(extension_state_cli));
	ast_manager_unregister("ExtensionStateList");
	if (extension_states) {
		ao2_callback(extension_states, OBJ_NODATA | OBJ_MULTIPLE | OBJ_UNLINK, extension_state_cleanup_individual, NULL);
		ao2_ref(extension_states, -1);
	}
	presence_state_sub = stasis_unsubscribe_and_join(presence_state_sub);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_extension_state_update_message_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_extension_state_remove_message_type);
}

int ast_extension_state_init(void)
{
	if (STASIS_MESSAGE_TYPE_INIT(ast_extension_state_update_message_type) != 0) {
		return -1;
	}

	if (STASIS_MESSAGE_TYPE_INIT(ast_extension_state_remove_message_type) != 0) {
		return -1;
	}

	/* Initialize extension state subsystem */
	extension_states = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		EXTENSION_STATE_BUCKETS, extension_state_hash_fn, NULL,
		extension_state_cmp_fn);
	if (!extension_states) {
		ast_log(LOG_ERROR, "Failed to allocate new states container\n");
		return -1;
	}

	extension_state_topic_all = stasis_topic_create("extension_state:all");
	if (!extension_state_topic_all) {
		ast_log(LOG_ERROR, "Failed to create extension state topic\n");
		return -1;
	}

	presence_state_sub = stasis_subscribe(ast_presence_state_topic_all(), extension_state_presence_state_cb, NULL);
	if (!presence_state_sub) {
		ast_log(LOG_ERROR, "Failed to create subscription to receive presence state updates\n");
		return -1;
	}
	stasis_subscription_accept_message_type(presence_state_sub, ast_presence_state_message_type());
	stasis_subscription_set_filter(presence_state_sub, STASIS_SUBSCRIPTION_FILTER_SELECTIVE);

	ast_cli_register_multiple(extension_state_cli, ARRAY_LEN(extension_state_cli));
	ast_manager_register_xml_core("ExtensionStateList", EVENT_FLAG_CALL | EVENT_FLAG_REPORTING, action_extensionstatelist);
	ast_register_cleanup(extension_state_cleanup);

	return 0;
}
