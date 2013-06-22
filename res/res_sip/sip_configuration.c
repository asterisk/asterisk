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
#include "asterisk/stasis_endpoints.h"

/*! \brief Number of buckets for persistent endpoint information */
#define PERSISTENT_BUCKETS 53

/*! \brief Persistent endpoint information */
struct sip_persistent_endpoint {
	/*! \brief Asterisk endpoint itself */
	struct ast_endpoint *endpoint;
	/*! \brief AORs that we should react to */
	char *aors;
};

/*! \brief Container for persistent endpoint information */
static struct ao2_container *persistent_endpoints;

static struct ast_sorcery *sip_sorcery;

/*! \brief Hashing function for persistent endpoint information */
static int persistent_endpoint_hash(const void *obj, const int flags)
{
	const struct sip_persistent_endpoint *persistent = obj;
	const char *id = (flags & OBJ_KEY ? obj : ast_endpoint_get_resource(persistent->endpoint));

	return ast_str_hash(id);
}

/*! \brief Comparison function for persistent endpoint information */
static int persistent_endpoint_cmp(void *obj, void *arg, int flags)
{
	const struct sip_persistent_endpoint *persistent1 = obj;
	const struct sip_persistent_endpoint *persistent2 = arg;
	const char *id = (flags & OBJ_KEY ? arg : ast_endpoint_get_resource(persistent2->endpoint));

	return !strcmp(ast_endpoint_get_resource(persistent1->endpoint), id) ? CMP_MATCH | CMP_STOP : 0;
}

/*! \brief Callback function for changing the state of an endpoint */
static int persistent_endpoint_update_state(void *obj, void *arg, int flags)
{
	struct sip_persistent_endpoint *persistent = obj;
	char *aor = arg;
	RAII_VAR(struct ast_sip_contact *, contact, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);

	if (!ast_strlen_zero(aor) && !strstr(persistent->aors, aor)) {
		return 0;
	}

	if ((contact = ast_sip_location_retrieve_contact_from_aor_list(persistent->aors))) {
		ast_endpoint_set_state(persistent->endpoint, AST_ENDPOINT_ONLINE);
		blob = ast_json_pack("{s: s}", "peer_status", "Reachable");
	} else {
		ast_endpoint_set_state(persistent->endpoint, AST_ENDPOINT_OFFLINE);
		blob = ast_json_pack("{s: s}", "peer_status", "Unreachable");
	}

	ast_endpoint_blob_publish(persistent->endpoint, ast_endpoint_state_type(), blob);

	ast_devstate_changed(AST_DEVICE_UNKNOWN, AST_DEVSTATE_CACHABLE, "Gulp/%s", ast_endpoint_get_resource(persistent->endpoint));

	return 0;
}

/*! \brief Function called when stuff relating to a contact happens (created/deleted) */
static void persistent_endpoint_contact_observer(const void *object)
{
	char *id = ast_strdupa(ast_sorcery_object_get_id(object)), *aor = NULL;

	aor = strsep(&id, ";@");

	ao2_callback(persistent_endpoints, OBJ_NODATA, persistent_endpoint_update_state, aor);
}

/*! \brief Observer for contacts so state can be updated on respective endpoints */
static struct ast_sorcery_observer state_contact_observer = {
	.created = persistent_endpoint_contact_observer,
	.deleted = persistent_endpoint_contact_observer,
};

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

static int show_contact(void *obj, void *arg, int flags)
{
	struct ast_sip_contact *contact = obj;
	struct ast_cli_args *a = arg;
	RAII_VAR(struct ast_sip_contact_status *, status, ast_sorcery_retrieve_by_id(
			 ast_sip_get_sorcery(), CONTACT_STATUS,
			 ast_sorcery_object_get_id(contact)), ao2_cleanup);

	ast_cli(a->fd, "\tContact %s:\n", contact->uri);

	if (!status) {
		ast_cli(a->fd, "\tStatus not found!\n");
		return 0;
	}

	ast_cli(a->fd, "\t\tavailable = %s\n", status->status ? "yes" : "no");

	if (status->status) {
		ast_cli(a->fd, "\t\tRTT = %lld microseconds\n", (long long)status->rtt);
	}

	return 0;
}

static void show_endpoint(struct ast_sip_endpoint *endpoint, struct ast_cli_args *a)
{
	char *aor_name, *aors;

	if (ast_strlen_zero(endpoint->aors)) {
		return;
	}

	aors = ast_strdupa(endpoint->aors);

	while ((aor_name = strsep(&aors, ","))) {
		RAII_VAR(struct ast_sip_aor *, aor,
			 ast_sip_location_retrieve_aor(aor_name), ao2_cleanup);
		RAII_VAR(struct ao2_container *, contacts, NULL, ao2_cleanup);

		if (!aor || !(contacts = ast_sip_location_retrieve_aor_contacts(aor))) {
			continue;
		}

		ast_cli(a->fd, "AOR %s:\n", ast_sorcery_object_get_id(aor));
		ao2_callback(contacts, OBJ_NODATA, show_contact, a);
	}

	return;
}

static char *cli_show_endpoint(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	const char *endpoint_name;

	switch (cmd) {
	case CLI_INIT:
		e->command = "sip show endpoint";
		e->usage =
			"Usage: sip show endpoint <endpoint>\n"
			"       Show the given SIP endpoint.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	endpoint_name = a->argv[3];

	if (!(endpoint = ast_sorcery_retrieve_by_id(
		      ast_sip_get_sorcery(), "endpoint", endpoint_name))) {
		ast_cli(a->fd, "Unable to retrieve endpoint %s\n", endpoint_name);
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "Endpoint %s:\n", endpoint_name);
	show_endpoint(endpoint, a);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_commands[] = {
	AST_CLI_DEFINE(handle_cli_show_endpoints, "Show SIP Endpoints"),
	AST_CLI_DEFINE(cli_show_endpoint, "Show SIP Endpoint")
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

static int connected_line_method_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	if (!strcasecmp(var->value, "invite") || !strcasecmp(var->value, "reinvite")) {
		endpoint->connected_line_method = AST_SIP_SESSION_REFRESH_METHOD_INVITE;
	} else if (!strcasecmp(var->value, "update")) {
		endpoint->connected_line_method = AST_SIP_SESSION_REFRESH_METHOD_UPDATE;
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

static int media_encryption_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	if (!strcasecmp("no", var->value)) {
		endpoint->media_encryption = AST_SIP_MEDIA_ENCRYPT_NONE;
	} else if (!strcasecmp("sdes", var->value)) {
		endpoint->media_encryption = AST_SIP_MEDIA_ENCRYPT_SDES;
	/*} else if (!strcasecmp("dtls", var->value)) {
		endpoint->media_encryption = AST_SIP_MEDIA_ENCRYPT_DTLS;*/
	} else {
		return -1;
	}

	return 0;
}

static int group_handler(const struct aco_option *opt,
			 struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	if (!strncmp(var->name, "callgroup", 9)) {
		if (!(endpoint->callgroup = ast_get_group(var->value))) {
			return -1;
		}
	} else if (!strncmp(var->name, "pickupgroup", 11)) {
		if (!(endpoint->pickupgroup = ast_get_group(var->value))) {
			return -1;
		}
	} else {
		return -1;
	}

	return 0;
}

static int named_groups_handler(const struct aco_option *opt,
				struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	if (!strncmp(var->name, "namedcallgroup", 14)) {
		if (!(endpoint->named_callgroups =
		      ast_get_namedgroups(var->value))) {
			return -1;
		}
	} else if (!strncmp(var->name, "namedpickupgroup", 16)) {
		if (!(endpoint->named_pickupgroups =
		      ast_get_namedgroups(var->value))) {
			return -1;
		}
	} else {
		return -1;
	}

	return 0;
}

static void *sip_nat_hook_alloc(const char *name)
{
	return ao2_alloc(sizeof(struct ast_sip_nat_hook), NULL);
}

/*! \brief Destructor function for persistent endpoint information */
static void persistent_endpoint_destroy(void *obj)
{
	struct sip_persistent_endpoint *persistent = obj;

	ast_endpoint_shutdown(persistent->endpoint);
	ast_free(persistent->aors);
}

/*! \brief Internal function which finds (or creates) persistent endpoint information */
static struct ast_endpoint *persistent_endpoint_find_or_create(const struct ast_sip_endpoint *endpoint)
{
	RAII_VAR(struct sip_persistent_endpoint *, persistent, NULL, ao2_cleanup);
	SCOPED_AO2LOCK(lock, persistent_endpoints);

	if (!(persistent = ao2_find(persistent_endpoints, ast_sorcery_object_get_id(endpoint), OBJ_KEY | OBJ_NOLOCK))) {
		if (!(persistent = ao2_alloc(sizeof(*persistent), persistent_endpoint_destroy))) {
			return NULL;
		}

		if (!(persistent->endpoint = ast_endpoint_create("Gulp", ast_sorcery_object_get_id(endpoint)))) {
			return NULL;
		}

		persistent->aors = ast_strdup(endpoint->aors);

		if (ast_strlen_zero(persistent->aors)) {
			ast_endpoint_set_state(persistent->endpoint, AST_ENDPOINT_UNKNOWN);
		} else {
			persistent_endpoint_update_state(persistent, NULL, 0);
		}

		ao2_link_flags(persistent_endpoints, persistent, OBJ_NOLOCK);
	}

	ao2_ref(persistent->endpoint, +1);
	return persistent->endpoint;
}

/*! \brief Callback function for when an object is finalized */
static int sip_endpoint_apply_handler(const struct ast_sorcery *sorcery, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	if (!(endpoint->persistent = persistent_endpoint_find_or_create(endpoint))) {
		return -1;
	}

	return 0;
}

int ast_res_sip_initialize_configuration(void)
{
	if (ast_cli_register_multiple(cli_commands, ARRAY_LEN(cli_commands))) {
		return -1;
	}

	if (!(persistent_endpoints = ao2_container_alloc(PERSISTENT_BUCKETS, persistent_endpoint_hash, persistent_endpoint_cmp))) {
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

	if (ast_sorcery_object_register(sip_sorcery, "endpoint", ast_sip_endpoint_alloc, NULL, sip_endpoint_apply_handler)) {
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
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "connected_line_method", "invite", connected_line_method_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "direct_media_glare_mitigation", "none", direct_media_glare_mitigation_handler, NULL, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "disable_direct_media_on_nat", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, disable_direct_media_on_nat));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "callerid", "", caller_id_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "callerid_privacy", "", caller_id_privacy_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "callerid_tag", "", caller_id_tag_handler, NULL, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "trust_id_inbound", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, trust_id_inbound));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "trust_id_outbound", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, trust_id_outbound));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "send_pai", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, send_pai));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "send_rpid", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, send_rpid));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "send_diversion", "yes", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, send_diversion));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "mailboxes", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, mailboxes));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "aggregate_mwi", "yes", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, aggregate_mwi));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "media_encryption", "no", media_encryption_handler, NULL, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "use_avpf", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, use_avpf));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "one_touch_recording", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, one_touch_recording));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "inband_progress", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, inband_progress));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "callgroup", "", group_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "pickupgroup", "", group_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "namedcallgroup", "", named_groups_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "namedpickupgroup", "", named_groups_handler, NULL, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "devicestate_busy_at", "0", OPT_UINT_T, 0, FLDSET(struct ast_sip_endpoint, devicestate_busy_at));

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

	if (ast_sip_initialize_sorcery_qualify(sip_sorcery)) {
		ast_log(LOG_ERROR, "Failed to register SIP qualify support with sorcery\n");
		ast_sorcery_unref(sip_sorcery);
		sip_sorcery = NULL;
		return -1;
	}

	ast_sorcery_observer_add(sip_sorcery, "contact", &state_contact_observer);

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
	endpoint->named_callgroups = ast_unref_namedgroups(endpoint->named_callgroups);
	endpoint->named_pickupgroups = ast_unref_namedgroups(endpoint->named_pickupgroups);
	ao2_cleanup(endpoint->persistent);
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
