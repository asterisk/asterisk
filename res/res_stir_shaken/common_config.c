/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2023, Sangoma Technologies Corporation
 *
 * George Joseph <gjoseph@sangoma.com>
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
#include "asterisk/cli.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/utils.h"
#include "asterisk/stasis.h"
#include "asterisk/security_events.h"

#define AST_API_MODULE
#include "stir_shaken.h"

static struct ast_sorcery *sorcery;
struct stasis_subscription *named_acl_changed_sub = NULL;

struct ast_sorcery *get_sorcery(void)
{
	return sorcery;
}

#define generate_bool_handler_functions(param_name) \
static const char *param_name ## _map[] = { \
	[ param_name ## _NOT_SET ] = "not_set", \
	[ param_name ## _YES ] = "yes", \
	[ param_name ## _NO ] = "no", \
}; \
enum param_name ## _enum \
	param_name ## _from_str(const char *value) \
{ \
	if (!strcasecmp(value, param_name ## _map[param_name ## _NOT_SET])) { \
		return param_name ## _NOT_SET; \
	} else if (ast_true(value)) { \
		return param_name ## _YES; \
	} else if (ast_false(value)) { \
		return param_name ## _NO; \
	} \
	ast_log(LOG_WARNING, "Unknown " #param_name " response value '%s'\n", value); \
	return param_name ## _UNKNOWN; \
}\
const char *param_name ## _to_str(enum param_name ## _enum value) \
{ \
	return ARRAY_IN_BOUNDS(value, param_name ## _map) ? \
		param_name ## _map[value] : NULL; \
}

generate_bool_handler_functions(use_rfc9410_responses);
generate_bool_handler_functions(send_mky);
generate_bool_handler_functions(check_tn_cert_public_url);
generate_bool_handler_functions(relax_x5u_port_scheme_restrictions);
generate_bool_handler_functions(relax_x5u_path_restrictions);

generate_bool_handler_functions(load_system_certs);

struct enum_name_xref_entry {
	int value;
	const char *name;
};

#define generate_enum_string_functions(param_name, default_value, ...)\
static struct enum_name_xref_entry param_name ## _map[] = { \
	__VA_ARGS__ \
} ; \
enum param_name ## _enum param_name ## _from_str( \
	const char *value) \
{ \
	int i; \
	for (i = 0; i < ARRAY_LEN(param_name ## _map); i++) { \
		if (strcasecmp(value, param_name ##_map[i].name) == 0) { \
			return param_name ##_map[i].value; \
		} \
	} \
	return param_name ## _ ## default_value; \
} \
const char *param_name ## _to_str( \
	enum param_name ## _enum value) \
{ \
	int i; \
	for (i = 0; i < ARRAY_LEN(param_name ## _map); i++) { \
		if (value == param_name ## _map[i].value) return param_name ## _map[i].name; \
	} \
	return NULL; \
}

generate_enum_string_functions(attest_level, UNKNOWN,
	{attest_level_NOT_SET, "not_set"},
	{attest_level_A, "A"},
	{attest_level_B, "B"},
	{attest_level_C, "C"},
);

generate_enum_string_functions(endpoint_behavior, OFF,
	{endpoint_behavior_OFF,  "off"},
	{endpoint_behavior_OFF,  "none"},
	{endpoint_behavior_ATTEST, "attest"},
	{endpoint_behavior_VERIFY, "verify"},
	{endpoint_behavior_ON, "on"},
	{endpoint_behavior_ON, "both"}
);

generate_enum_string_functions(stir_shaken_failure_action, CONTINUE,
	{stir_shaken_failure_action_CONTINUE, "continue"},
	{stir_shaken_failure_action_REJECT_REQUEST, "reject_request"},
	{stir_shaken_failure_action_CONTINUE_RETURN_REASON, "continue_return_reason"},
);

static const char *translate_value(const char *val)
{
	if (val[0] == '0'
		|| val[0] == '\0'
		|| strcmp(val, "not_set") == 0) {
		return "";
	}

	return val;
}

static void print_acl(int fd, struct ast_acl_list *acl_list, const char *prefix)
{
	struct ast_acl *acl;

	AST_LIST_LOCK(acl_list);
	AST_LIST_TRAVERSE(acl_list, acl, list) {
		if (ast_strlen_zero(acl->name)) {
			ast_cli(fd, "%s(permit/deny)\n", prefix);
		} else {
			ast_cli(fd, "%s%s\n", prefix, acl->name);
		}
		ast_ha_output(fd, acl->acl, prefix);
	}
	AST_LIST_UNLOCK(acl_list);
}

#define print_acl_cert_store(cfg, a, max_name_len) \
({ \
	if (cfg->vcfg_common.acl) { \
		ast_cli(a->fd, "x5u_acl:\n"); \
		print_acl(a->fd, cfg->vcfg_common.acl, "   "); \
	} else { \
		ast_cli(a->fd, "%-*s: (none)\n", max_name_len, "x5u_acl"); \
	}\
	if (cfg->vcfg_common.tcs) { \
		int count = 0; \
		ast_cli(a->fd, "%-*s:\n", max_name_len, "Verification CA certificate store"); \
		count = crypto_show_cli_store(cfg->vcfg_common.tcs, a->fd); \
		if (count == 0 && (!ast_strlen_zero(cfg->vcfg_common.ca_path) \
			|| !ast_strlen_zero(cfg->vcfg_common.crl_path))) { \
			ast_cli(a->fd, "   Note: Certs in ca_path or crl_path won't show until used.\n"); \
		} \
	} else { \
		ast_cli(a->fd, "%-*s: (none)\n", max_name_len, "Verification CA certificate store"); \
	} \
})

int config_object_cli_show(void *obj, void *arg, void *data, int flags)
{
	struct ast_cli_args *a = arg;
	struct config_object_cli_data *cli_data = data;
	struct ast_variable *options;
	struct ast_variable *i;
	const char *title = NULL;
	const char *cfg_name = NULL;
	int max_name_len = 0;

	if (!obj) {
		ast_cli(a->fd, "No stir/shaken configuration found\n");
		return 0;
	}

	if (!ast_strlen_zero(cli_data->title)) {
		title = cli_data->title;
	} else {
		title = ast_sorcery_object_get_type(obj);
	}
	max_name_len = strlen(title);

	if (cli_data->object_type == config_object_type_profile
		|| cli_data->object_type == config_object_type_tn) {
		cfg_name = ast_sorcery_object_get_id(obj);
		max_name_len += strlen(cfg_name) + 2 /* ": " */;
	}

	options = ast_variable_list_sort(ast_sorcery_objectset_create2(
		get_sorcery(), obj, AST_HANDLER_ONLY_STRING));
	if (!options) {
		return 0;
	}

	for (i = options; i; i = i->next) {
		int nlen = strlen(i->name);
		max_name_len = (nlen > max_name_len) ? nlen : max_name_len;
	}

	ast_cli(a->fd, "\n==============================================================================\n");
	if (ast_strlen_zero(cfg_name))  {
		ast_cli(a->fd, "%s\n", title);
	} else {
		ast_cli(a->fd, "%s: %s\n", title, cfg_name);
	}
	ast_cli(a->fd, "------------------------------------------------------------------------------\n");

	for (i = options; i; i = i->next) {
		if (!ast_strings_equal(i->name, "x5u_acl")) {
			ast_cli(a->fd, "%-*s: %s\n", max_name_len, i->name,
				translate_value(i->value));
		}
	}

	ast_variables_destroy(options);

	if (cli_data->object_type == config_object_type_profile) {
		struct profile_cfg *cfg = obj;
		print_acl_cert_store(cfg, a, max_name_len);
	} else if (cli_data->object_type == config_object_type_verification) {
		struct verification_cfg *cfg = obj;
		print_acl_cert_store(cfg, a, max_name_len);
	}
	ast_cli(a->fd, "---------------------------------------------\n\n"); \

	return 0;
}

char *config_object_tab_complete_name(const char *word, struct ao2_container *container)
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


/* Remove everything except 0-9, *, and # in telephone number according to RFC 8224
 * (required by RFC 8225 as part of canonicalization) */
char *canonicalize_tn(const char *tn, char *dest_tn)
{
	int i;
	const char *s = tn;
	size_t len = tn ? strlen(tn) : 0;
	char *new_tn = dest_tn;
	SCOPE_ENTER(3, "tn: %s\n", S_OR(tn, "(null)"));

	if (ast_strlen_zero(tn)) {
		*dest_tn = '\0';
		SCOPE_EXIT_RTN_VALUE(NULL, "Empty TN\n");
	}

	if (!dest_tn) {
		SCOPE_EXIT_RTN_VALUE(NULL, "No destination buffer\n");
	}

	for (i = 0; i < len; i++) {
		if (isdigit(*s) || *s == '#' || *s == '*') { /* Only characters allowed */
			*new_tn++ = *s;
		}
		s++;
	}
	*new_tn = '\0';
	SCOPE_EXIT_RTN_VALUE(dest_tn, "Canonicalized '%s' -> '%s'\n", tn, dest_tn);
}

char *canonicalize_tn_alloc(const char *tn)
{
	char *canon_tn = ast_strlen_zero(tn) ? NULL : ast_malloc(strlen(tn) + 1);
	if (!canon_tn) {
		return NULL;
	}
	return canonicalize_tn(tn, canon_tn);
}

static char *cli_verify_cert(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct profile_cfg *, profile, NULL, ao2_cleanup);
	RAII_VAR(struct verification_cfg *, vs_cfg, NULL, ao2_cleanup);
	struct crypto_cert_store *tcs;
	X509 *cert = NULL;
	const char *errmsg = NULL;

	switch(cmd) {
	case CLI_INIT:
		e->command = "stir_shaken verify certificate_file";
		e->usage =
			"Usage: stir_shaken verify certificate_file <certificate_file> [ <profile> ]\n"
			"       Verify an external certificate file against the global or profile verification store\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 4) {
			return config_object_tab_complete_name(a->word, profile_get_all());
		} else {
			return NULL;
		}
	}

	if (a->argc < 4) {
		return CLI_SHOWUSAGE;
	}

	if (a->argc == 5) {
		profile = profile_get_cfg(a->argv[4]);
		if (!profile) {
			ast_cli(a->fd, "Profile %s doesn't exist\n", a->argv[4]);
			return CLI_SUCCESS;
		}
		if (!profile->vcfg_common.tcs) {
			ast_cli(a->fd,"Profile %s doesn't have a certificate store\n", a->argv[4]);
			return CLI_SUCCESS;
		}
		tcs = profile->vcfg_common.tcs;
	} else {
		vs_cfg = vs_get_cfg();
		if (!vs_cfg) {
			ast_cli(a->fd, "No verification store found\n");
			return CLI_SUCCESS;
		}
		tcs = vs_cfg->vcfg_common.tcs;
	}

	cert = crypto_load_cert_from_file(a->argv[3]);
	if (!cert) {
		ast_cli(a->fd, "Failed to load certificate from %s.  See log for details\n", a->argv[3]);
		return CLI_SUCCESS;
	}

	if (crypto_is_cert_trusted(tcs, cert, &errmsg)) {
		ast_cli(a->fd, "Certificate %s trusted\n", a->argv[3]);
	} else {
		ast_cli(a->fd, "Certificate %s NOT trusted: %s\n", a->argv[3], errmsg);
	}
	X509_free(cert);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_commands[] = {
	AST_CLI_DEFINE(cli_verify_cert, "Verify a certificate file against the global or a profile verification store"),
};

int common_config_reload(void)
{
	SCOPE_ENTER(2, "Stir Shaken Reload\n");
	if (vs_reload()) {
		SCOPE_EXIT_RTN_VALUE(AST_MODULE_LOAD_DECLINE, "Stir Shaken VS Reload failed\n");
	}

	if (as_reload()) {
		SCOPE_EXIT_RTN_VALUE(AST_MODULE_LOAD_DECLINE, "Stir Shaken AS Reload failed\n");
	}

	if (tn_config_reload()) {
		SCOPE_EXIT_RTN_VALUE(AST_MODULE_LOAD_DECLINE, "Stir Shaken TN Reload failed\n");
	}

	if (profile_reload()) {
		SCOPE_EXIT_RTN_VALUE(AST_MODULE_LOAD_DECLINE, "Stir Shaken Profile Reload failed\n");
	}

	SCOPE_EXIT_RTN_VALUE(AST_MODULE_LOAD_SUCCESS, "Stir Shaken Reload Done\n");
}

int common_config_unload(void)
{
	ast_cli_unregister_multiple(cli_commands, ARRAY_LEN(cli_commands));

	profile_unload();
	tn_config_unload();
	as_unload();
	vs_unload();

	if (named_acl_changed_sub) {
		stasis_unsubscribe(named_acl_changed_sub);
		named_acl_changed_sub = NULL;
	}
	ast_sorcery_unref(sorcery);
	sorcery = NULL;

	return 0;
}

static void named_acl_changed_cb(void *data,
	struct stasis_subscription *sub, struct stasis_message *message)
{
	if (stasis_message_type(message) != ast_named_acl_change_type()) {
		return;
	}
	ast_log(LOG_NOTICE, "Named acl changed.  Reloading verification and profile\n");
	common_config_reload();
}

int common_config_load(void)
{
	SCOPE_ENTER(2, "Stir Shaken Load\n");

	if (!(sorcery = ast_sorcery_open())) {
		common_config_unload();
		SCOPE_EXIT_RTN_VALUE(AST_MODULE_LOAD_DECLINE, "Stir Shaken sorcery load failed\n");
	}

	if (vs_load()) {
		common_config_unload();
		SCOPE_EXIT_RTN_VALUE(AST_MODULE_LOAD_DECLINE, "Stir Shaken VS load failed\n");
	}

	if (as_load()) {
		common_config_unload();
		SCOPE_EXIT_RTN_VALUE(AST_MODULE_LOAD_DECLINE, "Stir Shaken AS load failed\n");
	}

	if (tn_config_load()) {
		common_config_unload();
		SCOPE_EXIT_RTN_VALUE(AST_MODULE_LOAD_DECLINE, "Stir Shaken TN load failed\n");
	}

	if (profile_load()) {
		common_config_unload();
		SCOPE_EXIT_RTN_VALUE(AST_MODULE_LOAD_DECLINE, "Stir Shaken profile load failed\n");
	}

	if (!named_acl_changed_sub) {
		named_acl_changed_sub = stasis_subscribe(ast_security_topic(),
			named_acl_changed_cb, NULL);
		if (!named_acl_changed_sub) {
			common_config_unload();
			SCOPE_EXIT_RTN_VALUE(AST_MODULE_LOAD_DECLINE, "Stir Shaken acl change subscribe failed\n");
		}
		stasis_subscription_accept_message_type(
			named_acl_changed_sub, ast_named_acl_change_type());
	}

	ast_cli_register_multiple(cli_commands, ARRAY_LEN(cli_commands));

	SCOPE_EXIT_RTN_VALUE(AST_MODULE_LOAD_SUCCESS, "Stir Shaken Load Done\n");
}
