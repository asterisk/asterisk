/*
 * sip_cli_commands.c
 *
 *  Created on: Jan 25, 2013
 *      Author: mjordan
 */

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>

#include "asterisk/res_sip.h"
#include "include/res_sip_private.h"
#include "asterisk/cli.h"
#include "asterisk/astobj2.h"
#include "asterisk/utils.h"
#include "asterisk/sorcery.h"
#include "asterisk/callerid.h"

static struct ast_sorcery *sip_sorcery;

static char *handle_cli_show_endpoints(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct ao2_container *, endpoints, NULL, ao2_cleanup);
	struct ao2_iterator it_endpoints;
	struct ast_sip_endpoint *endpoint;

	switch (cmd) {
	case CLI_INIT:
		e->command = "sip show endpoints";
		e->usage =
			"Usage: sip show endpoints\n"
			"       Show the registered SIP endpoints\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	endpoints = ast_res_sip_get_endpoints();
	if (!endpoints) {
		return CLI_FAILURE;
	}

	if (!ao2_container_count(endpoints)) {
		ast_cli(a->fd, "No endpoints found\n");
		return CLI_SUCCESS;
	}

	ast_cli(a->fd, "Endpoints:\n");
	it_endpoints = ao2_iterator_init(endpoints, 0);
	while ((endpoint = ao2_iterator_next(&it_endpoints))) {
		ast_cli(a->fd, "%s\n", ast_sorcery_object_get_id(endpoint));
		ao2_ref(endpoint, -1);
	}
	ao2_iterator_destroy(&it_endpoints);
	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_commands[] = {
	AST_CLI_DEFINE(handle_cli_show_endpoints, "Show SIP Endpoints"),
};

static int dtmf_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	if (!strcasecmp(var->value, "rfc4733")) {
		endpoint->dtmf = AST_SIP_DTMF_RFC_4733;
	} else if (!strcasecmp(var->value, "inband")) {
		endpoint->dtmf = AST_SIP_DTMF_INBAND;
	} else if (!strcasecmp(var->value, "info")) {
		endpoint->dtmf = AST_SIP_DTMF_INFO;
	} else if (!strcasecmp(var->value, "none")) {
		endpoint->dtmf = AST_SIP_DTMF_NONE;
	} else {
		return -1;
	}

	return 0;
}

static int prack_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	if (ast_true(var->value)) {
		endpoint->extensions |= PJSIP_INV_SUPPORT_100REL;
	} else if (ast_false(var->value)) {
		endpoint->extensions &= PJSIP_INV_SUPPORT_100REL;
	} else if (!strcasecmp(var->value, "required")) {
		endpoint->extensions |= PJSIP_INV_REQUIRE_100REL;
	} else {
		return -1;
	}

	return 0;
}

static int timers_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	if (ast_true(var->value)) {
		endpoint->extensions |= PJSIP_INV_SUPPORT_TIMER;
	} else if (ast_false(var->value)) {
		endpoint->extensions &= PJSIP_INV_SUPPORT_TIMER;
	} else if (!strcasecmp(var->value, "required")) {
		endpoint->extensions |= PJSIP_INV_REQUIRE_TIMER;
	} else if (!strcasecmp(var->value, "always")) {
		endpoint->extensions |= PJSIP_INV_ALWAYS_USE_TIMER;
	} else {
		return -1;
	}

	return 0;
}

static void destroy_auths(const char **auths, size_t num_auths)
{
	int i;
	for (i = 0; i < num_auths; ++i) {
		ast_free((char *) auths[i]);
	}
	ast_free(auths);
}

#define AUTH_INCREMENT 4

static const char **auth_alloc(const char *value, size_t *num_auths)
{
	char *auths = ast_strdupa(value);
	char *val;
	int num_alloced = 0;
	const char **alloced_auths = NULL;

	while ((val = strsep(&auths, ","))) {
		if (*num_auths >= num_alloced) {
			size_t size;
			num_alloced += AUTH_INCREMENT;
			size = num_alloced * sizeof(char *);
			alloced_auths = ast_realloc(alloced_auths, size);
			if (!alloced_auths) {
				goto failure;
			}
		}
		alloced_auths[*num_auths] = ast_strdup(val);
		if (!alloced_auths[*num_auths]) {
			goto failure;
		}
		++(*num_auths);
	}
	return alloced_auths;

failure:
	destroy_auths(alloced_auths, *num_auths);
	return NULL;
}

static int inbound_auth_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	endpoint->sip_inbound_auths = auth_alloc(var->value, &endpoint->num_inbound_auths);
	if (!endpoint->sip_inbound_auths) {
		return -1;
	}
	return 0;
}
static int outbound_auth_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	endpoint->sip_outbound_auths = auth_alloc(var->value, &endpoint->num_outbound_auths);
	if (!endpoint->sip_outbound_auths) {
		return -1;
	}
	return 0;
}

static int ident_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;
	char *idents = ast_strdupa(var->value);
	char *val;

	while ((val = strsep(&idents, ","))) {
		if (!strcasecmp(val, "username")) {
			endpoint->ident_method |= AST_SIP_ENDPOINT_IDENTIFY_BY_USERNAME;
		} else if (!strcasecmp(val, "location")) {
			endpoint->ident_method |= AST_SIP_ENDPOINT_IDENTIFY_BY_LOCATION;
		} else {
			ast_log(LOG_ERROR, "Unrecognized identification method %s specified for endpoint %s\n",
					val, ast_sorcery_object_get_id(endpoint));
			return -1;
		}
	}
	return 0;
}

static int direct_media_method_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	if (!strcasecmp(var->value, "invite") || !strcasecmp(var->value, "reinvite")) {
		endpoint->direct_media_method = AST_SIP_SESSION_REFRESH_METHOD_INVITE;
	} else if (!strcasecmp(var->value, "update")) {
		endpoint->direct_media_method = AST_SIP_SESSION_REFRESH_METHOD_UPDATE;
	} else {
		ast_log(LOG_NOTICE, "Unrecognized option value %s for %s on endpoint %s\n",
				var->value, var->name, ast_sorcery_object_get_id(endpoint));
		return -1;
	}
	return 0;
}

static int direct_media_glare_mitigation_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	if (!strcasecmp(var->value, "none")) {
		endpoint->direct_media_glare_mitigation = AST_SIP_DIRECT_MEDIA_GLARE_MITIGATION_NONE;
	} else if (!strcasecmp(var->value, "outgoing")) {
		endpoint->direct_media_glare_mitigation = AST_SIP_DIRECT_MEDIA_GLARE_MITIGATION_OUTGOING;
	} else if (!strcasecmp(var->value, "incoming")) {
		endpoint->direct_media_glare_mitigation = AST_SIP_DIRECT_MEDIA_GLARE_MITIGATION_INCOMING;
	} else {
		ast_log(LOG_NOTICE, "Unrecognized option value %s for %s on endpoint %s\n",
				var->value, var->name, ast_sorcery_object_get_id(endpoint));
		return -1;
	}

	return 0;
}

static int caller_id_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;
	char cid_name[80] = { '\0' };
	char cid_num[80] = { '\0' };

	ast_callerid_split(var->value, cid_name, sizeof(cid_name), cid_num, sizeof(cid_num));
	if (!ast_strlen_zero(cid_name)) {
		endpoint->id.name.str = ast_strdup(cid_name);
		if (!endpoint->id.name.str) {
			return -1;
		}
		endpoint->id.name.valid = 1;
	}
	if (!ast_strlen_zero(cid_num)) {
		endpoint->id.number.str = ast_strdup(cid_num);
		if (!endpoint->id.number.str) {
			return -1;
		}
		endpoint->id.number.valid = 1;
	}
	return 0;
}

static int caller_id_privacy_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;
	int callingpres = ast_parse_caller_presentation(var->value);
	if (callingpres == -1 && sscanf(var->value, "%d", &callingpres) != 1) {
		return -1;
	}
	endpoint->id.number.presentation = callingpres;
	endpoint->id.name.presentation = callingpres;
	return 0;
}

static int caller_id_tag_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;
	endpoint->id.tag = ast_strdup(var->value);
	return endpoint->id.tag ? 0 : -1;
}

static void *sip_nat_hook_alloc(const char *name)
{
	return ao2_alloc(sizeof(struct ast_sip_nat_hook), NULL);
}

int ast_res_sip_initialize_configuration(void)
{
	if (ast_cli_register_multiple(cli_commands, ARRAY_LEN(cli_commands))) {
		return -1;
	}

	if (!(sip_sorcery = ast_sorcery_open())) {
		ast_log(LOG_ERROR, "Failed to open SIP sorcery failed to open\n");
		return -1;
	}

	ast_sorcery_apply_config(sip_sorcery, "res_sip");

	if (ast_sip_initialize_sorcery_auth(sip_sorcery)) {
		ast_log(LOG_ERROR, "Failed to register SIP authentication support\n");
		ast_sorcery_unref(sip_sorcery);
		sip_sorcery = NULL;
		return -1;
	}

	ast_sorcery_apply_default(sip_sorcery, "endpoint", "config", "res_sip.conf,criteria=type=endpoint");

	ast_sorcery_apply_default(sip_sorcery, "nat_hook", "memory", NULL);

	if (ast_sorcery_object_register(sip_sorcery, "endpoint", ast_sip_endpoint_alloc, NULL, NULL)) {
		ast_log(LOG_ERROR, "Failed to register SIP endpoint object with sorcery\n");
		ast_sorcery_unref(sip_sorcery);
		sip_sorcery = NULL;
		return -1;
	}

	ast_sorcery_object_register(sip_sorcery, "nat_hook", sip_nat_hook_alloc, NULL, NULL);

	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "context", "default", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, context));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "disallow", "", OPT_CODEC_T, 0, FLDSET(struct ast_sip_endpoint, prefs, codecs));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "allow", "", OPT_CODEC_T, 1, FLDSET(struct ast_sip_endpoint, prefs, codecs));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "qualify_frequency", 0, OPT_UINT_T, PARSE_IN_RANGE, FLDSET(struct ast_sip_endpoint, qualify_frequency), 0, 86400);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "dtmfmode", "rfc4733", dtmf_handler, NULL, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "rtp_ipv6", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, rtp_ipv6));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "rtp_symmetric", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, rtp_symmetric));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "ice_support", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, ice_support));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "use_ptime", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, use_ptime));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "force_rport", "yes", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, force_rport));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "rewrite_contact", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, rewrite_contact));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "transport", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, transport));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "outbound_proxy", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, outbound_proxy));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "mohsuggest", "default", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, mohsuggest));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "100rel", "yes", prack_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "timers", "yes", timers_handler, NULL, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "timers_min_se", "90", OPT_UINT_T, 0, FLDSET(struct ast_sip_endpoint, min_se));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "timers_sess_expires", "1800", OPT_UINT_T, 0, FLDSET(struct ast_sip_endpoint, sess_expires));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "auth", "", inbound_auth_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "outbound_auth", "", outbound_auth_handler, NULL, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "aors", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, aors));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "external_media_address", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, external_media_address));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "identify_by", "username,location", ident_handler, NULL, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "direct_media", "yes", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, direct_media));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "direct_media_method", "invite", direct_media_method_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "direct_media_glare_mitigation", "none", direct_media_glare_mitigation_handler, NULL, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "disable_direct_media_on_nat", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, disable_direct_media_on_nat));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "callerid", "", caller_id_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "callerid_privacy", "", caller_id_privacy_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "callerid_tag", "", caller_id_tag_handler, NULL, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "trust_id_inbound", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, trust_id_inbound));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "trust_id_outbound", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, trust_id_outbound));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "send_pai", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, send_pai));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "send_rpid", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, send_rpid));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "mailboxes", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, mailboxes));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "aggregate_mwi", "yes", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, aggregate_mwi));

	if (ast_sip_initialize_sorcery_transport(sip_sorcery)) {
		ast_log(LOG_ERROR, "Failed to register SIP transport support with sorcery\n");
		ast_sorcery_unref(sip_sorcery);
		sip_sorcery = NULL;
		return -1;
	}

	if (ast_sip_initialize_sorcery_location(sip_sorcery)) {
		ast_log(LOG_ERROR, "Failed to register SIP location support with sorcery\n");
		ast_sorcery_unref(sip_sorcery);
		sip_sorcery = NULL;
		return -1;
	}

	if (ast_sip_initialize_sorcery_domain_alias(sip_sorcery)) {
		ast_log(LOG_ERROR, "Failed to register SIP domain aliases support with sorcery\n");
		ast_sorcery_unref(sip_sorcery);
		sip_sorcery = NULL;
		return -1;
	}

	ast_sorcery_load(sip_sorcery);

	return 0;
}

void ast_res_sip_destroy_configuration(void)
{
	ast_cli_unregister_multiple(cli_commands, ARRAY_LEN(cli_commands));
	ast_sorcery_unref(sip_sorcery);
}

int ast_res_sip_reload_configuration(void)
{
	if (sip_sorcery) {
		ast_sorcery_reload(sip_sorcery);
	}
	return 0;
}

static void endpoint_destructor(void* obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	ast_string_field_free_memory(endpoint);

	if (endpoint->codecs) {
		ast_format_cap_destroy(endpoint->codecs);
	}
	destroy_auths(endpoint->sip_inbound_auths, endpoint->num_inbound_auths);
	destroy_auths(endpoint->sip_outbound_auths, endpoint->num_outbound_auths);
	ast_party_id_free(&endpoint->id);
}

void *ast_sip_endpoint_alloc(const char *name)
{
	struct ast_sip_endpoint *endpoint = ao2_alloc(sizeof(*endpoint), endpoint_destructor);
	if (!endpoint) {
		return NULL;
	}
	if (ast_string_field_init(endpoint, 64)) {
		ao2_cleanup(endpoint);
		return NULL;
	}
	if (!(endpoint->codecs = ast_format_cap_alloc_nolock())) {
		ao2_cleanup(endpoint);
		return NULL;
	}
	ast_party_id_init(&endpoint->id);
	return endpoint;
}

struct ao2_container *ast_res_sip_get_endpoints(void)
{
	struct ao2_container *endpoints;

	endpoints = ast_sorcery_retrieve_by_fields(sip_sorcery, "endpoint", AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);

	return endpoints;
}

int ast_sip_retrieve_auths(const char *auth_names[], size_t num_auths, struct ast_sip_auth **out)
{
	int i;

	for (i = 0; i < num_auths; ++i) {
		out[i] = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), SIP_SORCERY_AUTH_TYPE, auth_names[i]);
		if (!out[i]) {
			ast_log(LOG_NOTICE, "Couldn't find auth '%s'. Cannot authenticate\n", auth_names[i]);
			return -1;
		}
	}

	return 0;
}

void ast_sip_cleanup_auths(struct ast_sip_auth *auths[], size_t num_auths)
{
	int i;
	for (i = 0; i < num_auths; ++i) {
		ao2_cleanup(auths[i]);
	}
}

struct ast_sorcery *ast_sip_get_sorcery(void)
{
	return sip_sorcery;
}

