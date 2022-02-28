/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2022, Sangoma Technologies Corporation
 *
 * Ben Ford <bford@sangoma.com>
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

#include "asterisk.h"

#include "asterisk/cli.h"
#include "asterisk/sorcery.h"

#include "stir_shaken.h"
#include "profile.h"
#include "asterisk/res_stir_shaken.h"

#define CONFIG_TYPE "profile"

static void stir_shaken_profile_destructor(void *obj)
{
	struct stir_shaken_profile *cfg = obj;

	ast_free_acl_list(cfg->acl);

	return;
}

static void *stir_shaken_profile_alloc(const char *name)
{
	struct stir_shaken_profile *cfg;

	cfg = ast_sorcery_generic_alloc(sizeof(*cfg), stir_shaken_profile_destructor);
	if (!cfg) {
		return NULL;
	}

	return cfg;
}

static struct stir_shaken_profile *stir_shaken_profile_get(const char *id)
{
	return ast_sorcery_retrieve_by_id(ast_stir_shaken_sorcery(), CONFIG_TYPE, id);
}

static struct ao2_container *stir_shaken_profile_get_all(void)
{
	return ast_sorcery_retrieve_by_fields(ast_stir_shaken_sorcery(), CONFIG_TYPE,
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
}

struct stir_shaken_profile *ast_stir_shaken_get_profile_by_name(const char *name)
{
	return ast_sorcery_retrieve_by_id(ast_stir_shaken_sorcery(), CONFIG_TYPE, name);
}

static int stir_shaken_profile_apply(const struct ast_sorcery *sorcery, void *obj)
{
	return 0;
}

static int stir_shaken_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct stir_shaken_profile *cfg = obj;

	if (!strcasecmp("attest", var->value)) {
		cfg->stir_shaken = STIR_SHAKEN_ATTEST;
	} else if (!strcasecmp("verify", var->value)) {
		cfg->stir_shaken = STIR_SHAKEN_VERIFY;
	} else if (!strcasecmp("on", var->value)) {
		cfg->stir_shaken = STIR_SHAKEN_ON;
	} else {
		ast_log(LOG_WARNING, "'%s' is not a valid value for option "
			"'stir_shaken' for %s %s\n",
		var->value, CONFIG_TYPE, ast_sorcery_object_get_id(cfg));
		return -1;
	}

	return 0;
}

static const char *stir_shaken_map[] = {
	[STIR_SHAKEN_ATTEST] = "attest",
	[STIR_SHAKEN_VERIFY] = "verify",
	[STIR_SHAKEN_ON] = "on",
};

static int stir_shaken_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct stir_shaken_profile *cfg = obj;
	if (ARRAY_IN_BOUNDS(cfg->stir_shaken, stir_shaken_map)) {
		*buf = ast_strdup(stir_shaken_map[cfg->stir_shaken]);
	}
	return 0;
}

static int stir_shaken_acl_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct stir_shaken_profile *cfg = obj;
	int error = 0;
	int ignore;

	if (ast_strlen_zero(var->value)) {
		return 0;
	}

	ast_append_acl(var->name, var->value, &cfg->acl, &error, &ignore);

	return error;
}

static int acl_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct stir_shaken_profile *cfg = obj;
	struct ast_acl_list *acl_list;
	struct ast_acl *first_acl;

	if (cfg && !ast_acl_list_is_empty(acl_list=cfg->acl)) {
		AST_LIST_LOCK(acl_list);
		first_acl = AST_LIST_FIRST(acl_list);
		if (ast_strlen_zero(first_acl->name)) {
			*buf = "deny/permit";
		} else {
			*buf = first_acl->name;
		}
		AST_LIST_UNLOCK(acl_list);
	}

	*buf = ast_strdup(*buf);
	return 0;
}

static char *stir_shaken_profile_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct stir_shaken_profile *cfg;

	switch(cmd) {
	case CLI_INIT:
		e->command = "stir_shaken show profile";
		e->usage =
			"Usage: stir_shaken show profile <id>\n"
			"       Show the stir/shaken profile settings for a given id\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			return stir_shaken_tab_complete_name(a->word, stir_shaken_profile_get_all());
		} else {
			return NULL;
		}
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	cfg = stir_shaken_profile_get(a->argv[3]);
	stir_shaken_cli_show(cfg, a, 0);
	ast_acl_output(a->fd, cfg->acl, NULL);
	ao2_cleanup(cfg);

	return CLI_SUCCESS;
}

static char *stir_shaken_profile_show_all(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_container *container;

	switch(cmd) {
	case CLI_INIT:
		e->command = "stir_shaken show profiles";
		e->usage =
			"Usage: stir_shaken show profiles\n"
			"       Show all profiles for stir/shaken\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	container = stir_shaken_profile_get_all();
	if (!container || ao2_container_count(container) == 0) {
		ast_cli(a->fd, "No stir/shaken ACLs found\n");
		ao2_cleanup(container);
		return CLI_SUCCESS;
	}

	ao2_callback(container, OBJ_NODATA, stir_shaken_cli_show, a);
	ao2_ref(container, -1);

	return CLI_SUCCESS;
}

static struct ast_cli_entry stir_shaken_profile_cli[] = {
	AST_CLI_DEFINE(stir_shaken_profile_show, "Show stir/shaken profile by id"),
	AST_CLI_DEFINE(stir_shaken_profile_show_all, "Show all stir/shaken profiles"),
};

int stir_shaken_profile_unload(void)
{
	ast_cli_unregister_multiple(stir_shaken_profile_cli,
		ARRAY_LEN(stir_shaken_profile_cli));

	return 0;
}

int stir_shaken_profile_load(void)
{
	struct ast_sorcery *sorcery = ast_stir_shaken_sorcery();

	ast_sorcery_apply_default(sorcery, CONFIG_TYPE, "config", "stir_shaken.conf,criteria=type=profile");

	if (ast_sorcery_object_register(sorcery, CONFIG_TYPE, stir_shaken_profile_alloc,
		NULL, stir_shaken_profile_apply)) {
		ast_log(LOG_ERROR, "stir/shaken - failed to register '%s' sorcery object\n", CONFIG_TYPE);
		return -1;
	}

	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, CONFIG_TYPE, "stir_shaken", "on", stir_shaken_handler, stir_shaken_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, CONFIG_TYPE, "deny", "", stir_shaken_acl_handler, NULL, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, CONFIG_TYPE, "permit", "", stir_shaken_acl_handler, NULL, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, CONFIG_TYPE, "acllist", "", stir_shaken_acl_handler, acl_to_str, NULL, 0, 0);

	ast_cli_register_multiple(stir_shaken_profile_cli,
		ARRAY_LEN(stir_shaken_profile_cli));

	return 0;
}
