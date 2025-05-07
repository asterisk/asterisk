/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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
 * \brief Config framework stuffz for ARI.
 * \author David M. Lee, II <dlee@digium.com>
 */

#include <limits.h>

#include "asterisk.h"

#include "asterisk/sorcery.h"
#include "asterisk/config_options.h"
#include "asterisk/http_websocket.h"
#include "asterisk/app.h"
#include "asterisk/channel.h"
#include "asterisk/vector.h"
#include "internal.h"

static struct ast_sorcery *sorcery;

struct outbound_websocket_state {
	enum ari_conf_owc_fields invalid_fields;
	char id[0];
};

#define OWC_STATES_BUCKETS 13
struct ao2_container *owc_states = NULL;

static void outbound_websocket_dtor(void *obj)
{
	struct ari_conf_outbound_websocket *owc = obj;

	ast_debug(3, "%s: Disposing of outbound websocket config\n",
		ast_sorcery_object_get_id(owc));
	ast_string_field_free_memory(owc);
}

static void *outbound_websocket_alloc(const char *id)
{
	struct ari_conf_outbound_websocket *owc = NULL;

	owc = ast_sorcery_generic_alloc(sizeof(*owc), outbound_websocket_dtor);
	if (!owc) {
		return NULL;
	}

	if (ast_string_field_init(owc, 1024) != 0) {
		ao2_cleanup(owc);
		return NULL;
	}

	ast_debug(2, "%s: Allocated outbound websocket config\n", id);
	return owc;
}

/*! \brief Parses the ast_ari_conf_outbound_ws_type enum from a config file */
static int outbound_websocket_connection_type_handler(const struct aco_option *opt,
	struct ast_variable *var, void *obj)
{
	struct ari_conf_outbound_websocket *ows = obj;

	if (strcasecmp(var->value, "persistent") == 0) {
		ows->connection_type = ARI_WS_TYPE_OUTBOUND_PERSISTENT;
	} else if (strcasecmp(var->value, "per_call_config") == 0) {
		ows->connection_type = ARI_WS_TYPE_OUTBOUND_PER_CALL_CONFIG;
	} else {
		return -1;
	}

	return 0;
}

static int outbound_websocket_connection_type_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ari_conf_outbound_websocket *owc = obj;

	if (owc->connection_type ==	ARI_WS_TYPE_OUTBOUND_PERSISTENT) {
		*buf = ast_strdup("persistent");
	} else if (owc->connection_type == ARI_WS_TYPE_OUTBOUND_PER_CALL_CONFIG) {
		*buf = ast_strdup("per_call_config");
	} else {
		return -1;
	}

	return 0;
}

/*!
 * \brief Callback to initialize an outbound websocket object
 * \retval 0 on success
 * \retval CMP_MATCH on error which will cause the object to be removed
 */
static int outbound_websocket_apply(const struct ast_sorcery *sorcery, void *obj)
{
	struct ari_conf_outbound_websocket *owc = obj;
	const char *id = ast_sorcery_object_get_id(owc);
	int res = 0;

	ast_debug(3, "%s: Initializing outbound websocket\n", id);

	if (ast_strlen_zero(owc->uri)) {
		ast_log(LOG_WARNING, "%s: Outbound websocket missing uri\n", id);
		res = -1;
	}
	if (ast_strlen_zero(owc->protocols)) {
		ast_log(LOG_WARNING, "%s: Outbound websocket missing protocols\n", id);
		res = -1;
	}
	if (ast_strlen_zero(owc->apps)) {
		ast_log(LOG_WARNING, "%s: Outbound websocket missing apps\n", id);
		res = -1;
	} else {
		char *apps = ast_strdupa(owc->apps);
		char *app;
		while ((app = ast_strsep(&apps, ',', AST_STRSEP_STRIP))) {

			if (ast_strlen_zero(app)) {
				ast_log(LOG_WARNING, "%s: Outbound websocket has empty app\n", id);
				res = -1;
			}
			if (strlen(app) > ARI_MAX_APP_NAME_LEN) {
				ast_log(LOG_WARNING, "%s: Outbound websocket app '%s' > %d characters\n",
					id, app, (int)ARI_MAX_APP_NAME_LEN);
				res = -1;
			}

		}
	}

	if (ast_strlen_zero(owc->local_ari_user)) {
		ast_log(LOG_WARNING, "%s: Outbound websocket missing local_ari_user\n", id);
		res = -1;
	}

	if (res != 0) {
		ast_log(LOG_WARNING, "%s: Outbound websocket configuration failed\n", id);
	} else {
		ast_debug(3, "%s: Outbound websocket configuration succeeded\n", id);
	}

	/* Reminder: If res is -1, the config will be discarded. */
	return res;
}

enum ari_conf_owc_fields ari_conf_owc_get_invalid_fields(const char *id)
{
	RAII_VAR(struct outbound_websocket_state *, state, NULL, ao2_cleanup);

	state = ao2_find(owc_states, id, OBJ_SEARCH_KEY);
	return state ? state->invalid_fields : ARI_OWC_FIELD_NONE;
}

static int outbound_websocket_validate_cb(void *obj, void *args, int flags)
{
	struct ari_conf_outbound_websocket *owc = obj;
	struct ari_conf_outbound_websocket *other_owc = NULL;
	RAII_VAR(struct ao2_container *, owcs, NULL, ao2_cleanup);
	struct ao2_iterator it;
	const char *id = ast_sorcery_object_get_id(owc);
	struct ast_vector_string apps = { 0, };
	struct ari_conf_user *user = NULL;
	struct outbound_websocket_state *state = NULL;
	int res = 0;

	ast_debug(2, "%s: Validating outbound websocket\n", id);

	owcs = ari_conf_get_owcs();
	if (!owcs || ao2_container_count(owcs) == 0) {
		return 0;
	}

	if (AST_VECTOR_INIT(&apps, 5) != 0) {
		return 0;
	}

	res = ast_vector_string_split(&apps, owc->apps, ",", 0, NULL);
	if (res != 0) {
		ast_log(LOG_WARNING, "%s: Outbound websocket apps '%s' failed to split\n",
			id, owc->apps);
		AST_VECTOR_RESET(&apps, ast_free_ptr);
		AST_VECTOR_FREE(&apps);
		return 0;
	}

	state = ao2_find(owc_states, id, OBJ_SEARCH_KEY);
	if (!state) {
		state = ao2_alloc(sizeof(*state) + strlen(id) + 1, NULL);
		if (!state) {
			ast_log(LOG_WARNING, "%s: Outbound websocket state allocation failed\n", id);
			AST_VECTOR_RESET(&apps, ast_free_ptr);
			AST_VECTOR_FREE(&apps);
			return 0;
		}
		strcpy(state->id, id); /* Safe */
		ast_debug(3, "%s: Created new outbound websocket state\n", id);
	} else {
		ast_debug(3, "%s: Outbound websocket state already exists\n", id);
	}
	state->invalid_fields = ARI_OWC_FIELD_NONE;

	/*
	 * Check all other owcs to make sure we don't have
	 * duplicate apps.
	 */
	it = ao2_iterator_init(owcs, 0);
	while ((other_owc = ao2_iterator_next(&it))) {
		const char *other_id = ast_sorcery_object_get_id(other_owc);
		if (!ast_strings_equal(other_id, id)) {
			int i = 0;
			for (i = 0; i < AST_VECTOR_SIZE(&apps); i++) {
				const char *app = AST_VECTOR_GET(&apps, i);
				if (ast_in_delimited_string(app, other_owc->apps, ',')) {
					ast_log(LOG_WARNING,
						"%s: Outbound websocket '%s' is also trying to register app '%s'\n",
						id, other_id, app);
					state->invalid_fields |= ARI_OWC_FIELD_APPS;
				}
			}
		}
		ao2_cleanup(other_owc);
		if (owc->invalid) {
			break;
		}
	}
	ao2_iterator_destroy(&it);
	AST_VECTOR_RESET(&apps, ast_free_ptr);
	AST_VECTOR_FREE(&apps);

	/*
	 * Check that the local_ari_user is valid and has
	 * a plain text password.
	 */
	user = ast_sorcery_retrieve_by_id(sorcery, "user", owc->local_ari_user);
	if (!user) {
		ast_log(LOG_WARNING, "%s: Outbound websocket ARI user '%s' not found\n",
			id, owc->local_ari_user);
		state->invalid_fields |= ARI_OWC_FIELD_LOCAL_ARI_USER;
	} else {
		if (user->password_format != ARI_PASSWORD_FORMAT_PLAIN) {
			ast_log(LOG_WARNING, "%s: Outbound websocket ARI user '%s' password MUST be plain text\n",
				id, owc->local_ari_user);
			state->invalid_fields |= ARI_OWC_FIELD_LOCAL_ARI_USER;
		}
		if (ast_string_field_set(owc, local_ari_password, user->password) != 0) {
			state->invalid_fields |= ARI_OWC_FIELD_LOCAL_ARI_USER;
		}
	}
	ao2_cleanup(user);

	/*
	 * The container has AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE set so
	 * this is an insert or replace operation.
	 */
	ao2_link(owc_states, state);
	ao2_cleanup(state);

	return 0;
}

static int outbound_websocket_state_cleanup(void *obj, void *arg, int flags)
{
	struct outbound_websocket_state *state = obj;
	struct ari_conf_outbound_websocket *owc = ari_conf_get_owc(state->id);
	int res = 0;

	if (!owc) {
		ast_debug(3, "%s: Cleaning up orphaned outbound websocket state\n", state->id);
		res = CMP_MATCH;
	}
	ao2_cleanup(owc);

	return res;
}

static void outbound_websockets_validate(const char *name)
{
	RAII_VAR(struct ao2_container *, owcs, ari_conf_get_owcs(), ao2_cleanup);

	ao2_callback(owcs, OBJ_NODATA, outbound_websocket_validate_cb, NULL);
	/* Clean up any states whose configs have disappeared. */
	ao2_callback(owc_states, OBJ_NODATA | OBJ_UNLINK,
		outbound_websocket_state_cleanup, NULL);
}

struct ao2_container *ari_conf_get_owcs(void)
{
	if (!sorcery) {
		return NULL;
	}

	return ast_sorcery_retrieve_by_fields(sorcery, "outbound_websocket",
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
}

struct ari_conf_outbound_websocket *ari_conf_get_owc(const char *id)
{
	if (!sorcery) {
		return NULL;
	}

	return ast_sorcery_retrieve_by_id(sorcery, "outbound_websocket", id);
}

struct ari_conf_outbound_websocket *ari_conf_get_owc_for_app(
	const char *app_name, unsigned int ws_type)
{
	struct ari_conf_outbound_websocket *owc = NULL;
	struct ao2_container *owcs = NULL;
	struct ao2_iterator i;

	if (ast_strlen_zero(app_name)) {
		return NULL;
	}

	ast_debug(3, "Checking outbound websockets for app '%s'\n", app_name);

	owcs = ari_conf_get_owcs();
	if (!owcs || ao2_container_count(owcs) == 0) {
		ast_debug(3, "No outbound websockets found\n");
		return NULL;
	}

	i = ao2_iterator_init(owcs, 0);
	while ((owc = ao2_iterator_next(&i))) {
		const char *id = ast_sorcery_object_get_id(owc);

		ast_debug(3, "%s: Checking outbound websocket apps '%s' for app '%s'\n",
			id, owc->apps, app_name);
		if (owc->connection_type & ws_type
			&& ast_in_delimited_string(app_name, owc->apps, ',')) {
			ast_debug(3, "%s: Found correct websocket type for apps '%s' for app '%s'\n",
				id, owc->apps, app_name);
			break;
		}
		ao2_cleanup(owc);
	}
	ao2_iterator_destroy(&i);
	ao2_cleanup(owcs);
	if (!owc) {
		ast_debug(3, "No outbound websocket found for app '%s'\n", app_name);
	}

	return owc;
}

const char *ari_websocket_type_to_str(enum ari_websocket_type type)
{
	switch (type) {
	case ARI_WS_TYPE_OUTBOUND_PERSISTENT:
		return "persistent";
	case ARI_WS_TYPE_OUTBOUND_PER_CALL:
		return "per_call";
	case ARI_WS_TYPE_OUTBOUND_PER_CALL_CONFIG:
		return "per_call_config";
	case ARI_WS_TYPE_INBOUND:
		return "inbound";
	case ARI_WS_TYPE_ANY:
		return "any";
	default:
		return "unknown";
	}
}

enum ari_conf_owc_fields ari_conf_owc_detect_changes(
	struct ari_conf_outbound_websocket *old_owc,
	struct ari_conf_outbound_websocket *new_owc)
{
	enum ari_conf_owc_fields changed = ARI_OWC_FIELD_NONE;
	const char *new_id = ast_sorcery_object_get_id(new_owc);
	RAII_VAR(struct ast_variable *, changes, NULL, ast_variables_destroy);
	struct ast_variable *v = NULL;
	int res = 0;

	res = ast_sorcery_diff(sorcery, old_owc, new_owc, &changes);
	if (res != 0) {
		ast_log(LOG_WARNING, "%s: Failed to create changeset\n", new_id);
		return ARI_OWC_FIELD_NONE;
	}

	for (v = changes; v; v = v->next) {
		ast_debug(2, "%s: %s changed to %s\n", new_id, v->name, v->value);
		if (ast_strings_equal(v->name, "apps")) {
			changed |= ARI_OWC_FIELD_APPS;
		} else if (ast_strings_equal(v->name, "subscribe_all")) {
			changed |= ARI_OWC_FIELD_SUBSCRIBE_ALL;
		} else if (ast_strings_equal(v->name, "connection_type")) {
			changed |= ARI_OWC_FIELD_CONNECTION_TYPE;
		} else if (ast_strings_equal(v->name, "uri")) {
			changed |= ARI_OWC_FIELD_URI;
		} else if (ast_strings_equal(v->name, "protocols")) {
			changed |= ARI_OWC_FIELD_PROTOCOLS;
		} else if (ast_strings_equal(v->name, "username")) {
			changed |= ARI_OWC_FIELD_USERNAME;
		} else if (ast_strings_equal(v->name, "password")) {
			changed |= ARI_OWC_FIELD_PASSWORD;
		} else if (ast_strings_equal(v->name, "local_ari_user")) {
			changed |= ARI_OWC_FIELD_LOCAL_ARI_USER;
		} else if (ast_strings_equal(v->name, "local_ari_password")) {
			changed |= ARI_OWC_FIELD_LOCAL_ARI_PASSWORD;
		} else if (ast_strings_equal(v->name, "tls_enabled")) {
			changed |= ARI_OWC_FIELD_TLS_ENABLED;
		} else if (ast_strings_equal(v->name, "ca_list_file")) {
			changed |= ARI_OWC_FIELD_CA_LIST_FILE;
		} else if (ast_strings_equal(v->name, "ca_list_path")) {
			changed |= ARI_OWC_FIELD_CA_LIST_PATH;
		} else if (ast_strings_equal(v->name, "cert_file")) {
			changed |= ARI_OWC_FIELD_CERT_FILE;
		} else if (ast_strings_equal(v->name, "priv_key_file")) {
			changed |= ARI_OWC_FIELD_PRIV_KEY_FILE;
		} else if (ast_strings_equal(v->name, "reconnect_interval")) {
			changed |= ARI_OWC_FIELD_RECONNECT_INTERVAL;
		} else if (ast_strings_equal(v->name, "reconnect_attempts")) {
			changed |= ARI_OWC_FIELD_RECONNECT_ATTEMPTS;
		} else if (ast_strings_equal(v->name, "connection_timeout")) {
			changed |= ARI_OWC_FIELD_CONNECTION_TIMEOUT;
		} else if (ast_strings_equal(v->name, "verify_server_cert")) {
			changed |= ARI_OWC_FIELD_VERIFY_SERVER_CERT;
		} else if (ast_strings_equal(v->name, "verify_server_hostname")) {
			changed |= ARI_OWC_FIELD_VERIFY_SERVER_HOSTNAME;
		} else {
			ast_debug(2, "%s: Unknown change %s\n", new_id, v->name);
		}
	}

	return changed;

}

/*! \brief \ref ast_ari_conf destructor. */
static void general_dtor(void *obj)
{
	struct ari_conf_general *cfg = obj;

	ast_string_field_free_memory(cfg);
}

static void *general_alloc(const char *name)
{
	struct ari_conf_general *general = ast_sorcery_generic_alloc(
		sizeof(*general), general_dtor);

	if (!general) {
		return NULL;
	}

	if (ast_string_field_init(general, 64) != 0) {
		return NULL;
	}

	return general;
}

#define MAX_VARS 128

static int general_apply(const struct ast_sorcery *sorcery, void *obj)
{
	struct ari_conf_general *general = obj;
	char *parse = NULL;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(vars)[MAX_VARS];
	);

	ast_debug(2, "Initializing general config\n");

	parse = ast_strdupa(general->channelvars);
	AST_STANDARD_APP_ARGS(args, parse);

	ast_channel_set_ari_vars(args.argc, args.vars);
	return 0;
}

/*! \brief Encoding format handler converts from boolean to enum. */
static int general_pretty_handler(const struct aco_option *opt,
	struct ast_variable *var, void *obj)
{
	struct ari_conf_general *general = obj;

	general->format = ast_true(var->value) ? AST_JSON_PRETTY : AST_JSON_COMPACT;

	return 0;
}

struct ari_conf_general* ari_conf_get_general(void)
{
	if (!sorcery) {
		return NULL;
	}

	return ast_sorcery_retrieve_by_id(sorcery, "general", "general");
}

static int general_pretty_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ari_conf_general *general = obj;

	if (general->format == AST_JSON_PRETTY) {
		*buf = ast_strdup("yes");
	} else {
		*buf = ast_strdup("no");
	}
	return 0;
}

/*! \brief Destructor for \ref ast_ari_conf_user */
static void user_dtor(void *obj)
{
	struct ari_conf_user *user = obj;
	ast_string_field_free_memory(user);
	ast_debug(3, "%s: Disposing of user\n", ast_sorcery_object_get_id(user));
}

/*! \brief Allocate an \ref ast_ari_conf_user for config parsing */
static void *user_alloc(const char *cat)
{
	struct ari_conf_user *user = ast_sorcery_generic_alloc(
		sizeof(*user), user_dtor);

	if (!user) {
		return NULL;
	}

	if (ast_string_field_init(user, 64) != 0) {
		ao2_cleanup(user);
		user = NULL;
	}

	return user;
}

static int user_apply(const struct ast_sorcery *sorcery, void *obj)
{
	struct ari_conf_user *user = obj;
	const char *id = ast_sorcery_object_get_id(user);

	ast_debug(2, "%s: Initializing user\n", id);

	if (ast_strlen_zero(user->password)) {
		ast_log(LOG_WARNING, "%s: User missing password\n", id);
		return -1;
	}

	return 0;
}

/*! \brief Parses the ast_ari_password_format enum from a config file */
static int user_password_format_handler(const struct aco_option *opt,
	struct ast_variable *var, void *obj)
{
	struct ari_conf_user *user = obj;

	if (strcasecmp(var->value, "plain") == 0) {
		user->password_format = ARI_PASSWORD_FORMAT_PLAIN;
	} else if (strcasecmp(var->value, "crypt") == 0) {
		user->password_format = ARI_PASSWORD_FORMAT_CRYPT;
	} else {
		return -1;
	}

	return 0;
}

static int user_password_format_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ari_conf_user *user = obj;

	if (user->password_format == ARI_PASSWORD_FORMAT_CRYPT) {
		*buf = ast_strdup("crypt");
	} else {
		*buf = ast_strdup("plain");
	}
	return 0;
}

struct ao2_container *ari_conf_get_users(void)
{
	if (!sorcery) {
		return NULL;
	}

	return ast_sorcery_retrieve_by_fields(sorcery, "user",
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
}

struct ari_conf_user *ari_conf_get_user(const char *username)
{
	if (!sorcery) {
		return NULL;
	}

	return ast_sorcery_retrieve_by_id(sorcery, "user", username);
}

/*
 * This is called by res_ari.c to validate the user and password
 * for the websocket connection.
 */
struct ari_conf_user *ari_conf_validate_user(const char *username,
	const char *password)
{
	struct ari_conf_user *user = NULL;
	int is_valid = 0;

	if (ast_strlen_zero(username) || ast_strlen_zero(password)) {
		return NULL;
	}

	user = ast_sorcery_retrieve_by_id(sorcery, "user", username);
	if (!user) {
		return NULL;
	}

	switch (user->password_format) {
	case ARI_PASSWORD_FORMAT_PLAIN:
		is_valid = strcmp(password, user->password) == 0;
		break;
	case ARI_PASSWORD_FORMAT_CRYPT:
		is_valid = ast_crypt_validate(password, user->password);
		break;
	}

	if (!is_valid) {
		ao2_cleanup(user);
		user = NULL;
	}

	return user;
}

int ari_sorcery_observer_add(const char *object_type,
	const struct ast_sorcery_observer *callbacks)
{
	if (!sorcery) {
		return -1;
	}
	return ast_sorcery_observer_add(sorcery, object_type, callbacks);
}

int ari_sorcery_observer_remove(const char *object_type,
	const struct ast_sorcery_observer *callbacks)
{
	if (!sorcery) {
		return -1;
	}
	ast_sorcery_observer_remove(sorcery, object_type, callbacks);
	return 0;
}

static void outbound_websocket_created_cb(const void *obj)
{
	const struct ari_conf_outbound_websocket *owc = obj;

	ast_debug(3, "%s: Outbound XXXXXXXXXXXX websocket created\n", ast_sorcery_object_get_id(owc));
}

static struct ast_sorcery_observer observer_callbacks = {
	.created = outbound_websocket_created_cb,
	.loaded = outbound_websockets_validate,
};

#define ari_conf_bool(object, option, field, def_value) \
	ast_sorcery_object_field_register(sorcery, #object, #option, \
		def_value, OPT_YESNO_T, 1, \
		FLDSET(struct ari_conf_ ## object, field))

#define _stringify(val) #val
#define ari_conf_int(object, option, field, def_value) \
	ast_sorcery_object_field_register(sorcery, #object, #option, \
		_stringify(def_value), OPT_INT_T, PARSE_IN_RANGE, \
		FLDSET(struct ari_conf_ ## object, field), INT_MIN, INT_MAX)

#define ari_conf_uint(object, option, field, def_value) \
	ast_sorcery_object_field_register(sorcery, #object, #option, \
		_stringify(def_value), OPT_UINT_T, PARSE_IN_RANGE, \
		FLDSET(struct ari_conf_ ## object, field), 0, UINT_MAX)

#define ari_conf_sf(object, option, field, def_value) \
	ast_sorcery_object_field_register(sorcery, #object, #option, \
		def_value, OPT_STRINGFIELD_T, 0, \
		STRFLDSET(struct ari_conf_ ##object, field))

#define ari_conf_cust(object, option, def_value) \
	ast_sorcery_object_field_register_custom(sorcery, #object, #option, \
		def_value, object ## _ ## option ## _handler, \
		object ## _ ## option ## _to_str, NULL, 0, 0)

AO2_STRING_FIELD_HASH_FN(outbound_websocket_state, id)
AO2_STRING_FIELD_CMP_FN(outbound_websocket_state, id)

static int ari_conf_init(void)
{
	ast_debug(2, "Initializing ARI configuration\n");

	owc_states = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX,
		AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE, OWC_STATES_BUCKETS,
		outbound_websocket_state_hash_fn, NULL,
		outbound_websocket_state_cmp_fn);
	if (!owc_states) {
		ast_log(LOG_ERROR, "Failed to allocate outbound websocket states\n");
		return -1;
	}

	if (!(sorcery = ast_sorcery_open())) {
		ast_log(LOG_ERROR, "Failed to open sorcery\n");
		return -1;
	}

	ast_sorcery_apply_default(sorcery, "general", "config",
		"ari.conf,criteria=type=general,single_object=yes,explicit_name=general");
	ast_sorcery_apply_default(sorcery, "user", "config",
		"ari.conf,criteria=type=user");
	ast_sorcery_apply_default(sorcery, "outbound_websocket", "config",
		"ari.conf,criteria=type=outbound_websocket");

	if (ast_sorcery_object_register(sorcery, "general", general_alloc, NULL, general_apply)) {
		ast_log(LOG_ERROR, "Failed to register ARI general object with sorcery\n");
		ast_sorcery_unref(sorcery);
		sorcery = NULL;
		return -1;
	}

	if (ast_sorcery_object_register(sorcery, "user", user_alloc, NULL, user_apply)) {
		ast_log(LOG_ERROR, "Failed to register ARI user object with sorcery\n");
		ast_sorcery_unref(sorcery);
		sorcery = NULL;
		return -1;
	}

	if (ast_sorcery_object_register(sorcery, "outbound_websocket", outbound_websocket_alloc,
		NULL, outbound_websocket_apply)) {
		ast_log(LOG_ERROR, "Failed to register ARI outbound_websocket object with sorcery\n");
		ast_sorcery_unref(sorcery);
		sorcery = NULL;
		return -1;
	}

	if (ast_sorcery_observer_add(sorcery, "outbound_websocket", &observer_callbacks)) {
		ast_log(LOG_ERROR, "Failed to register ARI outbound_websocket observer with sorcery\n");
		ast_sorcery_unref(sorcery);
		sorcery = NULL;
		return -1;
	}

	ast_sorcery_object_field_register_nodoc(sorcery, "general", "type", "", OPT_NOOP_T, 0, 0);
	ari_conf_sf(general, auth_realm, auth_realm, "Asterisk REST Interface");
	ari_conf_sf(general, allowed_origins, allowed_origins, "");
	ari_conf_sf(general, channelvars, channelvars, "");
	ari_conf_bool(general, enabled, enabled, "yes");
	ari_conf_cust(general, pretty, "no");
	ari_conf_int(general, websocket_write_timeout, write_timeout,
		AST_DEFAULT_WEBSOCKET_WRITE_TIMEOUT);


	ast_sorcery_object_field_register(sorcery, "user", "type", "", OPT_NOOP_T, 0, 0);
	ari_conf_sf(user, password, password, "");
	ari_conf_bool(user, read_only, read_only, "no");
	ari_conf_cust(user, password_format, "plain");

	ast_sorcery_object_field_register(sorcery, "outbound_websocket", "type", "", OPT_NOOP_T, 0, 0);
	ari_conf_sf(outbound_websocket, uri, uri, "");
	ari_conf_sf(outbound_websocket, protocols, protocols, "");
	ari_conf_sf(outbound_websocket, apps, apps, "");
	ari_conf_sf(outbound_websocket, username, username, "");
	ari_conf_sf(outbound_websocket, password, password, "");
	ari_conf_sf(outbound_websocket, local_ari_user, local_ari_user, "");
	ari_conf_sf(outbound_websocket, ca_list_file, ca_list_file, "");
	ari_conf_sf(outbound_websocket, ca_list_path, ca_list_path, "");
	ari_conf_sf(outbound_websocket, cert_file, cert_file, "");
	ari_conf_sf(outbound_websocket, priv_key_file, priv_key_file, "");
	ari_conf_bool(outbound_websocket, subscribe_all, subscribe_all, "no");
	ari_conf_bool(outbound_websocket, tls_enabled, tls_enabled, "no");
	ari_conf_bool(outbound_websocket, verify_server_cert, verify_server_cert, "yes");
	ari_conf_bool(outbound_websocket, verify_server_hostname, verify_server_hostname, "yes");
	ari_conf_cust(outbound_websocket, connection_type, "");
	ari_conf_int(outbound_websocket, connection_timeout, connect_timeout, 500);
	ari_conf_int(outbound_websocket, reconnect_attempts, reconnect_attempts, 4);
	ari_conf_int(outbound_websocket, reconnect_interval, reconnect_interval, 500);

	return 0;
}

int ari_conf_load(enum ari_conf_load_flags flags)
{
	void (*loader)(const struct ast_sorcery *sorcery, const char *type);
	const char *msg_prefix;

	if (flags & ARI_CONF_RELOAD) {
		loader = ast_sorcery_reload_object;
		msg_prefix= "Reloading";
	} else {
		loader = ast_sorcery_load_object;
		msg_prefix= "Loading";
	}

	if (flags & ARI_CONF_INIT) {
		if (ari_conf_init() != 0) {
			ast_log(LOG_ERROR, "Failed to initialize ARI configuration\n");
			return -1;
		}
	}

	if (!sorcery) {
		ast_log(LOG_ERROR, "ARI configuration not initialized\n");
		return -1;
	}

	if (flags & ARI_CONF_LOAD_GENERAL) {
		ast_debug(2, "%s ARI '%s' configuration\n", msg_prefix, "general");
		loader(sorcery, "general");
	}

	if (flags & ARI_CONF_LOAD_USER) {
		ast_debug(2, "%s ARI '%s' configuration\n", msg_prefix, "user");
		loader(sorcery, "user");
	}

	if (flags & ARI_CONF_LOAD_OWC) {
		ast_debug(2, "%s ARI '%s' configuration\n", msg_prefix, "outbound_websocket");
		loader(sorcery, "outbound_websocket");
	}

	return 0;
}

void ari_conf_destroy(void)
{
	ast_sorcery_unref(sorcery);
	sorcery = NULL;
	ao2_cleanup(owc_states);
}
