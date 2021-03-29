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
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/sorcery.h"
#include "asterisk/cli.h"
#include "asterisk/format.h"
#include "asterisk/format_cap.h"

/*** DOCUMENTATION
	<configInfo name="res_aeap" language="en_US">
		<synopsis>Asterisk External Application Protocol (AEAP) module for Asterisk</synopsis>
		<configFile name="aeap.conf">
			<configObject name="server">
				<synopsis>AEAP server options</synopsis>
				<configOption name="type">
					<synopsis>Must be of type 'server'.</synopsis>
				</configOption>
				<configOption name="server_url">
					<synopsis>The URL of the server to connect to.</synopsis>
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

struct aeap_server
{
	SORCERY_OBJECT(details);
	AST_DECLARE_STRING_FIELDS(
		/*! The URL of the server to connect to */
		AST_STRING_FIELD(server_url);
	);
	/*! An optional list of codecs that will be used if provided */
	struct ast_format_cap *codecs;
};

static void aeap_server_destructor(void *obj)
{
	struct aeap_server *cfg = obj;

	ast_string_field_free_memory(cfg);
	ao2_cleanup(cfg->codecs);
}

static void *aeap_server_alloc(const char *name)
{
	struct aeap_server *cfg;

	cfg = ast_sorcery_generic_alloc(sizeof(*cfg), aeap_server_destructor);
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

static int aeap_server_apply(const struct ast_sorcery *sorcery, void *obj)
{
	struct aeap_server *cfg = obj;

	if (ast_strlen_zero(cfg->server_url)) {
		ast_log(LOG_ERROR, "AEAP - Server URL must be present for server '%s'\n", ast_sorcery_object_get_id(cfg));
		return -1;
	}

	if (!ast_begins_with(cfg->server_url, "ws")) {
		ast_log(LOG_ERROR, "AEAP - Server URL must be ws or wss for server '%s'\n", ast_sorcery_object_get_id(cfg));
		return -1;
	}

	return 0;
}

static struct aeap_server *aeap_server_get(const char *id)
{
	return ast_sorcery_retrieve_by_id(aeap_sorcery, "server", id);
}

static struct ao2_container *aeap_server_get_all(void)
{
	return ast_sorcery_retrieve_by_fields(aeap_sorcery, "server",
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
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

	options = ast_variable_list_sort(ast_sorcery_objectset_create2(
		aeap_sorcery, obj, AST_HANDLER_ONLY_STRING));
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

static char *aeap_server_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct aeap_server *cfg;

	switch(cmd) {
	case CLI_INIT:
		e->command = "aeap show server";
		e->usage =
			"Usage: aeap show server <id>\n"
			"       Show the AEAP settings for a given server\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			return aeap_tab_complete_name(a->word, aeap_server_get_all());
		} else {
			return NULL;
		}
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	cfg = aeap_server_get(a->argv[3]);
	aeap_cli_show(cfg, a, 0);
	ao2_cleanup(cfg);

	return CLI_SUCCESS;
}

static char *aeap_server_show_all(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_container *container;

	switch(cmd) {
	case CLI_INIT:
		e->command = "aeap show servers";
		e->usage =
			"Usage: aeap show servers\n"
			"       Show all configured AEAP servers\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	container = aeap_server_get_all();
	if (!container || ao2_container_count(container) == 0) {
		ast_cli(a->fd, "No AEAP servers found\n");
		ao2_cleanup(container);
		return CLI_SUCCESS;
	}

	ao2_callback(container, OBJ_NODATA, aeap_cli_show, a);
	ao2_ref(container, -1);

	return CLI_SUCCESS;
}

static struct ast_cli_entry aeap_cli[] = {
	AST_CLI_DEFINE(aeap_server_show, "Show AEAP server configuration by id"),
	AST_CLI_DEFINE(aeap_server_show_all, "Show all AEAP server configurations"),
};

static int reload_module(void)
{
	return 0;
}

static int unload_module(void)
{
	ast_sorcery_unref(aeap_sorcery);
	aeap_sorcery = NULL;

	ast_cli_unregister_multiple(aeap_cli, ARRAY_LEN(aeap_cli));

	return 0;
}

static int load_module(void)
{
	if (!(aeap_sorcery = ast_sorcery_open()))
	{
		ast_log(LOG_ERROR, "AEAP - failed to open sorcery\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_apply_default(aeap_sorcery, "server", "config", "aeap.conf,criteria=type=server");

	if (ast_sorcery_object_register(aeap_sorcery, "server", aeap_server_alloc,
		NULL, aeap_server_apply)) {
		ast_log(LOG_ERROR, "AEAP - failed to register server sorcery object\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_object_field_register(aeap_sorcery, "server", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(aeap_sorcery, "server", "server_url", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct aeap_server, server_url));
	ast_sorcery_object_field_register(aeap_sorcery, "server", "codecs", "", OPT_CODEC_T, 1, FLDSET(struct aeap_server, codecs));

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
);
