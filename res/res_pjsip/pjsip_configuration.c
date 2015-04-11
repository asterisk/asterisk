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
#include "asterisk/test.h"

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
	struct ast_endpoint *endpoint = persistent->endpoint;
	char *aor = arg;
	struct ao2_container *contacts;
	struct ast_json *blob;
	struct ao2_iterator i;
	struct ast_sip_contact *contact;
	enum ast_endpoint_state state = AST_ENDPOINT_OFFLINE;

	if (!ast_strlen_zero(aor) && !strstr(persistent->aors, aor)) {
		return 0;
	}

	/* Find all the contacts for this endpoint.  If ANY are available,
	 * mark the endpoint as ONLINE.
	 */
	contacts = ast_sip_location_retrieve_contacts_from_aor_list(persistent->aors);
	if (contacts) {
		i = ao2_iterator_init(contacts, 0);
		while ((contact = ao2_iterator_next(&i))
			&& state == AST_ENDPOINT_OFFLINE) {
			struct ast_sip_contact_status *contact_status;
			const char *contact_id = ast_sorcery_object_get_id(contact);

			contact_status = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(),
				CONTACT_STATUS, contact_id);

			if (contact_status && contact_status->status == AVAILABLE) {
				state = AST_ENDPOINT_ONLINE;
			}
			ao2_cleanup(contact_status);
			ao2_cleanup(contact);
		}
		ao2_iterator_destroy(&i);
		ao2_ref(contacts, -1);
	}

	/* If there was no state change, don't publish anything. */
	if (ast_endpoint_get_state(endpoint) == state) {
		return 0;
	}

	if (state == AST_ENDPOINT_ONLINE) {
		ast_endpoint_set_state(endpoint, AST_ENDPOINT_ONLINE);
		blob = ast_json_pack("{s: s}", "peer_status", "Reachable");
		ast_verb(1, "Endpoint %s is now Reachable\n", ast_endpoint_get_resource(endpoint));
	} else {
		ast_endpoint_set_state(endpoint, AST_ENDPOINT_OFFLINE);
		blob = ast_json_pack("{s: s}", "peer_status", "Unreachable");
		ast_verb(1, "Endpoint %s is now Unreachable\n", ast_endpoint_get_resource(endpoint));
	}

	ast_endpoint_blob_publish(endpoint, ast_endpoint_state_type(), blob);
	ast_json_unref(blob);
	ast_devstate_changed(AST_DEVICE_UNKNOWN, AST_DEVSTATE_CACHABLE, "PJSIP/%s", ast_endpoint_get_resource(endpoint));

	return 0;
}

/*! \brief Function called when stuff relating to a contact happens (created/deleted) */
static void persistent_endpoint_contact_created_observer(const void *object)
{
	char *id = ast_strdupa(ast_sorcery_object_get_id(object)), *aor = NULL;

	aor = strsep(&id, ";@");

	ao2_callback(persistent_endpoints, OBJ_NODATA, persistent_endpoint_update_state, aor);
}

/*! \brief Function called when stuff relating to a contact happens (created/deleted) */
static void persistent_endpoint_contact_deleted_observer(const void *object)
{
	char *id = ast_strdupa(ast_sorcery_object_get_id(object));
	char *aor = NULL;
	char *contact = NULL;

	aor = id;
	/* Dynamic contacts are delimited with ";@" and static ones with "@@" */
	if ((contact = strstr(id, ";@")) || (contact = strstr(id, "@@"))) {
		*contact = '\0';
		contact += 2;
	} else {
		contact = id;
	}

	ast_verb(1, "Contact %s/%s is now Unavailable\n", aor, contact);

	ao2_callback(persistent_endpoints, OBJ_NODATA, persistent_endpoint_update_state, aor);
}

/*! \brief Observer for contacts so state can be updated on respective endpoints */
static const struct ast_sorcery_observer state_contact_observer = {
	.created = persistent_endpoint_contact_created_observer,
	.deleted = persistent_endpoint_contact_deleted_observer,
};

/*! \brief Function called when stuff relating to a contact status happens (updated) */
static void persistent_endpoint_contact_status_observer(const void *object)
{
	const struct ast_sip_contact_status *contact_status = object;
	char *id = ast_strdupa(ast_sorcery_object_get_id(object));
	char *aor = NULL;
	char *contact = NULL;

	/* If rtt_start is set (this is the outgoing OPTIONS) or
	 * there's no status change, ignore.
	 */
	if (contact_status->rtt_start.tv_sec > 0
		|| contact_status->status == contact_status->last_status) {
		return;
	}

	aor = id;
	/* Dynamic contacts are delimited with ";@" and static ones with "@@" */
	if ((contact = strstr(id, ";@")) || (contact = strstr(id, "@@"))) {
		*contact = '\0';
		contact += 2;
	} else {
		contact = id;
	}

	ast_test_suite_event_notify("AOR_CONTACT_UPDATE",
		"Contact: %s\r\n"
			"Status: %s",
		ast_sorcery_object_get_id(contact_status),
		(contact_status->status == AVAILABLE ? "Available" : "Unavailable"));

	ast_verb(1, "Contact %s/%s is now %s\n", aor, contact,
		contact_status->status == AVAILABLE ? "Available" : "Unavailable");

	ao2_callback(persistent_endpoints, OBJ_NODATA, persistent_endpoint_update_state, aor);
}

/*! \brief Observer for contacts so state can be updated on respective endpoints */
static const struct ast_sorcery_observer state_contact_status_observer = {
	.updated = persistent_endpoint_contact_status_observer,
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
        } else if (!strcasecmp(var->value, "auto")) {
                endpoint->dtmf = AST_SIP_DTMF_AUTO;
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
       case AST_SIP_DTMF_AUTO :
                *buf = "auto"; break;
	default:
		*buf = "none";
	}

	*buf = ast_strdup(*buf);
	return 0;
}

static int prack_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;

	/* clear all */
	endpoint->extensions.flags &= ~(PJSIP_INV_SUPPORT_100REL | PJSIP_INV_REQUIRE_100REL);

	if (ast_true(var->value)) {
		endpoint->extensions.flags |= PJSIP_INV_SUPPORT_100REL;
	} else if (!strcasecmp(var->value, "required")) {
		endpoint->extensions.flags |= PJSIP_INV_REQUIRE_100REL;
	} else if (!ast_false(var->value)){
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

	/* clear all */
	endpoint->extensions.flags &= ~(PJSIP_INV_SUPPORT_TIMER | PJSIP_INV_REQUIRE_TIMER
					| PJSIP_INV_ALWAYS_USE_TIMER);

	/* set only the specified flag and let pjsip normalize if needed */
	if (ast_true(var->value)) {
		endpoint->extensions.flags |= PJSIP_INV_SUPPORT_TIMER;
	} else if (!strcasecmp(var->value, "required")) {
		endpoint->extensions.flags |= PJSIP_INV_REQUIRE_TIMER;
	} else if (!strcasecmp(var->value, "always") || !strcasecmp(var->value, "forced")) {
		endpoint->extensions.flags |= PJSIP_INV_ALWAYS_USE_TIMER;
	} else if (!ast_false(var->value)) {
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
		endpoint->pickup.callgroup = ast_get_group(var->value);
	} else if (!strncmp(var->name, "pickup_group", 12)) {
		endpoint->pickup.pickupgroup = ast_get_group(var->value);
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
		if (ast_strlen_zero(var->value)) {
			endpoint->pickup.named_callgroups =
				ast_unref_namedgroups(endpoint->pickup.named_callgroups);
		} else if (!(endpoint->pickup.named_callgroups =
		      ast_get_namedgroups(var->value))) {
			return -1;
		}
	} else if (!strncmp(var->name, "named_pickup_group", 18)) {
		if (ast_strlen_zero(var->value)) {
			endpoint->pickup.named_pickupgroups =
				ast_unref_namedgroups(endpoint->pickup.named_pickupgroups);
		} else if (!(endpoint->pickup.named_pickupgroups =
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
	char *front, *back, *buf = name;

	/* strip out underscores in the name */
	front = strtok_r(buf, "_", &back);
	while (front) {
		int size = strlen(front);
		ast_copy_string(buf, front, size + 1);
		buf += size;
		front = strtok_r(NULL, "_", &back);
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
		buf, "%u", endpoint->media.rtp.dtls_cfg.rekey) >=0 ? 0 : -1;
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

static const char *ast_rtp_dtls_fingerprint_map[] = {
	[AST_RTP_DTLS_HASH_SHA256] = "SHA-256",
	[AST_RTP_DTLS_HASH_SHA1] = "SHA-1",
};

static int dtlsfingerprint_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;
	if (ARRAY_IN_BOUNDS(endpoint->media.rtp.dtls_cfg.hash, ast_rtp_dtls_fingerprint_map)) {
		*buf = ast_strdup(ast_rtp_dtls_fingerprint_map[endpoint->media.rtp.dtls_cfg.hash]);
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

static int tos_handler(const struct aco_option *opt,
	struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;
	unsigned int value;

	if (ast_str2tos(var->value, &value)) {
		ast_log(LOG_ERROR, "Error configuring endpoint '%s' - Could not "
			"interpret '%s' value '%s'\n",
			ast_sorcery_object_get_id(endpoint), var->name, var->value);
		return -1;
	}

	if (!strcmp(var->name, "tos_audio")) {
		endpoint->media.tos_audio = value;
	} else if (!strcmp(var->name, "tos_video")) {
		endpoint->media.tos_video = value;
	} else {
		/* If we reach this point, someone called the tos_handler when they shouldn't have. */
		ast_assert(0);
		return -1;
	}
	return 0;
}

static int tos_audio_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;

	if (ast_asprintf(buf, "%u", endpoint->media.tos_audio) == -1) {
		return -1;
	}
	return 0;
}

static int tos_video_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_endpoint *endpoint = obj;

	if (ast_asprintf(buf, "%u", endpoint->media.tos_video) == -1) {
		return -1;
	}
	return 0;
}

static int set_var_handler(const struct aco_option *opt,
	struct ast_variable *var, void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;
	struct ast_variable *new_var;
	char *name;
	char *val;

	if (ast_strlen_zero(var->value)) {
		return 0;
	}

	name = ast_strdupa(var->value);
	val = strchr(name, '=');

	if (!val) {
		return -1;
	}

	*val++ = '\0';

	if (!(new_var = ast_variable_new(name, val, ""))) {
		return -1;
	}

	ast_variable_list_append(&endpoint->channel_vars, new_var);

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

static int set_var_to_vl(const void *obj, struct ast_variable **fields)
{
	const struct ast_sip_endpoint *endpoint = obj;
	if (endpoint->channel_vars) {
		*fields = ast_variables_dup(endpoint->channel_vars);
	}
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
	} else if (endpoint->extensions.timer.min_se < 90) {
		ast_log(LOG_ERROR, "Session timer minimum expires time must be 90 or greater on endpoint '%s'\n",
			ast_sorcery_object_get_id(endpoint));
		return -1;
	} else if (endpoint->extensions.timer.sess_expires < endpoint->extensions.timer.min_se) {
		ast_log(LOG_ERROR, "Session timer expires must be greater than minimum session expires time on endpoint '%s'\n",
			ast_sorcery_object_get_id(endpoint));
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
	if (!ast_strlen_zero(ami->action_id)) {
		ast_str_append(&buf, 0, "ActionID: %s\r\n", ami->action_id);
	}
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
	RAII_VAR(struct ast_variable *, objset, ast_sorcery_objectset_create2(
			 ast_sip_get_sorcery(), obj, AST_HANDLER_ONLY_STRING), ast_variables_destroy);
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
	struct ast_sip_ami ami = { .s = s, .m = m, .action_id = astman_get_header(m, "ActionID"),
		.count = 0, };
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
		return 0;
	}

	astman_send_listack(s, m, "Following are Events for each object associated with the the Endpoint",
		"start");

	/* the endpoint detail needs to always come first so apply as such */
	if (format_ami_endpoint(endpoint, &ami) ||
	    ast_sip_format_endpoint_ami(endpoint, &ami, &count)) {
		astman_send_error_va(s, m, "Unable to format endpoint %s\n",
			endpoint_name);
	}

	astman_send_list_complete_start(s, m, "EndpointDetailComplete", ami.count + 1);
	astman_send_list_complete_end(s);

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
		return CMP_STOP;
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
	struct ast_sip_ami ami = { .s = s, .m = m, .action_id = astman_get_header(m, "ActionID"), };
	RAII_VAR(struct ao2_container *, endpoints, NULL, ao2_cleanup);
	int num;

	endpoints = ast_sip_get_endpoints();
	if (!endpoints) {
		astman_send_error(s, m, "Could not get endpoints\n");
		return 0;
	}

	if (!(num = ao2_container_count(endpoints))) {
		astman_send_error(s, m, "No endpoints found\n");
		return 0;
	}

	astman_send_listack(s, m, "A listing of Endpoints follows, presented as EndpointList events",
		"start");

	ao2_callback(endpoints, OBJ_NODATA, format_ami_endpoints, &ami);

	astman_send_list_complete_start(s, m, "EndpointListComplete", num);
	astman_send_list_complete_end(s);
	return 0;
}

static struct ao2_container *cli_endpoint_get_container(void)
{
	RAII_VAR(struct ao2_container *, container, NULL, ao2_cleanup);
	struct ao2_container *s_container;

	container = ast_sorcery_retrieve_by_fields(sip_sorcery, "endpoint",
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	if (!container) {
		return NULL;
	}

	s_container = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0,
		(void *)ast_sorcery_object_id_sort, (void *)ast_sorcery_object_id_compare);
	if (!s_container) {
		return NULL;
	}

	if (ao2_container_dup(s_container, container, 0)) {
		ao2_ref(s_container, -1);
		return NULL;
	}

	return s_container;
}

static int cli_channel_populate_container(void *obj, void *arg, int flags)
{
	struct ast_channel_snapshot *snapshot = obj;

	ao2_link(arg, snapshot);

	return 0;
}

static int cli_channel_iterate(void *container, ao2_callback_fn callback, void *args)
{
	const struct ast_sip_endpoint *endpoint = container;

	ast_sip_for_each_channel(endpoint, callback, args);

	return 0;
}

static int cli_channel_sort(const void *obj, const void *arg, int flags)
{
	const struct ast_channel_snapshot *left_obj = obj;
	const struct ast_channel_snapshot *right_obj = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = right_obj->name;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(left_obj->name, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncmp(left_obj->name, right_key, strlen(right_key));
		break;
	default:
		cmp = 0;
		break;
	}

	return cmp;
}

static int cli_channel_compare(void *obj, void *arg, int flags)
{
	const struct ast_channel_snapshot *left_obj = obj;
	const struct ast_channel_snapshot *right_obj = arg;
	const char *right_key = arg;
	int cmp = 0;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = right_obj->name;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		if (strcmp(left_obj->name, right_key) == 0) {
			cmp = CMP_MATCH | CMP_STOP;
		}
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		if (strncmp(left_obj->name, right_key, strlen(right_key)) == 0) {
			cmp = CMP_MATCH;
		}
		break;
	default:
		cmp = 0;
		break;
	}

	return cmp;
}

static int cli_channel_hash(const void *obj, int flags)
{
	const struct ast_channel_snapshot *snapshot = obj;

	if (flags & OBJ_SEARCH_OBJECT) {
		return ast_str_hash(snapshot->name);
	} else if (flags & OBJ_SEARCH_KEY) {
		return ast_str_hash(obj);
	}

	return -1;
}

static int cli_endpoint_gather_channels(void *obj, void *arg, int flags)
{
	struct ast_sip_endpoint *endpoint = obj;
	struct ao2_container *channels = arg;

	ast_sip_for_each_channel(endpoint, cli_channel_populate_container, channels);

	return 0;
}

static struct ao2_container *cli_channel_get_container(void)
{
	RAII_VAR(struct ao2_container *, parent_container, NULL, ao2_cleanup);
	struct ao2_container *child_container;

	parent_container = cli_endpoint_get_container();
	if (!parent_container) {
		return NULL;
	}
	child_container = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_NOLOCK, 0, 17,
		cli_channel_hash, cli_channel_sort, cli_channel_compare);
	if (!child_container) {
		return NULL;
	}

	ao2_callback(parent_container, OBJ_NODATA, cli_endpoint_gather_channels, child_container);

	return child_container;
}

static const char *cli_channel_get_id(const void *obj)
{
	const struct ast_channel_snapshot *snapshot = obj;

	return snapshot->name;
}

static void *cli_channel_retrieve_by_id(const char *id)
{
	RAII_VAR(struct ao2_container *, container, cli_channel_get_container(), ao2_cleanup);

	return ao2_find(container, id, OBJ_KEY | OBJ_NOLOCK);
}

static int cli_channel_print_header(void *obj, void *arg, int flags)
{
	struct ast_sip_cli_context *context = arg;
	int indent = CLI_INDENT_TO_SPACES(context->indent_level);
	int filler = CLI_LAST_TABSTOP - indent - 13;

	ast_assert(context->output_buffer != NULL);

	ast_str_append(&context->output_buffer, 0,
		"%*s:  <ChannelId%*.*s>  <State.....>  <Time(sec)>\n",
		indent, "Channel", filler, filler, CLI_HEADER_FILLER);
	if (context->recurse) {
		context->indent_level++;
		indent = CLI_INDENT_TO_SPACES(context->indent_level);
		filler = CLI_LAST_TABSTOP - indent - 38;
		ast_str_append(&context->output_buffer, 0,
			"%*s: <DialedExten%*.*s>  CLCID: <ConnectedLineCID.......>\n",
			indent, "Exten", filler, filler, CLI_HEADER_FILLER);
		context->indent_level--;
	}

	return 0;
}

static int cli_channel_print_body(void *obj, void *arg, int flags)
{
	const struct ast_channel_snapshot *snapshot = obj;
	struct ast_sip_cli_context *context = arg;
	struct timeval current_time;
	char *print_name = NULL;
	int print_name_len;
	int indent;
	int flexwidth;

	ast_assert(context->output_buffer != NULL);

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

	if (context->recurse) {
		context->indent_level++;
		indent = CLI_INDENT_TO_SPACES(context->indent_level);
		flexwidth = CLI_LAST_TABSTOP - indent - 25;

		ast_str_append(&context->output_buffer, 0,
			"%*s: %-*.*s  CLCID: \"%s\" <%s>\n",
			indent, "Exten",
			flexwidth, flexwidth,
			snapshot->exten,
			snapshot->connected_name,
			snapshot->connected_number
			);
		context->indent_level--;
		if (context->indent_level == 0) {
			ast_str_append(&context->output_buffer, 0, "\n");
		}
	}

	return 0;
}

static int cli_endpoint_iterate(void *obj, ao2_callback_fn callback, void *args)
{
	ao2_callback(obj, OBJ_NODATA, callback, args);

	return 0;
}

static void *cli_endpoint_retrieve_by_id(const char *id)
{
	return ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", id);
}

static void cli_endpoint_print_child_header(char *type, struct ast_sip_cli_context *context)
{
	RAII_VAR(struct ast_sip_cli_formatter_entry *, formatter_entry, NULL, ao2_cleanup);

	formatter_entry = ast_sip_lookup_cli_formatter(type);
	if (formatter_entry) {
		formatter_entry->print_header(NULL, context, 0);
	}
}

static int cli_endpoint_print_header(void *obj, void *arg, int flags)
{
	struct ast_sip_cli_context *context = arg;

	ast_assert(context->output_buffer != NULL);

	ast_str_append(&context->output_buffer, 0,
			" Endpoint:  <Endpoint/CID.....................................>  <State.....>  <Channels.>\n");

	if (context->recurse) {
		context->indent_level++;
		cli_endpoint_print_child_header("auth", context);
		cli_endpoint_print_child_header("aor", context);
		cli_endpoint_print_child_header("transport", context);
		cli_endpoint_print_child_header("identify", context);
		cli_endpoint_print_child_header("channel", context);
		context->indent_level--;
	}

	return 0;
}

static void cli_endpoint_print_child_body(char *type, const void *obj, struct ast_sip_cli_context *context)
{
	RAII_VAR(struct ast_sip_cli_formatter_entry *, formatter_entry, NULL, ao2_cleanup);

	formatter_entry = ast_sip_lookup_cli_formatter(type);
	if (formatter_entry) {
		formatter_entry->iterate((void *)obj, formatter_entry->print_body, context);
	}
}

static int cli_endpoint_print_body(void *obj, void *arg, int flags)
{
	struct ast_sip_endpoint *endpoint = obj;
	RAII_VAR(struct ast_endpoint_snapshot *, endpoint_snapshot, ast_sip_get_endpoint_snapshot(endpoint), ao2_cleanup);
	struct ast_sip_cli_context *context = arg;
	const char *id = ast_sorcery_object_get_id(endpoint);
	char *print_name = NULL;
	int print_name_len;
	char *number = S_COR(endpoint->id.self.number.valid,
		endpoint->id.self.number.str, NULL);
	int indent;
	int flexwidth;

	ast_assert(context->output_buffer != NULL);

	if (number) {
		print_name_len = strlen(id) + strlen(number) + 2;
		if (!(print_name = alloca(print_name_len))) {
			return -1;
		}
		snprintf(print_name, print_name_len, "%s/%s", id, number);
	}

	indent = CLI_INDENT_TO_SPACES(context->indent_level);
	flexwidth = CLI_LAST_TABSTOP - indent - 2;

	ast_str_append(&context->output_buffer, 0, "%*s:  %-*.*s  %-12.12s  %d of %.0f\n",
		indent, "Endpoint",
		flexwidth, flexwidth, print_name ? print_name : id,
		ast_sip_get_device_state(endpoint),
		endpoint_snapshot->num_channels,
		(double) endpoint->devicestate_busy_at ? endpoint->devicestate_busy_at :
														INFINITY
														);

	if (context->recurse) {
		context->indent_level++;

		context->auth_direction = "Out";
		cli_endpoint_print_child_body("auth", &endpoint->outbound_auths, context);
		context->auth_direction = "In";
		cli_endpoint_print_child_body("auth", &endpoint->inbound_auths, context);

		cli_endpoint_print_child_body("aor", endpoint->aors, context);
		cli_endpoint_print_child_body("transport", endpoint, context);
		cli_endpoint_print_child_body("identify", endpoint, context);
		cli_endpoint_print_child_body("channel", endpoint, context);

		context->indent_level--;

		if (context->indent_level == 0) {
			ast_str_append(&context->output_buffer, 0, "\n");
		}
	}

	if (context->show_details || (context->show_details_only_level_0 && context->indent_level == 0)) {
		ast_str_append(&context->output_buffer, 0, "\n");
		ast_sip_cli_print_sorcery_objectset(endpoint, context, 0);
	}

	return 0;
}

static struct ast_cli_entry cli_commands[] = {
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "List PJSIP Channels",
		.command = "pjsip list channels",
		.usage = "Usage: pjsip list channels\n"
				 "       List the active PJSIP channels\n"),
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "Show PJSIP Channels",
		.command = "pjsip show channels",
		.usage = "Usage: pjsip show channels\n"
				 "       List(detailed) the active PJSIP channels\n"),
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "Show PJSIP Channel",
		.command = "pjsip show channel",
		.usage = "Usage: pjsip show channel\n"
				 "       List(detailed) the active PJSIP channel\n"),

	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "List PJSIP Endpoints",
		.command = "pjsip list endpoints",
		.usage = "Usage: pjsip list endpoints\n"
				 "       List the configured PJSIP endpoints\n"),
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "Show PJSIP Endpoints",
		.command = "pjsip show endpoints",
		.usage = "Usage: pjsip show endpoints\n"
				 "       List(detailed) the configured PJSIP endpoints\n"),
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "Show PJSIP Endpoint",
		.command = "pjsip show endpoint",
		.usage = "Usage: pjsip show endpoint <id>\n"
				 "       Show the configured PJSIP endpoint\n"),
};

struct ast_sip_cli_formatter_entry *channel_formatter;
struct ast_sip_cli_formatter_entry *endpoint_formatter;

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

	ast_sip_initialize_cli();

	if (ast_sip_initialize_sorcery_auth()) {
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

	if (ast_sorcery_internal_object_register(sip_sorcery, "nat_hook", sip_nat_hook_alloc, NULL, NULL)) {
		ast_log(LOG_ERROR, "Failed to register nat_hook\n");
	}

	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "context", "default", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, context));
	ast_sorcery_object_field_register_alias(sip_sorcery, "endpoint", "disallow", "", OPT_CODEC_T, 0, FLDSET(struct ast_sip_endpoint, media.codecs));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "allow", "", OPT_CODEC_T, 1, FLDSET(struct ast_sip_endpoint, media.codecs));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "dtmf_mode", "rfc4733", dtmf_handler, dtmf_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "rtp_ipv6", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, media.rtp.ipv6));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "rtp_symmetric", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, media.rtp.symmetric));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "ice_support", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, media.rtp.ice_support));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "use_ptime", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, media.rtp.use_ptime));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "force_rport", "yes", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, nat.force_rport));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "rewrite_contact", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, nat.rewrite_contact));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "transport", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, transport));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "outbound_proxy", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, outbound_proxy));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "moh_suggest", "default", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, mohsuggest));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "100rel", "yes", prack_handler, prack_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "timers", "yes", timers_handler, timers_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "timers_min_se", "90", OPT_UINT_T, 0, FLDSET(struct ast_sip_endpoint, extensions.timer.min_se));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "timers_sess_expires", "1800", OPT_UINT_T, 0, FLDSET(struct ast_sip_endpoint, extensions.timer.sess_expires));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "auth", "", inbound_auth_handler, inbound_auths_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "outbound_auth", "", outbound_auth_handler, outbound_auths_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "aors", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, aors));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "media_address", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, media.address));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "identify_by", "username", ident_handler, ident_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "direct_media", "yes", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, media.direct_media.enabled));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "direct_media_method", "invite", direct_media_method_handler, direct_media_method_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "connected_line_method", "invite", connected_line_method_handler, connected_line_method_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "direct_media_glare_mitigation", "none", direct_media_glare_mitigation_handler, direct_media_glare_mitigation_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "disable_direct_media_on_nat", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, media.direct_media.disable_on_nat));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "callerid", "", caller_id_handler, caller_id_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "callerid_privacy", "allowed_not_screened", caller_id_privacy_handler, caller_id_privacy_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "callerid_tag", "", caller_id_tag_handler, caller_id_tag_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "trust_id_inbound", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, id.trust_inbound));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "trust_id_outbound", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, id.trust_outbound));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "send_pai", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, id.send_pai));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "send_rpid", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, id.send_rpid));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "rpid_immediate", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, id.rpid_immediate));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "send_diversion", "yes", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, id.send_diversion));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "mailboxes", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, subscription.mwi.mailboxes));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "aggregate_mwi", "yes", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, subscription.mwi.aggregate));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "media_encryption", "no", media_encryption_handler, media_encryption_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "use_avpf", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, media.rtp.use_avpf));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "force_avp", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, media.rtp.force_avp));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "media_use_received_transport", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, media.rtp.use_received_transport));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "one_touch_recording", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, info.recording.enabled));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "inband_progress", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, inband_progress));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "call_group", "", group_handler, callgroup_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "pickup_group", "", group_handler, pickupgroup_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "named_call_group", "", named_groups_handler, named_callgroups_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "named_pickup_group", "", named_groups_handler, named_pickupgroups_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "device_state_busy_at", "0", OPT_UINT_T, 0, FLDSET(struct ast_sip_endpoint, devicestate_busy_at));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "t38_udptl", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, media.t38.enabled));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "t38_udptl_ec", "none", t38udptl_ec_handler, t38udptl_ec_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "t38_udptl_maxdatagram", "0", OPT_UINT_T, 0, FLDSET(struct ast_sip_endpoint, media.t38.maxdatagram));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "fax_detect", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, faxdetect));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "t38_udptl_nat", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, media.t38.nat));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "t38_udptl_ipv6", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, media.t38.ipv6));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "tone_zone", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, zone));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "language", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, language));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "record_on_feature", "automixmon", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, info.recording.onfeature));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "record_off_feature", "automixmon", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, info.recording.offfeature));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "allow_transfer", "yes", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, allowtransfer));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "user_eq_phone", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, usereqphone));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "moh_passthrough", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, moh_passthrough));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "sdp_owner", "-", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, media.sdpowner));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "sdp_session", "Asterisk", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, media.sdpsession));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "tos_audio", "0", tos_handler, tos_audio_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "tos_video", "0", tos_handler, tos_video_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "cos_audio", "0", OPT_UINT_T, 0, FLDSET(struct ast_sip_endpoint, media.cos_audio));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "cos_video", "0", OPT_UINT_T, 0, FLDSET(struct ast_sip_endpoint, media.cos_video));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "allow_subscribe", "yes", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, subscription.allow));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "sub_min_expiry", "0", OPT_UINT_T, 0, FLDSET(struct ast_sip_endpoint, subscription.minexpiry));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "from_user", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, fromuser));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "from_domain", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, fromdomain));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "mwi_from_user", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, subscription.mwi.fromuser));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "rtp_engine", "asterisk", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_endpoint, media.rtp.engine));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "dtls_verify", "no", dtls_handler, dtlsverify_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "dtls_rekey", "0", dtls_handler, dtlsrekey_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "dtls_cert_file", "", dtls_handler, dtlscertfile_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "dtls_private_key", "", dtls_handler, dtlsprivatekey_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "dtls_cipher", "", dtls_handler, dtlscipher_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "dtls_ca_file", "", dtls_handler, dtlscafile_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "dtls_ca_path", "", dtls_handler, dtlscapath_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "dtls_setup", "", dtls_handler, dtlssetup_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "dtls_fingerprint", "", dtls_handler, dtlsfingerprint_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "srtp_tag_32", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, media.rtp.srtp_tag_32));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "media_encryption_optimistic", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_endpoint, media.rtp.encryption_optimistic));
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "redirect_method", "user", redirect_handler, NULL, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sip_sorcery, "endpoint", "set_var", "", set_var_handler, set_var_to_str, set_var_to_vl, 0, 0);
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "message_context", "", OPT_STRINGFIELD_T, 1, STRFLDSET(struct ast_sip_endpoint, message_context));
	ast_sorcery_object_field_register(sip_sorcery, "endpoint", "accountcode", "", OPT_STRINGFIELD_T, 1, STRFLDSET(struct ast_sip_endpoint, accountcode));

	if (ast_sip_initialize_sorcery_transport()) {
		ast_log(LOG_ERROR, "Failed to register SIP transport support with sorcery\n");
		ast_sorcery_unref(sip_sorcery);
		sip_sorcery = NULL;
		return -1;
	}

	if (ast_sip_initialize_sorcery_location()) {
		ast_log(LOG_ERROR, "Failed to register SIP location support with sorcery\n");
		ast_sorcery_unref(sip_sorcery);
		sip_sorcery = NULL;
		return -1;
	}

	if (ast_sip_initialize_sorcery_qualify()) {
		ast_log(LOG_ERROR, "Failed to register SIP qualify support with sorcery\n");
		ast_sorcery_unref(sip_sorcery);
		sip_sorcery = NULL;
		return -1;
	}

	ast_sorcery_observer_add(sip_sorcery, "contact", &state_contact_observer);
	ast_sorcery_observer_add(sip_sorcery, CONTACT_STATUS, &state_contact_status_observer);

	if (ast_sip_initialize_sorcery_domain_alias()) {
		ast_log(LOG_ERROR, "Failed to register SIP domain aliases support with sorcery\n");
		ast_sorcery_unref(sip_sorcery);
		sip_sorcery = NULL;
		return -1;
	}

	if (ast_sip_initialize_sorcery_global()) {
		ast_log(LOG_ERROR, "Failed to register SIP Global support\n");
		ast_sorcery_unref(sip_sorcery);
		sip_sorcery = NULL;
		return -1;
	}

	channel_formatter = ao2_alloc(sizeof(struct ast_sip_cli_formatter_entry), NULL);
	if (!channel_formatter) {
		ast_log(LOG_ERROR, "Unable to allocate memory for channel_formatter\n");
		ast_sorcery_unref(sip_sorcery);
		sip_sorcery = NULL;
		return -1;
	}
	channel_formatter->name = "channel";
	channel_formatter->print_header = cli_channel_print_header;
	channel_formatter->print_body = cli_channel_print_body;
	channel_formatter->get_container = cli_channel_get_container;
	channel_formatter->iterate = cli_channel_iterate;
	channel_formatter->retrieve_by_id = cli_channel_retrieve_by_id;
	channel_formatter->get_id = cli_channel_get_id;

	endpoint_formatter = ao2_alloc(sizeof(struct ast_sip_cli_formatter_entry), NULL);
	if (!endpoint_formatter) {
		ast_log(LOG_ERROR, "Unable to allocate memory for endpoint_formatter\n");
		ast_sorcery_unref(sip_sorcery);
		sip_sorcery = NULL;
		return -1;
	}
	endpoint_formatter->name = "endpoint";
	endpoint_formatter->print_header = cli_endpoint_print_header;
	endpoint_formatter->print_body = cli_endpoint_print_body;
	endpoint_formatter->get_container = cli_endpoint_get_container;
	endpoint_formatter->iterate = cli_endpoint_iterate;
	endpoint_formatter->retrieve_by_id = cli_endpoint_retrieve_by_id;
	endpoint_formatter->get_id = ast_sorcery_object_get_id;

	ast_sip_register_cli_formatter(channel_formatter);
	ast_sip_register_cli_formatter(endpoint_formatter);
	ast_cli_register_multiple(cli_commands, ARRAY_LEN(cli_commands));

	ast_sorcery_load(sip_sorcery);

	return 0;
}

void ast_res_pjsip_destroy_configuration(void)
{
	ast_sorcery_observer_remove(sip_sorcery, CONTACT_STATUS, &state_contact_status_observer);
	ast_sorcery_observer_remove(sip_sorcery, "contact", &state_contact_observer);
	ast_sip_destroy_sorcery_global();
	ast_sip_destroy_sorcery_location();
	ast_sip_destroy_sorcery_auth();
	ast_sip_destroy_sorcery_transport();
	ast_manager_unregister(AMI_SHOW_ENDPOINT);
	ast_manager_unregister(AMI_SHOW_ENDPOINTS);
	ast_cli_unregister_multiple(cli_commands, ARRAY_LEN(cli_commands));
	ast_sip_unregister_cli_formatter(endpoint_formatter);
	ast_sip_unregister_cli_formatter(channel_formatter);
	ast_sorcery_unref(sip_sorcery);
	ao2_cleanup(persistent_endpoints);
}

int ast_res_pjsip_reload_configuration(void)
{
	if (sip_sorcery) {
		ast_sorcery_reload(sip_sorcery);
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

	ao2_ref(endpoint->media.codecs, -1);
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
	if (!(endpoint->media.codecs = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
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
