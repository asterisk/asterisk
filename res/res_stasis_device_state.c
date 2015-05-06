/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Kevin Harwell <kharwell@digium.com>
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

/*** MODULEINFO
	<depend type="module">res_stasis</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/astdb.h"
#include "asterisk/astobj2.h"
#include "asterisk/module.h"
#include "asterisk/stasis_app_impl.h"
#include "asterisk/stasis_app_device_state.h"

#define DEVICE_STATE_SIZE 64
/*! astdb family name */
#define DEVICE_STATE_FAMILY "StasisDeviceState"
/*! Stasis device state provider */
#define DEVICE_STATE_PROVIDER_STASIS "Stasis"
/*! Scheme for custom device states */
#define DEVICE_STATE_SCHEME_STASIS "Stasis:"
/*! Scheme for device state subscriptions */
#define DEVICE_STATE_SCHEME_SUB "deviceState:"

/*! Number of hash buckets for device state subscriptions */
#define DEVICE_STATE_BUCKETS 37

/*! Container for subscribed device states */
static struct ao2_container *device_state_subscriptions;

/*!
 * \brief Device state subscription object.
 */
struct device_state_subscription {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(app_name);
		AST_STRING_FIELD(device_name);
	);
	/*! The subscription object */
	struct stasis_subscription *sub;
};

static int device_state_subscriptions_hash(const void *obj, const int flags)
{
	const struct device_state_subscription *object;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		object = obj;
		return ast_str_hash(object->device_name);
	case OBJ_SEARCH_KEY:
	default:
		/* Hash can only work on something with a full key. */
		ast_assert(0);
		return 0;
	}
}

static int device_state_subscriptions_cmp(void *obj, void *arg, int flags)
{
	const struct device_state_subscription *object_left = obj;
	const struct device_state_subscription *object_right = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		/* find objects matching both device and app names */
		if (strcmp(object_left->device_name,
			   object_right->device_name)) {
			return 0;
		}
		cmp = strcmp(object_left->app_name, object_right->app_name);
		break;
	case OBJ_SEARCH_KEY:
	case OBJ_SEARCH_PARTIAL_KEY:
		ast_assert(0); /* not supported by container */
		/* fall through */
	default:
		cmp = 0;
		break;
	}

	return cmp ? 0 : CMP_MATCH | CMP_STOP;
}

static void device_state_subscription_destroy(void *obj)
{
	struct device_state_subscription *sub = obj;
	sub->sub = stasis_unsubscribe(sub->sub);
	ast_string_field_free_memory(sub);
}

static struct device_state_subscription *device_state_subscription_create(
	const struct stasis_app *app, const char *device_name)
{
	struct device_state_subscription *sub = ao2_alloc(
		sizeof(*sub), device_state_subscription_destroy);
	const char *app_name = stasis_app_name(app);
	size_t size = strlen(device_name) + strlen(app_name) + 2;

	if (!sub) {
		return NULL;
	}

	if (ast_string_field_init(sub, size)) {
		ao2_ref(sub, -1);
		return NULL;
	}

	ast_string_field_set(sub, app_name, app_name);
	ast_string_field_set(sub, device_name, device_name);
	return sub;
}

static struct device_state_subscription *find_device_state_subscription(
	struct stasis_app *app, const char *name)
{
	struct device_state_subscription dummy_sub = {
		.app_name = stasis_app_name(app),
		.device_name = name
	};

	return ao2_find(device_state_subscriptions, &dummy_sub, OBJ_SEARCH_OBJECT);
}

static void remove_device_state_subscription(
	struct device_state_subscription *sub)
{
	ao2_unlink(device_state_subscriptions, sub);
}

struct ast_json *stasis_app_device_state_to_json(
	const char *name, enum ast_device_state state)
{
	return ast_json_pack("{s: s, s: s}",
			     "name", name,
			     "state", ast_devstate_str(state));
}

struct ast_json *stasis_app_device_states_to_json(void)
{
	struct ast_json *array = ast_json_array_create();
	RAII_VAR(struct ast_db_entry *, tree,
		 ast_db_gettree(DEVICE_STATE_FAMILY, NULL), ast_db_freetree);
	struct ast_db_entry *entry;

	for (entry = tree; entry; entry = entry->next) {
		const char *name = strrchr(entry->key, '/');
		if (!ast_strlen_zero(name)) {
			struct ast_str *device = ast_str_alloca(DEVICE_STATE_SIZE);
			ast_str_set(&device, 0, "%s%s",
				    DEVICE_STATE_SCHEME_STASIS, ++name);
			ast_json_array_append(
				array, stasis_app_device_state_to_json(
					ast_str_buffer(device),
					ast_device_state(ast_str_buffer(device))));
		}
	}

	return array;
}

static void send_device_state(struct device_state_subscription *sub,
			      const char *name, enum ast_device_state state)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);

	json = ast_json_pack("{s:s, s:s, s:o, s:o}",
			     "type", "DeviceStateChanged",
			     "application", sub->app_name,
			     "timestamp", ast_json_timeval(ast_tvnow(), NULL),
			     "device_state", stasis_app_device_state_to_json(
				     name, state));

	if (!json) {
		ast_log(LOG_ERROR, "Unable to create device state json object\n");
		return;
	}

	stasis_app_send(sub->app_name, json);
}

enum stasis_device_state_result stasis_app_device_state_update(
	const char *name, const char *value)
{
	size_t size = strlen(DEVICE_STATE_SCHEME_STASIS);
	enum ast_device_state state;

	ast_debug(3, "Updating device name = %s, value = %s", name, value);

	if (strncasecmp(name, DEVICE_STATE_SCHEME_STASIS, size)) {
		ast_log(LOG_ERROR, "Update can only be used to set "
			"'%s' device state!\n", DEVICE_STATE_SCHEME_STASIS);
		return STASIS_DEVICE_STATE_NOT_CONTROLLED;
	}

	name += size;
	if (ast_strlen_zero(name)) {
		ast_log(LOG_ERROR, "Update requires custom device name!\n");
		return STASIS_DEVICE_STATE_MISSING;
	}

	if (!value || (state = ast_devstate_val(value)) == AST_DEVICE_UNKNOWN) {
		ast_log(LOG_ERROR, "Unknown device state "
			"value '%s'\n", value);
		return STASIS_DEVICE_STATE_UNKNOWN;
	}

	ast_db_put(DEVICE_STATE_FAMILY, name, value);
	ast_devstate_changed(state, AST_DEVSTATE_CACHABLE, "%s%s",
			     DEVICE_STATE_SCHEME_STASIS, name);

	return STASIS_DEVICE_STATE_OK;
}

enum stasis_device_state_result stasis_app_device_state_delete(const char *name)
{
	const char *full_name = name;
	size_t size = strlen(DEVICE_STATE_SCHEME_STASIS);

	if (strncasecmp(name, DEVICE_STATE_SCHEME_STASIS, size)) {
		ast_log(LOG_ERROR, "Can only delete '%s' device states!\n",
			DEVICE_STATE_SCHEME_STASIS);
		return STASIS_DEVICE_STATE_NOT_CONTROLLED;
	}

	name += size;
	if (ast_strlen_zero(name)) {
		ast_log(LOG_ERROR, "Delete requires a device name!\n");
		return STASIS_DEVICE_STATE_MISSING;
	}

	if (ast_device_state_clear_cache(full_name)) {
		return STASIS_DEVICE_STATE_UNKNOWN;
	}

	ast_db_del(DEVICE_STATE_FAMILY, name);

	/* send state change for delete */
	ast_devstate_changed(
		AST_DEVICE_UNKNOWN, AST_DEVSTATE_CACHABLE, "%s%s",
		DEVICE_STATE_SCHEME_STASIS, name);

	return STASIS_DEVICE_STATE_OK;
}

static void populate_cache(void)
{
	RAII_VAR(struct ast_db_entry *, tree,
		 ast_db_gettree(DEVICE_STATE_FAMILY, NULL), ast_db_freetree);
	struct ast_db_entry *entry;

	for (entry = tree; entry; entry = entry->next) {
		const char *name = strrchr(entry->key, '/');
		if (!ast_strlen_zero(name)) {
			ast_devstate_changed(
				ast_devstate_val(entry->data),
				AST_DEVSTATE_CACHABLE, "%s%s\n",
				DEVICE_STATE_SCHEME_STASIS, name + 1);
		}
	}
}

static enum ast_device_state stasis_device_state_cb(const char *data)
{
	char buf[DEVICE_STATE_SIZE] = "";

	ast_db_get(DEVICE_STATE_FAMILY, data, buf, sizeof(buf));

	return ast_devstate_val(buf);
}

static void device_state_cb(void *data, struct stasis_subscription *sub,
			    struct stasis_message *msg)
{
	struct ast_device_state_message *device_state;

	if (ast_device_state_message_type() != stasis_message_type(msg)) {
		return;
	}

	device_state = stasis_message_data(msg);
	if (device_state->eid) {
		/* ignore non-aggregate states */
		return;
	}

	send_device_state(data, device_state->device, device_state->state);
}

static void *find_device_state(const struct stasis_app *app, const char *name)
{
	return device_state_subscription_create(app, name);
}

static int is_subscribed_device_state(struct stasis_app *app, const char *name)
{
	RAII_VAR(struct device_state_subscription *, sub,
		 find_device_state_subscription(app, name), ao2_cleanup);
	return sub != NULL;
}

static int subscribe_device_state(struct stasis_app *app, void *obj)
{
	struct device_state_subscription *sub = obj;

	ast_debug(3, "Subscribing to device %s", sub->device_name);

	if (is_subscribed_device_state(app, sub->device_name)) {
		ast_debug(3, "App %s is already subscribed to %s\n", stasis_app_name(app), sub->device_name);
		return 0;
	}

	if (!(sub->sub = stasis_subscribe_pool(
			ast_device_state_topic(sub->device_name),
			device_state_cb, sub))) {
		ast_log(LOG_ERROR, "Unable to subscribe to device %s\n",
			sub->device_name);
		return -1;
	}

	ao2_link(device_state_subscriptions, sub);
	return 0;
}

static int unsubscribe_device_state(struct stasis_app *app, const char *name)
{
	RAII_VAR(struct device_state_subscription *, sub,
		 find_device_state_subscription(app, name), ao2_cleanup);
	remove_device_state_subscription(sub);
	return 0;
}

static int device_to_json_cb(void *obj, void *arg, void *data, int flags)
{
	struct device_state_subscription *sub = obj;
	const char *app_name = arg;
	struct ast_json *array = data;

	if (strcmp(sub->app_name, app_name)) {
		return 0;
	}

	ast_json_array_append(
		array, ast_json_string_create(sub->device_name));
	return 0;

}

static void devices_to_json(const struct stasis_app *app, struct ast_json *json)
{
	struct ast_json *array = ast_json_array_create();
	ao2_callback_data(device_state_subscriptions, OBJ_NODATA,
			  device_to_json_cb, (void *)stasis_app_name(app), array);
	ast_json_object_set(json, "device_names", array);
}

struct stasis_app_event_source device_state_event_source = {
	.scheme = DEVICE_STATE_SCHEME_SUB,
	.find = find_device_state,
	.subscribe = subscribe_device_state,
	.unsubscribe = unsubscribe_device_state,
	.is_subscribed = is_subscribed_device_state,
	.to_json = devices_to_json
};

static int load_module(void)
{
	populate_cache();
	if (ast_devstate_prov_add(DEVICE_STATE_PROVIDER_STASIS,
				  stasis_device_state_cb)) {
		return AST_MODULE_LOAD_FAILURE;
	}

	if (!(device_state_subscriptions = ao2_container_alloc(
		      DEVICE_STATE_BUCKETS, device_state_subscriptions_hash,
		      device_state_subscriptions_cmp))) {
		return AST_MODULE_LOAD_FAILURE;
	}

	stasis_app_register_event_source(&device_state_event_source);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_devstate_prov_del(DEVICE_STATE_PROVIDER_STASIS);
	stasis_app_unregister_event_source(&device_state_event_source);
	ao2_cleanup(device_state_subscriptions);
	device_state_subscriptions = NULL;
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS, "Stasis application device state support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.nonoptreq = "res_stasis"
);
