/*
 * sip_cli_commands.c
 *
 *  Created on: Jan 25, 2013
 *      Author: mjordan
 */

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>

#include "asterisk/res_pjsip.h"
#include "include/res_pjsip_private.h"
#include "asterisk/res_pjsip_cli.h"
#include "asterisk/acl.h"
#include "asterisk/manager.h"
#include "asterisk/astobj2.h"
#include "asterisk/utils.h"
#include "asterisk/sorcery.h"
#include "asterisk/callerid.h"

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

	ast_devstate_changed(AST_DEVICE_UNKNOWN, AST_DEVSTATE_CACHABLE, "PJSIP/%s", ast_endpoint_get_resource(persistent->endpoint));

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
static const struct ast_sorcery_observer state_contact_observer = {
	.created = persistent_endpoint_contact_observer,
	.deleted = persistent_endpoint_contact_observer,
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

static int dtmf_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;

	switch (endpoint->dtmf) {
	case AST_SIP_DTMF_RFC_4733 :
		*buf = "rfc4733"; break;
	case AST_SIP_DTMF_INBAND :
		*buf = "inband"; break;
	case AST_SIP_DTMF_INFO :
		*buf = "info"; break;
	default:
		*buf = "none";
	}

	*buf = ast_strdup(*buf);
	return 0;
}

static int prack_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	if (ast_true(var->value)) {
		endpoint->extensions.flags |= PJSIP_INV_SUPPORT_100REL;
	} else if (ast_false(var->value)) {
		endpoint->extensions.flags &= PJSIP_INV_SUPPORT_100REL;
	} else if (!strcasecmp(var->value, "required")) {
		endpoint->extensions.flags |= PJSIP_INV_REQUIRE_100REL;
	} else {
		return -1;
	}

	return 0;
}

static int prack_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;

	if (endpoint->extensions.flags & PJSIP_INV_REQUIRE_100REL) {
		*buf = "required";
	} else if (endpoint->extensions.flags & PJSIP_INV_SUPPORT_100REL) {
		*buf = "yes";
	} else {
		*buf = "no";
	}

	*buf = ast_strdup(*buf);
	return 0;
}

static int timers_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	if (ast_true(var->value)) {
		endpoint->extensions.flags |= PJSIP_INV_SUPPORT_TIMER;
	} else if (ast_false(var->value)) {
		endpoint->extensions.flags &= PJSIP_INV_SUPPORT_TIMER;
	} else if (!strcasecmp(var->value, "required")) {
		endpoint->extensions.flags |= PJSIP_INV_REQUIRE_TIMER;
	} else if (!strcasecmp(var->value, "always")) {
		endpoint->extensions.flags |= PJSIP_INV_ALWAYS_USE_TIMER;
	} else {
		return -1;
	}

	return 0;
}

static int timers_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;

	if (endpoint->extensions.flags & PJSIP_INV_ALWAYS_USE_TIMER) {
		*buf = "always";
	} else if (endpoint->extensions.flags & PJSIP_INV_REQUIRE_TIMER) {
		*buf = "required";
	} else if (endpoint->extensions.flags & PJSIP_INV_SUPPORT_TIMER) {
		*buf = "yes";
	} else {
		*buf = "no";
	}

	*buf = ast_strdup(*buf);
	return 0;
}

void ast_sip_auth_vector_destroy(struct ast_sip_auth_vector *auths)
{
	int i;
	size_t size;

	if (!auths) {
		return;
	}

	size = AST_VECTOR_SIZE(auths);

	for (i = 0; i < size; ++i) {
		const char *name = AST_VECTOR_REMOVE_UNORDERED(auths, 0);
		ast_free((char *) name);
	}
	AST_VECTOR_FREE(auths);
}

int ast_sip_auth_vector_init(struct ast_sip_auth_vector *auths, const char *value)
{
	char *auth_names = ast_strdupa(value);
	char *val;

	ast_assert(auths != NULL);

	if (AST_VECTOR_SIZE(auths)) {
		ast_sip_auth_vector_destroy(auths);
	}
	if (AST_VECTOR_INIT(auths, 1)) {
		return -1;
	}

	while ((val = strsep(&auth_names, ","))) {
		val = ast_strdup(val);
		if (!val) {
			goto failure;
		}
		if (AST_VECTOR_APPEND(auths, val)) {
			goto failure;
		}
	}
	return 0;

failure:
	ast_sip_auth_vector_destroy(auths);
	return -1;
}

static int inbound_auth_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	return ast_sip_auth_vector_init(&endpoint->inbound_auths, var->value);
}

static int outbound_auth_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	return ast_sip_auth_vector_init(&endpoint->outbound_auths, var->value);
}

int ast_sip_auths_to_str(const struct ast_sip_auth_vector *auths, char **buf)
{
	if (!auths || !AST_VECTOR_SIZE(auths)) {
		return 0;
	}

	if (!(*buf = ast_calloc(MAX_OBJECT_FIELD, sizeof(char)))) {
		return -1;
	}

	/* I feel like accessing the vector's elem array directly is cheating...*/
	ast_join_delim(*buf, MAX_OBJECT_FIELD, auths->elems, AST_VECTOR_SIZE(auths), ',');
	return 0;
}

static int inbound_auths_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;
	return ast_sip_auths_to_str(&endpoint->inbound_auths, buf);
}

static int outbound_auths_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;
	return ast_sip_auths_to_str(&endpoint->outbound_auths, buf);
}

static int ident_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;
	char *idents = ast_strdupa(var->value);
	char *val;

	while ((val = strsep(&idents, ","))) {
		if (!strcasecmp(val, "username")) {
			endpoint->ident_method |= AST_SIP_ENDPOINT_IDENTIFY_BY_USERNAME;
		} else {
			ast_log(LOG_ERROR, "Unrecognized identification method %s specified for endpoint %s\n",
					val, ast_sorcery_object_get_id(endpoint));
			return -1;
		}
	}
	return 0;
}

static int ident_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;
	switch (endpoint->ident_method) {
	case AST_SIP_ENDPOINT_IDENTIFY_BY_USERNAME :
		*buf = "username"; break;
	default:
		return 0;
	}

	*buf = ast_strdup(*buf);
	return 0;
}

static int redirect_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	if (!strcasecmp(var->value, "user")) {
		endpoint->redirect_method = AST_SIP_REDIRECT_USER;
	} else if (!strcasecmp(var->value, "uri_core")) {
		endpoint->redirect_method = AST_SIP_REDIRECT_URI_CORE;
	} else if (!strcasecmp(var->value, "uri_pjsip")) {
		endpoint->redirect_method = AST_SIP_REDIRECT_URI_PJSIP;
	} else {
		ast_log(LOG_ERROR, "Unrecognized redirect method %s specified for endpoint %s\n",
			var->value, ast_sorcery_object_get_id(endpoint));
		return -1;
	}

	return 0;
}

static int direct_media_method_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	if (!strcasecmp(var->value, "invite") || !strcasecmp(var->value, "reinvite")) {
		endpoint->media.direct_media.method = AST_SIP_SESSION_REFRESH_METHOD_INVITE;
	} else if (!strcasecmp(var->value, "update")) {
		endpoint->media.direct_media.method = AST_SIP_SESSION_REFRESH_METHOD_UPDATE;
	} else {
		ast_log(LOG_NOTICE, "Unrecognized option value %s for %s on endpoint %s\n",
				var->value, var->name, ast_sorcery_object_get_id(endpoint));
		return -1;
	}
	return 0;
}

static const char *id_configuration_refresh_methods[] = {
	[AST_SIP_SESSION_REFRESH_METHOD_INVITE] = "invite",
	[AST_SIP_SESSION_REFRESH_METHOD_UPDATE] = "update"
};

static int direct_media_method_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;
	if (ARRAY_IN_BOUNDS(endpoint->id.refresh_method, id_configuration_refresh_methods)) {
		*buf = ast_strdup(id_configuration_refresh_methods[endpoint->id.refresh_method]);
	}
	return 0;
}

static int connected_line_method_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	if (!strcasecmp(var->value, "invite") || !strcasecmp(var->value, "reinvite")) {
		endpoint->id.refresh_method = AST_SIP_SESSION_REFRESH_METHOD_INVITE;
	} else if (!strcasecmp(var->value, "update")) {
		endpoint->id.refresh_method = AST_SIP_SESSION_REFRESH_METHOD_UPDATE;
	} else {
		ast_log(LOG_NOTICE, "Unrecognized option value %s for %s on endpoint %s\n",
				var->value, var->name, ast_sorcery_object_get_id(endpoint));
		return -1;
	}
	return 0;
}

static int connected_line_method_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;
	*buf = ast_strdup(id_configuration_refresh_methods[endpoint->id.refresh_method]);
	return 0;
}

static int direct_media_glare_mitigation_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	if (!strcasecmp(var->value, "none")) {
		endpoint->media.direct_media.glare_mitigation = AST_SIP_DIRECT_MEDIA_GLARE_MITIGATION_NONE;
	} else if (!strcasecmp(var->value, "outgoing")) {
		endpoint->media.direct_media.glare_mitigation = AST_SIP_DIRECT_MEDIA_GLARE_MITIGATION_OUTGOING;
	} else if (!strcasecmp(var->value, "incoming")) {
		endpoint->media.direct_media.glare_mitigation = AST_SIP_DIRECT_MEDIA_GLARE_MITIGATION_INCOMING;
	} else {
		ast_log(LOG_NOTICE, "Unrecognized option value %s for %s on endpoint %s\n",
				var->value, var->name, ast_sorcery_object_get_id(endpoint));
		return -1;
	}

	return 0;
}

static const char *direct_media_glare_mitigation_map[] = {
	[AST_SIP_DIRECT_MEDIA_GLARE_MITIGATION_NONE] = "none",
	[AST_SIP_DIRECT_MEDIA_GLARE_MITIGATION_OUTGOING] = "outgoing",
	[AST_SIP_DIRECT_MEDIA_GLARE_MITIGATION_INCOMING] = "incoming"
};

static int direct_media_glare_mitigation_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;
	if (ARRAY_IN_BOUNDS(endpoint->media.direct_media.glare_mitigation, direct_media_glare_mitigation_map)) {
		*buf = ast_strdup(direct_media_glare_mitigation_map[endpoint->media.direct_media.glare_mitigation]);
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
		endpoint->id.self.name.str = ast_strdup(cid_name);
		if (!endpoint->id.self.name.str) {
			return -1;
		}
		endpoint->id.self.name.valid = 1;
	}
	if (!ast_strlen_zero(cid_num)) {
		endpoint->id.self.number.str = ast_strdup(cid_num);
		if (!endpoint->id.self.number.str) {
			return -1;
		}
		endpoint->id.self.number.valid = 1;
	}
	return 0;
}

static int caller_id_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;
	const char *name = S_COR(endpoint->id.self.name.valid,
				 endpoint->id.self.name.str, NULL);
	const char *number = S_COR(endpoint->id.self.number.valid,
				   endpoint->id.self.number.str, NULL);

	/* make sure size is at least 10 - that should cover the "<unknown>"
	   case as well as any additional formatting characters added in
	   the name and/or number case. */
	int size = 10;
	size += name ? strlen(name) : 0;
	size += number ? strlen(number) : 0;

	if (!(*buf = ast_calloc(size + 1, sizeof(char)))) {
		return -1;
	}

	ast_callerid_merge(*buf, size + 1, name, number, NULL);
	return 0;
}

static int caller_id_privacy_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;
	int callingpres = ast_parse_caller_presentation(var->value);
	if (callingpres == -1 && sscanf(var->value, "%d", &callingpres) != 1) {
		return -1;
	}
	endpoint->id.self.number.presentation = callingpres;
	endpoint->id.self.name.presentation = callingpres;
	return 0;
}

static int caller_id_privacy_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;
	const char *presentation = ast_named_caller_presentation(
		endpoint->id.self.name.presentation);

	*buf = ast_strdup(presentation);
	return 0;
}

static int caller_id_tag_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;
	endpoint->id.self.tag = ast_strdup(var->value);
	return endpoint->id.self.tag ? 0 : -1;
}

static int caller_id_tag_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;
	*buf = ast_strdup(endpoint->id.self.tag);
	return 0;
}

static int media_encryption_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	if (!strcasecmp("no", var->value)) {
		endpoint->media.rtp.encryption = AST_SIP_MEDIA_ENCRYPT_NONE;
	} else if (!strcasecmp("sdes", var->value)) {
		endpoint->media.rtp.encryption = AST_SIP_MEDIA_ENCRYPT_SDES;
	} else if (!strcasecmp("dtls", var->value)) {
		endpoint->media.rtp.encryption = AST_SIP_MEDIA_ENCRYPT_DTLS;
		ast_rtp_dtls_cfg_parse(&endpoint->media.rtp.dtls_cfg, "dtlsenable", "yes");
	} else {
		return -1;
	}

	return 0;
}

static const char *media_encryption_map[] = {
	[AST_SIP_MEDIA_TRANSPORT_INVALID] = "invalid",
	[AST_SIP_MEDIA_ENCRYPT_NONE] = "none",
	[AST_SIP_MEDIA_ENCRYPT_SDES] = "sdes",
	[AST_SIP_MEDIA_ENCRYPT_DTLS] = "dtls",
};

static int media_encryption_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;
	if (ARRAY_IN_BOUNDS(endpoint->media.rtp.encryption, media_encryption_map)) {
		*buf = ast_strdup(media_encryption_map[
					  endpoint->media.rtp.encryption]);
	}
	return 0;
}

static int group_handler(const struct aco_option *opt,
			 struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	if (!strncmp(var->name, "call_group", 10)) {
		if (!(endpoint->pickup.callgroup = ast_get_group(var->value))) {
			return -1;
		}
	} else if (!strncmp(var->name, "pickup_group", 12)) {
		if (!(endpoint->pickup.pickupgroup = ast_get_group(var->value))) {
			return -1;
		}
	} else {
		return -1;
	}

	return 0;
}

static int callgroup_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;

	if (!(*buf = ast_calloc(MAX_OBJECT_FIELD, sizeof(char)))) {
		return -1;
	}

	ast_print_group(*buf, MAX_OBJECT_FIELD, endpoint->pickup.callgroup);
	return 0;
}

static int pickupgroup_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;

	if (!(*buf = ast_calloc(MAX_OBJECT_FIELD, sizeof(char)))) {
		return -1;
	}

	ast_print_group(*buf, MAX_OBJECT_FIELD, endpoint->pickup.pickupgroup);
	return 0;
}

static int named_groups_handler(const struct aco_option *opt,
				struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	if (!strncmp(var->name, "named_call_group", 16)) {
		if (!(endpoint->pickup.named_callgroups =
		      ast_get_namedgroups(var->value))) {
			return -1;
		}
	} else if (!strncmp(var->name, "named_pickup_group", 18)) {
		if (!(endpoint->pickup.named_pickupgroups =
		      ast_get_namedgroups(var->value))) {
			return -1;
		}
	} else {
		return -1;
	}

	return 0;
}

static int named_callgroups_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;
	RAII_VAR(struct ast_str *, str, ast_str_create(MAX_OBJECT_FIELD), ast_free);

	ast_print_namedgroups(&str, endpoint->pickup.named_callgroups);
	*buf = ast_strdup(ast_str_buffer(str));
	return 0;
}

static int named_pickupgroups_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;
	RAII_VAR(struct ast_str *, str, ast_str_create(MAX_OBJECT_FIELD), ast_free);

	ast_print_namedgroups(&str, endpoint->pickup.named_pickupgroups);
	*buf = ast_strdup(ast_str_buffer(str));
	return 0;
}

static int dtls_handler(const struct aco_option *opt,
			 struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;
	char *name = ast_strdupa(var->name);
	char *front, *buf = name;

	/* strip out underscores in the name */
	front = strtok(buf, "_");
	while (front) {
		int size = strlen(front);
		ast_copy_string(buf, front, size + 1);
		buf += size;
		front = strtok(NULL, "_");
	}

	return ast_rtp_dtls_cfg_parse(&endpoint->media.rtp.dtls_cfg, name, var->value);
}

static int dtlsverify_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;
	*buf = ast_strdup(AST_YESNO(endpoint->media.rtp.dtls_cfg.verify));
	return 0;
}

static int dtlsrekey_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;

	return ast_asprintf(
		buf, "%d", endpoint->media.rtp.dtls_cfg.rekey) >=0 ? 0 : -1;
}

static int dtlscertfile_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;
	*buf = ast_strdup(endpoint->media.rtp.dtls_cfg.certfile);
	return 0;
}

static int dtlsprivatekey_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;
	*buf = ast_strdup(endpoint->media.rtp.dtls_cfg.pvtfile);
	return 0;
}

static int dtlscipher_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;
	*buf = ast_strdup(endpoint->media.rtp.dtls_cfg.cipher);
	return 0;
}

static int dtlscafile_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;
	*buf = ast_strdup(endpoint->media.rtp.dtls_cfg.cafile);
	return 0;
}

static int dtlscapath_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;
	*buf = ast_strdup(endpoint->media.rtp.dtls_cfg.capath);
	return 0;
}

static const char *ast_rtp_dtls_setup_map[] = {
	[AST_RTP_DTLS_SETUP_ACTIVE] = "active",
	[AST_RTP_DTLS_SETUP_PASSIVE] = "passive",
	[AST_RTP_DTLS_SETUP_ACTPASS] = "actpass",
	[AST_RTP_DTLS_SETUP_HOLDCONN] = "holdconn",
};

static int dtlssetup_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;
	if (ARRAY_IN_BOUNDS(endpoint->media.rtp.dtls_cfg.default_setup, ast_rtp_dtls_setup_map)) {
		*buf = ast_strdup(ast_rtp_dtls_setup_map[endpoint->media.rtp.dtls_cfg.default_setup]);
	}
	return 0;
}

static int t38udptl_ec_handler(const struct aco_option *opt,
	struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	if (!strcmp(var->value, "none")) {
		endpoint->media.t38.error_correction = UDPTL_ERROR_CORRECTION_NONE;
	} else if (!strcmp(var->value, "fec")) {
		endpoint->media.t38.error_correction = UDPTL_ERROR_CORRECTION_FEC;
	} else if (!strcmp(var->value, "redundancy")) {
		endpoint->media.t38.error_correction = UDPTL_ERROR_CORRECTION_REDUNDANCY;
	} else {
		return -1;
	}

	return 0;
}

static const char *ast_t38_ec_modes_map[] = {
	[UDPTL_ERROR_CORRECTION_NONE] = "none",
	[UDPTL_ERROR_CORRECTION_FEC] = "fec",
	[UDPTL_ERROR_CORRECTION_REDUNDANCY] = "redundancy"
};

static int t38udptl_ec_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;
	if (ARRAY_IN_BOUNDS(endpoint->media.t38.error_correction, ast_t38_ec_modes_map)) {
		*buf = ast_strdup(ast_t38_ec_modes_map[
					  endpoint->media.t38.error_correction]);
	}
	return 0;
}

static int set_var_handler(const struct aco_option *opt,
	struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;
	struct ast_variable *new_var;
	char *name = ast_strdupa(var->value), *val = strchr(name, '=');

	if (!val) {
		return -1;
	}

	*val++ = '\0';
	if (!(new_var = ast_variable_new(name, val, ""))) {
		return -1;
	}

	new_var->next = endpoint->channel_vars;
	endpoint->channel_vars = new_var;

	return 0;
}

static int set_var_to_str(const void *obj, const intptr_t *args, char **buf)
{
	struct ast_str *str = ast_str_create(MAX_OBJECT_FIELD);
	const struct ast_sip_endpoint *endpoint = obj;
	struct ast_variable *var;

	for (var = endpoint->channel_vars; var; var = var->next) {
		ast_str_append(&str, 0, "%s=%s,", var->name, var->value);
	}

	*buf = ast_strdup(ast_str_truncate(str, -1));
	ast_free(str);
	return 0;
}

static void *sip_nat_hook_alloc(const char *name)
{
	return ast_sorcery_generic_alloc(sizeof(struct ast_sip_nat_hook), NULL);
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

		if (!(persistent->endpoint = ast_endpoint_create("PJSIP", ast_sorcery_object_get_id(endpoint)))) {
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

/*! \brief Helper function which validates an outbound proxy */
static int outbound_proxy_validate(void *data)
{
	const char *proxy = data;
	pj_pool_t *pool;
	pj_str_t tmp;
	static const pj_str_t ROUTE_HNAME = { "Route", 5 };

	pool = pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(), "Outbound Proxy Validation", 256, 256);
	if (!pool) {
		return -1;
	}

	pj_strdup2_with_null(pool, &tmp, proxy);
	if (!pjsip_parse_hdr(pool, &ROUTE_HNAME, tmp.ptr, tmp.slen, NULL)) {
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		return -1;
	}

	pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
	return 0;
}

/*! \brief Callback function for when an object is finalized */
static int sip_endpoint_apply_handler(const struct ast_sorcery *sorcery, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	if (!(endpoint->persistent = persistent_endpoint_find_or_create(endpoint))) {
		return -1;
	}

	if (!ast_strlen_zero(endpoint->outbound_proxy) &&
		ast_sip_push_task_synchronous(NULL, outbound_proxy_validate, (char*)endpoint->outbound_proxy)) {
		ast_log(LOG_ERROR, "Invalid outbound proxy '%s' specified on endpoint '%s'\n",
			endpoint->outbound_proxy, ast_sorcery_object_get_id(endpoint));
		return -1;
	}

	return 0;
}

const char *ast_sip_get_device_state(const struct ast_sip_endpoint *endpoint)
{
	char device[MAX_OBJECT_FIELD];

	snprintf(device, MAX_OBJECT_FIELD, "PJSIP/%s", ast_sorcery_object_get_id(endpoint));
	return ast_devstate2str(ast_device_state(device));
}

struct ast_endpoint_snapshot *ast_sip_get_endpoint_snapshot(
	const struct ast_sip_endpoint *endpoint)
{
	return ast_endpoint_latest_snapshot(
		ast_endpoint_get_tech(endpoint->persistent),
		ast_endpoint_get_resource(endpoint->persistent));
}

int ast_sip_for_each_channel_snapshot(
	const struct ast_endpoint_snapshot *endpoint_snapshot,
	ao2_callback_fn on_channel_snapshot, void *arg)
{
	int num, num_channels = endpoint_snapshot->num_channels;

	if (!on_channel_snapshot || !num_channels) {
		return 0;
	}

	for (num = 0; num < num_channels; ++num) {
		RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL, ao2_cleanup);
		int res;

		snapshot = ast_channel_snapshot_get_latest(endpoint_snapshot->channel_ids[num]);
		if (!snapshot) {
			continue;
		}

		res = on_channel_snapshot(snapshot, arg, 0);
		if (res) {
			return -1;
		}
	}
	return 0;
}

int ast_sip_for_each_channel(
	const struct ast_sip_endpoint *endpoint,
	ao2_callback_fn on_channel_snapshot, void *arg)
{
	RAII_VAR(struct ast_endpoint_snapshot *, endpoint_snapshot, ast_sip_get_endpoint_snapshot(endpoint), ao2_cleanup);
	return ast_sip_for_each_channel_snapshot(endpoint_snapshot, on_channel_snapshot, arg);
}

static int active_channels_to_str_cb(void *object, void *arg, int flags)
{
	const struct ast_channel_snapshot *snapshot = object;
	struct ast_str **buf = arg;
	ast_str_append(buf, 0, "%s,", snapshot->name);
	return 0;
}

static void active_channels_to_str(const struct ast_sip_endpoint *endpoint,
				   struct ast_str **str)
{

	RAII_VAR(struct ast_endpoint_snapshot *, endpoint_snapshot,
		 ast_sip_get_endpoint_snapshot(endpoint), ao2_cleanup);

	if (endpoint_snapshot) {
		return;
	}

	ast_sip_for_each_channel_snapshot(endpoint_snapshot,
					  active_channels_to_str_cb, str);
	ast_str_truncate(*str, -1);
}

#define AMI_DEFAULT_STR_SIZE 512

struct ast_str *ast_sip_create_ami_event(const char *event, struct ast_sip_ami *ami)
{
	struct ast_str *buf = ast_str_create(AMI_DEFAULT_STR_SIZE);

	if (!(buf)) {
		astman_send_error_va(ami->s, ami->m, "Unable create event "
				     "for %s\n", event);
		return NULL;
	}

	ast_str_set(&buf, 0, "Event: %s\r\n", event);
	return buf;
}

static void sip_sorcery_object_ami_set_type_name(const void *obj, struct ast_str **buf)
{
	ast_str_append(buf, 0, "ObjectType: %s\r\n",
		       ast_sorcery_object_get_type(obj));
	ast_str_append(buf, 0, "ObjectName: %s\r\n",
		       ast_sorcery_object_get_id(obj));
}

int ast_sip_sorcery_object_to_ami(const void *obj, struct ast_str **buf)
{
	RAII_VAR(struct ast_variable *, objset, ast_sorcery_objectset_create(
			 ast_sip_get_sorcery(), obj), ast_variables_destroy);
	struct ast_variable *i;

	if (!objset) {
		return -1;
	}

	sip_sorcery_object_ami_set_type_name(obj, buf);

	for (i = objset; i; i = i->next) {
		RAII_VAR(char *, camel, ast_to_camel_case(i->name), ast_free);
		ast_str_append(buf, 0, "%s: %s\r\n", camel, i->value);
	}

	return 0;
}

static int sip_endpoints_aors_ami(void *obj, void *arg, int flags)
{
	struct ast_sip_aor *aor = obj;
	struct ast_str **buf = arg;

	ast_str_append(buf, 0, "Contacts: ");
	ast_sip_for_each_contact(aor, ast_sip_contact_to_str, arg);
	ast_str_append(buf, 0, "\r\n");

	return 0;
}

static int sip_endpoint_to_ami(const struct ast_sip_endpoint *endpoint,
			       struct ast_str **buf)
{
	if (ast_sip_sorcery_object_to_ami(endpoint, buf)) {
		return -1;
	}

	ast_str_append(buf, 0, "DeviceState: %s\r\n",
		       ast_sip_get_device_state(endpoint));

	ast_str_append(buf, 0, "ActiveChannels: ");
	active_channels_to_str(endpoint, buf);
	ast_str_append(buf, 0, "\r\n");

	return 0;
}

static int format_ami_endpoint(const struct ast_sip_endpoint *endpoint,
			       struct ast_sip_ami *ami)
{
	RAII_VAR(struct ast_str *, buf,
		 ast_sip_create_ami_event("EndpointDetail", ami), ast_free);

	if (!buf) {
		return -1;
	}

	sip_endpoint_to_ami(endpoint, &buf);
	astman_append(ami->s, "%s\r\n", ast_str_buffer(buf));
	return 0;
}

#define AMI_SHOW_ENDPOINTS "PJSIPShowEndpoints"
#define AMI_SHOW_ENDPOINT "PJSIPShowEndpoint"

static int ami_show_endpoint(struct mansession *s, const struct message *m)
{
	struct ast_sip_ami ami = { .s = s, .m = m };
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	const char *endpoint_name = astman_get_header(m, "Endpoint");
	int count = 0;

	if (ast_strlen_zero(endpoint_name)) {
		astman_send_error_va(s, m, "%s requires an endpoint name\n",
			AMI_SHOW_ENDPOINT);
		return 0;
	}

	if (!strncasecmp(endpoint_name, "pjsip/", 6)) {
		endpoint_name += 6;
	}

	if (!(endpoint = ast_sorcery_retrieve_by_id(
		      ast_sip_get_sorcery(), "endpoint", endpoint_name))) {
		astman_send_error_va(s, m, "Unable to retrieve endpoint %s\n",
			endpoint_name);
		return -1;
	}

	astman_send_listack(s, m, "Following are Events for each object "
			    "associated with the the Endpoint", "start");

	/* the endpoint detail needs to always come first so apply as such */
	if (format_ami_endpoint(endpoint, &ami) ||
	    ast_sip_format_endpoint_ami(endpoint, &ami, &count)) {
		astman_send_error_va(s, m, "Unable to format endpoint %s\n",
			endpoint_name);
	}

	astman_append(s,
		      "Event: EndpointDetailComplete\r\n"
		      "EventList: Complete\r\n"
		      "ListItems: %d\r\n\r\n", count + 1);
	return 0;
}

static int format_str_append_auth(const struct ast_sip_auth_vector *auths,
				  struct ast_str **buf)
{
	char *str = NULL;
	if (ast_sip_auths_to_str(auths, &str)) {
		return -1;
	}
	ast_str_append(buf, 0, "%s", str ? str : "");
	ast_free(str);
	return 0;
}

static int format_ami_endpoints(void *obj, void *arg, int flags)
{

	struct ast_sip_endpoint *endpoint = obj;
	struct ast_sip_ami *ami = arg;
	RAII_VAR(struct ast_str *, buf,
		 ast_sip_create_ami_event("EndpointList", ami), ast_free);

	if (!buf) {
		return -1;
	}

	sip_sorcery_object_ami_set_type_name(endpoint, &buf);
	ast_str_append(&buf, 0, "Transport: %s\r\n",
		       endpoint->transport);
	ast_str_append(&buf, 0, "Aor: %s\r\n",
		       endpoint->aors);

	ast_str_append(&buf, 0, "Auths: ");
	format_str_append_auth(&endpoint->inbound_auths, &buf);
	ast_str_append(&buf, 0, "\r\n");

	ast_str_append(&buf, 0, "OutboundAuths: ");
	format_str_append_auth(&endpoint->outbound_auths, &buf);
	ast_str_append(&buf, 0, "\r\n");

	ast_sip_for_each_aor(endpoint->aors,
			     sip_endpoints_aors_ami, &buf);

	ast_str_append(&buf, 0, "DeviceState: %s\r\n",
		       ast_sip_get_device_state(endpoint));

	ast_str_append(&buf, 0, "ActiveChannels: ");
	active_channels_to_str(endpoint, &buf);
	ast_str_append(&buf, 0, "\r\n");

	astman_append(ami->s, "%s\r\n", ast_str_buffer(buf));
	return 0;
}

static int ami_show_endpoints(struct mansession *s, const struct message *m)
{
	struct ast_sip_ami ami = { .s = s, .m = m };
	RAII_VAR(struct ao2_container *, endpoints, NULL, ao2_cleanup);
	int num;

	endpoints = ast_sip_get_endpoints();
	if (!endpoints) {
		return -1;
	}

	if (!(num = ao2_container_count(endpoints))) {
		astman_send_error(s, m, "No endpoints found\n");
		return 0;
	}

	astman_send_listack(s, m, "A listing of Endpoints follows, "
			    "presented as EndpointList events", "start");

	ao2_callback(endpoints, OBJ_NODATA, format_ami_endpoints, &ami);

	astman_append(s,
		      "Event: EndpointListComplete\r\n"
		      "EventList: Complete\r\n"
		      "ListItems: %d\r\n\r\n", num);
	return 0;
}

static int populate_channel_container(void *obj, void *arg, int flags) {
	struct ast_channel_snapshot *snapshot = obj;
	struct ao2_container *container = arg;
	ao2_link_flags(container, snapshot, OBJ_NOLOCK);
	return 0;
}

static int gather_endpoint_channels(void *obj, void *arg, int flags) {
	struct ast_sip_endpoint *endpoint = obj;
	struct ao2_container *channels = arg;
	ast_sip_for_each_channel(endpoint, populate_channel_container, channels);
	return 0;
}

static struct ao2_container *cli_get_channel_container(struct ast_sorcery *sip_sorcery)
{
	RAII_VAR(struct ao2_container *, parent_container, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, s_parent_container, NULL, ao2_cleanup);
	struct ao2_container *child_container;

	parent_container = ast_sorcery_retrieve_by_fields(sip_sorcery, "endpoint",
				AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	if (!parent_container) {
		return NULL;
	}

	s_parent_container = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0, &ast_sorcery_object_id_compare, NULL);
	if (!s_parent_container) {
		return NULL;
	}

	if (ao2_container_dup(s_parent_container, parent_container, OBJ_ORDER_ASCENDING)) {
		return NULL;
	}

	child_container = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0, NULL, NULL);
	if (!child_container) {
		return NULL;
	}

	ao2_callback(s_parent_container, OBJ_NODATA, gather_endpoint_channels, child_container);

	return child_container;
}

static int cli_print_channel_header(void *obj, void *arg, int flags)
{
	struct ast_sip_cli_context *context = arg;
	int indent = CLI_INDENT_TO_SPACES(context->indent_level);
	int filler = CLI_LAST_TABSTOP - indent - 13;

	if (!context->output_buffer) {
		return -1;
	}
	ast_str_append(&context->output_buffer, 0,
		"%*s:  <ChannelId%*.*s>  <State.....>  <Time(sec)>\n",
		indent, "Channel", filler, filler, CLI_HEADER_FILLER);

	context->indent_level++;
	indent = CLI_INDENT_TO_SPACES(context->indent_level);
	filler = CLI_LAST_TABSTOP - indent - 38;
	ast_str_append(&context->output_buffer, 0,
		"%*s:  <Codec>  Exten: <DialedExten%*.*s>  CLCID: <ConnectedLineCID.......>\n",
		indent, "Codec", filler, filler, CLI_HEADER_FILLER);
	context->indent_level--;
	return 0;
}

static int cli_print_channel_body(void *obj, void *arg, int flags) {
	struct ast_channel_snapshot *snapshot = obj;
	struct ast_sip_cli_context *context = arg;
	struct timeval current_time;
	char *print_name = NULL;
	int print_name_len;
	int indent;
	int flexwidth;

	if (!context->output_buffer) {
		return -1;
	}

	gettimeofday(&current_time, NULL);

	print_name_len = strlen(snapshot->name) + strlen(snapshot->appl) + 2;
	if (!(print_name = alloca(print_name_len))) {
		return -1;
	}

	snprintf(print_name, print_name_len, "%s/%s", snapshot->name, snapshot->appl);

	indent = CLI_INDENT_TO_SPACES(context->indent_level);
	flexwidth = CLI_LAST_TABSTOP - indent;

	ast_str_append(&context->output_buffer, 0, "%*s: %-*.*s %-12.12s  %11ld\n",
		CLI_INDENT_TO_SPACES(context->indent_level), "Channel",
		flexwidth, flexwidth,
		print_name,
		ast_state2str(snapshot->state),
		current_time.tv_sec - snapshot->creationtime.tv_sec);

	context->indent_level++;
	indent = CLI_INDENT_TO_SPACES(context->indent_level);
	flexwidth = CLI_LAST_TABSTOP - indent - 25;

	ast_str_append(&context->output_buffer, 0, "%*s:  %-7s  Exten: %-*.*s  CLCID: \"%s\" <%s>\n",
		indent, "Codec",
		snapshot->nativeformats,
		flexwidth, flexwidth,
		snapshot->exten,
		snapshot->connected_name,
		snapshot->connected_number
		);
	context->indent_level--;
	return 0;
}

static struct ao2_container *cli_get_endpoint_container(struct ast_sorcery *sip_sorcery)
{
	return ast_sip_get_endpoints();
}

static int cli_print_endpoint_header(void *obj, void *arg, int flags)
{
	struct ast_sip_cli_context *context = arg;
	struct ast_sip_cli_formatter_entry *formatter_entry;

	if (!context->output_buffer) {
		return -1;
	}
	ast_str_append(&context->output_buffer, 0,
			" <Endpoint/CID................................................>  <State.....>  <Channels.>\n");

	if (context->recurse) {
		context->indent_level++;
		formatter_entry = ast_sip_lookup_cli_formatter("auth");
		if (formatter_entry) {
			formatter_entry->print_header(NULL, context, 0);
		}
		formatter_entry = ast_sip_lookup_cli_formatter("aor");
		if (formatter_entry) {
			formatter_entry->print_header(NULL, context, 0);
		}
		formatter_entry = ast_sip_lookup_cli_formatter("identify");
		if (formatter_entry) {
			formatter_entry->print_header(NULL, context, 0);
		}
		formatter_entry = ast_sip_lookup_cli_formatter("channel");
		if (formatter_entry) {
			formatter_entry->print_header(NULL, context, 0);
		}
		context->indent_level--;
	}
	return 0;
}

static int cli_print_endpoint_body(void *obj, void *arg, int flags) {
	struct ast_sip_endpoint *endpoint = obj;
	RAII_VAR(struct ast_endpoint_snapshot *, endpoint_snapshot, ast_sip_get_endpoint_snapshot(endpoint), ao2_cleanup);
	struct ast_sip_cli_context *context = arg;
	const char *id = ast_sorcery_object_get_id(endpoint);
	struct ast_sip_cli_formatter_entry *formatter_entry;
	char *print_name = NULL;
	int print_name_len;
	char *number = S_COR(endpoint->id.self.number.valid,
		endpoint->id.self.number.str, NULL);

	if (!context->output_buffer) {
		return -1;
	}

	context->current_endpoint = endpoint;

	if (number) {
		print_name_len = strlen(id) + strlen(number) + 2;
		if (!(print_name = alloca(print_name_len))) {
			return -1;
		}
		snprintf(print_name, print_name_len, "%s/%s", id, number);
	}

	ast_str_append(&context->output_buffer, 0, " %-62s  %-12.12s  %d of %.0f\n",
		print_name ? print_name : id,
		ast_sip_get_device_state(endpoint),
		endpoint_snapshot->num_channels,
		(double) endpoint->devicestate_busy_at ? endpoint->devicestate_busy_at :
														INFINITY
														);

	if (context->recurse) {
		context->indent_level++;

		formatter_entry = ast_sip_lookup_cli_formatter("auth");
		if (formatter_entry) {
			context->auth_direction = "Out";
			ast_sip_for_each_auth(&endpoint->outbound_auths, formatter_entry->print_body, context);
			context->auth_direction = "In";
			ast_sip_for_each_auth(&endpoint->inbound_auths, formatter_entry->print_body, context);
		}
		formatter_entry = ast_sip_lookup_cli_formatter("aor");
		if (formatter_entry) {
			ast_sip_for_each_aor(endpoint->aors, formatter_entry->print_body, context);
		}
		formatter_entry = ast_sip_lookup_cli_formatter("channel");
		if (formatter_entry) {
			ast_sip_for_each_channel(endpoint, formatter_entry->print_body, context);
		}

		context->indent_level--;
	}

	if (context->show_details || (context->show_details_only_level_0 && context->indent_level == 0)) {
		ast_str_append(&context->output_buffer, 0, "\n");
		ast_sip_cli_print_sorcery_objectset(endpoint, context, 0);
	}

	return 0;
}

static struct ast_sip_cli_formatter_entry cli_channel_formatter = {
	.name = "channel",
	.print_header = cli_print_channel_header,
	.print_body = cli_print_channel_body,
	.get_container = cli_get_channel_container,
};

static struct ast_sip_cli_formatter_entry cli_endpoint_formatter = {
	.name = "endpoint",
	.print_header = cli_print_endpoint_header,
	.print_body = cli_print_endpoint_body,
	.get_container = cli_get_endpoint_container,
};


int ast_res_pjsip_initialize_configuration(const struct ast_module_info *ast_module_info)
{

	if (ast_manager_register_xml(AMI_SHOW_ENDPOINTS, EVENT_FLAG_SYSTEM, ami_show_endpoints) ||
	    ast_manager_register_xml(AMI_SHOW_ENDPOINT, EVENT_FLAG_SYSTEM, ami_show_endpoint)) {
		return -1;
	}

	if (!(persistent_endpoints = ao2_container_alloc(PERSISTENT_BUCKETS, persistent_endpoint_hash, persistent_endpoint_cmp))) {
		return -1;
	}

	if (!(sip_sorcery = ast_sorcery_open())) {
		ast_log(LOG_ERROR, "Failed to open SIP sorcery failed to open\n");
		return -1;
	}

	ast_sorcery_apply_config(sip_sorcery, "res_pjsip");

	ast_sip_initialize_cli(sip_sorcery);
	ast_sip_register_cli_formatter(&cli_channel_formatter);
	ast_sip_register_cli_formatter(&cli_endpoint_formatter);


	if (ast_sip_initialize_sorcery_auth(sip_sorcery)) {
		ast_log(LOG_ERROR, "Failed to register SIP authentication support\n");
		ast_sorcery_unref(sip_sorcery);
		sip_sorcery = NULL;
		return -1;
	}

	ast_sorcery_apply_default(sip_sorcery, "endpoint", "config", "pjsip.conf,criteria=type=endpoint");

	ast_sorcery_apply_default(sip_sorcery, "nat_hook", "memory", NULL);

	if (ast_sorcery_object_register(sip_sorcery, "endpoint", ast_sip_endpoint_alloc, NULL, sip_endpoint_apply_handler)) {
		ast_log(LOG_ERROR, "Failed to register SIP endpoint object with sorcery\n");
		ast_sorcery_unref(sip_sorcery);
		sip_sorcery = NULL;
		return -1;
	}

	ast_sorcery_internal_object_register(sip_sorcery, "nat_hook", sip_nat_hook_alloc, NULL, NULL);

	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "context", "default", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, context));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "disallow", "", OPT_CODEC_T, 0, FLDSET(struct ast_sip_endpoint, media.prefs, media.codecs));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "allow", "", OPT_CODEC_T, 1, FLDSET(struct ast_sip_endpoint, media.prefs, media.codecs));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "dtmf_mode", "rfc4733", dtmf_handler, dtmf_to_str, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "rtp_ipv6", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, media.rtp.ipv6));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "rtp_symmetric", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, media.rtp.symmetric));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "ice_support", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, media.rtp.ice_support));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "use_ptime", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, media.rtp.use_ptime));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "force_rport", "yes", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, nat.force_rport));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "rewrite_contact", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, nat.rewrite_contact));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "transport", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, transport));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "outbound_proxy", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, outbound_proxy));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "moh_suggest", "default", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, mohsuggest));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "100rel", "yes", prack_handler, prack_to_str, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "timers", "yes", timers_handler, timers_to_str, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "timers_min_se", "90", OPT_UINT_T, 0, FLDSET(struct ast_sip_endpoint, extensions.timer.min_se));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "timers_sess_expires", "1800", OPT_UINT_T, 0, FLDSET(struct ast_sip_endpoint, extensions.timer.sess_expires));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "auth", "", inbound_auth_handler, inbound_auths_to_str, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "outbound_auth", "", outbound_auth_handler, outbound_auths_to_str, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "aors", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, aors));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "media_address", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, media.address));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "identify_by", "username", ident_handler, ident_to_str, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "direct_media", "yes", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, media.direct_media.enabled));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "direct_media_method", "invite", direct_media_method_handler, direct_media_method_to_str, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "connected_line_method", "invite", connected_line_method_handler, connected_line_method_to_str, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "direct_media_glare_mitigation", "none", direct_media_glare_mitigation_handler, direct_media_glare_mitigation_to_str, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "disable_direct_media_on_nat", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, media.direct_media.disable_on_nat));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "callerid", "", caller_id_handler, caller_id_to_str, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "callerid_privacy", "", caller_id_privacy_handler, caller_id_privacy_to_str, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "callerid_tag", "", caller_id_tag_handler, caller_id_tag_to_str, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "trust_id_inbound", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, id.trust_inbound));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "trust_id_outbound", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, id.trust_outbound));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "send_pai", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, id.send_pai));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "send_rpid", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, id.send_rpid));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "send_diversion", "yes", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, id.send_diversion));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "mailboxes", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, subscription.mwi.mailboxes));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "aggregate_mwi", "yes", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, subscription.mwi.aggregate));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "media_encryption", "no", media_encryption_handler, media_encryption_to_str, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "use_avpf", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, media.rtp.use_avpf));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "one_touch_recording", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, info.recording.enabled));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "inband_progress", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, inband_progress));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "call_group", "", group_handler, callgroup_to_str, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "pickup_group", "", group_handler, pickupgroup_to_str, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "named_call_group", "", named_groups_handler, named_callgroups_to_str, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "named_pickup_group", "", named_groups_handler, named_pickupgroups_to_str, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "device_state_busy_at", "0", OPT_UINT_T, 0, FLDSET(struct ast_sip_endpoint, devicestate_busy_at));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "t38_udptl", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, media.t38.enabled));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "t38_udptl_ec", "none", t38udptl_ec_handler, t38udptl_ec_to_str, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "t38_udptl_maxdatagram", "0", OPT_UINT_T, 0, FLDSET(struct ast_sip_endpoint, media.t38.maxdatagram));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "fax_detect", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, faxdetect));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "t38_udptl_nat", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, media.t38.nat));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "t38_udptl_ipv6", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, media.t38.ipv6));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "tone_zone", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, zone));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "language", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, language));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "record_on_feature", "automixmon", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, info.recording.onfeature));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "record_off_feature", "automixmon", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, info.recording.offfeature));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "allow_transfer", "yes", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, allowtransfer));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "sdp_owner", "-", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, media.sdpowner));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "sdp_session", "Asterisk", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, media.sdpsession));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "tos_audio", "0", OPT_UINT_T, 0, FLDSET(struct ast_sip_endpoint, media.tos_audio));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "tos_video", "0", OPT_UINT_T, 0, FLDSET(struct ast_sip_endpoint, media.tos_video));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "cos_audio", "0", OPT_UINT_T, 0, FLDSET(struct ast_sip_endpoint, media.cos_audio));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "cos_video", "0", OPT_UINT_T, 0, FLDSET(struct ast_sip_endpoint, media.cos_video));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "allow_subscribe", "yes", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, subscription.allow));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "sub_min_expiry", "0", OPT_UINT_T, 0, FLDSET(struct ast_sip_endpoint, subscription.minexpiry));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "from_user", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, fromuser));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "from_domain", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, fromdomain));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "mwi_from_user", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, subscription.mwi.fromuser));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "rtp_engine", "asterisk", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, media.rtp.engine));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "dtls_verify", "", dtls_handler, dtlsverify_to_str, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "dtls_rekey", "", dtls_handler, dtlsrekey_to_str, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "dtls_cert_file", "", dtls_handler, dtlscertfile_to_str, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "dtls_private_key", "", dtls_handler, dtlsprivatekey_to_str, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "dtls_cipher", "", dtls_handler, dtlscipher_to_str, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "dtls_ca_file", "", dtls_handler, dtlscafile_to_str, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "dtls_ca_path", "", dtls_handler, dtlscapath_to_str, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "dtls_setup", "", dtls_handler, dtlssetup_to_str, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "srtp_tag_32", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, media.rtp.srtp_tag_32));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "redirect_method", "user", redirect_handler, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "set_var", "", set_var_handler, set_var_to_str, 0, 0);

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

	if (ast_sip_initialize_sorcery_global(sip_sorcery)) {
		ast_log(LOG_ERROR, "Failed to register SIP Global support\n");
		ast_sorcery_unref(sip_sorcery);
		sip_sorcery = NULL;
		return -1;
	}

	ast_sorcery_load(sip_sorcery);

	return 0;
}

void ast_res_pjsip_destroy_configuration(void)
{
	ast_manager_unregister(AMI_SHOW_ENDPOINT);
	ast_manager_unregister(AMI_SHOW_ENDPOINTS);
	ast_sorcery_unref(sip_sorcery);
}

/*!
 * \internal
 * \brief Reload configuration within a PJSIP thread
 */
static int reload_configuration_task(void *obj)
{
	if (sip_sorcery) {
		ast_sorcery_reload(sip_sorcery);
	}
	return 0;
}

int ast_res_pjsip_reload_configuration(void)
{
	if (ast_sip_push_task(NULL, reload_configuration_task, NULL)) {
		ast_log(LOG_WARNING, "Failed to reload PJSIP configuration\n");
	}

	return 0;
}

static void subscription_configuration_destroy(struct ast_sip_endpoint_subscription_configuration *subscription)
{
	ast_string_field_free_memory(&subscription->mwi);
}

static void info_configuration_destroy(struct ast_sip_endpoint_info_configuration *info)
{
	ast_string_field_free_memory(&info->recording);
}

static void media_configuration_destroy(struct ast_sip_endpoint_media_configuration *media)
{
	ast_string_field_free_memory(&media->rtp);
	ast_string_field_free_memory(media);
}

static void endpoint_destructor(void* obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	ast_string_field_free_memory(endpoint);

	if (endpoint->media.codecs) {
		ast_format_cap_destroy(endpoint->media.codecs);
	}
	subscription_configuration_destroy(&endpoint->subscription);
	info_configuration_destroy(&endpoint->info);
	media_configuration_destroy(&endpoint->media);
	ast_sip_auth_vector_destroy(&endpoint->inbound_auths);
	ast_sip_auth_vector_destroy(&endpoint->outbound_auths);
	ast_party_id_free(&endpoint->id.self);
	endpoint->pickup.named_callgroups = ast_unref_namedgroups(endpoint->pickup.named_callgroups);
	endpoint->pickup.named_pickupgroups = ast_unref_namedgroups(endpoint->pickup.named_pickupgroups);
	ao2_cleanup(endpoint->persistent);
	ast_variables_destroy(endpoint->channel_vars);
}

static int init_subscription_configuration(struct ast_sip_endpoint_subscription_configuration *subscription)
{
	return ast_string_field_init(&subscription->mwi, 64);
}

static int init_info_configuration(struct ast_sip_endpoint_info_configuration *info)
{
	return ast_string_field_init(&info->recording, 32);
}

static int init_media_configuration(struct ast_sip_endpoint_media_configuration *media)
{
	return ast_string_field_init(media, 64) || ast_string_field_init(&media->rtp, 32);
}

void *ast_sip_endpoint_alloc(const char *name)
{
	struct ast_sip_endpoint *endpoint = ast_sorcery_generic_alloc(sizeof(*endpoint), endpoint_destructor);
	if (!endpoint) {
		return NULL;
	}
	if (ast_string_field_init(endpoint, 64)) {
		ao2_cleanup(endpoint);
		return NULL;
	}
	if (!(endpoint->media.codecs = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_NOLOCK))) {
		ao2_cleanup(endpoint);
		return NULL;
	}
	if (init_subscription_configuration(&endpoint->subscription)) {
		ao2_cleanup(endpoint);
		return NULL;
	}
	if (init_info_configuration(&endpoint->info)) {
		ao2_cleanup(endpoint);
		return NULL;
	}
	if (init_media_configuration(&endpoint->media)) {
		ao2_cleanup(endpoint);
		return NULL;
	}
	ast_party_id_init(&endpoint->id.self);
	return endpoint;
}

struct ao2_container *ast_sip_get_endpoints(void)
{
	struct ao2_container *endpoints;

	endpoints = ast_sorcery_retrieve_by_fields(sip_sorcery, "endpoint", AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);

	return endpoints;
}

struct ast_sip_endpoint *ast_sip_default_outbound_endpoint(void)
{
	RAII_VAR(char *, name, ast_sip_global_default_outbound_endpoint(), ast_free);
	return ast_strlen_zero(name) ? NULL : ast_sorcery_retrieve_by_id(
		sip_sorcery, "endpoint", name);
}

int ast_sip_retrieve_auths(const struct ast_sip_auth_vector *auths, struct ast_sip_auth **out)
{
	int i;

	for (i = 0; i < AST_VECTOR_SIZE(auths); ++i) {
		/* Using AST_VECTOR_GET is safe since the vector is immutable */
		const char *name = AST_VECTOR_GET(auths, i);
		out[i] = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), SIP_SORCERY_AUTH_TYPE, name);
		if (!out[i]) {
			ast_log(LOG_NOTICE, "Couldn't find auth '%s'. Cannot authenticate\n", name);
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
