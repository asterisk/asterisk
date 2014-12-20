/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Copyright (C) 2012, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * Jonathan Rose <jrose@digium.com>
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
 * \brief Named Access Control Lists
 *
 * \author Jonathan Rose <jrose@digium.com>
 *
 * \note Based on a feature proposed by
 * Olle E. Johansson <oej@edvina.net>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/config.h"
#include "asterisk/config_options.h"
#include "asterisk/event.h"
#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/acl.h"
#include "asterisk/astobj2.h"
#include "asterisk/paths.h"

#define NACL_CONFIG "acl.conf"
#define ACL_FAMILY "acls"

struct named_acl_global_config {
	AST_DECLARE_STRING_FIELDS(
		/* Nothing here yet. */
	);
};

/*
 * Configuration structure - holds pointers to ao2 containers used for configuration
 * Since there isn't a general level or any other special levels for acl.conf at this
 * time, it's really a config options friendly wrapper for the named ACL container
 */
struct named_acl_config {
	struct named_acl_global_config *global;
	struct ao2_container *named_acl_list;
};

static AO2_GLOBAL_OBJ_STATIC(globals);

/*! \note These functions are used for placing/retrieving named ACLs in their ao2_container. */
static void *named_acl_config_alloc(void);
static void *named_acl_alloc(const char *cat);
static void *named_acl_find(struct ao2_container *container, const char *cat);

/* Config type for named ACL profiles (must not be named general) */
static struct aco_type named_acl_type = {
	.type = ACO_ITEM,                  /*!< named_acls are items stored in containers, not individual global objects */
	.category_match = ACO_BLACKLIST,
	.category = "^general$",           /*!< Match everything but "general" */
	.item_alloc = named_acl_alloc,     /*!< A callback to allocate a new named_acl based on category */
	.item_find = named_acl_find,       /*!< A callback to find a named_acl in some container of named_acls */
	.item_offset = offsetof(struct named_acl_config, named_acl_list), /*!< Could leave this out since 0 */
};

/* Config type for the general part of the ACL profile (must be named general) */
static struct aco_type global_option = {
	.type = ACO_GLOBAL,
	.item_offset = offsetof(struct named_acl_config, global),
	.category_match = ACO_WHITELIST,
	.category = "^general$",
};

/* This array of aco_type structs is necessary to use aco_option_register */
struct aco_type *named_acl_types[] = ACO_TYPES(&named_acl_type);

struct aco_type *global_options[] = ACO_TYPES(&global_option);

struct aco_file named_acl_conf = {
	.filename = "acl.conf",
	.types = ACO_TYPES(&named_acl_type, &global_option),
};

/* Create a config info struct that describes the config processing for named ACLs. */
CONFIG_INFO_STANDARD(cfg_info, globals, named_acl_config_alloc,
	.files = ACO_FILES(&named_acl_conf),
);

struct named_acl {
	struct ast_ha *ha;
	char name[ACL_NAME_LENGTH]; /* Same max length as a configuration category */
};

static int named_acl_hash_fn(const void *obj, const int flags)
{
	const struct named_acl *entry = obj;
	return ast_str_hash(entry->name);
}

static int named_acl_cmp_fn(void *obj, void *arg, const int flags)
{
	struct named_acl *entry1 = obj;
	struct named_acl *entry2 = arg;

	return (!strcmp(entry1->name, entry2->name)) ? (CMP_MATCH | CMP_STOP) : 0;
}

/*! \brief destructor for named_acl_config */
static void named_acl_config_destructor(void *obj)
{
	struct named_acl_config *cfg = obj;
	ao2_cleanup(cfg->named_acl_list);
	ao2_cleanup(cfg->global);
}

static void named_acl_global_config_destructor(void *obj)
{
	struct named_acl_global_config *global = obj;
	ast_string_field_free_memory(global);
}

/*! \brief allocator callback for named_acl_config. Notice it returns void * since it is used by
 * the backend config code
 */
static void *named_acl_config_alloc(void)
{
	struct named_acl_config *cfg;

	if (!(cfg = ao2_alloc(sizeof(*cfg), named_acl_config_destructor))) {
		return NULL;
	}

	if (!(cfg->global = ao2_alloc(sizeof(*cfg->global), named_acl_global_config_destructor))) {
		goto error;
	}

	if (ast_string_field_init(cfg->global, 128)) {
		goto error;
	}

	if (!(cfg->named_acl_list = ao2_container_alloc(37, named_acl_hash_fn, named_acl_cmp_fn))) {
		goto error;
	}

	return cfg;

error:
	ao2_ref(cfg, -1);
	return NULL;
}

/*! \brief Destroy a named ACL object */
static void destroy_named_acl(void *obj)
{
	struct named_acl *named_acl = obj;
	ast_free_ha(named_acl->ha);
}

/*!
 * \brief Create a named ACL structure
 *
 * \param cat name given to the ACL
 * \retval NULL failure
 *\retval non-NULL successfully allocated named ACL
 */
void *named_acl_alloc(const char *cat)
{
	struct named_acl *named_acl;

	named_acl = ao2_alloc(sizeof(*named_acl), destroy_named_acl);
	if (!named_acl) {
		return NULL;
	}

	ast_copy_string(named_acl->name, cat, sizeof(named_acl->name));

	return named_acl;
}

/*!
 * \brief Find a named ACL in a container by its name
 *
 * \param container ao2container holding the named ACLs
 * \param name of the ACL wanted to be found
 * \retval pointer to the named ACL if available. Null if not found.
 */
void *named_acl_find(struct ao2_container *container, const char *cat)
{
	struct named_acl tmp;
	ast_copy_string(tmp.name, cat, sizeof(tmp.name));
	return ao2_find(container, &tmp, OBJ_POINTER);
}

/*!
 * \internal
 * \brief Callback function to compare the ACL order of two given categories.
 *        This function is used to sort lists of ACLs received from realtime.
 *
 * \param p first category being compared
 * \param q second category being compared
 *
 * \retval -1 (p < q)
 * \retval 0 (p == q)
 * \retval 1 (p > q)
 */
static int acl_order_comparator(struct ast_category *p, struct ast_category *q)
{
	int p_value = 0, q_value = 0;
	struct ast_variable *p_var = ast_category_first(p);
	struct ast_variable *q_var = ast_category_first(q);

	while (p_var) {
		if (!strcasecmp(p_var->name, "rule_order")) {
			p_value = atoi(p_var->value);
			break;
		}
		p_var = p_var->next;
	}

	while (q_var) {
		if (!strcasecmp(q_var->name, "rule_order")) {
			q_value = atoi(q_var->value);
			break;
		}
		q_var = q_var->next;
	}

	if (p_value < q_value) {
		return -1;
	} else if (q_value < p_value) {
		return 1;
	}

	return 0;
}

/*!
 * \internal
 * \brief Search for a named ACL via realtime Database and build the named_acl
 *        if it is valid.
 *
 * \param name of the ACL wanted to be found
 * \retval pointer to the named ACL if available. Null if the ACL subsystem is unconfigured.
 */
static struct named_acl *named_acl_find_realtime(const char *name)
{
	struct ast_config *cfg;
	char *item = NULL;
	const char *systemname = NULL;
	struct ast_ha *built_ha = NULL;
	struct named_acl *acl;

	/* If we have a systemname set in the global options, we only want to retrieve entries with a matching systemname field. */
	systemname = ast_config_AST_SYSTEM_NAME;

	if (ast_strlen_zero(systemname)) {
		cfg = ast_load_realtime_multientry(ACL_FAMILY, "name", name, SENTINEL);
	} else {
		cfg = ast_load_realtime_multientry(ACL_FAMILY, "name", name, "systemname", systemname, SENTINEL);
	}

	if (!cfg) {
		return NULL;
	}

	/* At this point, the configuration must be sorted by the order field. */
	ast_config_sort_categories(cfg, 0, acl_order_comparator);

	while ((item = ast_category_browse(cfg, item))) {
		int append_ha_error = 0;
		const char *order = ast_variable_retrieve(cfg, item, "rule_order");
		const char *sense = ast_variable_retrieve(cfg, item, "sense");
		const char *rule = ast_variable_retrieve(cfg, item, "rule");

		built_ha = ast_append_ha(sense, rule, built_ha, &append_ha_error);
		if (append_ha_error) {
			/* We need to completely reject an ACL that contains any bad rules. */
			ast_log(LOG_ERROR, "Rejecting realtime ACL due to bad ACL definition '%s': %s - %s - %s\n", name, order, sense, rule);
			ast_free_ha(built_ha);
			return NULL;
		}
	}

	ast_config_destroy(cfg);

	acl = named_acl_alloc(name);
	if (!acl) {
		ast_log(LOG_ERROR, "allocation error\n");
		ast_free_ha(built_ha);
		return NULL;
	}

	acl->ha = built_ha;

	return acl;
}

struct ast_ha *ast_named_acl_find(const char *name, int *is_realtime, int *is_undefined) {
	struct ast_ha *ha = NULL;

	RAII_VAR(struct named_acl_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct named_acl *, named_acl, NULL, ao2_cleanup);

	if (is_realtime) {
		*is_realtime = 0;
	}

	if (is_undefined) {
		*is_undefined = 0;
	}

	/* If the config or its named_acl_list hasn't been initialized, abort immediately. */
	if ((!cfg) || (!(cfg->named_acl_list))) {
		ast_log(LOG_ERROR, "Attempted to find named ACL '%s', but the ACL configuration isn't available.\n", name);
		return NULL;
	}

	named_acl = named_acl_find(cfg->named_acl_list, name);

	/* If a named ACL couldn't be retrieved locally, we need to try realtime storage. */
	if (!named_acl) {
		RAII_VAR(struct named_acl *, realtime_acl, NULL, ao2_cleanup);

		/* Attempt to create from realtime */
		if ((realtime_acl = named_acl_find_realtime(name))) {
			if (is_realtime) {
				*is_realtime = 1;
			}
			ha = ast_duplicate_ha_list(realtime_acl->ha);
			return ha;
		}

		/* Couldn't create from realtime. Raise relevant flags and print relevant warnings. */
		if (ast_realtime_is_mapping_defined(ACL_FAMILY) && !ast_check_realtime(ACL_FAMILY)) {
			ast_log(LOG_WARNING, "ACL '%s' does not exist. The ACL will be marked as undefined and will automatically fail if applied.\n"
				"This ACL may exist in the configured realtime backend, but that backend hasn't been registered yet. "
				"Fix this establishing preload for the backend in 'modules.conf'.\n", name);
		} else {
			ast_log(LOG_WARNING, "ACL '%s' does not exist. The ACL will be marked as undefined and will automatically fail if applied.\n", name);
		}

		if (is_undefined) {
			*is_undefined = 1;
		}

		return NULL;
	}

	ha = ast_duplicate_ha_list(named_acl->ha);

	if (!ha) {
		ast_log(LOG_NOTICE, "ACL '%s' contains no rules. It is valid, but it will accept addresses unconditionally.\n", name);
	}

	return ha;
}

/*!
 * \internal
 * \brief Sends an update event corresponding to a given named ACL that has changed.
 *
 * \param name Name of the ACL that has changed. May be an empty string (but not NULL)
 *        If name is an empty string, then all ACLs must be refreshed.
 *
 * \retval 0 success
 * \retval 1 failure
 */
static int push_acl_change_event(char *name)
{
	struct ast_event *event = ast_event_new(AST_EVENT_ACL_CHANGE,
							AST_EVENT_IE_DESCRIPTION, AST_EVENT_IE_PLTYPE_STR, name,
							AST_EVENT_IE_END);
	if (!event) {
		ast_log(LOG_ERROR, "Failed to allocate acl.conf reload event. Some modules will have out of date ACLs.\n");
		return -1;
	}

	if (ast_event_queue(event)) {
		ast_event_destroy(event);
		ast_log(LOG_ERROR, "Failed to queue acl.conf reload event. Some modules will have out of date ACLs.\n");
		return -1;
	}

	return 0;
}

/*!
 * \internal
 * \brief reload configuration for named ACLs
 *
 * \param fd file descriptor for CLI client
 */
int ast_named_acl_reload(void)
{
	enum aco_process_status status;

	status = aco_process_config(&cfg_info, 1);

	if (status == ACO_PROCESS_ERROR) {
		ast_log(LOG_WARNING, "Could not reload ACL config\n");
		return 0;
	}

	if (status == ACO_PROCESS_UNCHANGED) {
		/* We don't actually log anything if the config was unchanged,
		 * but we don't need to send a config change event either.
		 */
		return 0;
	}

	/* We need to push an ACL change event with no ACL name so that all subscribers update with all ACLs */
	push_acl_change_event("");

	return 0;
}

/*!
 * \internal
 * \brief secondary handler for the 'acl show <name>' command (with arg)
 *
 * \param fd file descriptor of the cli
 * \name name of the ACL requested for display
 */
static void cli_display_named_acl(int fd, const char *name)
{
	struct ast_ha *ha;
	int ha_index = 0;
	int is_realtime = 0;

	RAII_VAR(struct named_acl_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct named_acl *, named_acl, NULL, ao2_cleanup);

	/* If the configuration or the configuration's named_acl_list is unavailable, abort. */
	if ((!cfg) || (!cfg->named_acl_list)) {
		ast_log(LOG_ERROR, "Attempted to show named ACL '%s', but the acl configuration isn't available.\n", name);
		return;
	}

	named_acl = named_acl_find(cfg->named_acl_list, name);

	/* If the named_acl couldn't be found with the search, also abort. */
	if (!named_acl) {
		if (!(named_acl = named_acl_find_realtime(name))) {
			ast_cli(fd, "\nCould not find ACL named '%s'\n", name);
			return;
		}

		is_realtime = 1;
	}

	ast_cli(fd, "\nACL: %s%s\n---------------------------------------------\n", name, is_realtime ? " (realtime)" : "");
	for (ha = named_acl->ha; ha; ha = ha->next) {
		char *addr = ast_strdupa(ast_sockaddr_stringify_addr(&ha->addr));
		char *mask = ast_sockaddr_stringify_addr(&ha->netmask);
		ast_cli(fd, "%3d: %s - %s/%s\n", ha_index, ha->sense == AST_SENSE_ALLOW ? "allow" : " deny", addr, mask);
		ha_index++;
	}
}

/*!
 * \internal
 * \brief secondary handler for the 'acl show' command (no args)
 *
 * \param fd file descriptor of the cli
 */
static void cli_display_named_acl_list(int fd)
{
	struct ao2_iterator i;
	void *o;
	RAII_VAR(struct named_acl_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);

	ast_cli(fd, "\nacl\n---\n");

	if (!cfg || !cfg->named_acl_list) {
		ast_cli(fd, "ACL configuration isn't available.\n");
		return;
	}
	i = ao2_iterator_init(cfg->named_acl_list, 0);

	while ((o = ao2_iterator_next(&i))) {
		struct named_acl *named_acl = o;
		ast_cli(fd, "%s\n", named_acl->name);
		ao2_ref(o, -1);
	}

	ao2_iterator_destroy(&i);
}

/* \brief ACL command show <name> */
static char *handle_show_named_acl_cmd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct named_acl_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	int length;
	int which;
	struct ao2_iterator i;
	struct named_acl *named_acl;
	char *match = NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "acl show";
		e->usage =
			"Usage: acl show [name]\n"
			"   Shows a list of named ACLs or lists all entries in a given named ACL.\n";
		return NULL;
	case CLI_GENERATE:
		if (!cfg) {
			return NULL;
		}
		length = strlen(a->word);
		which = 0;
		i = ao2_iterator_init(cfg->named_acl_list, 0);
		while ((named_acl = ao2_iterator_next(&i))) {
			if (!strncasecmp(a->word, named_acl->name, length) && ++which > a->n) {
				match = ast_strdup(named_acl->name);
				ao2_ref(named_acl, -1);
				break;
			}
			ao2_ref(named_acl, -1);
		}
		ao2_iterator_destroy(&i);
		return match;

	}

	if (a->argc == 2) {
		cli_display_named_acl_list(a->fd);
		return CLI_SUCCESS;
	}

	if (a->argc == 3) {
		cli_display_named_acl(a->fd, a->argv[2]);
		return CLI_SUCCESS;
	}


	return CLI_SHOWUSAGE;
}

static struct ast_cli_entry cli_named_acl[] = {
	AST_CLI_DEFINE(handle_show_named_acl_cmd, "Show a named ACL or list all named ACLs"),
};

static void named_acl_cleanup(void)
{
	ast_cli_unregister_multiple(cli_named_acl, ARRAY_LEN(cli_named_acl));

	aco_info_destroy(&cfg_info);
	ao2_global_obj_release(globals);
}

int ast_named_acl_init()
{
	ast_cli_register_multiple(cli_named_acl, ARRAY_LEN(cli_named_acl));

	ast_register_atexit(named_acl_cleanup);

	if (aco_info_init(&cfg_info)) {
		return 0;
	}

	/* Register the per level options. */
	aco_option_register(&cfg_info, "permit", ACO_EXACT, named_acl_types, NULL, OPT_ACL_T, 1, FLDSET(struct named_acl, ha));
	aco_option_register(&cfg_info, "deny", ACO_EXACT, named_acl_types, NULL, OPT_ACL_T, 0, FLDSET(struct named_acl, ha));

	aco_process_config(&cfg_info, 0);

	return 0;
}
