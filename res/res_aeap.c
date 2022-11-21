/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2021, Sangoma Technologies Corporation
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

/*** MODULEINFO
	<depend>res_http_websocket</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/astobj2.h"
#include "asterisk/module.h"
#include "asterisk/sorcery.h"
#include "asterisk/cli.h"
#include "asterisk/format.h"
#include "asterisk/format_cap.h"
#include "asterisk/res_aeap.h"

#include "res_aeap/general.h"

/*** DOCUMENTATION
	<configInfo name="res_aeap" language="en_US">
		<synopsis>Asterisk External Application Protocol (AEAP) module for Asterisk</synopsis>
		<configFile name="aeap.conf">
			<configObject name="client">
				<synopsis>AEAP client options</synopsis>
				<configOption name="type">
					<synopsis>Must be of type 'client'.</synopsis>
				</configOption>
				<configOption name="url">
					<synopsis>The URL of the server to connect to.</synopsis>
				</configOption>
				<configOption name="protocol">
					<synopsis>The application protocol.</synopsis>
				</configOption>
				<configOption name="codecs">
				        <synopsis>Optional media codec(s)</synopsis>
					<description><para>
					If this is specified, Asterisk will use this for codec related negotiations
					with the external application. Otherwise, Asterisk will default to using the
					codecs configured on the endpoint.
					</para></description>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
 ***/

/* Asterisk External Application Protocol sorcery object */
static struct ast_sorcery *aeap_sorcery;

struct ast_sorcery *ast_aeap_sorcery(void) {
	return aeap_sorcery;
}

struct ast_aeap_client_config
{
	SORCERY_OBJECT(details);
	AST_DECLARE_STRING_FIELDS(
		/*! The URL of the server to connect to */
		AST_STRING_FIELD(url);
		/*! The application protocol */
		AST_STRING_FIELD(protocol);
	);
	/*! An optional list of codecs that will be used if provided */
	struct ast_format_cap *codecs;
};

static void client_config_destructor(void *obj)
{
	struct ast_aeap_client_config *cfg = obj;

	ast_string_field_free_memory(cfg);
	ao2_cleanup(cfg->codecs);
}

static void *client_config_alloc(const char *name)
{
	struct ast_aeap_client_config *cfg;

	cfg = ast_sorcery_generic_alloc(sizeof(*cfg), client_config_destructor);
	if (!cfg) {
		return NULL;
	}

	if (ast_string_field_init(cfg, 512)) {
		ao2_ref(cfg, -1);
		return NULL;
	}

	if (!(cfg->codecs = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		ao2_ref(cfg, -1);
		return NULL;
	}

	return cfg;
}

static int client_config_apply(const struct ast_sorcery *sorcery, void *obj)
{
	struct ast_aeap_client_config *cfg = obj;

	if (ast_strlen_zero(cfg->url)) {
		ast_log(LOG_ERROR, "AEAP - URL must be present for '%s'\n", ast_sorcery_object_get_id(cfg));
		return -1;
	}

	if (!ast_begins_with(cfg->url, "ws")) {
		ast_log(LOG_ERROR, "AEAP - URL must be ws or wss for '%s'\n", ast_sorcery_object_get_id(cfg));
		return -1;
	}

	return 0;
}

const struct ast_format_cap *ast_aeap_client_config_codecs(const struct ast_aeap_client_config *cfg)
{
	return cfg->codecs;
}

int ast_aeap_client_config_has_protocol(const struct ast_aeap_client_config *cfg,
	const char *protocol)
{
	return !strcmp(protocol, cfg->protocol);
}

struct ao2_container *ast_aeap_client_configs_get(const char *protocol)
{
	struct ao2_container *container;
	struct ast_variable *var;

	var = protocol ? ast_variable_new("protocol ==", protocol, "") : NULL;

	container = ast_sorcery_retrieve_by_fields(aeap_sorcery,
		AEAP_CONFIG_CLIENT, AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, var);

	ast_variables_destroy(var);

	return container;
}

static struct ast_aeap_client_config *client_config_get(const char *id)
{
	return ast_sorcery_retrieve_by_id(aeap_sorcery, AEAP_CONFIG_CLIENT, id);
}

static char *aeap_tab_complete_name(const char *word, struct ao2_container *container)
{
	void *obj;
	struct ao2_iterator it;
	int wordlen = strlen(word);
	int ret;

	it = ao2_iterator_init(container, 0);
	while ((obj = ao2_iterator_next(&it))) {
		if (!strncasecmp(word, ast_sorcery_object_get_id(obj), wordlen)) {
			ret = ast_cli_completion_add(ast_strdup(ast_sorcery_object_get_id(obj)));
			if (ret) {
				ao2_ref(obj, -1);
				break;
			}
		}
		ao2_ref(obj, -1);
	}
	ao2_iterator_destroy(&it);

	ao2_ref(container, -1);

	return NULL;
}

static int aeap_cli_show(void *obj, void *arg, int flags)
{
	struct ast_cli_args *a = arg;
	struct ast_variable *options;
	struct ast_variable *i;

	if (!obj) {
		ast_cli(a->fd, "No AEAP configuration found\n");
		return 0;
	}

	options = ast_variable_list_sort(ast_sorcery_objectset_create(aeap_sorcery, obj));
	if (!options) {
		return 0;
	}

	ast_cli(a->fd, "%s: %s\n", ast_sorcery_object_get_type(obj),
		ast_sorcery_object_get_id(obj));

	for (i = options; i; i = i->next) {
		ast_cli(a->fd, "\t%s: %s\n", i->name, i->value);
	}

	ast_cli(a->fd, "\n");

	ast_variables_destroy(options);

	return 0;
}

static char *client_config_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_aeap_client_config *cfg;

	switch(cmd) {
	case CLI_INIT:
		e->command = "aeap show client";
		e->usage =
			"Usage: aeap show client <id>\n"
			"       Show the AEAP settings for a given client\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			return aeap_tab_complete_name(a->word, ast_aeap_client_configs_get(NULL));
		} else {
			return NULL;
		}
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	cfg = client_config_get(a->argv[3]);
	aeap_cli_show(cfg, a, 0);
	ao2_cleanup(cfg);

	return CLI_SUCCESS;
}

static char *client_config_show_all(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_container *container;

	switch(cmd) {
	case CLI_INIT:
		e->command = "aeap show clients";
		e->usage =
			"Usage: aeap show clients\n"
			"       Show all configured AEAP clients\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	container = ast_aeap_client_configs_get(NULL);
	if (!container || ao2_container_count(container) == 0) {
		ast_cli(a->fd, "No AEAP clients found\n");
		ao2_cleanup(container);
		return CLI_SUCCESS;
	}

	ao2_callback(container, OBJ_NODATA, aeap_cli_show, a);
	ao2_ref(container, -1);

	return CLI_SUCCESS;
}

static struct ast_cli_entry aeap_cli[] = {
	AST_CLI_DEFINE(client_config_show, "Show AEAP client configuration by id"),
	AST_CLI_DEFINE(client_config_show_all, "Show all AEAP client configurations"),
};

static struct ast_aeap *aeap_create(const char *id,	const struct ast_aeap_params *params,
	int connect, int timeout)
{
	struct ast_aeap_client_config *cfg;
	struct ast_aeap *aeap;
	const char *url = NULL;
	const char *protocol = NULL;

	cfg = client_config_get(id);
	if (cfg) {
		url = cfg->url;
		protocol = cfg->protocol;
	}

#ifdef TEST_FRAMEWORK
	else if (ast_begins_with(id, "_aeap_test_")) {
		url = "ws://127.0.0.1:8088/ws";
		protocol = id;
	}
#endif

	if (!url && !protocol) {
		ast_log(LOG_ERROR, "AEAP: unable to get configuration for '%s'\n", id);
		return NULL;
	}

	aeap = connect ? ast_aeap_create_and_connect(url, params, url, protocol, timeout) :
		ast_aeap_create(url, params);

	ao2_cleanup(cfg);
	return aeap;
}

struct ast_aeap *ast_aeap_create_by_id(const char *id, const struct ast_aeap_params *params)
{
	return aeap_create(id, params, 0, 0);
}

struct ast_aeap *ast_aeap_create_and_connect_by_id(const char *id,
	const struct ast_aeap_params *params, int timeout)
{
	return aeap_create(id, params, 1, timeout);
}

struct ast_variable *ast_aeap_custom_fields_get(const char *id)
{
	struct ast_aeap_client_config *cfg;
	struct ast_variable *vars;

	cfg = client_config_get(id);
	if (!cfg) {
		ast_log(LOG_WARNING, "AEAP: no client configuration '%s' to get fields\n", id);
		return NULL;
	}

	vars = ast_sorcery_objectset_create(aeap_sorcery, cfg);

	ao2_ref(cfg, -1);
	return vars;
}

static int reload_module(void)
{
	ast_sorcery_reload(aeap_sorcery);

	return 0;
}

static int unload_module(void)
{
	ast_sorcery_unref(aeap_sorcery);
	aeap_sorcery = NULL;

	ast_cli_unregister_multiple(aeap_cli, ARRAY_LEN(aeap_cli));

	aeap_general_finalize();

	return 0;
}

static int load_module(void)
{
	if (aeap_general_initialize()) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (!(aeap_sorcery = ast_sorcery_open()))
	{
		ast_log(LOG_ERROR, "AEAP - failed to open sorcery\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_apply_default(aeap_sorcery, AEAP_CONFIG_CLIENT, "config", "aeap.conf,criteria=type=client");

	if (ast_sorcery_object_register(aeap_sorcery, "client", client_config_alloc,
		NULL, client_config_apply)) {
		ast_log(LOG_ERROR, "AEAP - failed to register client sorcery object\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_object_field_register(aeap_sorcery, AEAP_CONFIG_CLIENT, "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(aeap_sorcery, AEAP_CONFIG_CLIENT, "url", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_aeap_client_config, url));
	ast_sorcery_object_field_register(aeap_sorcery, AEAP_CONFIG_CLIENT, "protocol", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_aeap_client_config, protocol));
	ast_sorcery_object_field_register(aeap_sorcery, AEAP_CONFIG_CLIENT, "codecs", "", OPT_CODEC_T, 1, FLDSET(struct ast_aeap_client_config, codecs));

	ast_sorcery_load(aeap_sorcery);

	ast_cli_register_multiple(aeap_cli, ARRAY_LEN(aeap_cli));

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER,
	"Asterisk External Application Protocol Module for Asterisk",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
	.requires = "res_http_websocket",
);
