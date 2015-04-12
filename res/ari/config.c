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

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/config_options.h"
#include "asterisk/http_websocket.h"
#include "internal.h"

/*! \brief Locking container for safe configuration access. */
static AO2_GLOBAL_OBJ_STATIC(confs);

/*! \brief Mapping of the ARI conf struct's globals to the
 *         general context in the config file. */
static struct aco_type general_option = {
	.type = ACO_GLOBAL,
	.name = "general",
	.item_offset = offsetof(struct ast_ari_conf, general),
	.category = "^general$",
	.category_match = ACO_WHITELIST,
};

static struct aco_type *general_options[] = ACO_TYPES(&general_option);

/*! \brief Encoding format handler converts from boolean to enum. */
static int encoding_format_handler(const struct aco_option *opt,
	struct ast_variable *var, void *obj)
{
	struct ast_ari_conf_general *general = obj;

	if (!strcasecmp(var->name, "pretty")) {
		general->format = ast_true(var->value) ?
			AST_JSON_PRETTY : AST_JSON_COMPACT;
	} else {
		return -1;
	}

	return 0;
}

/*! \brief Parses the ast_ari_password_format enum from a config file */
static int password_format_handler(const struct aco_option *opt,
	struct ast_variable *var, void *obj)
{
	struct ast_ari_conf_user *user = obj;

	if (strcasecmp(var->value, "plain") == 0) {
		user->password_format = ARI_PASSWORD_FORMAT_PLAIN;
	} else if (strcasecmp(var->value, "crypt") == 0) {
		user->password_format = ARI_PASSWORD_FORMAT_CRYPT;
	} else {
		return -1;
	}

	return 0;
}

/*! \brief Destructor for \ref ast_ari_conf_user */
static void user_dtor(void *obj)
{
	struct ast_ari_conf_user *user = obj;
	ast_debug(3, "Disposing of user %s\n", user->username);
	ast_free(user->username);
}

/*! \brief Allocate an \ref ast_ari_conf_user for config parsing */
static void *user_alloc(const char *cat)
{
	RAII_VAR(struct ast_ari_conf_user *, user, NULL, ao2_cleanup);

	if (!cat) {
		return NULL;
	}

	ast_debug(3, "Allocating user %s\n", cat);

	user = ao2_alloc_options(sizeof(*user), user_dtor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!user) {
		return NULL;
	}

	user->username = ast_strdup(cat);
	if (!user->username) {
		return NULL;
	}

	ao2_ref(user, +1);
	return user;
}

/*! \brief Sorting function for use with red/black tree */
static int user_sort_cmp(const void *obj_left, const void *obj_right, int flags)
{
	const struct ast_ari_conf_user *user_left = obj_left;

	if (flags & OBJ_PARTIAL_KEY) {
		const char *key_right = obj_right;
		return strncasecmp(user_left->username, key_right,
			strlen(key_right));
	} else if (flags & OBJ_KEY) {
		const char *key_right = obj_right;
		return strcasecmp(user_left->username, key_right);
	} else {
		const struct ast_ari_conf_user *user_right = obj_right;
		const char *key_right = user_right->username;
		return strcasecmp(user_left->username, key_right);
	}
}

/*! \brief \ref aco_type item_find function */
static void *user_find(struct ao2_container *tmp_container, const char *cat)
{
	if (!cat) {
		return NULL;
	}

	return ao2_find(tmp_container, cat, OBJ_KEY);
}

static struct aco_type user_option = {
	.type = ACO_ITEM,
	.name = "user",
	.category_match = ACO_BLACKLIST,
	.category = "^general$",
	.matchfield = "type",
	.matchvalue = "user",
	.item_alloc = user_alloc,
	.item_find = user_find,
	.item_offset = offsetof(struct ast_ari_conf, users),
};

static struct aco_type *user[] = ACO_TYPES(&user_option);

/*! \brief \ref ast_ari_conf destructor. */
static void conf_destructor(void *obj)
{
	struct ast_ari_conf *cfg = obj;

	ast_string_field_free_memory(cfg->general);

	ao2_cleanup(cfg->general);
	ao2_cleanup(cfg->users);
}

/*! \brief Allocate an \ref ast_ari_conf for config parsing */
static void *conf_alloc(void)
{
	RAII_VAR(struct ast_ari_conf *, cfg, NULL, ao2_cleanup);

	cfg = ao2_alloc_options(sizeof(*cfg), conf_destructor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!cfg) {
		return NULL;
	}

	cfg->general = ao2_alloc_options(sizeof(*cfg->general), NULL,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!cfg->general) {
		return NULL;
	}
	aco_set_defaults(&general_option, "general", cfg->general);

	if (ast_string_field_init(cfg->general, 64)) {
		return NULL;
	}

	cfg->users = ao2_container_alloc_rbtree(AO2_ALLOC_OPT_LOCK_NOLOCK,
		AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE, user_sort_cmp, NULL);

	ao2_ref(cfg, +1);
	return cfg;
}

#define CONF_FILENAME "ari.conf"

/*! \brief The conf file that's processed for the module. */
static struct aco_file conf_file = {
	/*! The config file name. */
	.filename = CONF_FILENAME,
	/*! The mapping object types to be processed. */
	.types = ACO_TYPES(&general_option, &user_option),
};

CONFIG_INFO_STANDARD(cfg_info, confs, conf_alloc,
		     .files = ACO_FILES(&conf_file));

struct ast_ari_conf *ast_ari_config_get(void)
{
	struct ast_ari_conf *res = ao2_global_obj_ref(confs);
	if (!res) {
		ast_log(LOG_ERROR,
			"Error obtaining config from " CONF_FILENAME "\n");
	}
	return res;
}

struct ast_ari_conf_user *ast_ari_config_validate_user(const char *username,
	const char *password)
{
	RAII_VAR(struct ast_ari_conf *, conf, NULL, ao2_cleanup);
	RAII_VAR(struct ast_ari_conf_user *, user, NULL, ao2_cleanup);
	int is_valid = 0;

	conf = ast_ari_config_get();
	if (!conf) {
		return NULL;
	}

	user = ao2_find(conf->users, username, OBJ_KEY);
	if (!user) {
		return NULL;
	}

	if (ast_strlen_zero(user->password)) {
		ast_log(LOG_WARNING,
			"User '%s' missing password; authentication failed\n",
			user->username);
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
		return NULL;
	}

	ao2_ref(user, +1);
	return user;
}

/*! \brief Callback to validate a user object */
static int validate_user_cb(void *obj, void *arg, int flags)
{
	struct ast_ari_conf_user *user = obj;

	if (ast_strlen_zero(user->password)) {
		ast_log(LOG_WARNING, "User '%s' missing password\n",
			user->username);
	}

	return 0;
}

/*! \brief Load (or reload) configuration. */
static int process_config(int reload)
{
	RAII_VAR(struct ast_ari_conf *, conf, NULL, ao2_cleanup);

	switch (aco_process_config(&cfg_info, reload)) {
	case ACO_PROCESS_ERROR:
		return -1;
	case ACO_PROCESS_OK:
	case ACO_PROCESS_UNCHANGED:
		break;
	}

	conf = ast_ari_config_get();
	if (!conf) {
		ast_assert(0); /* We just configured; it should be there */
		return -1;
	}

	if (conf->general->enabled) {
		if (ao2_container_count(conf->users) == 0) {
			ast_log(LOG_ERROR, "No configured users for ARI\n");
		} else {
			ao2_callback(conf->users, OBJ_NODATA, validate_user_cb, NULL);
		}
	}

	return 0;
}

int ast_ari_config_init(void)
{
	if (aco_info_init(&cfg_info)) {
		aco_info_destroy(&cfg_info);
		return -1;
	}

	aco_option_register(&cfg_info, "enabled", ACO_EXACT, general_options,
		"yes", OPT_BOOL_T, 1,
		FLDSET(struct ast_ari_conf_general, enabled));
	aco_option_register_custom(&cfg_info, "pretty", ACO_EXACT,
		general_options, "no",  encoding_format_handler, 0);
	aco_option_register(&cfg_info, "auth_realm", ACO_EXACT, general_options,
		"Asterisk REST Interface", OPT_CHAR_ARRAY_T, 0,
		FLDSET(struct ast_ari_conf_general, auth_realm),
		ARI_AUTH_REALM_LEN);
	aco_option_register(&cfg_info, "allowed_origins", ACO_EXACT, general_options,
		"", OPT_STRINGFIELD_T, 0,
		STRFLDSET(struct ast_ari_conf_general, allowed_origins));
	aco_option_register(&cfg_info, "websocket_write_timeout", ACO_EXACT, general_options,
		AST_DEFAULT_WEBSOCKET_WRITE_TIMEOUT_STR, OPT_INT_T, PARSE_IN_RANGE,
		FLDSET(struct ast_ari_conf_general, write_timeout), 1, INT_MAX);

	aco_option_register(&cfg_info, "type", ACO_EXACT, user, NULL,
		OPT_NOOP_T, 0, 0);
	aco_option_register(&cfg_info, "read_only", ACO_EXACT, user,
		"no", OPT_BOOL_T, 1,
		FLDSET(struct ast_ari_conf_user, read_only));
	aco_option_register(&cfg_info, "password", ACO_EXACT, user,
		"", OPT_CHAR_ARRAY_T, 0,
		FLDSET(struct ast_ari_conf_user, password), ARI_PASSWORD_LEN);
	aco_option_register_custom(&cfg_info, "password_format", ACO_EXACT,
		user, "plain",  password_format_handler, 0);

	return process_config(0);
}

int ast_ari_config_reload(void)
{
	return process_config(1);
}

void ast_ari_config_destroy(void)
{
	aco_info_destroy(&cfg_info);
	ao2_global_obj_release(confs);
}
